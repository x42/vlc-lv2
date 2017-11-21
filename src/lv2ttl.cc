/*
 *  Copyright (C) 2016 Robin Gareus <robin@gareus.org>
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
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
# include <windows.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/event/event.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/resize-port/resize-port.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/port-props/port-props.h"

#include "lilv/lilv.h"

#include "loadlib.h"
#include "lv2ttl.h"

#ifndef UINT32_MAX
# define UINT32_MAX (4294967295U)
#endif

/* helpers */

static const char* port_name (const LilvPlugin* p, const LilvPort* port)
{
	if (!port) { return ""; }
	LilvNode* name = lilv_port_get_name (p, port);
	return lilv_node_as_string (name);
}

static const char* port_symbol (const LilvPlugin* p, const LilvPort* port)
{
	if (!port) { return ""; }
	const LilvNode* sym = lilv_port_get_symbol (p, port);
	return lilv_node_as_string (sym);
}

static char* node_strdup (LilvNode* val)
{
	if (val) {
		char* rv = strdup (lilv_node_as_string (val));
		lilv_node_free (val);
		return (rv);
	}
	return strdup ("");
}

static char* file_strdup (const LilvNode* val)
{
	const char* const lib_uri  = lilv_node_as_uri (val);
	char* const       lib_path = lilv_file_uri_parse (lib_uri, NULL);
	if (!lib_path) {
		return strdup ("");
	}
	return strdup (lib_path);
}

static enum PortType type_to_enum (int type, int direction)
{
	if (direction == 1) {
		switch (type) {
			case 1: return CONTROL_IN;
			case 2: return AUDIO_IN;
			case 3: return ATOM_IN;
			case 4: return MIDI_IN;
		}
	} else {
		switch (type) {
			case 1: return CONTROL_OUT;
			case 2: return AUDIO_OUT;
			case 3: return ATOM_OUT;
			case 4: return MIDI_OUT;
		}
	}
	assert (0);
	return CONTROL_IN;
}

/* parser */

class LV2Parser
{
	public:
		LV2Parser (RtkLv2Description*);
		~LV2Parser ();

		int parse (const char* uri);
		int parse (const char* uri, const LilvPlugin*);

	private:

		const char* port_docs (const LilvPlugin* p, const LilvPort* port) {
			LilvNodes* comments = lilv_port_get_value (p, port, uri_rdfs_comment);
			if (comments) {
				const char *docs = lilv_node_as_string (lilv_nodes_get_first (comments));
				lilv_nodes_free (comments);
				return docs;
			}
			return "";
		}

	private:
		LilvWorld* world;
		RtkLv2Description* desc;

		LilvNode* uri_atom_supports;
		LilvNode* rsz_minimumSize;
		LilvNode* uri_midi_event;
		LilvNode* uri_time_position;
		LilvNode* uri_rdfs_comment;
		LilvNode* lv2_reportsLatency;
		LilvNode* lv2_toggled;
		LilvNode* lv2_integer;
		LilvNode* lv2_sampleRate;
		LilvNode* lv2_enumeration;
		LilvNode* ext_logarithmic;
		LilvNode* ext_rangeSteps;
		LilvNode* lv2_minorVersion;
		LilvNode* lv2_microVersion;
		LilvNode* ext_notOnGUI;
		LilvNode* ext_expensive;
		LilvNode* ext_causesArtifacts;
		LilvNode* ext_notAutomatic;
		LilvNode* lv2_enabled;
		LilvNode* lv2_InputPort;
		LilvNode* lv2_inPlaceBroken;
};

LV2Parser::LV2Parser (RtkLv2Description* d)
	: desc (d)
{
	world = lilv_world_new ();

	lilv_world_load_all (world);

	uri_atom_supports   = lilv_new_uri (world, LV2_ATOM__supports);
	rsz_minimumSize     = lilv_new_uri (world, LV2_RESIZE_PORT__minimumSize);
	uri_midi_event      = lilv_new_uri (world, LV2_MIDI__MidiEvent);
	uri_time_position   = lilv_new_uri (world, LV2_TIME__Position);
	uri_rdfs_comment    = lilv_new_uri (world, LILV_NS_RDFS "comment");
	lv2_reportsLatency  = lilv_new_uri (world, LV2_CORE__reportsLatency);
	lv2_toggled         = lilv_new_uri (world, LV2_CORE__toggled);
	lv2_integer         = lilv_new_uri (world, LV2_CORE__integer);
	lv2_sampleRate      = lilv_new_uri (world, LV2_CORE__sampleRate);
	lv2_enumeration     = lilv_new_uri (world, LV2_CORE__enumeration);
	ext_logarithmic     = lilv_new_uri (world, LV2_PORT_PROPS__logarithmic);
	ext_rangeSteps      = lilv_new_uri (world, LV2_PORT_PROPS__rangeSteps);
	lv2_minorVersion    = lilv_new_uri (world, LV2_CORE__minorVersion);
	lv2_microVersion    = lilv_new_uri (world, LV2_CORE__microVersion);
	ext_notOnGUI        = lilv_new_uri (world, LV2_PORT_PROPS__notOnGUI);
	ext_expensive       = lilv_new_uri (world, LV2_PORT_PROPS__expensive);
	ext_causesArtifacts = lilv_new_uri (world, LV2_PORT_PROPS__causesArtifacts);
	ext_notAutomatic    = lilv_new_uri (world, LV2_PORT_PROPS__notAutomatic);
	lv2_enabled         = lilv_new_uri (world, LV2_CORE_PREFIX "enabled");
	lv2_InputPort       = lilv_new_uri (world, LILV_URI_INPUT_PORT);
	lv2_inPlaceBroken   = lilv_new_uri(world, LV2_CORE__inPlaceBroken);
}

LV2Parser::~LV2Parser ()
{
	lilv_node_free (uri_atom_supports);
	lilv_node_free (rsz_minimumSize);
	lilv_node_free (uri_midi_event);
	lilv_node_free (uri_time_position);
	lilv_node_free (uri_rdfs_comment);
	lilv_node_free (lv2_reportsLatency);
	lilv_node_free (lv2_toggled);
	lilv_node_free (lv2_integer);
	lilv_node_free (lv2_sampleRate);
	lilv_node_free (lv2_enumeration);
	lilv_node_free (ext_logarithmic);
	lilv_node_free (ext_rangeSteps);
	lilv_node_free (lv2_minorVersion);
	lilv_node_free (lv2_microVersion);
	lilv_node_free (ext_notOnGUI);
	lilv_node_free (ext_expensive);
	lilv_node_free (ext_causesArtifacts);
	lilv_node_free (ext_notAutomatic);
	lilv_node_free (lv2_enabled);
	lilv_node_free (lv2_InputPort);
	lilv_node_free (lv2_inPlaceBroken);
	lilv_world_free (world);
}

int LV2Parser::parse (const char* plugin_uri)
{
	LilvNode* uri = lilv_new_uri (world, plugin_uri);
	if (!uri) {
		fprintf (stderr, "LV2Host: Invalid plugin URI\n");
		return 1;
	}

	const LilvPlugins* plugins = lilv_world_get_all_plugins (world);
	const LilvPlugin*  p       = lilv_plugins_get_by_uri (plugins, uri);
	lilv_node_free (uri);
	if (!p) {
		fprintf (stderr, "LV2Host: Plugin not found.\n");
		return 1;
	}
	return parse (plugin_uri, p);
}

int LV2Parser::parse (const char* plugin_uri, const LilvPlugin* p)
{
	int err = 0;
	desc->dsp_uri = strdup (plugin_uri);
	desc->gui_uri = NULL;

	desc->send_time_info = false;
	desc->has_state_interface = false;
	desc->min_atom_bufsiz = 8192;
	desc->latency_ctrl_port = UINT32_MAX;
	desc->enable_ctrl_port = UINT32_MAX;

	desc->plugin_name = node_strdup (lilv_plugin_get_name (p));
	desc->vendor      = node_strdup (lilv_plugin_get_author_name (p));

	desc->bundle_path = file_strdup (lilv_plugin_get_bundle_uri (p));
	desc->dsp_path    = file_strdup (lilv_plugin_get_library_uri (p));
	desc->gui_path    = 0;

#ifdef CHECK_OPEN_ONLY
	int fd = open (desc->dsp_path, 0);
	if (fd < 0) {
		fprintf (stderr, "Cannot open DSP: '%s' for '%s'\n", desc->dsp_path, plugin_uri);
		return -1;
	}
	close (fd);
#else
	void* handle = open_lv2_lib (desc->dsp_path);
	void* func = (void*) x_dlfunc (handle, "lv2_descriptor");
	close_lv2_lib (handle);
	if (!func) {
		fprintf (stderr, "Cannot open DSP: '%s' for '%s'\n", desc->dsp_path, plugin_uri);
		return -1;
	}
#endif

	if (lilv_plugin_has_feature (p, lv2_inPlaceBroken)) {
		fprintf (stderr, "Cannot use plugin '%s' since it cannot do inplace processing\n", plugin_uri);
		return -1;
	}

	LilvNodes* mv = lilv_plugin_get_value (p, lv2_minorVersion);
	if (mv) {
		desc->version_minor = atoi (lilv_node_as_string (lilv_nodes_get_first (mv)));
		lilv_nodes_free (mv);
	}
	mv = lilv_plugin_get_value (p, lv2_microVersion);
	if (mv) {
		desc->version_micro = atoi (lilv_node_as_string (lilv_nodes_get_first (mv)));
		lilv_nodes_free (mv);
	}

	LilvNodes* features = lilv_plugin_get_required_features (p);
	if (features) {
		LILV_FOREACH(nodes, i, features) {
			const char* rf = lilv_node_as_uri (lilv_nodes_get (features, i));
			bool ok = false;
			if (!strcmp (rf, "http://lv2plug.in/ns/ext/urid#map")) { ok = true; }
			if (!strcmp (rf, "http://lv2plug.in/ns/ext/urid#unmap")) { ok = true; }
			if (!strcmp (rf, "http://lv2plug.in/ns/ext/worker#schedule")) { ok = true; }
			if (!strcmp (rf, "http://lv2plug.in/ns/ext/options#options")) { ok = true; }
			if (!ok) {
				fprintf (stderr, "Unsupported required feature: '%s' in '%s'\n", rf, plugin_uri);
				err = 1;
			}
		}
	}
	lilv_nodes_free(features);

	if (err) {
		return err;
	}

	LilvUIs* uis = lilv_plugin_get_uis (p);
#ifdef _WIN32
	static const char* suppored_ui = LV2_UI__WindowsUI;
#elif defined __APPLE__
	static const char* suppored_ui = LV2_UI__CocoaUI;
#else
	static const char* suppored_ui = LV2_UI__X11UI;
#endif

	LILV_FOREACH(uis, i, uis) {
		const LilvUI* ui = lilv_uis_get (uis, i);
		const LilvNodes* types = lilv_ui_get_classes (ui);
		LILV_FOREACH(nodes, t, types) {
			const char* type = lilv_node_as_uri (lilv_nodes_get (types, t));
			if (!strcmp (type, suppored_ui)) {
				free (desc->gui_path);
				free (desc->bundle_path);
				desc->gui_uri = strdup (lilv_node_as_uri (lilv_ui_get_uri(ui)));
				desc->gui_path = file_strdup (lilv_ui_get_binary_uri (ui));
				desc->bundle_path = file_strdup (lilv_ui_get_bundle_uri (ui));
			}
		}
	}
	lilv_uis_free(uis);

#ifdef CHECK_OPEN_ONLY
	if (desc->gui_path) {
		int fd = open (desc->gui_path, 0);
		if (fd < 0) {
			fprintf (stderr, "Cannot open GUI: '%s' for '%s'\n", desc->gui_path, plugin_uri);
			free (desc->gui_uri);
			free (desc->gui_path);
			free (desc->bundle_path);
			desc->gui_uri = NULL;
			desc->gui_path = NULL;
			desc->bundle_path = NULL;
		}
		close (fd);
	}
#else
	if (desc->gui_path) {
		handle = open_lv2_lib (desc->gui_path);
		func = (void*) x_dlfunc (handle, "lv2ui_descriptor");
		close_lv2_lib (handle);
		if (!func) {
			fprintf (stderr, "Cannot open GUI: '%s' for '%s'\n", desc->gui_path, plugin_uri);
			free (desc->gui_uri);
			free (desc->gui_path);
			free (desc->bundle_path);
			desc->gui_uri = NULL;
			desc->gui_path = NULL;
			desc->bundle_path = NULL;
		}
	}
#endif

	LilvNodes* data = lilv_plugin_get_extension_data(p);
	LILV_FOREACH(nodes, i, data) {
		if (!strcmp (LV2_STATE__interface, lilv_node_as_uri (lilv_nodes_get (data, i)))) {
			desc->has_state_interface = true;
		}
	}
	lilv_nodes_free(data);

	/* Ports */
	const uint32_t num_ports = lilv_plugin_get_num_ports (p);
	float* mins     = (float*)calloc (num_ports, sizeof (float));
	float* maxes    = (float*)calloc (num_ports, sizeof (float));
	float* defaults = (float*)calloc (num_ports, sizeof (float));

	lilv_plugin_get_port_ranges_float (p, mins, maxes, defaults);

	desc->ports = (struct LV2Port*) calloc (num_ports, sizeof (struct LV2Port));

	for (uint32_t pi = 0; pi < num_ports; ++pi) {
		const LilvPort* port = lilv_plugin_get_port_by_index (p, pi);
		int direction = -1;
		int type = -1;

		const LilvNodes* classes = lilv_port_get_classes (p, port);
		LILV_FOREACH (nodes, i, classes) {
			const LilvNode* value = lilv_nodes_get (classes, i);
			if (!strcmp (lilv_node_as_uri (value), LV2_CORE__InputPort)) {
				direction = 1;
			}
			else if (!strcmp (lilv_node_as_uri (value), LV2_CORE__OutputPort)) {
				direction = 2;
			}
			else if (!strcmp (lilv_node_as_uri (value), LV2_CORE__ControlPort)) {
				type = 1;
			}
			else if (!strcmp (lilv_node_as_uri (value), LV2_CORE__AudioPort)) {
				type = 2;
			}
			else if (!strcmp (lilv_node_as_uri (value), LV2_CORE__CVPort)) {
				type = 2;
			}
			else if (!strcmp (lilv_node_as_uri (value), LV2_ATOM__AtomPort)) {
				// TODO check for resize-port
				LilvNodes* atom_supports = lilv_port_get_value (p, port, uri_atom_supports);
				if (lilv_nodes_contains (atom_supports, uri_midi_event)) {
					type = 4;
				} else {
					type = 3;
				}
				if (lilv_nodes_contains (atom_supports, uri_time_position)) {
					desc->send_time_info = true;
				}
				lilv_nodes_free (atom_supports);
				LilvNodes* min_size_v = lilv_port_get_value (p, port, rsz_minimumSize);
				LilvNode* min_size = min_size_v ? lilv_nodes_get_first (min_size_v) : NULL;
				if (min_size && lilv_node_is_int (min_size)) {
					int minimumSize = lilv_node_as_int (min_size);
					if (desc->min_atom_bufsiz < (uint32_t) minimumSize) {
						desc->min_atom_bufsiz = minimumSize;
					}
				}
				lilv_nodes_free (min_size_v);
			}
		}

		if (direction == -1 || type == -1) {
			fprintf (stderr, "LV2Host: Error: parsing port #%d\n", pi);
			err = 1;
			break;
		}

		if (direction == 2 && type == 1 && lilv_port_has_property (p, port, lv2_reportsLatency)) {
			desc->latency_ctrl_port = pi;
		}

		desc->ports[pi].porttype = type_to_enum (type, direction);
		desc->ports[pi].name     = strdup (port_name (p, port));
		desc->ports[pi].symbol   = strdup (port_symbol (p, port));
		desc->ports[pi].doc      = strdup (port_docs (p, port));

		desc->ports[pi].val_default  = defaults[pi];
		desc->ports[pi].val_min      = mins[pi];
		desc->ports[pi].val_max      = maxes[pi];

		desc->ports[pi].not_on_gui = false;
		desc->ports[pi].not_automatic = false;

		if (direction == 1 && type == 1 && lilv_port_has_property (p, port, ext_notOnGUI)) {
			desc->ports[pi].not_on_gui = true;
		}

		if (direction == 1 && type == 1 && lilv_port_has_property (p, port, ext_expensive)) {
			desc->ports[pi].not_automatic = true;
		}

		if (direction == 1 && type == 1 && lilv_port_has_property (p, port, ext_causesArtifacts)) {
			desc->ports[pi].not_automatic = true;
		}

		if (direction == 1 && type == 1 && lilv_port_has_property (p, port, ext_notAutomatic)) {
			desc->ports[pi].not_automatic = true;
		}

		LilvNode* steps = lilv_port_get (p, port, ext_rangeSteps);
		if (steps) {
			desc->ports[pi].steps = lilv_node_as_float (steps);
			lilv_node_free (steps);
		} else {
			desc->ports[pi].steps = 100.f;
		}

		desc->ports[pi].toggled      = lilv_port_has_property (p, port, lv2_toggled);
		desc->ports[pi].integer_step = lilv_port_has_property (p, port, lv2_integer);
		desc->ports[pi].logarithmic  = lilv_port_has_property (p, port, ext_logarithmic);
		desc->ports[pi].sr_dependent = lilv_port_has_property (p, port, lv2_sampleRate);
		desc->ports[pi].enumeration  = lilv_port_has_property (p, port, lv2_enumeration);

		// TODO get scalepoints

		if (direction == 1) {
			switch (type) {
				case 2: desc->nports_audio_in++; break;
				case 4: desc->nports_midi_in++; break;
				case 1: desc->nports_ctrl_in++; break;
				case 3: desc->nports_atom_in++; break;
				default: break;
			}
		} else if (direction == 2) {
			switch (type) {
				case 2: desc->nports_audio_out++; break;
				case 4: desc->nports_midi_out++; break;
				case 1: desc->nports_ctrl_out++; break;
				case 3: desc->nports_atom_out++; break;
				default: break;
			}
		}
	}

	desc->nports_total = num_ports;
	desc->nports_ctrl = desc->nports_ctrl_in + desc->nports_ctrl_out;


	const LilvPort* port = lilv_plugin_get_port_by_designation (
			p, lv2_InputPort, lv2_enabled);
	if (port) {
		desc->enable_ctrl_port = lilv_port_get_index (p, port);
	}

	free (mins);
	free (maxes);
	free (defaults);
	return err;
}

/* this filters out unsupported plugins */
static int verify_support (RtkLv2Description* desc) {
	// TODO check if inplaceBroken -> ignore

	if (desc->gui_uri == NULL) {
		fprintf (stderr, "Unsupported LV2 Plugin '%s' (no native GUI)\n", desc->dsp_uri ? desc->dsp_uri : "??");
		return -1;
	}

	if (desc->nports_total == 0) {
		fprintf (stderr, "Unsupported LV2 Plugin '%s' (no ports)\n", desc->dsp_uri ? desc->dsp_uri : "??");
		return -1;
	}
	if (!desc->plugin_name) {
		fprintf (stderr, "Unsupported LV2 Plugin '%s' (no plugin name)\n", desc->dsp_uri ? desc->dsp_uri : "??");
		return -1;
	}
	if ((desc->nports_midi_in + desc->nports_atom_in) > 1 || (desc->nports_midi_out + desc->nports_atom_out) > 1)
	{
		fprintf (stderr, "Unsupported LV2 Plugin '%s' (> 1 atom port)\n", desc->dsp_uri ? desc->dsp_uri : "??");
		return -1;
	}
	return 0;
}

/* ****************************************************************************
 * public API
 */

RtkLv2Description* get_desc_by_uri (const char* uri)
{
	RtkLv2Description* desc = (RtkLv2Description*) calloc (1, sizeof (RtkLv2Description));
	LV2Parser lp (desc);
	if (lp.parse (uri)) {
		free_desc (desc);
		return NULL;
	}
	if (verify_support (desc)) {
		free_desc (desc);
		return NULL;
	}
	return desc;
}

void clear_desc (RtkLv2Description* desc)
{
	if (!desc) { return; }
	free (desc->dsp_uri);
	free (desc->gui_uri);
	free (desc->plugin_name);
	free (desc->vendor);
	free (desc->bundle_path);
	free (desc->dsp_path);
	free (desc->gui_path);
	for (uint32_t i = 0; i < desc->nports_total; ++i) {
		free (desc->ports[i].name);
		free (desc->ports[i].symbol);
		free (desc->ports[i].doc);
	}
	free (desc->ports);
	memset (desc, 0, sizeof (RtkLv2Description));
}

void free_desc (RtkLv2Description* desc)
{
	if (!desc) { return; }
	clear_desc (desc);
	free (desc);
}

int lv2ls (char*** uris, char*** names)
{
	int n_plugins = 0;
	LilvWorld* world = lilv_world_new ();
	const LilvPlugins* all_plugins;
	LilvIter* iter;

	lilv_world_load_all (world);
	all_plugins = lilv_world_get_all_plugins (world);
	iter = lilv_plugins_begin (all_plugins);

	RtkLv2Description* desc = (RtkLv2Description*) calloc (1, sizeof (RtkLv2Description));
	LV2Parser lp (desc);

	while (!lilv_plugins_is_end (all_plugins, iter)) {
		const LilvPlugin* p = lilv_plugins_get (all_plugins, iter);

#if 1
		clear_desc (desc);
		if (lp.parse (lilv_node_as_string (lilv_plugin_get_uri (p)), p)) {
			iter = lilv_plugins_next (all_plugins, iter);
			continue;
		}
		if (verify_support (desc)) {
			iter = lilv_plugins_next (all_plugins, iter);
			continue;
		}
#endif

		*uris = (char**) realloc (*uris, (n_plugins + 2) * sizeof (char*));
		*names = (char**) realloc (*names, (n_plugins + 2) * sizeof (char*));

		(*uris)[n_plugins] = strdup (lilv_node_as_string (lilv_plugin_get_uri (p)));
		(*uris)[n_plugins + 1] = NULL;
		(*names)[n_plugins] = strdup (lilv_node_as_string (lilv_plugin_get_name (p)));
		(*names)[n_plugins + 1] = NULL;

		//printf ("%s -- %s\n", (*uris)[n_plugins], (*names)[n_plugins]);
		++n_plugins;

		iter = lilv_plugins_next (all_plugins, iter);
	}
	free_desc (desc);
	return n_plugins;
}

void lv2free (char** uris, char** names)
{
	// TODO iterate over uris, names and free strings
	free (*uris);
	free (*names);
}
