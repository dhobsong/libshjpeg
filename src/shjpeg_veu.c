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

#include <stdio.h>
#include <unistd.h>

#include <shveu/shveu.h>
#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#include "shjpeg_veu.h"

/*
 * Initialize VEU
 */

int shjpeg_veu_init(shjpeg_internal_t * data, shjpeg_veu_t * veu)
{
	/* Set color conversion to BT601, full range data */
	shveu_set_color_conversion(data->veu, 0, 1);

	if (shveu_setup(data->veu, &veu->src, &veu->dst, SHVEU_NO_ROT) != 0) {
		fprintf(stderr, "libshjpeg: %s: ERROR in shveu_setup!\n", __func__);
		return 1;
	}

	return 0;
}

/*
 * Set JPU as the destination of VEU
 */

void shjpeg_veu_set_dst_jpu(shjpeg_internal_t * data)
{
	u32 py;
	u32 pc;

	if (data->veu_linebuf) {
		py = shjpeg_jpu_getreg32(data, JPU_JIFESYA2);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFESCA2);
	} else {
		py = shjpeg_jpu_getreg32(data, JPU_JIFESYA1);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFESCA1);
	}

	shveu_set_dst_phys(data->veu, py, pc);
}

/*
 * Set JPU as the source of VEU
 */

void shjpeg_veu_set_src_jpu(shjpeg_internal_t * data)
{
	u32 py;
	u32 pc;

	if (data->veu_linebuf) {
		py = shjpeg_jpu_getreg32(data, JPU_JIFDDYA2);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFDDCA2);
	} else {
		py = shjpeg_jpu_getreg32(data, JPU_JIFDDYA1);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFDDCA1);
	}

	shveu_set_src_phys(data->veu, py, pc);
}

/*
 * Set VEU Source to the given address
 */

void shjpeg_veu_set_src(shjpeg_internal_t * data, u32 src_y, u32 src_c)
{
	shveu_set_src_phys(data->veu, src_y, src_c);
}

/*
 * Start VEU
 */

void shjpeg_veu_start(shjpeg_internal_t * data, int bundle_mode)
{
	if (bundle_mode) {
		shveu_start_bundle(data->veu, SHJPEG_JPU_LINEBUFFER_HEIGHT);
	} else {
		shveu_start(data->veu);
	}
}
