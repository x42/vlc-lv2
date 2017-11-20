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

#ifndef _worker_h_
#define _worker_h_

#include <vlc_common.h>
#include <vlc_threads.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"

#include "ringbuffer.h"

namespace Lv2Vlc {

class Lv2Worker
{
	public:
		Lv2Worker (const LV2_Worker_Interface* iface, LV2_Handle handle);
		~Lv2Worker ();

		static LV2_Worker_Status lv2_worker_schedule (
				LV2_Worker_Schedule_Handle handle,
				uint32_t size,
				const void* data)
		{
			Lv2Worker* self = (Lv2Worker*) handle;
			return self->schedule (size, data);
		}

		LV2_Worker_Status schedule (uint32_t size, const void* data);
		LV2_Worker_Status respond (uint32_t size, const void* data);
		void emit_response ();
		void set_freewheeling (bool yn) { _freewheeling = yn; }
		void run ();
		void end_run () {
			if (_iface->end_run) {
				_iface->end_run (_handle);
			}
		}

	private:
		Lv2VlcUtil::RingBuffer<char> _requests;
		Lv2VlcUtil::RingBuffer<char> _responses;

		const LV2_Worker_Interface*  _iface;
		LV2_Handle                   _handle;

		vlc_thread_t                 _thread;
		vlc_mutex_t                  _lock;
		vlc_cond_t                   _ready;
		volatile bool                _run;
		bool                         _freewheeling;
};

} /* namespace */
#endif
