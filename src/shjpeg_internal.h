/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2008-2009 IGEL Co.,Ltd.
 * Copyright (C) 2008-2009 Renesas Technology Corp.
 * Copyright (C) 2008 Denis Oliver Kropp
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifndef __shjpeg_internal_h__
#define __shjpeg_internal_h__

#include <shjpeg/shjpeg_types.h>
#include "shjpeg_utils.h"

/*
 * UIO capabilities
 */

typedef enum {
	UIO_CAPS_VEU3F = 0x00000001,	// VEU is VEU3F
} uio_caps_t;

/*
 * private data struct of SH7722_JPEG
 */

typedef struct {
	int ref_count;		// reference counter

	int jpu_uio_num;	// ID for JPU UIO
	int jpu_uio_fd;		// fd for JPU UIO
	int veu_uio_num;	// ID for VEU UIO
	int veu_uio_fd;		// fd for VEU UIO

	void *jpeg_virt;	// virt addr of cont buffer
	unsigned long jpeg_phys;	// phys addr of cont buffer
	unsigned long jpeg_size;	// size of contiguous buffer

	unsigned long jpeg_lb1;	// phys addr of line buffer 1
	unsigned long jpeg_lb2;	// phys addr of line buffer 2

	unsigned long jpeg_data;	// phys addr of jpeg data

	// XXX: mmio_* -> jpu_*
	unsigned long jpu_phys;	// phys addr of JPU regs
	volatile void *jpu_base;	// virt addr to JPU regs
	unsigned long jpu_size;	// size of JPU reg range

	unsigned long veu_phys;	// phys addr of VEU regs
	volatile void *veu_base;	// virt addr of VEU regs
	unsigned long veu_size;	// size of VEU reg range

	/* uio device list */
	int uio_count;		// number of UIO device
	char **uio_device;	// list of uio device
	char **uio_dpath;	// list of uio device path

	/* UIO flags */
	uio_caps_t uio_caps;	// device details

	/* internal to state machine */
	uint32_t jpeg_buffers;
	int jpeg_buffer;
	uint32_t jpeg_error;
	int jpeg_encode;
	int jpeg_reading;
	int jpeg_writing;
	int jpeg_height;
	int jpeg_end;
	uint32_t jpeg_linebufs;
	int jpeg_linebuf;
	int jpeg_line;

	int jpu_running;
	int jpu_lb_first_irq;

	int veu_linebuf;
	int veu_running;

	/* internal data */
	shjpeg_context_t *context;

	/*Added for YCbCr (or other software assist) support*/
	unsigned long        user_jpeg_data;	// user overridable phys add
						// of jpeg data = jpeg_data
						// if user uses defaults
	void               *user_jpeg_virt;	// vir addr of user_jpeg_data;
	void               *jpeg_lb1_virt;	// virt addr of line buffer 1
	void               *jpeg_lb2_virt;	// virt addr of line buffer 2
} shjpeg_internal_t;

/* page alignment */
#define _PAGE_SIZE (getpagesize())
#define _PAGE_ALIGN(len) (((len) + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1))

#endif				/* !__shjpeg_internal_h__ */
