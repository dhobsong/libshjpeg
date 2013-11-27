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
#include <unistd.h>

#include <shvio/shvio.h>
#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#include "shjpeg_vio.h"

/*
 * Initialize VIO
 */

int shjpeg_vio_init(shjpeg_internal_t * data, shjpeg_vio_t * vio)
{
	/* Set color conversion to BT601, full range data */
	shvio_set_color_conversion(data->vio, 0, 1);

	if (shvio_setup(data->vio, &vio->src, &vio->dst, SHVIO_NO_ROT) != 0) {
		fprintf(stderr, "libshjpeg: %s: ERROR in shvio_setup!\n", __func__);
		return 1;
	}

	return 0;
}

/*
 * Set JPU as the destination of VIO
 */

void shjpeg_vio_set_dst_jpu(shjpeg_internal_t * data)
{
	u32 py;
	u32 pc;

	if (data->vio_linebuf) {
		py = shjpeg_jpu_getreg32(data, JPU_JIFESYA2);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFESCA2);
	} else {
		py = shjpeg_jpu_getreg32(data, JPU_JIFESYA1);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFESCA1);
	}

	shvio_set_dst_phys(data->vio, py, pc);
}

/*
 * Set JPU as the source of VIO
 */

void shjpeg_vio_set_src_jpu(shjpeg_internal_t * data)
{
	u32 py;
	u32 pc;

	if (data->vio_linebuf) {
		py = shjpeg_jpu_getreg32(data, JPU_JIFDDYA2);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFDDCA2);
	} else {
		py = shjpeg_jpu_getreg32(data, JPU_JIFDDYA1);
		pc = shjpeg_jpu_getreg32(data, JPU_JIFDDCA1);
	}

	shvio_set_src_phys(data->vio, py, pc);
}

/*
 * Set VIO Source to the given address
 */

void shjpeg_vio_set_src(shjpeg_internal_t * data, u32 src_y, u32 src_c)
{
	shvio_set_src_phys(data->vio, src_y, src_c);
}

/*
 * Start VIO
 */

void shjpeg_vio_start(shjpeg_internal_t * data, int bundle_mode)
{
	if (bundle_mode) {
		shvio_start_bundle(data->vio, SHJPEG_JPU_LINEBUFFER_HEIGHT);
	} else {
		shvio_start(data->vio);
	}
}
