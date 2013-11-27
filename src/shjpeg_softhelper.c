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
#include <string.h>
#include <unistd.h>
#include "shjpeg_softhelper.h"
#include "shjpeg_jpu.h"

#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>

#define YMASK1 0x000000ff
#define YMASK2 0xff000000
#define YMASK3 0x00ff0000
#define YMASK4 0x0000ff00

#define CBMASK1 0x0000ff00
#define CBMASK2 0x000000ff
#define CBMASK3 0xff000000
#define CBMASK4 0x00ff0000

#define CRMASK1 0x00ff0000
#define CRMASK2 0x0000ff00
#define CRMASK3 0x000000ff
#define CRMASK4 0xff000000

void
free_frame_buffer_virtual(vmap_data_t *mdata)
{
	if (mdata->mapaddr) {
		munmap(mdata->mapaddr, mdata->mapbuflen);
		close(mdata->mapfd);
		mdata->mapaddr = NULL;
		mdata->mapbuflen = 0;
		mdata->mapfd = 0;
	}
}

int
get_frame_buffer_virtual(shjpeg_internal_t * data,
		shjpeg_context_t * context,
		vmap_data_t * mdata,
		shjpeg_pixelformat format,
		unsigned long phys)
{

	char path[MAXPATHLEN];
	off_t offset, virt_offset=0;
	mdata->mapbuflen = _PAGE_ALIGN(SHJPEG_PF_PLANE_MULTIPLY
		(format, context->height) *
		context->pitch) + _PAGE_SIZE;

	snprintf(path, MAXPATHLEN, "/dev/mem");
	offset = phys & ~(_PAGE_SIZE - 1);
	virt_offset = (phys & (_PAGE_SIZE - 1));
	mdata->mapfd = open(path, O_RDWR);
	if (mdata->mapfd < 0) {
		D_PERROR ("libshjpeg: Could not open %s,!", path);
		return -1;
	}

	mdata->mapaddr = mmap(NULL, mdata->mapbuflen,
		PROT_READ | PROT_WRITE, MAP_SHARED,
		mdata->mapfd, offset);

	if (mdata->mapaddr == MAP_FAILED) {
		D_PERROR ("libshjpeg: Could not map %s at "
			"0x%08lx (length %d)", path, phys,
			mdata->mapbuflen);
		close(mdata->mapfd);
		return -1;
	}
	data->user_jpeg_virt = mdata->mapaddr + virt_offset;
	return 0;
}

void soft_get_src_jpu(shjpeg_internal_t * data, void **ydata, void **cdata)
{
	if (!data->vio_linebuf) {
		*ydata = data->jpeg_lb1_virt;
		*cdata =
		    data->jpeg_lb1_virt + SHJPEG_JPU_LINEBUFFER_SIZE_Y;
	} else {
		*ydata = data->jpeg_lb2_virt;
		*cdata =
		    data->jpeg_lb2_virt + SHJPEG_JPU_LINEBUFFER_SIZE_Y;
	}
}

/****************
 *soft_fromYCbCr_byword
 *converts YCbCr data to NV16 (To convert to NV12, the JPU setup must
 *			       also be changed)
 *
 *Load 3 words of YCbCr data from the input buffer
 *and convert to 1 word of Y and 1 word of CbCr data
 *i.e.  Y3Y2Y1Y0 + Cr1Cb1Cr0Cb0 ->  Y1Cr0Cb0Y0 + Cb1Y2Cr0Cb0 + Cr1Cb1Y3Cr1
 *
 **************/
int
soft_fromYCbCr_byword(shjpeg_internal_t * data,
	       shjpeg_context_t * context,
	       u8 * outydata, u8 * outcdata, u8 * inbuf, int lines)
{
	int x, y;
	unsigned long inpitchskip;
	unsigned long outpitchskip;
	int width = (context->width + (CONV_CHUNK_SIZE -1)) / CONV_CHUNK_SIZE;
	u32 yword, cword;
	u32 inworda, inwordb, inwordc;
	u8 *in_buffer, *out_ybuffer, *out_cbuffer;
	u8 *inroot, *yroot, *croot;

#ifdef TIME_INFO
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 0
	};
	static long timer = 0;
	static int called = 0;

	gettimeofday(&tv, NULL);
	timer -= 1000 * tv.tv_sec;
	timer -= tv.tv_usec / 1000;
#endif
	outpitchskip =
	    SHJPEG_JPU_LINEBUFFER_PITCH;
	inpitchskip =
	    context->pitch - width * CONV_CHUNK_SIZE * 3;

	inroot = in_buffer = malloc(context->pitch * lines + CONV_CHUNK_SIZE);

	yroot = out_ybuffer = malloc(SHJPEG_JPU_LINEBUFFER_PITCH) ;
	croot = out_cbuffer = malloc(SHJPEG_JPU_LINEBUFFER_PITCH);
	memcpy (in_buffer, inbuf, context->pitch * lines);
	for (y = 0; y < lines; y++) {
		for (x = 0; x < width; x++) {
			u16 accumb, accumr;
			inworda = *(u32 *) in_buffer;
			inwordb = *(u32 *) (in_buffer+4);
			inwordc = *(u32 *) (in_buffer+8);

			yword = (inworda & YMASK1) |
				((inworda & YMASK2) >> 16) |
				(inwordb & YMASK3)  |
				((inwordc & YMASK4) << 16);

			accumb = ((inworda & CBMASK1) >> 8) +
				(inwordb & CBMASK2);

			accumr = ((inworda & CRMASK1) >> 16) +
				((inwordb & CRMASK2) >> 8);

			cword = (accumb >> 1) | ((accumr >> 1) << 8);

			accumb = ((inwordb & CBMASK3) >> 24) +
				((inwordc & CBMASK4) >> 16);

			accumr = (inwordc & CRMASK3) +
				((inwordc & CRMASK4) >> 24);

			cword |= ((accumb >> 1) | ((accumr >> 1) << 8)) << 16;

			*(u32 *) out_ybuffer = yword;
			*(u32 *) out_cbuffer = cword;
			out_ybuffer += 4;
			out_cbuffer += 4;
			in_buffer += 12;
		}
		memcpy (outydata, yroot, width * CONV_CHUNK_SIZE);
		memcpy (outcdata, croot, width * CONV_CHUNK_SIZE);
		outydata += outpitchskip;
		outcdata += outpitchskip;
		out_ybuffer = yroot;
		out_cbuffer = croot;
		in_buffer += inpitchskip;
	}
	free(yroot);
	free(croot);
	free(inroot);

#ifdef TIME_INFO
	gettimeofday(&tv, NULL);
	timer += 1000 * tv.tv_sec;
	timer += tv.tv_usec / 1000;
	called++;
	if (lines < SHJPEG_JPU_LINEBUFFER_HEIGHT) {
		fprintf(stderr, "Total time in soft compress: %ldms\n",
			timer);
		fprintf(stderr, "Total number of times called: %d\n",
			called);
	}
#endif
	return 0;
}

/****************
 *soft_toYCbCr_byword
 *converts NV12/NV16 data to YCbCr.
 *
 *Load 1 word (32 bits) of Y data and 1 word of CbCr data from
 *the input buffer and convert to 4 YCbCr pixels.
 *i.e.  Y3Y2Y1Y0 + Cr1Cb1Cr0Cb0 ->  Y1Cr0Cb0Y0 + Cb1Y2Cr0Cb0 + Cr1Cb1Y3Cr1
 *
 **************/
int
soft_toYCbCr_byword(shjpeg_internal_t * data,
	     shjpeg_context_t * context,
	     u8 * inydata, u8 * incdata, u8 * outbuf, int lines)
{
	int x, y;
	boolean mode420 = context->mode420;
	u32 outwords[3];

	u32 yword;
	u32 cword;
	u8 *outroot, *yroot, *croot;
	u8 *out_buffer, *in_ybuffer, *in_cbuffer;

	//unsigned long syncout = (unsigned long) outbuf;

	unsigned int width = (context->width + (CONV_CHUNK_SIZE -1 ))
				/ CONV_CHUNK_SIZE;

	long inpitchskip;
	long outpitchskip;

#ifdef TIME_INFO
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 0
	};
	static long timer = 0;
	static int called = 0;
	gettimeofday(&tv, NULL);
	timer -= 1000 * tv.tv_sec;
	timer -= tv.tv_usec / 1000;
#endif
	yroot = in_ybuffer = malloc(SHJPEG_JPU_LINEBUFFER_PITCH);
	croot = in_cbuffer = malloc(SHJPEG_JPU_LINEBUFFER_PITCH);
	outroot = out_buffer = malloc(context->pitch * lines +
			CONV_CHUNK_SIZE * 3);

	inpitchskip = SHJPEG_JPU_LINEBUFFER_PITCH;
	outpitchskip = context->pitch - (width * CONV_CHUNK_SIZE) * 3;

	for (y = 0; y < lines; y++) {
		memcpy(in_ybuffer, inydata, width * CONV_CHUNK_SIZE);
		memcpy(in_cbuffer, incdata, width * CONV_CHUNK_SIZE);
		for (x = 0; x < width; x++) {
			yword = *(u32 *) in_ybuffer;
			cword = *(u32 *) in_cbuffer;

			/*outwords[0]= Y1Cr0Cb0Y0*/
			outwords[0] = (yword & 0xff) |
				   ((cword & 0xffff) << 8) |
				   ((yword & 0xff00) << 16);

			/*outwords[1] = Cb1Y2Cr0Cb0*/
			outwords[1] = (cword & 0xffff) |
				   (yword & 0xff0000) |
				   (cword & 0xff0000) << 8;

			/*outwords[2] = Cr1Cb1Y3Cr1*/
			outwords[2] = ((cword & 0xff000000) >> 24) |
				   ((yword & 0xff000000) >> 16) |
				   (cword & 0xffff0000);

			*(u32 *) (out_buffer) = outwords[0];
			*(u32 *) (out_buffer+4) = outwords[1];
			*(u32 *) (out_buffer+8) = outwords[2];
			in_ybuffer += 4;
			in_cbuffer += 4;
			out_buffer += 12;
		}
		if (!mode420 || y & 1) {
			incdata += inpitchskip;
		}
		inydata += inpitchskip;
		in_ybuffer = yroot;
		in_cbuffer = croot;
		out_buffer += outpitchskip;
	}
	memcpy(outbuf, outroot, context->pitch * lines);
	free(yroot);
	free(croot);
	free(outroot);

#ifdef TIME_INFO
	gettimeofday(&tv, NULL);
	timer += 1000 * tv.tv_sec;
	timer += tv.tv_usec / 1000;
	called++;
	if (lines < SHJPEG_JPU_LINEBUFFER_HEIGHT) {
		fprintf(stderr, "Total time in soft conversion: %ldms\n",
			timer);
		fprintf(stderr, "Total number of times called: %d\n",
			called);

	}
#endif
	return 0;
}

int
soft_fromYCbCr_bybyte(shjpeg_internal_t * data,
	       shjpeg_context_t * context,
	       u8 * outydata, u8 * outcdata, u8 * inbuf, int lines)
{
	int x, y;
	unsigned short accum_cb = 0;
	unsigned short accum_cr = 0;
	u8 *dst_ydata, *dst_cdata, *in_buffer;
#ifdef USE_CACHED
	u8 *yroot, *croot, *inroot;
#endif
	unsigned long inpitchskip;
	unsigned long outpitchskip;
#ifdef TIME_INFO
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 0
	};
	static long timer = 0;
	static int called = 0;

	gettimeofday(&tv, NULL);
	timer -= 1000 * tv.tv_sec;
	timer -= tv.tv_usec / 1000;
#endif

#ifdef USE_CACHED
	yroot = dst_ydata = malloc(SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	croot = dst_cdata = malloc(SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	inroot = in_buffer = malloc(context->pitch * lines + 4);
	memcpy(in_buffer, inbuf, context->pitch * lines);
#else
	dst_ydata = outydata;
	dst_cdata = outcdata;
	in_buffer = inbuf;
#endif

	outpitchskip = SHJPEG_JPU_LINEBUFFER_PITCH - context->width;
	inpitchskip = context->pitch - context->width * 3;

	for (y = 0; y < lines; y++) {
		for (x = 0; x < context->width; x++) {
			*(dst_ydata++) = *(in_buffer++);
			accum_cb += *(in_buffer++);
			accum_cr += *(in_buffer++);
			if (x & 1) {
				*(dst_cdata++) = accum_cb >> 1;
				*(dst_cdata++) = accum_cr >> 1;
				accum_cb = accum_cr = 0;
			}
		}
		if (context->width & 1) {
			*(dst_cdata++) = accum_cb;
			*(dst_cdata) = accum_cr;
			accum_cb = accum_cr = 0;
		}
		dst_ydata += outpitchskip;
		dst_cdata += outpitchskip;
		in_buffer += inpitchskip;
	}

#ifdef USE_CACHED
	memcpy(outydata, yroot, SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	memcpy(outcdata, croot, SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	free(yroot);
	free(croot);
	free(inroot);
#endif

#ifdef TIME_INFO
	gettimeofday(&tv, NULL);
	timer += 1000 * tv.tv_sec;
	timer += tv.tv_usec / 1000;
	called++;
	if (lines < SHJPEG_JPU_LINEBUFFER_HEIGHT) {
		fprintf(stderr, "Total time in soft conversion: %ldms\n",
			timer);
		fprintf(stderr, "Total number of times called: %d\n",
			called);
	}
#endif
	return 0;
}

int
soft_toYCbCr_bybyte(shjpeg_internal_t * data,
	     shjpeg_context_t * context,
	     u8 * inydata, u8 * incdata, u8 * outbuf, int lines)
{
	int x, y;
	boolean mode420 = context->mode420;
	u8 *rewind;
	u8 *out_buffer;
	u8 *src_ydata;
	u8 *src_cdata;
	unsigned int halfwidth = context->width / 2 + (context->width % 2);
#ifdef USE_CACHED
	u8 *croot, *yroot, *root;
	unsigned long outsize =
	    context->pitch > (halfwidth * 6) ? context->pitch : halfwidth * 6;
#endif
	long inpitchskip;
	long outpitchskip;

#ifdef TIME_INFO
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 0
	};
	static long timer = 0;
	static int called = 0;
	gettimeofday(&tv, NULL);
	timer -= 1000 * tv.tv_sec;
	timer -= tv.tv_usec / 1000;
#endif
#ifdef USE_CACHED
	outsize = outsize * lines;
	root = out_buffer = malloc(outsize);
	yroot = src_ydata = malloc(SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	croot = src_cdata = malloc(SHJPEG_JPU_LINEBUFFER_PITCH * lines);

	memcpy(src_ydata, inydata, SHJPEG_JPU_LINEBUFFER_PITCH * lines);
	memcpy(src_cdata, incdata, SHJPEG_JPU_LINEBUFFER_PITCH * lines);
#else
	out_buffer = outbuf;
	src_ydata = inydata;
	src_cdata = incdata;
#endif

	inpitchskip = SHJPEG_JPU_LINEBUFFER_PITCH - halfwidth*2;
	outpitchskip =
	    context->pitch - (halfwidth * 2) * 3;
	rewind = src_cdata;

	for (y = 0; y < lines; y++) {
		for (x = 0; x < halfwidth; x++) {
			*(out_buffer++) = *(src_ydata++);	//Y
			*(out_buffer++) = *(src_cdata);		//U
			*(out_buffer++) = *(src_cdata + 1);	//V

			*(out_buffer++) = *(src_ydata++);	//Y
			*(out_buffer++) = *(src_cdata);		//U
			*(out_buffer++) = *(src_cdata + 1);	//V
			src_cdata += 2;
		}
		if (y & 1 || !mode420) {
			src_cdata += inpitchskip;
			rewind = src_cdata;
		} else {
			src_cdata = rewind;
		}
		src_ydata += inpitchskip;
		out_buffer += outpitchskip;
	}

#ifdef USE_CACHED
	memcpy(outbuf, root, context->pitch * lines);
	free(root);
	free(yroot);
	free(croot);
#endif

#ifdef TIME_INFO
	gettimeofday(&tv, NULL);
	btimer += 1000 * tv.tv_sec;
	timer += tv.tv_usec / 1000;
	called++;
	if (lines < SHJPEG_JPU_LINEBUFFER_HEIGHT) {
		fprintf(stderr, "Total time in soft conversion: %ldms\n",
			timer);
		fprintf(stderr, "Total number of times called: %d\n",
			called);

	}
#endif
	return 0;
}
