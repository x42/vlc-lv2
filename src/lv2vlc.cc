#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_block.h>
#include <vlc_modules.h>
#include <vlc_variables.h>
#include <vlc_vout.h>
#include <vlc_vout_window.h>

#include "lv2desc.h"
#include "lv2ttl.h"
#include "lv2plugin.h"

/* save/restore plugin-state in memory */
#define VOLATILE_STATE 1

struct filter_sys_t
{
	RtkLv2Description* desc;
	LV2Plugin*         plugin;
	unsigned int       n_chn;
	float**            buffers;

	/* GUI */
	vlc_thread_t thread;
	vlc_sem_t    ready;
	bool         run_ui;
};

static void*
GUIThread (void *p_data)
{
	filter_t  *p_filter = (filter_t*)p_data;
	filter_sys_t *p_sys = p_filter->p_sys;

	vout_window_cfg_t cfg;

	cfg.is_standalone = true;
	cfg.x = 0;
	cfg.y = 0;
	cfg.width = 100;
	cfg.height = 100;

#if defined(_WIN32)
	cfg.type = VOUT_WINDOW_TYPE_HWND;
#elif defined(__APPLE__)
	cfg.type = VOUT_WINDOW_TYPE_NSOBJECT;
#else
	cfg.type = VOUT_WINDOW_TYPE_XID;
#endif

#ifdef VLC3API
	vout_window_t* window = vout_window_New (VLC_OBJECT (p_filter), "$window", &cfg, NULL);
#else
	vout_window_t* window = vout_window_New (VLC_OBJECT (p_filter), "$window", &cfg);
#endif

#if defined(_WIN32)
	void* handle = (void*) window->handle.hwnd;
#elif defined(__APPLE__)
	void* handle = (void*) window->handle.nsobject; // NSView*
#else
	void* handle = (void*) (intptr_t)window->handle.xid;
#endif

	if (!p_sys->plugin->ui ().open (handle)) {
		vlc_sem_post (&p_sys->ready);
		vout_window_Delete (window);
		return NULL;
	}

	vlc_sem_post (&p_sys->ready);

	while (p_sys->run_ui) {
		// TODO: forward size-requests for X11 UI
		int w, h;
		p_sys->plugin->ui ().idle ();
		if (p_sys->plugin->ui ().need_resize (w, h)) {
			vout_window_Control (window, VOUT_WINDOW_SET_SIZE, w, h);
		}
		// 25fps ~ 40ms
#if defined(_WIN32)
		Sleep (40);
#else
		usleep (40000);
#endif
	}

	p_sys->plugin->ui ().close ();
	vout_window_Delete (window);
	return NULL;
}

static block_t*
Process (filter_t* p_filter, block_t* block)
{
	filter_sys_t *p_sys = p_filter->p_sys;
	float* ibp = (float*)block->p_buffer;
	float* obp = (float*)block->p_buffer;
	size_t n_samples = block->i_nb_samples;
	size_t n_chn = p_filter->fmt_in.audio.i_channels;

	assert (n_chn == p_sys->n_chn);

	// de-interleave and split into at most 8192 sample chunks
	// TODO: optimize, map channels
	uint32_t n_proc = 0;
	for (size_t s = 0; s < n_samples; ++s) {
		for (size_t c = 0; c < n_chn; ++c) {
			p_sys->buffers[c][n_proc] = *(ibp++);
		}
		if (++n_proc == 8192) {
			p_sys->plugin->process (p_sys->buffers, n_proc);
			for (size_t t = 0; t < n_proc; ++t) {
				for (size_t d = 0; d < n_chn; ++d) {
					*(obp++) = p_sys->buffers[d][t];
				}
			}
			n_proc = 0;
		}
	}
	if (n_proc > 0) {
		p_sys->plugin->process (p_sys->buffers, n_proc);
		for (size_t t = 0; t < n_proc; ++t) {
			for (size_t d = 0; d < n_chn; ++d) {
				*(obp++) = p_sys->buffers[d][t];
			}
		}
	}

	return block;
}


#if VOLATILE_STATE // TODO save to disk, per plugin URI
static void*   lv2_plugin_state_data = NULL;
static int32_t lv2_plugin_state_size = 0;
#endif

static int
Open (vlc_object_t* obj)
{
	filter_t* p_filter = (filter_t*)obj;
	filter_sys_t *p_sys = p_filter->p_sys = (filter_sys_t*) malloc (sizeof (*p_sys));
	if (!p_sys) {
		return VLC_EGENERIC;
	}

	fprintf (stderr, "LV2VLC Open %d %d\n", p_filter->fmt_in.audio.i_rate, p_filter->fmt_in.audio.i_channels);

	p_filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
	p_filter->fmt_out.audio = p_filter->fmt_in.audio;
	p_sys->n_chn = p_filter->fmt_in.audio.i_channels;

	// TODO "Host wrapper" for multiple plugins.
	// also handle channel mapping (replicate plugin instance as needed ...)

	p_sys->desc = get_desc_by_uri (var_CreateGetStringCommand (p_filter, "uri"));

	if (!p_sys->desc) {
		free (p_sys);
		return VLC_EGENERIC;
	}

	if (p_sys->desc->nports_audio_in != p_sys->desc->nports_audio_out || p_sys->desc->nports_audio_in != p_sys->n_chn) {
		fprintf (stderr, "Skipping LV2 plugin -- mismatched channel count\n");
		free_desc (p_sys->desc);
		free (p_sys);
		return VLC_EGENERIC;
	}

	try {
		p_sys->plugin = new LV2Plugin (p_sys->desc, p_filter->fmt_in.audio.i_rate);
	} catch (...) {
		free_desc (p_sys->desc);
		free (p_sys);
		return VLC_EGENERIC;
	}

	/* Create GUI thread */
	vlc_sem_init (&p_sys->ready, 0);
	if (p_sys->plugin->ui ().has_editor ()) {
		p_sys->run_ui = true;
		if (vlc_clone (&p_sys->thread, GUIThread, p_filter, VLC_THREAD_PRIORITY_VIDEO)) {
			delete p_sys->plugin; // free()s p_sys->desc
			vlc_sem_destroy (&p_sys->ready);
			free (p_sys);
			return VLC_EGENERIC;
		}
		/* Wait for the ui thread. */
		vlc_sem_wait (&p_sys->ready);
	} else {
		p_sys->run_ui = false;
	}

	/* allocate non-interleaved buffers */

	p_sys->buffers = (float**) malloc (sizeof (float*) * p_sys->n_chn);
	for (unsigned int c = 0; c < p_sys->n_chn; ++c) {
		p_sys->buffers[c] = (float*) malloc (8192 * sizeof (float));
		// TODO catch OOM.
	}

	p_filter->pf_audio_filter = Process;
#if VOLATILE_STATE
	if (lv2_plugin_state_data) {
		p_sys->plugin->load_state (lv2_plugin_state_data, lv2_plugin_state_size);
	}
#endif
	return VLC_SUCCESS;
}

static void
Close (vlc_object_t* obj)
{
	filter_t* p_filter = (filter_t*)obj;
	filter_sys_t *p_sys = p_filter->p_sys;

#if VOLATILE_STATE
	lv2_plugin_state_size =  p_sys->plugin->save_state (&lv2_plugin_state_data);
#endif
	/* Terminate GUI thread. */
	if (p_sys->run_ui) {
		p_sys->run_ui = false;
		vlc_join (p_sys->thread, NULL);
	}
	vlc_sem_destroy (&p_sys->ready);

	delete p_sys->plugin; // free()s p_sys->desc

	for (unsigned int c = 0; c < p_sys->n_chn; ++c) {
		free (p_sys->buffers[c]);
	}
	free (p_sys->buffers);
	free (p_sys);
}


// TODO: free on module unload
static char** uris = NULL;
static char** names = NULL;
static int n_plugs = 0;

vlc_module_begin ()
	if (!uris) {
		n_plugs = lv2ls (&uris, &names);
	}
	set_shortname ("LV2")
	set_description ("Load LV2 Audio Plugins")
	set_callbacks (Open, Close)
	set_capability ("audio filter", 0)
	set_category (CAT_AUDIO)
	set_subcategory (SUBCAT_AUDIO_AFILTER)

	add_string ("uri", "", "Plugin", "Select Plugin", false)
	vlc_config_set (VLC_CONFIG_LIST, n_plugs, uris, names);
vlc_module_end ()
