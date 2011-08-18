/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2009,2010 IGEL Co.,Ltd.
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

/*
 Here is a very rough outline of the processing:

 The callee provides a bitmask of input buffers to be processed.
 The JPU hardware processes the input buffers on a 'line buffer' basis and
 fires off interrupts when each line buffer processing is complete. Each line
 buffer is fixed at 16 pixels. For jpeg decode, the line buffer IRQ happens
 when the JPU has output a line buffer; for encode it happens when the JPU has
 finished with a line buffer of input.

 When all of the data in the input buffer is consumed, the JPU will fire a
 'reload complete' interrupt. In order to get the JPU to start again, you send
 a 'read restart' command. The 'reload complete' interrupt can occur during or
 after line buffer processing.

 The VEU can scale & color convert pixel data and this is setup to work on
 line buffers as they are needed or output from the JPU.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_regs.h"
#include "shjpeg_jpu.h"
#include "shjpeg_veu.h"
#include "shjpeg_softhelper.h"

/*
 * reset JPU
 */
void shjpeg_jpu_reset(shjpeg_internal_t * data)
{
	/* bus reset */
	shjpeg_jpu_setreg32(data, JPU_JCCMD, 0x80);

	/* software reset */
	shjpeg_jpu_setreg32(data, JPU_JCCMD, 0x1000);

	/* wait for reset */
	while (shjpeg_jpu_getreg32(data, JPU_JCCMD) & 0x1000)
		usleep(1);
}

static void
start_jpu_line(shjpeg_context_t *context, shjpeg_internal_t *data)
{
	D_INFO("libshjpeg: jpu: start LB%d (LCMDx)", data->jpeg_linebuf);
	shjpeg_jpu_setreg32(data, JPU_JCCMD,
		JPU_JCCMD_LCMD1 | JPU_JCCMD_LCMD2);
	data->jpu_line_bufs_pending++;
}

static void
process_jpu_ints(int ints, shjpeg_context_t *context,
		shjpeg_internal_t *data, int *done)
{
	/* Header */
	if (ints & JPU_JINTS_INS3_HEADER) {
		/* Note: This IRQ is never enabled */
		D_INFO("libshjpeg: header=%dx%d",
			shjpeg_jpu_getreg32(data, JPU_JIFDDHSZ),
			shjpeg_jpu_getreg32(data, JPU_JIFDDVSZ));
	}

	/* Error */
	if (ints & JPU_JINTS_INS5_ERROR) {
		data->jpeg_error = shjpeg_jpu_getreg32(data, JPU_JCDERR);
		D_INFO("libshjpeg: error");
		*done = 1;
	}

	/* Done - but image transfer not complete */
	if (ints & JPU_JINTS_INS6_DONE) {
		D_INFO("libshjpeg: done");
		if (!(ints & JPU_JINTS_INS10_XFER_DONE))
			D_ERROR("libshjpeg: Error: No INS10 interrupt");
	}

	/* Done */
	if (ints & JPU_JINTS_INS10_XFER_DONE) {
		data->jpeg_end = 1;
		D_INFO("libshjpeg: xfer done");
		*done = 1;
	}

	/* Line buffer processing complete */
	if (ints & (JPU_JINTS_INS11_LINEBUF0 | JPU_JINTS_INS12_LINEBUF1)) {
		D_INFO ("libshjpeg: jpu: finished LB%d", data->jpeg_linebuf);
		/* move index to next buffer */
		data->jpeg_linebuf = (data->jpeg_linebuf + 1) % 2;
		data->jpu_line_bufs_pending--;
		data->jpu_line_bufs_done++;
	}

	/* Loaded */
	if (ints & JPU_JINTS_INS13_LOADED) {
		D_INFO ("libshjpeg: load complete (%d)", data->jpeg_buffer);
		data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		data->jpeg_buffer = (data->jpeg_buffer + 1) % 2;
		*done = 1;
	}

	/* Reload - means input buffer has been used, need more data */
	if (ints & JPU_JINTS_INS14_RELOAD) {
		D_INFO ("libshjpeg: reload complete (%d)", data->jpeg_buffer);
		data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		data->jpeg_buffer = (data->jpeg_buffer + 1) % 2;
		*done = 1;
	}
}

/* Wait for JPU processing to finish. It may not finish processing
   a line buffer if a reload is required */
static int
wait_and_process_jpu(shjpeg_context_t * context,
		shjpeg_internal_t * data, int *done)
{
	int ret, ints, val;

	struct pollfd fds[] = {
		{
		 .fd = data->jpu_uio_fd,
		 .events = POLLIN,
		 }
	};

	// wait for IRQ. time out set to 1sec.
	fds[0].revents = 0;
	ret = poll(fds, 1, 1000);

	// timeout or some error.
	if (ret == 0) {
		D_ERROR ("libshjpeg: jpu: Error TIMEOUT");
		errno = ETIMEDOUT;
		return -1;
	}

	if (ret < 0) {
		D_ERROR("libshjpeg: no IRQ - poll() failed");
		return -1;
	}

	/* Handle IRQs */
	if (fds[0].revents & POLLIN) {
		/* read number of interrupts */
		if (read(data->jpu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
			D_ERROR ("libshjpeg: no IRQ - read() failed");
			errno = EIO;
			return -1;
		}

		/* get JPU IRQ stats */
		ints = shjpeg_jpu_getreg32(data, JPU_JINTS);
		shjpeg_jpu_setreg32(data, JPU_JINTS, ~ints & JPU_JINTS_MASK);

		D_INFO("libshjpeg: JPU interrupt 0x%08x(%08x) "
			"(veu_linebuf: %d, jpeg_linebuf: %d, "
			"jpeg_buffers: %d)",
			ints, shjpeg_jpu_getreg32(data, JPU_JINTS),
			data->veu_linebuf, data->jpeg_linebuf,
			data->jpeg_buffers);

		if (ints) {
			process_jpu_ints(ints, context, data, done);
		}

		if (ints &
		    (JPU_JINTS_INS3_HEADER | JPU_JINTS_INS5_ERROR |
		     JPU_JINTS_INS10_XFER_DONE)) {
			D_INFO ("libshjpeg: JPU_JCCMD:END");
			shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_END);
		}

		/* re-enable IRQ */
		val = 1;
		if (write(data->jpu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
			D_PERROR("libshjpeg: write() to uio failed.");
			return -1;
		}
	}

	return 0;
}

static void
start_veu(shjpeg_context_t * context,
	shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	D_INFO("libshjpeg: veu: start LB%d", data->veu_linebuf);

	if (data->jpeg_encode) {
		shjpeg_veu_set_src(data, jpeg->sa_y, jpeg->sa_c);
		shjpeg_veu_set_dst_jpu(data);
		shjpeg_veu_start(data, 0);

		/* Update the source addresses for the next call */
		jpeg->sa_y += jpeg->sa_inc;
		jpeg->sa_c += jpeg->sa_inc;
	} else {
		/* Set the VEU src addrs using value of data->veu_linebuf */
		shjpeg_veu_set_src_jpu(data);
		shjpeg_veu_start(data, 1);
	}
}

static int
wait_and_process_veu(shjpeg_context_t * context,
		shjpeg_internal_t * data)
{
	shveu_wait(data->veu);

	D_INFO("libshjpeg: veu: finished LB%d", data->veu_linebuf);

	/* point to the other buffer */
	data->veu_linebuf = (data->veu_linebuf + 1) % 2;
	data->veu_line_bufs_done++;

	return 0;
}

/* Colorspace conversion in software */
static void
shjpeg_sw_convert(shjpeg_context_t * context,
		shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	void *ydata, *cdata;
	int lines;

	soft_get_src_jpu(data, &ydata, &cdata);
	lines = context->height - jpeg->soft_line;
	if (lines > SHJPEG_JPU_LINEBUFFER_HEIGHT)
		lines =  SHJPEG_JPU_LINEBUFFER_HEIGHT;

	if (lines <= 0)
		return;

	D_INFO("libshjpeg: soft: process LB%d", data->veu_linebuf);
	if (data->jpeg_encode) {
		soft_fromYCbCr(data, context, ydata, cdata,
			data->user_jpeg_virt + jpeg->soft_offset, lines);
	} else {
		soft_toYCbCr(data, context, ydata, cdata,
			data->user_jpeg_virt + jpeg->soft_offset, lines);
	}
	jpeg->soft_offset += context->pitch * lines;
	jpeg->soft_line += lines;
	data->veu_linebuf = (data->veu_linebuf + 1) % 2;
	data->veu_line_bufs_done++;
}

/* Do colour space converion on a line buffer */
static int
shjpeg_convert(shjpeg_context_t * context,
		shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	int ret = 0;
	int hw_convert = (jpeg->flags & SHJPEG_JPU_FLAG_CONVERT);
	int sw_convert = (jpeg->flags & SHJPEG_JPU_FLAG_SOFTCONVERT);

	if (hw_convert) {
		start_veu(context, data, jpeg);
		ret = wait_and_process_veu(context, data);
	} else if (sw_convert) {
		shjpeg_sw_convert(context, data, jpeg);
	} else {
		data->veu_line_bufs_done++;
	}

	return ret;
}

static int
jpu_encode(shjpeg_context_t * context,
		shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	int done = 0;

	D_INFO("libshjpeg: jpu: WRITE_RESTART LB=%d", data->jpeg_linebuf);
	shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_WRITE_RESTART);

	while (!done) {
		if ((data->jpu_line_bufs_done < data->veu_line_bufs_done) &&
		    (data->jpu_line_bufs_pending == 0)) {
			start_jpu_line(context, data);
		}

		if (data->jpu_line_bufs_done == data->veu_line_bufs_done) {
			shjpeg_convert(context, data, jpeg);
		}

		if (!data->jpeg_end &&
		    (data->jpu_line_bufs_pending > 0)) {
			if (wait_and_process_jpu(context, data, &done) < 0)
				return -1;
		}
	}


	return 0;
}

static int
jpu_decode(shjpeg_context_t * context,
		shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	int done = 0;

	D_INFO("libshjpeg: jpu: READ_RESTART LB%d", data->jpeg_linebuf);
	shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_READ_RESTART);

	while (!done) {
		if (!data->jpeg_end &&
		    (data->jpu_line_bufs_pending == 0)) {
			start_jpu_line(context, data);
		}

		if (data->jpu_line_bufs_done > data->veu_line_bufs_done) {
			shjpeg_convert(context, data, jpeg);
		}

		if (!data->jpeg_end &&
		    (data->jpu_line_bufs_pending > 0)) {
			if (wait_and_process_jpu(context, data, &done) < 0)
				return -1;
		}
	}

	if (!data->jpeg_error &&
	    data->jpu_line_bufs_done > data->veu_line_bufs_done) {
		shjpeg_convert(context, data, jpeg);
	}

	return 0;
}


/*
 * Main JPU control
 */
int
shjpeg_jpu_run(shjpeg_context_t * context,
		shjpeg_internal_t * data, shjpeg_jpu_t * jpeg)
{
	int encode = (jpeg->flags & SHJPEG_JPU_FLAG_ENCODE);

	D_DEBUG_AT(SH7722_JPEG, "%s: entering", __FUNCTION__);

	switch (jpeg->state) {
	case SHJPEG_JPU_START:
		D_INFO("START (buffers: %d, flags: 0x%x)", jpeg->buffers,
		       jpeg->flags);

		data->jpeg_end = 0;
		data->jpeg_error = 0;
		data->jpeg_encode = encode;
		data->jpeg_buffer = 0;
		data->jpeg_buffers = jpeg->buffers;
		data->jpeg_linebuf = 0;
		data->jpu_line_bufs_pending = (encode) ? 0 : 2;
		data->jpu_line_bufs_done = 0;

		data->veu_linebuf = 0;
		data->veu_line_bufs_done = 0;

		jpeg->state = SHJPEG_JPU_RUN;
		jpeg->error = 0;

		shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_START);

		/* Encode: Scale/convert one buffer in advance of the JPU */
		if (encode) {
			shjpeg_convert(context, data, jpeg);
		}

		break;

	case SHJPEG_JPU_RUN:
		D_INFO("RUN (buffers: %d)", jpeg->buffers);

		/* Validate loaded buffers. */
		data->jpeg_buffers |= jpeg->buffers;
		break;

	default:
		D_ERROR("libshjpeg: %s: "
			"INVALID STATE %d! (status 0x%08x, ints 0x%08x)",
			__FUNCTION__, jpeg->state,
			shjpeg_jpu_getreg32(data, JPU_JCSTS),
			shjpeg_jpu_getreg32(data, JPU_JINTS));
		errno = EINVAL;
		return -1;
	}


	if (encode) {
		if (jpu_encode(context, data, jpeg) < 0)
			return -1;
	} else {
		if (jpu_decode(context, data, jpeg) < 0)
			return -1;
	}


	if (data->jpeg_error) {
		/* Return error. */
		jpeg->state = SHJPEG_JPU_END;
		jpeg->error = data->jpeg_error;
		D_INFO("libshjpeg: '-> ERROR (0x%x)", jpeg->error);
	} else {
		/* Return buffers to reload or to empty. */
		jpeg->buffers = data->jpeg_buffers ^ 3;

		if (data->jpeg_end) {
			D_INFO("libshjpeg: '-> END");

			/* Return end. */
			jpeg->state = SHJPEG_JPU_END;
			jpeg->buffers |= 1 << data->jpeg_buffer;
		} else if (encode) {
			D_INFO("libshjpeg: '-> LOADED (%d)", jpeg->buffers);
		} else {
			D_INFO("libshjpeg: '-> RELOAD (%d)", jpeg->buffers);
		}
	}

	return 0;
}

/*
 * Init quantization table
 */

void shjpeg_jpu_init_quantization_table(shjpeg_internal_t * data)
{
	/* Init quantization tables. */
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(0), 0x100B0B0E);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(1), 0x0C0A100E);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(2), 0x0D0E1211);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(3), 0x10131828);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(4), 0x1A181616);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(5), 0x18312325);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(6), 0x1D283A33);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(7), 0x3D3C3933);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(8), 0x38374048);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(9), 0x5C4E4044);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(10), 0x57453738);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(11), 0x506D5157);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(12), 0x5F626768);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(13), 0x673E4D71);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(14), 0x79706478);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL0(15), 0x5C656763);

	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(0), 0x11121218);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(1), 0x15182F1A);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(2), 0x1A2F6342);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(3), 0x38426363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(4), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(5), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(6), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(7), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(8), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(9), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(10), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(11), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(12), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(13), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(14), 0x63636363);
	shjpeg_jpu_setreg32(data, JPU_JCQTBL1(15), 0x63636363);
}

/*
 * Init huffman table
 */
void shjpeg_jpu_init_huffman_table(shjpeg_internal_t * data)
{
	/* Init huffman tables. */
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(0), 0x00010501);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(1), 0x01010101);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(2), 0x01000000);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(3), 0x00000000);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(4), 0x00010203);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(5), 0x04050607);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD0(6), 0x08090A0B);

	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(0), 0x00030101);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(1), 0x01010101);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(2), 0x01010100);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(3), 0x00000000);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(4), 0x00010203);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(5), 0x04050607);
	shjpeg_jpu_setreg32(data, JPU_JCHTBD1(6), 0x08090A0B);

	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(0), 0x00020103);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(1), 0x03020403);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(2), 0x05050404);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(3), 0x0000017D);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(4), 0x01020300);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(5), 0x04110512);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(6), 0x21314106);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(7), 0x13516107);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(8), 0x22711432);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(9), 0x8191A108);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(10), 0x2342B1C1);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(11), 0x1552D1F0);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(12), 0x24336272);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(13), 0x82090A16);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(14), 0x1718191A);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(15), 0x25262728);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(16), 0x292A3435);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(17), 0x36373839);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(18), 0x3A434445);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(19), 0x46474849);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(20), 0x4A535455);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(21), 0x56575859);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(22), 0x5A636465);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(23), 0x66676869);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(24), 0x6A737475);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(25), 0x76777879);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(26), 0x7A838485);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(27), 0x86878889);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(28), 0x8A929394);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(29), 0x95969798);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(30), 0x999AA2A3);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(31), 0xA4A5A6A7);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(32), 0xA8A9AAB2);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(33), 0xB3B4B5B6);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(34), 0xB7B8B9BA);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(35), 0xC2C3C4C5);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(36), 0xC6C7C8C9);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(37), 0xCAD2D3D4);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(38), 0xD5D6D7D8);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(39), 0xD9DAE1E2);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(40), 0xE3E4E5E6);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(41), 0xE7E8E9EA);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(42), 0xF1F2F3F4);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(43), 0xF5F6F7F8);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA0(44), 0xF9FA0000);

	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(0), 0x00020102);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(1), 0x04040304);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(2), 0x07050404);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(3), 0x00010277);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(4), 0x00010203);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(5), 0x11040521);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(6), 0x31061241);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(7), 0x51076171);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(8), 0x13223281);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(9), 0x08144291);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(10), 0xA1B1C109);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(11), 0x233352F0);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(12), 0x156272D1);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(13), 0x0A162434);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(14), 0xE125F117);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(15), 0x18191A26);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(16), 0x2728292A);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(17), 0x35363738);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(18), 0x393A4344);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(19), 0x45464748);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(20), 0x494A5354);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(21), 0x55565758);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(22), 0x595A6364);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(23), 0x65666768);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(24), 0x696A7374);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(25), 0x75767778);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(26), 0x797A8283);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(27), 0x84858687);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(28), 0x88898A92);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(29), 0x93949596);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(30), 0x9798999A);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(31), 0xA2A3A4A5);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(32), 0xA6A7A8A9);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(33), 0xAAB2B3B4);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(34), 0xB5B6B7B8);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(35), 0xB9BAC2C3);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(36), 0xC4C5C6C7);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(37), 0xC8C9CAD2);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(38), 0xD3D4D5D6);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(39), 0xD7D8D9DA);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(40), 0xE2E3E4E5);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(41), 0xE6E7E8E9);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(42), 0xEAF2F3F4);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(43), 0xF5F6F7F8);
	shjpeg_jpu_setreg32(data, JPU_JCHTBA1(44), 0xF9FA0000);
}
