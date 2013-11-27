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
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/file.h>

#include <uiomux/uiomux.h>
#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"


/*
 * shutdown UIO dev
 */

static void uio_shutdown(shjpeg_internal_t * data)
{
	uiomux_free(data->uiomux, UIOMUX_JPU, data->jpeg_virt,
		SHJPEG_JPU_RELOAD_SIZE * 2);
	uiomux_free(data->uiomux, UIOMUX_JPU, data->jpeg_lb1_virt,
		SHJPEG_JPU_LINEBUFFER_SIZE);
	uiomux_free(data->uiomux, UIOMUX_JPU, data->jpeg_lb2_virt,
		SHJPEG_JPU_LINEBUFFER_SIZE);
#if defined(HAVE_SHVIO)
	shvio_close(data->vio);
#endif
	uiomux_close(data->uiomux);

	/* deinit */
	data->jpu_base = NULL;
	data->jpeg_virt = NULL;
}

/*
 * initialize UIO
 */
static int uio_init(shjpeg_context_t * context, shjpeg_internal_t * data)
{
	const char* uio_device_names[] = {
	   /*    Name     Resource ID */
		"JPU",    /* 1 << 0 */
		NULL
	};
	D_DEBUG_AT(SH7722_JPEG, "( %p )", data);


#if defined(HAVE_SHVIO)
	/* Open VIO */
	data->vio = shvio_open_named(SHVIO_UIO_NAME);
	if (!data->vio) {
		D_ERROR("libshjpeg: Cannot open VIO!");
		return -1;
	}
#endif

	/* Open JPU */
	data->uiomux = uiomux_open_named(uio_device_names);

	if (!data->uiomux) {
		D_ERROR("libshjpeg: Cannot open JPU UIO device");
		return -1;
	}

	/*
	 * Get registers and contiguous memory for JPU.
	 */
	if(uiomux_get_mmio(data->uiomux, UIOMUX_JPU, &data->jpu_phys,
		&data->jpu_size, (void *) &data->jpu_base) <= 0) {
		D_ERROR("libshjpeg: Can't get JPU base address!");
		goto error;
	}

	/* size of JPEG memory */
	if(uiomux_get_mem(data->uiomux, UIOMUX_JPU, NULL,
		&data->jpeg_size, NULL) <= 0) {
		D_ERROR("libshjpeg: Can't get JPU base address!");
		goto error;
	}

	/* initialize buffer base address */
	data->jpeg_virt = uiomux_malloc(data->uiomux, UIOMUX_JPU,
			SHJPEG_JPU_RELOAD_SIZE * 2, 1);

	data->jpeg_phys = uiomux_virt_to_phys(data->uiomux, UIOMUX_JPU,
			data->jpeg_virt);

	/* Register the memory with UIOMux */
	uiomux_register (data->jpeg_virt, data->jpeg_phys, data->jpeg_size);

	// line buffer 1
	data->jpeg_lb1_virt = uiomux_malloc(data->uiomux, UIOMUX_JPU,
			SHJPEG_JPU_LINEBUFFER_SIZE, 1);
	data->jpeg_lb1 = uiomux_virt_to_phys(data->uiomux, UIOMUX_JPU,
			data->jpeg_lb1_virt);

	// line buffer 2	
	data->jpeg_lb2_virt = uiomux_malloc(data->uiomux, UIOMUX_JPU,
			SHJPEG_JPU_LINEBUFFER_SIZE, 1);
	data->jpeg_lb2 = uiomux_virt_to_phys(data->uiomux, UIOMUX_JPU,
			data->jpeg_lb2_virt);

	D_INFO ("libshjpeg: jpu_phys=%08lx(%08lx)",
		data->jpeg_phys, data->jpeg_size);

	return 0;

      error:
	/* unmap memory in the case of error */
	uio_shutdown(data);
	uiomux_unregister (data->jpeg_virt);

	return -1;
}

/*
 * allocate UIO memory for output buffer
 */
void *
shjpeg_malloc(shjpeg_context_t * context,
	      shjpeg_pixelformat format,
	      int width, int height, int pitch,
	      size_t *allocated_size)
{
	shjpeg_internal_t *data;
	void *vaddr;
	int req_size = pitch * SHJPEG_PF_PLANE_MULTIPLY(format, height);

	data = context->internal_data;
	vaddr = uiomux_malloc(data->uiomux, UIOMUX_JPU, req_size, 8);
	if (*allocated_size)
		*allocated_size = req_size;

	return vaddr;
}

/*
 * allocate UIO memory for output buffer
 */
void
shjpeg_free(shjpeg_context_t * context,
	    void *vaddr, size_t size)
{
	shjpeg_internal_t *data;

	data = context->internal_data;
	uiomux_free(data->uiomux, UIOMUX_JPU, vaddr, size);
}

/*
 * Main routines
 */

static shjpeg_internal_t data = {
	.ref_count = 0,
	.ref_mutex = PTHREAD_MUTEX_INITIALIZER,
};

/*
 * init libshjpeg
 */

shjpeg_context_t *shjpeg_init(int verbose)
{
	shjpeg_context_t *context;

	/* initialize context */
	if ((context = malloc(sizeof(shjpeg_context_t))) == NULL) {
		if (verbose)
			perror
			    ("libshjpeg: Can't allocate libshjpeg context - ");
		return NULL;
	}
	memset((void *) context, 0, sizeof(shjpeg_context_t));

	data.context = context;
	context->internal_data = &data;
	context->verbose = verbose;

	D_INFO("libshjpeg: %s - allocated memory.", __FUNCTION__);

	/* check ref count */

	pthread_mutex_lock(&data.ref_mutex);
	if (data.ref_count) {
		data.ref_count++;
		pthread_mutex_unlock(&data.ref_mutex);
		return context;
	}

	/* init uio */
	if (uio_init(context, &data)) {
		D_ERROR("libshjpeg: UIO initialization failed.");
		free(context);
		pthread_mutex_unlock(&data.ref_mutex);
		return NULL;
	}

	data.ref_count = 1;

	pthread_mutex_unlock(&data.ref_mutex);
	return context;
}

/*
 * shutdown libshjpeg
 */

void shjpeg_shutdown(shjpeg_context_t * context)
{
	/* clean up */
	if (context)
		free(context);

	pthread_mutex_lock(&data.ref_mutex);
	if (!data.ref_count)
		goto quit;

	if (--data.ref_count)
		goto quit;

	/* shutdown uio */
	uio_shutdown(&data);
	uiomux_unregister (data.jpeg_virt);

      quit:
	pthread_mutex_unlock(&data.ref_mutex);
	return;
}

/*
 * get contiguous memory info
 */

int
shjpeg_get_frame_buffer(shjpeg_context_t * context,
			void **buffer, size_t * size)
{
	if (!data.ref_count) {
		D_ERROR("libshjpeg: not initialized yet.");
		return -1;
	}

	if (buffer)
		*buffer = (void *) data.jpeg_virt + SHJPEG_JPU_SIZE;

	if (size)
		*size = data.jpeg_size - SHJPEG_JPU_SIZE;

	return 0;
}
