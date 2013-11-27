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

#ifndef __shjpeg_jpu_h__
#define __shjpeg_jpu_h__

#include "shjpeg_regs.h"
#include "shjpeg_utils.h"

#define SHJPEG_JPU_RELOAD_SIZE       (64 * 1024)
#define SHJPEG_JPU_LINEBUFFER_PITCH  (2560)
#define SHJPEG_JPU_LINEBUFFER_HEIGHT (16)
#define SHJPEG_JPU_LINEBUFFER_SIZE  \
	 (SHJPEG_JPU_LINEBUFFER_PITCH * SHJPEG_JPU_LINEBUFFER_HEIGHT * 2)
#define SHJPEG_JPU_LINEBUFFER_SIZE_Y \
	(SHJPEG_JPU_LINEBUFFER_PITCH * SHJPEG_JPU_LINEBUFFER_HEIGHT)
#define SHJPEG_JPU_SIZE  \
        (SHJPEG_JPU_LINEBUFFER_SIZE * 2 + SHJPEG_JPU_RELOAD_SIZE * 2)

typedef enum {
	SHJPEG_JPU_START,
	SHJPEG_JPU_RUN,
	SHJPEG_JPU_END
} shjpeg_jpu_state_t;

typedef enum {
	SHJPEG_JPU_FLAG_RELOAD = 0x00000001,	/* enable reload mode */
	SHJPEG_JPU_FLAG_CONVERT = 0x00000002,	/* enable conv. through VIO */
	SHJPEG_JPU_FLAG_ENCODE = 0x00000004,	/* set encoding mode */
	SHJPEG_JPU_FLAG_SOFTCONVERT= 0x00000008	/* set encoding mode */
} shjpeg_jpu_flags_t;

typedef struct {
	/* starting, running or ended (done/error) */
	shjpeg_jpu_state_t state;
	/* control decoding options */
	shjpeg_jpu_flags_t flags;

	/* input = loaded buffers, output = buffers to reload */
	u32 buffers;
	/* valid in END state, non-zero means error */
	u32 error;

	int height;

	/* to update VIO source addresses */
	u32 sa_y;
	u32 sa_c;
	u32 sa_inc;

	u32 soft_offset;  //write position in output buffer
	u32 soft_line;
} shjpeg_jpu_t;

/* read/write from/to registers */
static inline u32
shjpeg_jpu_getreg32(shjpeg_internal_t * data, u32 address)
{
	D_ASSERT(address < data->jpu_size);

	return *(volatile u32 *) (data->jpu_base + address);
}

static inline void
shjpeg_jpu_setreg32(shjpeg_internal_t * data, u32 address, u32 value)
{
	D_ASSERT(address < data->jpu_size);

	*(volatile u32 *) (data->jpu_base + address) = value;

#ifdef SHJPEG_DEBUG
	{
		shjpeg_context_t *context = data->context;
		if (address <= JPU_JIFDDCA2)
			D_INFO("%s: written %08x(%08x) at %s(%08x)",
			       __FUNCTION__, value,
			       shjpeg_jpu_getreg32(data, address),
			       jpu_reg_str[address >> 2], address);
	}
#endif
}

/* external function */
void shjpeg_jpu_reset(shjpeg_internal_t * data);
int shjpeg_jpu_run(shjpeg_context_t * context, shjpeg_internal_t * data,
		   shjpeg_jpu_t * jpeg);

void shjpeg_jpu_init_quantization_table(shjpeg_internal_t * data);
void shjpeg_jpu_init_huffman_table(shjpeg_internal_t * data);

#endif				/* !__shjpeg_jpu_h__ */
