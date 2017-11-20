/*
 *  Copyright (C) 2016,2017 Robin Gareus <robin@gareus.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#ifndef UPDATE_FREQ_RATIO
# define UPDATE_FREQ_RATIO 60 // MAX # of audio-cycles per GUI-refresh
#endif

static const size_t atom_buf_size = 8192;

#include "lv2/lv2plug.in/ns/ext/buf-size/buf-size.h"
#include "lv2/lv2plug.in/ns/ext/parameters/parameters.h"

#include "loadlib.h"
#include "lv2ttl.h"
#include "lv2plugin.h"

using namespace Lv2Vlc;

extern "C" {
	char* lilv_dirname(const char* path);
}

LV2Plugin::LV2Plugin (RtkLv2Description* desc, float rate)
	: ctrl_to_ui (1 + UPDATE_FREQ_RATIO * desc->nports_ctrl)
	, atom_to_ui (1 + UPDATE_FREQ_RATIO * desc->min_atom_bufsiz)
	, atom_from_ui (UPDATE_FREQ_RATIO * desc->min_atom_bufsiz)
	, _desc (desc)
	, _plugin_dsp (0)
	, _plugin_instance (0)
	, _sample_rate (rate)
	, _ui (this)
	, _worker (0)
	, worker_iface (0)
	, _portmap_atom_to_ui (UINT32_MAX)
	, _portmap_atom_from_ui (UINT32_MAX)
	, _ui_sync (true)
	, _active (false)
{
	_lib_handle = open_lv2_lib (desc->dsp_path);
	lv2_descriptor = (const LV2_Descriptor* (*)(uint32_t)) x_dlfunc (_lib_handle, "lv2_descriptor");

	if (!lv2_descriptor) {
		fprintf (stderr, "LV2Host: missing lv2_descriptor symbol for '%s'.\n", _desc->dsp_uri);
		throw -1;
	}

	_ports = (float*) malloc (_desc->nports_total * sizeof (float));
	_ports_pre = (float*) malloc (_desc->nports_total * sizeof (float));

	_atom_in = (LV2_Atom_Sequence*) malloc (_desc->min_atom_bufsiz + sizeof (uint8_t));
	_atom_out = (LV2_Atom_Sequence*) malloc (_desc->min_atom_bufsiz + sizeof (uint8_t));

	/* prepare LV2 feature set */

	schedule.handle = NULL;
	schedule.schedule_work = &Lv2Worker::lv2_worker_schedule;
	uri_map.handle = &_map;
	uri_map.map = &Lv2UriMap::uri_to_id;
	uri_unmap.handle = &_map;
	uri_unmap.unmap = &Lv2UriMap::id_to_uri;

	init ();
}

void LV2Plugin::init ()
{

	_uri.atom_Float           = _map.uri_to_id (LV2_ATOM__Float);
	_uri.atom_Int             = _map.uri_to_id (LV2_ATOM__Int);
	_uri.param_sampleRate     = _map.uri_to_id (LV2_PARAMETERS__sampleRate);
	_uri.bufsz_minBlockLength = _map.uri_to_id (LV2_BUF_SIZE__minBlockLength);
	_uri.bufsz_maxBlockLength = _map.uri_to_id (LV2_BUF_SIZE__maxBlockLength);
	_uri.bufsz_sequenceSize   = _map.uri_to_id (LV2_BUF_SIZE__sequenceSize);

	/* options to pass to plugin */
	const LV2_Options_Option options[] = {
		{ LV2_OPTIONS_INSTANCE, 0, _uri.param_sampleRate,
			sizeof(float), _uri.atom_Float, &_sample_rate },
#if 0
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_minBlockLength,
			sizeof(int32_t), _uri.atom_Int, &_block_size },
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_maxBlockLength,
			sizeof(int32_t), _uri.atom_Int, &_block_size },
#endif
		{ LV2_OPTIONS_INSTANCE, 0, _uri.bufsz_sequenceSize,
			sizeof(int32_t), _uri.atom_Int, &atom_buf_size },
		{ LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL }
	};

	/* lv2 host features */
	const LV2_Feature schedule_feature = { LV2_WORKER__schedule, &schedule };
	const LV2_Feature map_feature      = { LV2_URID__map, &uri_map};
	const LV2_Feature unmap_feature    = { LV2_URID__unmap, &uri_unmap };
	const LV2_Feature options_feature  = { LV2_OPTIONS__options, (void*)&options };

	const LV2_Feature* features[] = {
		&map_feature,
		&unmap_feature,
		&schedule_feature,
		&options_feature,
		NULL
	};

	/* resolve descriptors */
	uint32_t index = 0;
	while (lv2_descriptor) {
		_plugin_dsp = lv2_descriptor (index);
		if (!_plugin_dsp) { break; }
		if (!strcmp (_plugin_dsp->URI, _desc->dsp_uri)) { break; }
		++index;
	}

	if (!_plugin_dsp) {
		fprintf (stderr, "LV2Host: cannot descriptor for '%s'.\n", _desc->dsp_uri);
		throw -2;
	}

	/* init plugin */
	char* dirname = lilv_dirname (_desc->dsp_path);
	_plugin_instance = _plugin_dsp->instantiate (_plugin_dsp, _sample_rate, dirname, features);
	free (dirname);

	if (!_plugin_instance) {
		fprintf (stderr, "LV2Host: failed to instantiate '%s'.\n", _desc->dsp_uri);
		throw -3;
	}

	/* connect ports */
	uint32_t c_ain  = 0;
	uint32_t c_aout = 0;

	for (uint32_t p=0; p < _desc->nports_total; ++p) {
		switch (_desc->ports[p].porttype) {
			case CONTROL_IN:
				_ports[p] = _desc->ports[p].val_default;
				//printf ("CTRL %d = %f # %s\n", p, _ports[p], _desc->ports[p].name);
				_plugin_dsp->connect_port (_plugin_instance, p, &_ports[p]);
				{
					ParamVal pv (p, _ports[p]);
					ctrl_to_ui.write (&pv, 1);
				}
				break;
			case CONTROL_OUT:
				_plugin_dsp->connect_port (_plugin_instance, p, &_ports[p]);
				break;
			case MIDI_IN:
			case ATOM_IN:
				_portmap_atom_from_ui = p;
				_plugin_dsp->connect_port (_plugin_instance, p, _atom_in);
				break;
			case MIDI_OUT:
			case ATOM_OUT:
				_portmap_atom_to_ui = p;
				_plugin_dsp->connect_port (_plugin_instance, p, _atom_out);
				break;
			case AUDIO_IN:
				++c_ain;
				break;
			case AUDIO_OUT:
				++c_aout;
				break;
			default:
				break;
		}
	}

	assert (c_ain == _desc->nports_audio_in);
	assert (c_aout == _desc->nports_audio_out);

	if (_desc->nports_atom_out > 0 || _desc->nports_atom_in > 0 || _desc->nports_midi_in > 0 || _desc->nports_midi_out > 0) {
		_uri.atom_Sequence       = _map.uri_to_id (LV2_ATOM__Sequence);
		_uri.atom_EventTransfer  = _map.uri_to_id (LV2_ATOM__eventTransfer);

		lv2_atom_forge_init (&lv2_forge, &uri_map);
	} else {
		memset (&_uri, 0, sizeof (URIs));
	}

	if (_plugin_dsp->extension_data) {
		worker_iface = (const LV2_Worker_Interface*) _plugin_dsp->extension_data (LV2_WORKER__interface);
	}

	if (worker_iface) {
		_worker = new Lv2Worker (worker_iface, _plugin_instance);
		schedule.handle = _worker;
	}
}

void LV2Plugin::deinit ()
{
	delete _worker;

	suspend ();

	if (_plugin_dsp && _plugin_instance && _plugin_dsp->cleanup) {
		_plugin_dsp->cleanup (_plugin_instance);
	}
}

LV2Plugin::~LV2Plugin ()
{
	deinit ();

	free (_ports);
	free (_ports_pre);
	free (_atom_in);
	free (_atom_out);
	free_desc (_desc);
	close_lv2_lib (_lib_handle);
}

/* ****************************************************************************
 * Parameters
 */
bool LV2Plugin::set_parameter (int32_t p, float val)
{
	if (_ports[p] == val) {
		return false;
	}

	_ports[p] = val;

	if (_ui.is_open ()) {
		if (ctrl_to_ui.write_space () > 0) {
			ParamVal pv (p, _ports[p]);
			ctrl_to_ui.write (&pv, 1);
		}
	}
	return true;
}

/* ****************************************************************************
 * State
 */

void LV2Plugin::resume ()
{
	if (_active) {
		return;
	}
	if (_plugin_dsp->activate) {
		_plugin_dsp->activate (_plugin_instance);
	}
	_active = true;
}

void LV2Plugin::suspend ()
{
	if (!_active) {
		return;
	}
	if (_plugin_dsp->deactivate) {
		_plugin_dsp->deactivate (_plugin_instance);
	}
	_active = false;
}

/* ****************************************************************************
 * Process Audio/Midi
 */

void LV2Plugin::process (float** iobuf, int32_t n_samples)
{
	int ins = 0;
	int outs = 0;

	/* re-connect audio buffers */
	for (uint32_t p = 0; p < _desc->nports_total; ++p) {
		switch (_desc->ports[p].porttype) {
			case AUDIO_IN:
				_plugin_dsp->connect_port (_plugin_instance, p, iobuf[ins++]);
				break;
			case AUDIO_OUT:
				_plugin_dsp->connect_port (_plugin_instance, p, iobuf[outs++]);
				break;
			default:
				break;
		}
	}

	/* atom buffers */
	if (_desc->nports_atom_in > 0 || _desc->nports_midi_in > 0) {
		/* start Atom sequence */
		_atom_in->atom.type = _uri.atom_Sequence;
		_atom_in->atom.size = 8;
		LV2_Atom_Sequence_Body *body = &_atom_in->body;
		body->unit = 0; // URID of unit of event time stamp LV2_ATOM__timeUnit ??
		body->pad  = 0; // unused
		uint8_t* seq = (uint8_t*) (body + 1);

		if (_ui.has_editor ()) {
			while (atom_from_ui.read_space () > sizeof (LV2_Atom)) {
				LV2_Atom a;
				atom_from_ui.read ((char *) &a, sizeof (LV2_Atom));
				uint32_t padded_size = _atom_in->atom.size + a.size + sizeof (int64_t);
				if (_desc->min_atom_bufsiz > padded_size) {
					memset (seq, 0, sizeof (int64_t)); // LV2_Atom_Event->time
					seq += sizeof (int64_t);
					atom_from_ui.read ((char *) seq, a.size);
					seq += a.size;
					_atom_in->atom.size += a.size + sizeof (int64_t);
				}
			}
		}
	}

	if (_desc->nports_atom_out > 0 || _desc->nports_midi_out > 0) {
		_atom_out->atom.type = 0;
		_atom_out->atom.size = _desc->min_atom_bufsiz;
	}

	/* make a backup copy, to see what is changed */
	memcpy (_ports_pre, _ports, _desc->nports_total * sizeof (float));

	_plugin_dsp->run (_plugin_instance, n_samples);

	/* handle worker emit response  - may amend Atom seq... */
	if (_worker) {
		_worker->emit_response ();
	}

	/* create port-events for changed values */
	if (_ui.is_open ()) {
		for (uint32_t p = 0; p < _desc->nports_total; ++p) {
			if (_desc->ports[p].porttype == CONTROL_IN && _ui_sync) {
				ParamVal pv (p, _ports[p]);
				ctrl_to_ui.write (&pv, 1);
				continue;
			}
			if (_desc->ports[p].porttype != CONTROL_OUT) {
				continue;
			}
			if (_ports_pre[p] == _ports[p] && !_ui_sync) {
				continue;
			}

			if (p == _desc->latency_ctrl_port) {
				//
			}

			if (ctrl_to_ui.write_space () < 1) {
				continue;
			}
			ParamVal pv (p, _ports[p]);
			ctrl_to_ui.write (&pv, 1);
		}
		_ui_sync = false;
	} else {
		_ui_sync = true;
	}

	/* Atom sequence port-events */
	if (_desc->nports_atom_out + _desc->nports_midi_out > 0 && _atom_out->atom.size > sizeof (LV2_Atom)) {
		if (_ui.is_open () && atom_to_ui.write_space () >= _atom_out->atom.size + 2 * sizeof (LV2_Atom)) {
			LV2_Atom a = {_atom_out->atom.size + (uint32_t) sizeof (LV2_Atom), 0};

			atom_to_ui.write ((char *) &a, sizeof (LV2_Atom));
			atom_to_ui.write ((char *) _atom_out, a.size);
		}
	}

	/* signal worker end of process run */
	if (_worker) {
		_worker->end_run ();
	}
}
