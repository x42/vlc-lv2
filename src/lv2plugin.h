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

#ifndef __lv2_plugin__
#define __lv2_plugin__

#include <math.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/uri-map/uri-map.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/instance-access/instance-access.h"

#include "lv2desc.h"
#include "ringbuffer.h"
#include "uri_map.h"
#include "worker.h"

struct URIs {
	LV2_URID midi_MidiEvent;
	LV2_URID atom_Sequence;
	LV2_URID atom_EventTransfer;

	LV2_URID atom_Float;
	LV2_URID atom_Int;

	LV2_URID param_sampleRate;
	LV2_URID bufsz_minBlockLength;
	LV2_URID bufsz_maxBlockLength;
	LV2_URID bufsz_sequenceSize;
};

class LV2Plugin;

class LV2PluginUI
{
	public:
		LV2PluginUI (LV2Plugin*);
		~LV2PluginUI ();

		bool open (void* ptr);
		void close ();
		bool is_open () const;
		void idle ();

		bool has_editor () const { return plugin_gui != NULL; }
		void write_to_dsp (uint32_t port_index, uint32_t buffer_size, uint32_t port_protocol, const void* buffer);

		static int ui_resize (LV2UI_Feature_Handle handle, int width, int height) {
			LV2PluginUI* self = (LV2PluginUI*) handle;
			self->set_size (width, height);
			return 0;
		}

		void set_size (int width, int height) {
			_width  = width;
			_height = height;
			_queue_resize = true;
		}

		bool need_resize (int& width, int& height) {
			if (!_queue_resize) {
				return false;
			}
			width  = _width;
			height = _height;
			_queue_resize = false;
			return true;
		}

	protected:
		LV2Plugin* _lv2plugin;

		const LV2UI_Descriptor *plugin_gui;
		LV2UI_Handle gui_instance;

		LV2UI_Widget _widget;

		LV2_URID_Map   uri_map;
		LV2_URID_Unmap uri_unmap;
		LV2UI_Resize   lv2ui_resize;

		LV2UI_Idle_Interface* _idle_iface;
		LV2_Atom_Sequence* _atombuf;
		LV2_URID _uri_atom_EventTransfer;

		int  _width;
		int  _height;
		bool _queue_resize;

		void* _lib_handle;
};

class LV2Plugin
{
	public:
		LV2Plugin (RtkLv2Description*, float rate);
		~LV2Plugin ();

		void process (float**, int32_t);

		bool set_parameter (int32_t, float);
		LV2PluginUI& ui () { return _ui; }

		void resume ();
		void suspend ();

		int32_t save_state (void** data);
		int32_t load_state (void* data, int32_t size);

		struct LV2PortProperty {
			uint32_t key;
			uint32_t type;
			uint32_t flags;
			uint32_t size;
			void*    value;
		};

		struct LV2PortValue {
			float    value;
			char*    symbol;
		};

		struct LV2State {
			uint32_t         n_props;
			uint32_t         n_values;
			LV2PortProperty* props;
			LV2PortValue*    values;
		};

	private:
		friend class LV2PluginUI;

		LV2_Handle plugin_instance () const { return _plugin_instance; }
		LV2_Descriptor const* plugin_dsp () const { return _plugin_dsp; }
		RtkLv2Description const* desc () const { return _desc; }
		const char* bundle_path () const { return _desc->bundle_path; }

		uint32_t portmap_atom_to_ui () const { return _portmap_atom_to_ui; }

		struct ParamVal {
			ParamVal () : p (0) , v (0) {}
			ParamVal (uint32_t pp, float vv) : p (pp), v (vv) {}
			uint32_t p;
			float    v;
		};

		Lv2VlcUtil::RingBuffer<struct ParamVal> ctrl_to_ui;
		Lv2VlcUtil::RingBuffer<char> atom_to_ui;
		Lv2VlcUtil::RingBuffer<char> atom_from_ui;

		void* map_instance () const { return (void*)&_map; }
		LV2_URID map_uri (const char* uri) {
			return _map.uri_to_id (uri);
		}

	private:
		void init ();
		void deinit ();

		size_t serialize_state (LV2State* state, void** data);
		LV2State* unserialize_state (void* data, size_t s);

		RtkLv2Description*     _desc;
		const LV2_Descriptor*  _plugin_dsp;
		LV2_Handle             _plugin_instance;

		float _sample_rate;

		Lv2Vlc::Lv2UriMap  _map;
		LV2PluginUI        _ui;
		Lv2Vlc::Lv2Worker* _worker;
		URIs               _uri;

		LV2_Atom_Forge        lv2_forge;
		LV2_Worker_Schedule   schedule;
		LV2_URID_Map          uri_map;
		LV2_URID_Unmap        uri_unmap;

		const LV2_Worker_Interface* worker_iface;

		LV2_Atom_Sequence* _atom_in;
		LV2_Atom_Sequence* _atom_out;
		uint32_t _portmap_atom_to_ui;
		uint32_t _portmap_atom_from_ui;

		float* _ports;
		float* _ports_pre;

		bool _ui_sync;
		bool _active;

		void* _lib_handle;
		const LV2_Descriptor* (*lv2_descriptor)(uint32_t index);
};
#endif
