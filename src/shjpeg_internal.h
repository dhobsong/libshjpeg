/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2008-2009 IGEL Co.,Ltd.
 * Copyright (C) 2008-2009 Renesas Technology Corp.
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

#ifndef __shjpeg_internal_h__
#define __shjpeg_internal_h__

#include <shveu/shveu.h>
#include <shjpeg/shjpeg_types.h>
#include "shjpeg_utils.h"

/*
 * private data struct of SH7722_JPEG
 */

typedef struct {
	int ref_count;		// reference counter

	int jpu_uio_num;	// ID for JPU UIO
	int jpu_uio_fd;		// fd for JPU UIO

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

	SHVEU *veu;

	/* uio device list */
	int uio_count;		// number of UIO device
	char **uio_device;	// list of uio device
	char **uio_dpath;	// list of uio device path

	/* internal to state machine */
	uint32_t jpeg_buffers;
	int jpeg_buffer;
	uint32_t jpeg_error;
	int jpeg_encode;
	int jpeg_end;
	int jpeg_linebuf;

	int jpu_line_bufs_pending;
	int jpu_line_bufs_done;

	int veu_linebuf;
	int veu_line_bufs_done;

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
