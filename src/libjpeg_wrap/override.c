/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2010 IGEL Co.,Ltd.
 * Copyright (C) 2008,2009 Renesas Technology Corp.
 * Copyright (C) 2008 Denis Oliver Kropp
 *
 * This library is dual licensed.
 * You are free to use this library under either the MIT or
 * the GNU LGPL version 2 license.
 *
 * For more information please refer to the licensing files
 * in the root directory of this library package.
 *
 * GNU LGPL license: COPYING_LGPL
 * MIT license: COPYING_MIT
 */

#include <string.h>
#include <pthread.h>
#include "shjpeg_internal.h"
#include "jpeg_io.h"
#include "override.h"
#include "libmessage.h"
#include "shjpeg/shjpeg.h"
#include "shjpeg/shjpeg_types.h"

extern hooks_t libjpeg_hooks, *active_hooks;
static pthread_mutex_t ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

cinfo_context_t *context_list_head = NULL;

cinfo_context_t *get_cinfo_context(j_common_ptr cinfo) {
	pthread_mutex_lock(&ctx_mutex);
	/* first check if we have a matching pointer */
	cinfo_context_t *ctx = context_list_head;
	while (ctx) {
		if (ctx->cinfo == cinfo) {
			pthread_mutex_unlock(&ctx_mutex);
			return ctx;
		}
		ctx = ctx->next;
	}
	pthread_mutex_unlock(&ctx_mutex);
	return NULL;
}

cinfo_context_t *create_cinfo_context(j_common_ptr cinfo) {
	cinfo_context_t *ctx;
	if ((ctx = get_cinfo_context(cinfo)))
		return ctx;
	ctx = malloc (sizeof (*ctx));
	memset(ctx, 0, sizeof (*ctx));
	ctx->cinfo = cinfo;
	ctx->context = NULL;
	ctx->prev = NULL;
	pthread_mutex_lock(&ctx_mutex);
	ctx->next = context_list_head;
	if (ctx->next)
		ctx->next->prev = ctx;
	context_list_head = ctx;
	pthread_mutex_unlock(&ctx_mutex);
	return ctx;
}

void free_cinfo_context(cinfo_context_t *ctx) {
	pthread_mutex_lock(&ctx_mutex);
	if (ctx->prev) {
		ctx->prev->next = ctx->next;
	} else {
		context_list_head = ctx->next;
	}
	if (ctx->next)
		ctx->next->prev = ctx->prev;

	free(ctx);
	pthread_mutex_unlock(&ctx_mutex);
}

shjpeg_sops jpeg_src_ops = {
	.init = jpeg_src_init,
	.read = jpeg_src_read,
	.write = NULL,
	.finalize = jpeg_src_finalize,
};

shjpeg_sops jpeg_dest_ops = {
	.init = jpeg_dest_init,
	.read = NULL,
	.write = jpeg_dest_write,
	.finalize = jpeg_dest_finalize,
};


static shjpeg_pixelformat get_shjpeg_pixelformat(J_COLOR_SPACE color_space)
{
	switch (color_space) {
	case JCS_RGB:
		return SHJPEG_PF_RGB24;
#ifdef ANDROID_RGB
	case JCS_RGBA_8888:
		return SHJPEG_PF_RGB32;
	case JCS_RGB_565:
		return SHJPEG_PF_RGB16;
#endif
	case JCS_YCbCr:
		return SHJPEG_PF_YCbCr;
	case JCS_GRAYSCALE:
	default:
		return SHJPEG_PF_NONE;
	}
}
static boolean is_jpu_supported_decompress(j_decompress_ptr cinfo)
{
	/*If no file input specified revert to libjpeg */
	if (!cinfo->src) {
		return FALSE;
	}
	/*We do not support on-the-fly colormapping */
	if (cinfo->output_components != cinfo->out_color_components)
		return FALSE;

	/*Check for a valid colour space */
	if (get_shjpeg_pixelformat(cinfo->out_color_space) ==
	    SHJPEG_PF_NONE)
		return FALSE;

	/*Use shjpeg unless 4:4:4 colour mode */
	if (cinfo->jpeg_color_space != JCS_YCbCr ||
	    ((cinfo->comp_info[1].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor) &&
	    (cinfo->comp_info[1].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor) &&
	    (cinfo->comp_info[2].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor) &&
	    (cinfo->comp_info[2].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor)))
		return FALSE;

	return TRUE;

}

void
shjpeg_CreateDecompress(j_decompress_ptr cinfo, int version,
			size_t structsize)
{
	/*double check the version again here, since we may be compiled
	   against a different version from both the loaded library and the
	   appication */
	if (version != JPEG_LIB_VERSION) {
		cinfo->mem = NULL;
		ERREXIT2(cinfo, SHJMSG_BAD_VERSION, JPEG_LIB_VERSION,
			 version);
	}
	libjpeg_hooks.jpeg_CreateDecompress(cinfo, version, structsize);
	create_cinfo_context((j_common_ptr) cinfo);
}

int shjpeg_read_header(j_decompress_ptr cinfo, boolean require_image)
{
	int ret;
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	ctx->cache_con.fill_buffer_function = cinfo->src->fill_input_buffer;
	cinfo->src->fill_input_buffer = cache_input_buffer;

	ctx->cache_con.total_buffer_size = 0;
	ctx->cache_con.last_buffer_size = cinfo->src->bytes_in_buffer;
	ctx->cache_con.current_start = ctx->cache_con.current_read =
	    (void *) cinfo->src->next_input_byte;

	ret = libjpeg_hooks.jpeg_read_header(cinfo, require_image);
	cinfo->src->fill_input_buffer = ctx->cache_con.fill_buffer_function;
	memcpy(&ctx->sops, &jpeg_src_ops, sizeof(jpeg_src_ops));
	ctx->sops.read = jpeg_src_read_header;
	return ret;
}

boolean shjpeg_start_decompress(j_decompress_ptr cinfo)
{
	shjpeg_pixelformat format;
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;
	shjpeg_internal_t *data;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	if (!is_jpu_supported_decompress(cinfo)) {
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		return libjpeg_hooks.jpeg_start_decompress(cinfo);
	}
	TRACEMS(cinfo, 1, SHJMSG_JPU_MODE);

	/*libjpeg should be initialized and the headers should
	   have been read by this point, so we can skip over that
	   part of the shjpeg_decode_init */

	CALL_API_FUNC(jpeg_calc_output_dimensions, cinfo)
	ctx->context = shjpeg_init(1);
	context = ctx->context;
	if (!context) {
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		return libjpeg_hooks.jpeg_start_decompress(cinfo);
	}
	context->width = cinfo->output_width;
	context->height = cinfo->output_height;
	context->pitch = context->width * cinfo->out_color_components;
	context->pitch = (context->pitch + 7) & ~7;	// 8 byte align
	data = context->internal_data;

	ctx->hardware_buf.bufsize = context->pitch * context->height;
	ctx->hardware_buf.virt_addr = uiomux_malloc(data->uiomux, UIOMUX_JPU,
		ctx->hardware_buf.bufsize, 1);

	if (!ctx->hardware_buf.virt_addr) {
		TRACEMS(cinfo, 1, SHJMSG_NO_MEMORY);
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		return libjpeg_hooks.jpeg_start_decompress(cinfo);
	}

	ctx->hardware_buf.phys_addr = uiomux_virt_to_phys(data->uiomux,
		UIOMUX_JPU, ctx->hardware_buf.virt_addr);

	context->mode444 = 0;

	/* True if 4:2:0 */
	context->mode420 =
	    (cinfo->comp_info[1].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor / 2) &&
	    (cinfo->comp_info[1].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor / 2) &&
	    (cinfo->comp_info[2].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor / 2) &&
	    (cinfo->comp_info[2].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor / 2);

	context->libjpeg_disabled = 1;
	if (!ctx->sops.init)
		memcpy(&ctx->sops, &jpeg_src_ops, sizeof(jpeg_src_ops));
	context->sops = &ctx->sops;

	context->priv_data = (void *) ctx;

	format = get_shjpeg_pixelformat(cinfo->out_color_space);

	if (shjpeg_decode_run(context, format,
			      ctx->hardware_buf.virt_addr, context->width,
			      context->height, context->pitch) < 0) {
		//this is not a recoverable failure... abort
		shjpeg_decode_shutdown(context);
		shjpeg_shutdown(context);
		return libjpeg_hooks.jpeg_start_decompress(cinfo);
	}
	ctx->jpumode = 1;
	return TRUE;
}

JDIMENSION
shjpeg_read_scanlines(j_decompress_ptr cinfo, JSAMPARRAY scanlines,
		      JDIMENSION max_lines)
{
	int linecnt = 0;
	void *buffer;
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	if (!ctx->jpumode)
		return libjpeg_hooks.jpeg_read_scanlines(cinfo,
				scanlines, max_lines);

	context = ctx->context;

	buffer = ctx->hardware_buf.virt_addr;
	max_lines = max_lines + cinfo->output_scanline > context->height ?
	    context->height - cinfo->output_scanline : max_lines;
	for (linecnt = 0; linecnt < max_lines; linecnt++) {
		memcpy(scanlines[linecnt],
		       buffer + (cinfo->output_scanline * context->pitch),
		       context->width * cinfo->out_color_components);
		cinfo->output_scanline++;
	}
	return max_lines;
}

boolean shjpeg_finish_decompress(j_decompress_ptr cinfo)
{
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;
	shjpeg_internal_t *data;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	if (!ctx->jpumode)
		return libjpeg_hooks.jpeg_finish_decompress(cinfo);

	context = ctx->context;
	data = context->internal_data;
	uiomux_free(data->uiomux, UIOMUX_JPU, ctx->hardware_buf.virt_addr,
		ctx->hardware_buf.bufsize);
	memset(&ctx->hardware_buf, 0, sizeof (ctx->hardware_buf));

	if (context->sops->finalize) {
		context->sops->finalize(context->priv_data);
	}
	ctx->jpumode = 0;
	CALL_API_FUNC(jpeg_abort_decompress, cinfo)
	return TRUE;
}

/*Compress Object functions*/

/*shjpeg_CreateCompress
 * double check the version again here, since we may be compiled
 * against a different version from both the loaded library and the
 * appication */
void
shjpeg_CreateCompress(j_compress_ptr cinfo, int version, size_t structsize)
{
	if (version != JPEG_LIB_VERSION) {
		cinfo->mem = NULL;
		ERREXIT2(cinfo, SHJMSG_BAD_VERSION, JPEG_LIB_VERSION,
			 version);
	}
	libjpeg_hooks.jpeg_CreateCompress(cinfo, version, structsize);
	create_cinfo_context((j_common_ptr) cinfo);
}

static boolean is_jpu_supported_compress(j_compress_ptr cinfo, boolean wat)
{
	/*If no file output specified */
	if (!cinfo->dest) {
		return FALSE;
	}
	if (!wat)
		return FALSE;
	if (get_shjpeg_pixelformat(cinfo->in_color_space) ==
	    SHJPEG_PF_NONE)
		return FALSE;
	return TRUE;
}

void shjpeg_start_compress(j_compress_ptr cinfo, boolean write_all_tables)
{
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;
	shjpeg_internal_t *data;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);


	if (!is_jpu_supported_compress(cinfo, write_all_tables)) {
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		libjpeg_hooks.jpeg_start_compress(cinfo, write_all_tables);
		return;
	}
	TRACEMS(cinfo, 1, SHJMSG_JPU_MODE);
	ctx->context = shjpeg_init(1);
	context = ctx->context;
	if (!context) {
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		libjpeg_hooks.jpeg_start_compress(cinfo, write_all_tables);
		return;
	}
	context->width = cinfo->image_width;
	context->height = cinfo->image_height;
	context->pitch = context->width * cinfo->input_components;
	context->pitch = (context->pitch + 7) & ~7;	// 8 byte align
	data = context->internal_data;

	ctx->hardware_buf.bufsize = context->pitch * context->height;
	ctx->hardware_buf.virt_addr = uiomux_malloc(data->uiomux, UIOMUX_JPU,
		ctx->hardware_buf.bufsize, 1);

	if (!ctx->hardware_buf.virt_addr) {
		TRACEMS(cinfo, 1, SHJMSG_NO_MEMORY);
		TRACEMS(cinfo, 1, SHJMSG_LIBJPEG_MODE);
		libjpeg_hooks.jpeg_start_compress(cinfo, write_all_tables);
		return;
	}
	ctx->hardware_buf.phys_addr = uiomux_virt_to_phys(data->uiomux,
		UIOMUX_JPU, ctx->hardware_buf.virt_addr);

	context->libjpeg_disabled = 1;

	memcpy(&ctx->sops, &jpeg_dest_ops, sizeof(jpeg_dest_ops));
	context->sops = &ctx->sops;

	context->priv_data = (void *) ctx;

	ctx->jpumode = 1;
}

JDIMENSION
shjpeg_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines,
		       JDIMENSION num_lines)
{
	int linecnt = 0;
	void *buffer;
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	if (!ctx->jpumode)
		return libjpeg_hooks.jpeg_write_scanlines(cinfo,
				scanlines, num_lines);
	context = ctx->context;

	buffer = ctx->hardware_buf.virt_addr;
	num_lines = num_lines + cinfo->next_scanline > context->height ?
	    context->height - cinfo->next_scanline : num_lines;
	for (linecnt = 0; linecnt < num_lines; linecnt++) {
		memcpy(buffer + (cinfo->next_scanline * context->pitch),
		       scanlines[linecnt],
		       context->width * cinfo->input_components);
		cinfo->next_scanline++;
	}
	return num_lines;
}

void shjpeg_finish_compress(j_compress_ptr cinfo)
{
	shjpeg_pixelformat format;
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;
	shjpeg_internal_t *data;

	if (!ctx)
		ERREXIT(cinfo, SHJMSG_INVALID_CONTEXT);

	if (!ctx->jpumode)
		return libjpeg_hooks.jpeg_finish_compress(cinfo);

	context = ctx->context;
	format = get_shjpeg_pixelformat(cinfo->in_color_space);
	if (shjpeg_encode(context, format,
			  ctx->hardware_buf.virt_addr, context->width,
			  context->height, context->pitch) < 0) {
		ERREXIT(cinfo, SHJMSG_COMPRESS_ERR);
	}
	data = context->internal_data;
	uiomux_free(data->uiomux, UIOMUX_JPU, ctx->hardware_buf.virt_addr,
		ctx->hardware_buf.bufsize);
	if (context->sops->finalize) {
		context->sops->finalize(context->priv_data);
	}
	ctx->jpumode = 0;
}

/*redefine the error message table as a concatenation of the standard
  errors and our newly defined ones*/
#define JVERSION NULL
#define JCOPYRIGHT NULL
const char *shjpeg_message_table[] = {
#define JMESSAGE(code,string)   string ,
#include <jerror.h>
#define JMESSAGE(code,string)   "SHJPEG: "string ,
#include "libmessage.h"
	NULL
};

struct jpeg_error_mgr *shjpeg_std_error(struct jpeg_error_mgr *err)
{
	struct jpeg_error_mgr *ret;
	ret = libjpeg_hooks.jpeg_std_error(err);

	shjpeg_message_table[JMSG_VERSION - JMSG_NOMESSAGE] =
		err->jpeg_message_table[JMSG_VERSION - JMSG_NOMESSAGE];

	shjpeg_message_table[JMSG_COPYRIGHT - JMSG_NOMESSAGE] =
		err->jpeg_message_table[JMSG_COPYRIGHT - JMSG_NOMESSAGE];

	ret->jpeg_message_table = shjpeg_message_table;
	ret->last_jpeg_message = (int) SHJMSG_LASTMSGCODE - 1;
	return ret;
}

/*jpeg_abort and jpeg_destroy can be called at
  any time, so we cannot make assumptions about the current state*/
void shjpeg_abort(j_common_ptr cinfo)
{
	cinfo_context_t *ctx = get_cinfo_context((j_common_ptr) cinfo);
	shjpeg_context_t *context = NULL;

	if (ctx) {
		context = ctx->context;
		if (cinfo->is_decompressor) {
			libjpeg_hooks.jpeg_abort_decompress(&context->
							    jpeg_decomp);
		}
		if (ctx->hardware_buf.bufsize) {
			shjpeg_internal_t *data = context->internal_data;
			uiomux_free(data->uiomux, UIOMUX_JPU,
				ctx->hardware_buf.virt_addr,
				ctx->hardware_buf.bufsize);
			memset(&ctx->hardware_buf, 0,
					sizeof (ctx->hardware_buf));
		}
	}
	libjpeg_hooks.jpeg_abort(cinfo);
}

void shjpeg_destroy(j_common_ptr cinfo)
{
	cinfo_context_t *ctx = get_cinfo_context(cinfo);
	shjpeg_context_t *context = NULL;

	if (ctx) {
		context = ctx->context;
		free_cinfo_context(ctx);
	}
	if (context) {
		if (cinfo->is_decompressor) {
			shjpeg_decode_shutdown(context);
		}
		if (ctx->hardware_buf.bufsize) {
			shjpeg_internal_t *data = context->internal_data;
			uiomux_free(data->uiomux, UIOMUX_JPU,
				ctx->hardware_buf.virt_addr,
				ctx->hardware_buf.bufsize);
			memset(&ctx->hardware_buf, 0,
					sizeof (ctx->hardware_buf));
		}
		shjpeg_shutdown(context);
		context = NULL;
	}
	libjpeg_hooks.jpeg_destroy(cinfo);
}
