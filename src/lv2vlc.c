#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_block.h>
#include <vlc_modules.h>

struct filter_sys_t
{
	float dummy;
};

static block_t*
Process (filter_t* p_filter, block_t* block)
{
	float* buf = (float*)block->p_buffer;
	size_t n_samples = block->i_nb_samples;
	size_t n_chn = p_filter->fmt_in.audio.i_channels;

	//fprintf (stderr, "Test %ld %ld\n", n_samples, n_chn);
	for (size_t s = 0; s < n_samples; ++s) {
		for (size_t chn = 0; chn < n_chn; ++chn) {
			*(buf++) *= 2;
		}
	}

	return block;
}

static int
Open (vlc_object_t* obj)
{
	filter_t* p_filter = (filter_t*)obj;
	filter_sys_t *p_sys = p_filter->p_sys = (filter_sys_t*) malloc (sizeof(*p_sys));
	if (!p_sys) {
		return VLC_EGENERIC;
	}
	p_filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
	p_filter->fmt_out.audio = p_filter->fmt_in.audio;

	fprintf (stderr, "LV2VLC Open %d %d\n", p_filter->fmt_in.audio.i_rate, p_filter->fmt_in.audio.i_channels);
	// rate = p_filter->fmt_in.audio.i_rate
	// n_ch = p_filter->fmt_in.audio.i_channels

	p_filter->pf_audio_filter = Process;
	return VLC_SUCCESS;

}

static void
Close (vlc_object_t* obj)
{
	fprintf (stderr, "LV2VLC Close\n");
	filter_t* p_filter = (filter_t*)obj;
	free (p_filter->p_sys);
}

vlc_module_begin()
	set_shortname ("LV2")
	set_description ("Load LV2 Audio Plugins")
	set_callbacks (Open, Close)
	set_capability ("audio filter", 0)
	set_category (CAT_AUDIO)
	set_subcategory (SUBCAT_AUDIO_AFILTER)
vlc_module_end ()
