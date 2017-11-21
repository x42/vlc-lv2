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
#include <stdlib.h>
#include <stdint.h>

#include "loadlib.h"
#include "lv2plugin.h"

using namespace Lv2Vlc;

static void write_function (
		LV2UI_Controller controller,
		uint32_t         port_index,
		uint32_t         buffer_size,
		uint32_t         port_protocol,
		const void*      buffer)
{
	LV2PluginUI* ui = (LV2PluginUI*) controller;
	if (buffer_size == 0) {
		return;
	}

	ui->write_to_dsp (port_index, buffer_size, port_protocol, buffer);
}

LV2PluginUI::LV2PluginUI (LV2Plugin* effect)
	: _lv2plugin (effect)
	, plugin_gui (0)
	, gui_instance (0)
	, _widget (0)
	, _idle_iface (0)
	, _atombuf (0)
	, _width (100)
	, _height (100)
	, _queue_resize (false)
	, _lib_handle (0)
{
	RtkLv2Description const* desc = _lv2plugin->desc ();

	if (!desc->gui_path) {
		return;
	}

	_lib_handle = open_lv2_lib (desc->gui_path);
	const LV2UI_Descriptor* (*lv2ui_descriptor)(uint32_t index) =
		(const LV2UI_Descriptor* (*)(uint32_t)) x_dlfunc (_lib_handle, "lv2ui_descriptor");

	uint32_t index = 0;
	while (lv2ui_descriptor) {
		plugin_gui = lv2ui_descriptor (index);
		if (!plugin_gui) { break; }
		if (!strcmp (plugin_gui->URI, desc->gui_uri)) { break; }
		++index;
	}

	if (!plugin_gui) {
		return;
	}

	_atombuf = (LV2_Atom_Sequence*) malloc (desc->min_atom_bufsiz * sizeof (uint8_t));
	_uri_atom_EventTransfer = _lv2plugin->map_uri (LV2_ATOM__eventTransfer);
}

LV2PluginUI::~LV2PluginUI ()
{
	if (plugin_gui && gui_instance && plugin_gui->cleanup) {
		plugin_gui->cleanup (gui_instance);
	}
	free (_atombuf);
	close_lv2_lib (_lib_handle);
}

bool LV2PluginUI::open (void* ptr)
{
	if (!plugin_gui || gui_instance) {
		return false;
	}

	uri_map.handle = _lv2plugin->map_instance ();
	uri_map.map = &Lv2UriMap::uri_to_id;
	uri_unmap.handle = uri_map.handle;
	uri_unmap.unmap = &Lv2UriMap::id_to_uri;

	lv2ui_resize.handle = this;
	lv2ui_resize.ui_resize = &LV2PluginUI::ui_resize;

	const LV2_Feature resize_feature   = { LV2_UI__resize, &lv2ui_resize};
	const LV2_Feature parent_feature   = { LV2_UI__parent, ptr};
	const LV2_Feature map_feature      = { LV2_URID__map, &uri_map};
	const LV2_Feature unmap_feature    = { LV2_URID__unmap, &uri_unmap };
	const LV2_Feature instance_feature = { LV2_INSTANCE_ACCESS_URI, _lv2plugin->plugin_instance ()};

	const LV2_Feature* ui_features[] = {
		&map_feature, &unmap_feature,
		&resize_feature,
		&parent_feature,
		&instance_feature,
		NULL
	};

	gui_instance = plugin_gui->instantiate (plugin_gui,
			_lv2plugin->plugin_dsp ()->URI, _lv2plugin->bundle_path (),
			&write_function, (void*)this,
			(void **)&_widget, ui_features);

	if (!is_open () || !_widget) {
		return false;
	}

	if (plugin_gui->extension_data) {
		_idle_iface = (LV2UI_Idle_Interface*) plugin_gui->extension_data (LV2_UI__idleInterface);
	}

	idle ();
	idle ();

	return true;
}

bool LV2PluginUI::is_open () const
{
	return (gui_instance != 0);
}

void LV2PluginUI::close ()
{
	if (plugin_gui && gui_instance && plugin_gui->cleanup) {
		plugin_gui->cleanup (gui_instance);
	}

	gui_instance = 0;
}

void LV2PluginUI::idle ()
{
	if (!gui_instance) {
		return;
	}

	while (_lv2plugin->ctrl_to_ui.read_space () >= 1) {
		LV2Plugin::ParamVal pv;
		_lv2plugin->ctrl_to_ui.read (&pv, 1);
		plugin_gui->port_event (gui_instance, pv.p, sizeof (float), 0, &pv.v);
	}

	const uint32_t portmap_atom_to_ui = _lv2plugin->portmap_atom_to_ui ();

	while (_lv2plugin->atom_to_ui.read_space () > sizeof (LV2_Atom) && portmap_atom_to_ui != UINT32_MAX) {
		LV2_Atom a;
		_lv2plugin->atom_to_ui.read ((char *) &a, sizeof (LV2_Atom));
		_lv2plugin->atom_to_ui.read ((char *) _atombuf, a.size);
		LV2_Atom_Event const* ev = (LV2_Atom_Event const*)((&(_atombuf)->body) + 1); // lv2_atom_sequence_begin
		while ((const uint8_t*)ev < ((const uint8_t*) &(_atombuf)->body + (_atombuf)->atom.size)) {
			plugin_gui->port_event (gui_instance, portmap_atom_to_ui,
					ev->body.size, _uri_atom_EventTransfer, &ev->body);
			ev = (LV2_Atom_Event const*) /* lv2_atom_sequence_next() */
				((const uint8_t*)ev + sizeof (LV2_Atom_Event) + ((ev->body.size + 7) & ~7));
		}
	}

	if (_idle_iface) {
		_idle_iface->idle (gui_instance);
	}
}

void
LV2PluginUI::write_to_dsp (uint32_t port_index, uint32_t buffer_size, uint32_t port_protocol, const void* buffer)
{
	if (port_protocol != 0) {
		if (_lv2plugin->atom_from_ui.write_space () >= buffer_size + sizeof (LV2_Atom)) {
			LV2_Atom a = {buffer_size, 0};
			_lv2plugin->atom_from_ui.write ((char *) &a, sizeof (LV2_Atom));
			_lv2plugin->atom_from_ui.write ((char *) buffer, buffer_size);
		}
		return;
	}

	if (buffer_size != sizeof (float)) {
		return;
	}

	RtkLv2Description const* desc = _lv2plugin->desc ();
	if (desc->ports[port_index].porttype != CONTROL_IN) {
		fprintf (stderr, "LV2Host: write_function() not a control input\n");
		return;
	}

	/* set control port */
	float val = *((float*)buffer);
	_lv2plugin->set_parameter (port_index, val);
}
