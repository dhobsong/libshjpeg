/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2009 IGEL Co.,Ltd.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <pthread.h>
#include <setjmp.h>

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#if defined(HAVE_SHVIO)
#include "shjpeg_vio.h"
#endif
#include "shjpeg_softhelper.h"

/*
 * Decode using H/W
 */

static int
decode_hw(shjpeg_internal_t * data,
	  shjpeg_context_t * context,
	  shjpeg_pixelformat format,
	  unsigned long phys, int width, int height, int pitch)
{
	int ret;
	unsigned int len;
	bool reload = false;
	shjpeg_jpu_t jpeg;
#if defined(HAVE_SHVIO)
	shjpeg_vio_t vio;
#endif
	vmap_data_t mdata;
	D_ASSERT(data != NULL);

	memset(&mdata, 0, sizeof(mdata));
#if defined(HAVE_SHVIO)
	memset((void*)&vio, 0, sizeof(shjpeg_vio_t));

	D_DEBUG_AT(SH7722_JPEG, "%s( %p, 0x%08lx|%d [%dx%d] %08x )",
		   __FUNCTION__, data, phys, pitch,
		   context->width, context->height, format);

	vio.dst.format = shjpeg_vio_color(format);
	if ((vio.dst.format == REN_UNKNOWN) &&
	    (format != SHJPEG_PF_YCbCr)) {
		D_BUG("unexpected format %08x", format);
		return -1;
	}
	vio.dst.pitch = pitch / size_y(vio.dst.format, 1, 0);
#else
	if ((context->mode420 && format != SHJPEG_PF_NV12) ||
	    (!context->mode420 && format != SHJPEG_PF_NV16)) {
		D_PERROR("libshjpeg: unexpected format %08x", format);
		return -1;
	}
#endif /* defined(HAVE_SHVIO) */

	D_DEBUG_AT(SH7722_JPEG, "		 -> locking JPU...");

	/* Locking JPU using uiomux_lock */
	if (uiomux_lock(data->uiomux, UIOMUX_JPU) < 0) {
		D_PERROR("libshjpeg: Could not lock JPEG engine!");
		return -1;
	}
	D_DEBUG_AT(SH7722_JPEG, "		 -> loading...");

	/* Fill first reload buffer. */
	if (!context->sops->read) {
		D_ERROR("libshjpeg: read operation not set!");
		return -1;
	}

	len = SHJPEG_JPU_RELOAD_SIZE;
	ret = context->sops->read(context->priv_data, &len,
				  (void *) data->jpeg_virt);
	if (ret) {
		D_DERROR(ret,
			 "libshjpeg: Could not fill first reload buffer!");
		if (uiomux_unlock(data->uiomux, UIOMUX_JPU) < 0) {
			D_PERROR("libshjpeg: unlock UIO failed.");
		}
		return -1;
	}

	D_DEBUG_AT(SH7722_JPEG, " -> %d/%dbytes filled",
		   len, SHJPEG_JPU_RELOAD_SIZE);
	D_DEBUG_AT(SH7722_JPEG, " -> setting...");

	/* Initialize JPEG state. */
	jpeg.state = SHJPEG_JPU_START;
	jpeg.flags = 0;
	jpeg.buffers = 1;

	/*
	 * Enable reload if buffer was filled completely (coded data
	 * length >= one reload buffer).
	 */
	if (len == SHJPEG_JPU_RELOAD_SIZE) {
		jpeg.flags |= SHJPEG_JPU_FLAG_RELOAD;
		reload = true;
	}

	/* Program JPU from RESET. */
	shjpeg_jpu_reset(data);

	shjpeg_jpu_setreg32(data, JPU_JCMOD,
			    JPU_JCMOD_INPUT_CTRL | JPU_JCMOD_DSP_DECODE);
	shjpeg_jpu_setreg32(data, JPU_JIFCNT, JPU_JIFCNT_VJSEL_JPU);
	shjpeg_jpu_setreg32(data, JPU_JIFECNT, JPU_JIFECNT_SWAP_4321);
	shjpeg_jpu_setreg32(data, JPU_JIFDSA1, data->jpeg_phys);
	shjpeg_jpu_setreg32(data, JPU_JIFDSA2,
			    data->jpeg_phys + SHJPEG_JPU_RELOAD_SIZE);
	shjpeg_jpu_setreg32(data, JPU_JIFDDRSZ,
			    ((u32)len + 255) & 0x00ffff00);

	if ((context->mode420 && format == SHJPEG_PF_NV12) ||
	    (!context->mode420 && format == SHJPEG_PF_NV16)) {
	/* Setup JPU for decoding in frame mode (directly to surface). */
		shjpeg_jpu_setreg32(data, JPU_JINTE,
				    JPU_JINTS_INS5_ERROR |
				    JPU_JINTS_INS6_DONE |
				    JPU_JINTS_INS10_XFER_DONE |
				    (reload ?  JPU_JINTS_INS14_RELOAD : 0));
		shjpeg_jpu_setreg32(data, JPU_JIFDCNT,
				    JPU_JIFDCNT_SWAP_4321 |
				    (reload ?  JPU_JIFDCNT_RELOAD_ENABLE : 0));
		shjpeg_jpu_setreg32(data, JPU_JIFDDYA1, phys);
		shjpeg_jpu_setreg32(data, JPU_JIFDDCA1,
				    phys + pitch * height);
		shjpeg_jpu_setreg32(data, JPU_JIFDDMW,
				    ((u32)pitch + 7) & ~0x07);
	} else {
		/* Setup JPU for decoding in line buffer mode. */
		shjpeg_jpu_setreg32(data, JPU_JINTE,
				    JPU_JINTS_INS5_ERROR |
				    JPU_JINTS_INS6_DONE |
				    JPU_JINTS_INS10_XFER_DONE |
				    JPU_JINTS_INS11_LINEBUF0 |
				    JPU_JINTS_INS12_LINEBUF1 |
				    (reload ?  JPU_JINTS_INS14_RELOAD : 0));

		shjpeg_jpu_setreg32(data, JPU_JIFDCNT,
				    JPU_JIFDCNT_LINEBUF_MODE |
				    (SHJPEG_JPU_LINEBUFFER_HEIGHT << 16) |
				    JPU_JIFDCNT_SWAP_4321 |
				    (reload ? JPU_JIFDCNT_RELOAD_ENABLE : 0));

		shjpeg_jpu_setreg32(data, JPU_JIFDDYA1, data->jpeg_lb1);
		shjpeg_jpu_setreg32(data, JPU_JIFDDCA1,
				    data->jpeg_lb1 +
				    SHJPEG_JPU_LINEBUFFER_SIZE_Y);
		shjpeg_jpu_setreg32(data, JPU_JIFDDYA2, data->jpeg_lb2);
		shjpeg_jpu_setreg32(data, JPU_JIFDDCA2,
				    data->jpeg_lb2 +
				    SHJPEG_JPU_LINEBUFFER_SIZE_Y);
		shjpeg_jpu_setreg32(data, JPU_JIFDDMW,
				    SHJPEG_JPU_LINEBUFFER_PITCH);

		if (format == SHJPEG_PF_YCbCr) {
			jpeg.flags |= SHJPEG_JPU_FLAG_SOFTCONVERT;
			jpeg.soft_offset = jpeg.soft_line = 0;
			context->pitch = pitch;
			if (get_frame_buffer_virtual(data, context,
					&mdata, format, phys) < 0) {
				D_ERROR("Get vframe buff err\n");
				goto end;
			}
		}
#if defined(HAVE_SHVIO)
		/* Setup VIO for conversion/scaling (from line buffer to surface). */
		else {
			jpeg.flags |= SHJPEG_JPU_FLAG_CONVERT;
			/* Setup VIO for conversion/scaling
				(from line buffer to surface). */
			/* source */
			if (context->mode420)
				vio.src.format = REN_NV12;
			else
				vio.src.format = REN_NV16;
			vio.src.w = context->width;
			vio.src.h = context->height;
			vio.src.pitch = SHJPEG_JPU_LINEBUFFER_PITCH;

			/* destination */
			vio.dst.w = width;
			vio.dst.h = height;

			/* Use valid virtual addresses to get through init */
			vio.src.py = vio.dst.py = data->jpeg_lb1_virt;
			vio.src.pc = vio.dst.pc = data->jpeg_lb1_virt;
			vio.src.pa = vio.dst.pa = NULL;

			/* set VIO */
			shjpeg_vio_init(data, &vio);

			/* Set the correct physical addresses */
			shvio_set_src_phys(data->vio, 0, 0);
			shvio_set_dst_phys(data->vio, phys, phys + pitch * height);
		}
#endif /* defined(HAVE_SHVIO) */
	}


	D_DEBUG_AT(SH7722_JPEG, " -> starting...");

	/* Start state machine */
	for (;;) {
		int i;

		/* Run the state machine. */
		if (shjpeg_jpu_run(context, data, &jpeg) < 0) {
			ret = -1;
			D_PERROR("libshjpeg: running JPU failed!\n");
			break;
		}

		D_ASSERT(jpeg.state != SHJPEG_JPU_START);

		/* Handle end (or error). */
		if (jpeg.state == SHJPEG_JPU_END) {
			if (jpeg.error) {
				D_ERROR("libshjpeg: ERROR 0x%x!\n",
					jpeg.error);
				ret = -1;
			}

			break;
		}
		/* Check for reload requests. */
		for (i = 2; i >= 1; i--) {
			if ((jpeg.buffers & i) &&
				       (jpeg.flags & SHJPEG_JPU_FLAG_RELOAD)) {
				void *ptr;
				D_ASSERT(reload);
				len = SHJPEG_JPU_RELOAD_SIZE;
				ptr = (void *) data->jpeg_virt +
				    (i - 1) * SHJPEG_JPU_RELOAD_SIZE;
				ret = context->sops->read(context->priv_data,
							&len, ptr);
				if (ret) {
					D_DERROR(ret,
						 "libshjpeg: Can't fill %s "
						 "reload buffer!\n",
						 i == 1 ? "first" : "second");
					jpeg.buffers &= ~i;
					jpeg.flags &= ~SHJPEG_JPU_FLAG_RELOAD;
				} else if (len <
					   SHJPEG_JPU_RELOAD_SIZE)
					jpeg.flags &= ~SHJPEG_JPU_FLAG_RELOAD;

				D_DEBUG_AT(SH7722_JPEG,
					   "libshjpeg: %d/%dbytes filled\n",
					   len, SHJPEG_JPU_RELOAD_SIZE);
			} else
				jpeg.buffers &= ~i;
		}
	}

	free_frame_buffer_virtual(&mdata);

#if defined(HAVE_SHVIO)
end:
#endif
	/* Unlocking JPU using uiomux_unlock */
	if (uiomux_unlock(data->uiomux, UIOMUX_JPU)) {
		D_PERROR("libshjpeg: Could not unlock JPEG engine!");
		ret = -1;
	}
	return ret;
}


/*
 * Software based decoding w/ libjpeg
 */

#define PIXEL_RGB16(r,g,b)	 ( (((r)&0xF8) << 8) | \
				 (((g)&0xFC) << 3) | \
				 (((b)&0xF8) >> 3) )

#define PIXEL_RGB32(r,g,b)	 ( ((r) << 16) | \
				 ((g) <<  8) | \
				 (b) )

static void
write_rgb_span(uint8_t * src, void *dst, int len,
	       shjpeg_pixelformat format)
{
	int i;

	switch (format) {
	case SHJPEG_PF_RGB16:
		for (i = 0; i < len; i++)
			((uint16_t *) dst)[i] = PIXEL_RGB16(src[i * 3 + 0],
							    src[i * 3 + 1],
							    src[i * 3 + 2]);
		break;

	case SHJPEG_PF_RGB24:
		memcpy(dst, src, len * 3);
		break;

	case SHJPEG_PF_RGB32:
		for (i = 0; i < len; i++)
			((uint32_t *) dst)[i] = PIXEL_RGB32(src[i * 3 + 0],
							    src[i * 3 + 1],
							    src[i * 3 + 2]);
		break;

	default:
		D_ONCE("unimplemented destination format (0x%08x)",
		       format);
		break;
	}
}

static inline void
copy_line_nv16(uint16_t * yy,
	       uint16_t * cbcr, const uint8_t * src_ycbcr, int width)
{
	int x;

	for (x = 0; x < width / 2; x++) {
#ifdef WORDS_BIGENDIAN
		yy[x] = (src_ycbcr[0] << 8) | src_ycbcr[3];
#else
		yy[x] = (src_ycbcr[3] << 8) | src_ycbcr[0];
#endif

		cbcr[x] = (((src_ycbcr[2] + src_ycbcr[5]) << 7) & 0xff00) |
		    ((src_ycbcr[1] + src_ycbcr[4]) >> 1);

		src_ycbcr += 6;
	}
}

static inline void
copy_line_y(uint16_t * yy, const uint8_t * src_ycbcr, int width)
{
	int x;

	for (x = 0; x < width / 2; x++) {
#ifdef WORDS_BIGENDIAN
		yy[x] = (src_ycbcr[0] << 8) | src_ycbcr[3];
#else
		yy[x] = (src_ycbcr[3] << 8) | src_ycbcr[0];
#endif

		src_ycbcr += 6;
	}
}

static int
decode_sw(shjpeg_context_t * context,
	  shjpeg_pixelformat format,
	  void *addr, int width, int height, int pitch)
{
	JSAMPARRAY buffer;	/* Output row buffer */
	int row_stride;		/* physical row width in output buffer */
	void *addr_uv = addr + height * pitch;
	j_decompress_ptr cinfo = &context->jpeg_decomp;

	D_ASSERT(context != NULL);

	D_DEBUG_AT(SH7722_JPEG, "%s( %p, %p|%d [%dx%d] %08x )",
		   __FUNCTION__, context, addr, pitch, context->width,
		   context->height, format);

	cinfo->output_components = 3;

	/*
	 * XXX: Calculate destination base address. rect->{x,y} are x/y offsets
	 * from top left corner, i.e. base address.
	 */
	//addr += DFB_BYTES_PER_LINE( format, rect->x ) + rect->y * pitch;

	/* Not all formats yet :( */
	switch (format) {
	case SHJPEG_PF_RGB16:
	case SHJPEG_PF_RGB24:
	case SHJPEG_PF_RGB32:
		cinfo->out_color_space = JCS_RGB;
		break;

	case SHJPEG_PF_NV12:
		//addr_uv += rect->x + rect->y / 2 * pitch;
		cinfo->out_color_space = JCS_YCbCr;
		width = (width + 1) & ~1;
		break;

	case SHJPEG_PF_NV16:
		//addr_uv += rect->x + rect->y * pitch;
		cinfo->out_color_space = JCS_YCbCr;
		width = (width + 1) & ~1;
		break;

	default:
		return -1;
	}

	D_DEBUG_AT(SH7722_JPEG, "		 -> decoding...");

	jpeg_start_decompress(cinfo);
	row_stride = ((cinfo->output_width + 1) & ~1) * 3;
	buffer = (*cinfo->mem->alloc_sarray) ((j_common_ptr) cinfo,
					      JPOOL_IMAGE, row_stride, 1);

	while (cinfo->output_scanline < cinfo->output_height) {
		jpeg_read_scanlines(cinfo, buffer, 1);

		switch (format) {
		case SHJPEG_PF_NV12:
			if (cinfo->output_scanline & 1) {
				// copy_line_nv16(addr, addr_uv, *buffer,
				//                  (rect->w + 1) & ~1 );
				copy_line_nv16(addr, addr_uv, *buffer,
					       width);
				addr_uv += pitch;
			} else
				// copy_line_y( addr, *buffer, (rect->w + 1)
				//              & ~1 );
				copy_line_y(addr, *buffer, width);
			break;

		case SHJPEG_PF_NV16:
			copy_line_nv16(addr, addr_uv, *buffer, width);
			addr_uv += pitch;
			break;

		default:
			write_rgb_span(*buffer, addr, width, format);
			break;
		}

		addr += pitch;
	}

	jpeg_finish_decompress(cinfo);

	return 0;
}

/*
 * libjpeg initialization
 */

#define SHJPEG_STREAM_BUF_SIZE	0x10000

typedef struct {
	struct jpeg_source_mgr pub;	/* public fields */
	JOCTET *data;		/* start of buffer */
} shjpeg_stream_source_mgr;

typedef shjpeg_stream_source_mgr *shjpeg_stream_src_ptr;

/*
 * callbacks for input source
 */

/* init */
static void shjpeg_libjpeg_init_source(j_decompress_ptr cinfo)
{
	shjpeg_context_t *context =
	    (shjpeg_context_t *) cinfo->client_data;

	if (context->sops->init)
		context->sops->init(context->priv_data);
}

/*
 * fill input buffer
 */
static boolean shjpeg_libjpeg_fill_input_buffer(j_decompress_ptr cinfo)
{
	shjpeg_stream_src_ptr src = (shjpeg_stream_src_ptr) cinfo->src;
	shjpeg_context_t *context =
	    (shjpeg_context_t *) cinfo->client_data;
	size_t nbytes = SHJPEG_STREAM_BUF_SIZE;
	int ret = 1;

	if (context->sops->read)
		ret = context->sops->read(context->priv_data, &nbytes,
					  (void *) src->data);

	if (ret || nbytes <= 0) {
		/* Insert a fake EOI marker */
		src->data[0] = (JOCTET) 0xff;
		src->data[1] = (JOCTET) JPEG_EOI;
		nbytes = 2;
	}

	src->pub.next_input_byte = src->data;
	src->pub.bytes_in_buffer = nbytes;

	return TRUE;
}

/*
 * skip data
 */
static void
shjpeg_libjpeg_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	shjpeg_stream_src_ptr src = (shjpeg_stream_src_ptr) cinfo->src;

	if (num_bytes > 0) {
		while (num_bytes > (long) src->pub.bytes_in_buffer) {
			num_bytes -= (long) src->pub.bytes_in_buffer;
			(void) shjpeg_libjpeg_fill_input_buffer(cinfo);
		}
		src->pub.next_input_byte += (size_t) num_bytes;
		src->pub.bytes_in_buffer -= (size_t) num_bytes;
	}
}

/*
 * terminate
 */
static void shjpeg_libjpeg_term_source(j_decompress_ptr cinfo)
{
	shjpeg_context_t *context =
	    (shjpeg_context_t *) cinfo->client_data;

	if (context->sops->finalize)
		context->sops->finalize(context->priv_data);
}


/*
 * initialize input source for libjpeg
 */

static void
shjpeg_init_src(shjpeg_context_t * context, j_decompress_ptr cinfo)
{
	shjpeg_stream_src_ptr src;

	cinfo->client_data = context;
	cinfo->src = (struct jpeg_source_mgr *)
	    cinfo->mem->alloc_small((j_common_ptr) cinfo, JPOOL_PERMANENT,
				    sizeof(shjpeg_stream_source_mgr));
	src = (shjpeg_stream_src_ptr) cinfo->src;

	src->data = (JOCTET *)
	    cinfo->mem->alloc_small((j_common_ptr) cinfo, JPOOL_PERMANENT,
				    SHJPEG_STREAM_BUF_SIZE *
				    sizeof(JOCTET));

	src->pub.init_source = shjpeg_libjpeg_init_source;
	src->pub.fill_input_buffer = shjpeg_libjpeg_fill_input_buffer;
	src->pub.skip_input_data = shjpeg_libjpeg_skip_input_data;
	/* use default method */
	src->pub.resync_to_restart = jpeg_resync_to_restart;
	src->pub.term_source = shjpeg_libjpeg_term_source;
	src->pub.bytes_in_buffer = 0;	/* forces fill_input_buffer on
					   first read */
	src->pub.next_input_byte = NULL;	/* until buffer loaded */
}

struct my_error_mgr {
	struct jpeg_error_mgr pub;	/* "public" fields */
	jmp_buf setjmp_buffer;	/* for return to caller */
};

static void jpeglib_panic(j_common_ptr cinfo)
{
	struct my_error_mgr *myerr = (struct my_error_mgr *) cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

/*******************************************************************/

/*
 * decode JPEG header
 */

int shjpeg_decode_init(shjpeg_context_t * context)
{
	shjpeg_internal_t *data;
	struct my_error_mgr jerr;
	j_decompress_ptr cinfo;

	if (!context) {
		D_ERROR("libshjpeg: invalid context passed.");
		return -1;
	}

	data = (shjpeg_internal_t *) context->internal_data;

	/* check ref counter */
	if (!data->ref_count) {
		D_ERROR("libshjpeg: not initialized yet.");
		return -1;
	}

	/* initialize libjpeg */
	cinfo = &context->jpeg_decomp;
	cinfo->err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeglib_panic;

	if (setjmp(jerr.setjmp_buffer)) {
		D_ERROR("libshjpeg: Error while reading headers!");
		jpeg_destroy_decompress(cinfo);
		return -1;
	}

	jpeg_create_decompress(cinfo);
	shjpeg_init_src(context, cinfo);
	jpeg_read_header(cinfo, TRUE);
	jpeg_calc_output_dimensions(cinfo);

	context->width = cinfo->output_width;
	context->height = cinfo->output_height;

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

	/* True if 4:4:4 */
	context->mode444 =
	    (cinfo->comp_info[1].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor) &&
	    (cinfo->comp_info[1].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor) &&
	    (cinfo->comp_info[2].h_samp_factor ==
	     cinfo->comp_info[0].h_samp_factor) &&
	    (cinfo->comp_info[2].v_samp_factor ==
	     cinfo->comp_info[0].v_samp_factor);

	return 0;
}

/*
 * deocde main
 */

int
shjpeg_decode_run(shjpeg_context_t * context,
		  shjpeg_pixelformat format,
		  void *virt, int width, int height, int pitch)
{
	shjpeg_internal_t *data;
	unsigned long phys;
	struct my_error_mgr jerr;
	int ret = -1;

	data = (shjpeg_internal_t *) context->internal_data;

	/* sanity check */
	if (!data->ref_count) {
		D_ERROR("libshjpeg: not initialized yet.");
		return -1;
	}

	/* check if we got a large enough surface */
	if ((context->width > width) ||
	    (context->height > height) ||
	    ((context->width * (SHJPEG_PF_PITCH_MULTIPLY(format))) > pitch)) {
		D_ERROR("libshjpeg: width, height or pitch doesn't fit.");
		return -1;
	}

	/* error if virtual address is not given */
	if (!virt) {
		D_ERROR("libshjpeg: buffer address is not given.");
		return -1;
	}

	data->user_jpeg_data = phys =
		uiomux_all_virt_to_phys(virt);

	/* error if the specified address cannot be converted */
	if (!phys) {
		D_ERROR("libshjpeg: buffer address is invalid.");
		return -1;
	}

	context->jpeg_decomp.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeglib_panic;

	if (setjmp(jerr.setjmp_buffer)) {
		D_ERROR
		    ("libshjpeg: Error while decoding image with libjpeg!");
		return -1;
	}

	switch (format) {
	case SHJPEG_PF_NV12:
	case SHJPEG_PF_NV16:
	case SHJPEG_PF_RGB16:
	case SHJPEG_PF_RGB32:
	case SHJPEG_PF_RGB24:
	case SHJPEG_PF_YCbCr:
		break;

	default:
		D_ERROR("libshjpeg: Unsupported destination format.");
		return -1;
	}

	// Reset libjpeg used flag to zero
	context->libjpeg_used = 0;

	if ((!context->mode444) && (context->libjpeg_disabled >= 0)) {
		if (context->sops->init)
			context->sops->init(context->priv_data);

		ret = decode_hw(data, context, format, phys, width,
				height, pitch);
	}

	if ((context->libjpeg_disabled <= 0) && (ret)) {
		ret = decode_sw(context, format, virt, width,
				height, pitch);

		// set the flag to notify the use of libjpeg
		if (!ret)
			context->libjpeg_used = 1;
	}

	return ret;
}

/*
 * clean decode context
 */

void shjpeg_decode_shutdown(shjpeg_context_t * context)
{
	jpeg_destroy_decompress(&context->jpeg_decomp);
}
