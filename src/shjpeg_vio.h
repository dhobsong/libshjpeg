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

#ifndef __shjpeg_vio_h__
#define __shjpeg_vio_h__

#include <shvio/shvio.h>
#include "shjpeg_utils.h"

typedef struct {
	struct ren_vid_surface src;
	struct ren_vid_surface dst;
} shjpeg_vio_t;


static inline ren_vid_format_t shjpeg_vio_color(shjpeg_pixelformat format)
{
	switch (format) {
	case SHJPEG_PF_NV12: return REN_NV12; break;
	case SHJPEG_PF_NV16: return REN_NV16; break;
	case SHJPEG_PF_RGB16: return REN_RGB565; break;
	case SHJPEG_PF_RGB32: return REN_RGB32; break;
	case SHJPEG_PF_RGB24: return REN_RGB24;	break;
	default: break;
	}

	return REN_UNKNOWN;
}

/* external function */
int shjpeg_vio_init(shjpeg_internal_t * data, shjpeg_vio_t * vio);
void shjpeg_vio_set_dst_jpu(shjpeg_internal_t *);
void shjpeg_vio_set_src_jpu(shjpeg_internal_t *);
void shjpeg_vio_set_src(shjpeg_internal_t *, u32, u32);
void shjpeg_vio_start(shjpeg_internal_t *, int);

#endif				/* !__shjpeg_jpu_h__ */
