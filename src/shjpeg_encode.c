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

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#if defined(HAVE_SHVIO)
#include "shjpeg_vio.h"
#endif
#include "shjpeg_softhelper.h"

static inline int coded_data_amount(shjpeg_internal_t * data)
{
	return (shjpeg_jpu_getreg32(data, JPU_JCDTCU) << 16) |
	    (shjpeg_jpu_getreg32(data, JPU_JCDTCM) << 8) |
	    (shjpeg_jpu_getreg32(data, JPU_JCDTCD));
}

static int
encode_hw(shjpeg_internal_t * data,
	  shjpeg_context_t * context,
	  shjpeg_pixelformat format,
	  unsigned long phys, int width, int height, int pitch)
{
	int ret = 0;
	int i;
	int written = 0;
	bool mode420 = false;
	shjpeg_jpu_t jpeg;
	vmap_data_t mdata;

	D_DEBUG_AT(SH7722_JPEG, "( %p, 0x%08lx|%d [%dx%d])",
		   data, phys, pitch, width, height);

	memset(&mdata, 0, sizeof(mdata));

	D_DEBUG_AT(SH7722_JPEG, "	 -> locking JPU...");

	/* Locking JPU using uiomux_lock */
	if (uiomux_lock (data->uiomux, UIOMUX_JPU) < 0) {
		D_PERROR("libshjpeg: Could not lock JPEG engine!");
		return -1;
	}

	D_DEBUG_AT(SH7722_JPEG, "	 -> opening file for writing...");

	if (context->sops->init)
		context->sops->init(context->priv_data);

	D_DEBUG_AT(SH7722_JPEG, "	 -> setting...");

	/* Initialize JPEG state. */
	jpeg.state = SHJPEG_JPU_START;
	jpeg.flags = SHJPEG_JPU_FLAG_ENCODE;
	jpeg.buffers = 3;

	/* Always enable reload mode. */
	jpeg.flags |= SHJPEG_JPU_FLAG_RELOAD;

	/* Program JPU from RESET. */
	shjpeg_jpu_reset(data);
	shjpeg_jpu_setreg32(data, JPU_JCMOD,
			    JPU_JCMOD_INPUT_CTRL | JPU_JCMOD_DSP_ENCODE |
			    (mode420 ? 2 : 1));

	shjpeg_jpu_setreg32(data, JPU_JCQTN, 0x14);
	shjpeg_jpu_setreg32(data, JPU_JCHTN, 0x3c);
	shjpeg_jpu_setreg32(data, JPU_JCDRIU, 0x02);
	shjpeg_jpu_setreg32(data, JPU_JCDRID, 0x00);
	shjpeg_jpu_setreg32(data, JPU_JCHSZU, width >> 8);
	shjpeg_jpu_setreg32(data, JPU_JCHSZD, width & 0xff);
	shjpeg_jpu_setreg32(data, JPU_JCVSZU, height >> 8);
	shjpeg_jpu_setreg32(data, JPU_JCVSZD, height & 0xff);
	shjpeg_jpu_setreg32(data, JPU_JIFCNT, JPU_JIFCNT_VJSEL_JPU);
	shjpeg_jpu_setreg32(data, JPU_JIFDCNT, JPU_JIFDCNT_SWAP_4321);
	shjpeg_jpu_setreg32(data, JPU_JIFEDA1, data->jpeg_phys);
	shjpeg_jpu_setreg32(data, JPU_JIFEDA2,
			    data->jpeg_phys + SHJPEG_JPU_RELOAD_SIZE);
	shjpeg_jpu_setreg32(data, JPU_JIFEDRSZ, SHJPEG_JPU_RELOAD_SIZE);
	shjpeg_jpu_setreg32(data, JPU_JIFESHSZ,
			    ((u32)width + 3) & 0x00000ffc);
	shjpeg_jpu_setreg32(data, JPU_JIFESVSZ,
			    ((u32)height + 3) & 0x00000ffc);

	if (format == SHJPEG_PF_NV12 || format == SHJPEG_PF_NV16) {
		/* Setup JPU for encoding in frame mode (directly from surface). */
		shjpeg_jpu_setreg32(data, JPU_JINTE,
				    JPU_JINTS_INS10_XFER_DONE |
				    JPU_JINTS_INS13_LOADED);
		shjpeg_jpu_setreg32(data, JPU_JIFECNT,
				    JPU_JIFECNT_SWAP_4321 |
				    JPU_JIFECNT_RELOAD_ENABLE | (mode420 ?
								 1 : 0));

		shjpeg_jpu_setreg32(data, JPU_JIFESYA1, phys);
		shjpeg_jpu_setreg32(data, JPU_JIFESCA1,
				    phys + pitch * height);
		shjpeg_jpu_setreg32(data, JPU_JIFESMW,
				    ((u32)pitch + 7) & 0x0ff8);
	} else {
		jpeg.height = height;

		/* Setup JPU for encoding in line buffer mode. */
		shjpeg_jpu_setreg32(data, JPU_JINTE,
				    JPU_JINTS_INS11_LINEBUF0 |
				    JPU_JINTS_INS12_LINEBUF1 |
				    JPU_JINTS_INS10_XFER_DONE |
				    JPU_JINTS_INS13_LOADED);
		shjpeg_jpu_setreg32(data, JPU_JIFECNT,
				    JPU_JIFECNT_LINEBUF_MODE |
				    (SHJPEG_JPU_LINEBUFFER_HEIGHT << 16) |
				    JPU_JIFECNT_SWAP_4321 |
				    JPU_JIFECNT_RELOAD_ENABLE | (mode420 ?
								 1 : 0));

		shjpeg_jpu_setreg32(data, JPU_JIFESYA1, data->jpeg_lb1);
		shjpeg_jpu_setreg32(data, JPU_JIFESCA1,
				    data->jpeg_lb1 +
				    SHJPEG_JPU_LINEBUFFER_SIZE_Y);
		shjpeg_jpu_setreg32(data, JPU_JIFESYA2, data->jpeg_lb2);
		shjpeg_jpu_setreg32(data, JPU_JIFESCA2,
				    data->jpeg_lb2 +
				    SHJPEG_JPU_LINEBUFFER_SIZE_Y);
		shjpeg_jpu_setreg32(data, JPU_JIFESMW,
				    SHJPEG_JPU_LINEBUFFER_PITCH);
		/* configs */
		jpeg.sa_y = phys;
		jpeg.sa_c = phys + pitch * height;
		jpeg.sa_inc = pitch * SHJPEG_JPU_LINEBUFFER_HEIGHT;

		if (format == SHJPEG_PF_YCbCr) {
			jpeg.flags |= SHJPEG_JPU_FLAG_SOFTCONVERT;
			jpeg.soft_offset = jpeg.soft_line = 0;

			context->pitch = pitch;
			if (get_frame_buffer_virtual(data, context,
					&mdata, format, phys) < 0) {
				return -1;
			}

		}
#if defined(HAVE_SHVIO)
		else {
			jpeg.flags |= SHJPEG_JPU_FLAG_CONVERT;
			/* save some attibutes of input image
			   to set up the vio hardware later */
			context->pitch = pitch;
			context->format = format;
		}
#endif /* defined(HAVE_SHVIO) */
	}

	/* init QT/HT */
	shjpeg_jpu_init_quantization_table(data);
	shjpeg_jpu_init_huffman_table(data);

	D_DEBUG_AT(SH7722_JPEG, "	 -> starting...");

	/* State machine. */
	for (;;) {
		/* Run the state machine. */
		if (shjpeg_jpu_run(context, data, &jpeg) < 0) {
			D_PERROR("libshjpeg: shjpeg_jpu_run() failed!");
			ret = -1;
			break;
		}

		D_ASSERT(jpeg.state != SHJPEG_JPU_START);

		/* Check for loaded buffers. */
		for (i = 1; i <= 2; i++) {
			if (jpeg.buffers & i) {
				int amount =
				    coded_data_amount(data) - written;
				size_t len;
				void *ptr;

				if (amount > SHJPEG_JPU_RELOAD_SIZE)
					amount = SHJPEG_JPU_RELOAD_SIZE;

				D_INFO ("libshjpeg: Coded data amount: "
					"+ %5d (buffer %d)", amount, i);

				ptr =
				    (void *) data->jpeg_virt + (i - 1) *
						SHJPEG_JPU_RELOAD_SIZE;
				len = amount;
				context->sops->write(context->priv_data,
						     &len, ptr);
				written += len;
			}
		}

		/* Handle end (or error). */
		if (jpeg.state == SHJPEG_JPU_END) {
			if (jpeg.error) {
				D_ERROR("libshjpeg: ERROR 0x%x!",
					jpeg.error);
				ret = -1;
			}

			break;
		}
	}

	D_INFO
	    ("libshjpeg: Coded data amount: = %5d (written: %d, buffers: %d)",
	     coded_data_amount(data), written, jpeg.buffers);

	free_frame_buffer_virtual(&mdata);

	/* Unlocking JPU using uiomux_unlock */
	if (uiomux_unlock(data->uiomux, UIOMUX_JPU)) {
		ret = -1;
		D_PERROR("libshjpeg: Could not unlock JPEG engine!");
	}

	return ret;
}

/*
 * shpjpeg_encode()
 */

int
shjpeg_encode(shjpeg_context_t * context,
	      shjpeg_pixelformat format,
	      void *virt, int width, int height, int pitch)
{
	shjpeg_internal_t *data;
	unsigned long phys;

	if (!context) {
		D_ERROR("libjpeg: invalid context passed.");
		return -1;
	}

	data = (shjpeg_internal_t *) context->internal_data;

	/* check ref counter */
	if (!data->ref_count) {
		D_ERROR("libshjpeg: not initialized yet.");
		return -1;
	}

	/* error if physical address is not given */
	if (!virt) {
		D_ERROR("libshjpeg: buffer address is not given.");
		return -1;
	}

	phys = uiomux_virt_to_phys(data->uiomux, UIOMUX_JPU, virt);

	switch (format) {
	case SHJPEG_PF_NV12:
	case SHJPEG_PF_NV16:
	case SHJPEG_PF_RGB16:
	case SHJPEG_PF_RGB32:
	case SHJPEG_PF_RGB24:
	case SHJPEG_PF_YCbCr:
		break;

	default:
		return -1;
	}

	/* TODO: Support for clipping and resize */

	/* start hardware encoding */
	return encode_hw(data, context, format, phys, width, height,
			 pitch);
}
