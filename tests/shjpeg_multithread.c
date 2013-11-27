/*
 * Copyright 2009 IGEL Co.,Ltd.
 * Copyright 2008,2009 Renesas Solutions Co.
 * Copyright 2008 Denis Oliver Kropp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include <shjpeg/shjpeg.h>

struct fb_fix_screeninfo fb_info;
struct fb_var_screeninfo fb_vinfo;

#define IMG_WIDTH	320
#define IMG_HEIGHT	240
#define IMG_BPP		2

struct jpeg_buffer {
	char	*ptr;
	int	wr_count;
	int	rd_count;
};

int sops_init(void *priv)
{
    struct jpeg_buffer *jbuf = (struct jpeg_buffer *)priv;
    jbuf->wr_count = jbuf->rd_count = 0;

    return 0;
}

int sops_read(void *priv, size_t *nbytes, void *dataptr)
{
    struct jpeg_buffer *jbuf = (struct jpeg_buffer *)priv;

    memcpy(dataptr, jbuf->ptr + jbuf->rd_count, *nbytes);
    jbuf->rd_count += *nbytes;
    return 0;
}

int sops_write(void *priv, size_t *nbytes, void *dataptr)
{
    struct jpeg_buffer *jbuf = (struct jpeg_buffer *)priv;
    memcpy(jbuf->ptr + jbuf->wr_count, dataptr, *nbytes);
    jbuf->wr_count += *nbytes;
    return 0;
}

void sops_finalize(void *priv)
{
}

shjpeg_sops my_sops = {
    .init     = sops_init,
    .read     = sops_read,
    .write    = sops_write,
    .finalize = sops_finalize,
};

const char *argv0;

int setup_framebuffer(char *fbdev, int *fd, void **fbvirt) {
	int fb_fd;
	fb_fd = open(fbdev, O_RDWR, 0);
        if (ioctl (fb_fd, FBIOGET_FSCREENINFO, &fb_info) < 0) {
                printf("FSCREENINFO error");
                return -1;
        }
        if (ioctl (fb_fd, FBIOGET_VSCREENINFO, &fb_vinfo) < 0) {
                printf("VSCREENINFO error");
                return -1;
        }
	*fbvirt = mmap (0, fb_info.smem_len, PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (!*fbvirt) {
		printf("Cannot map framebuffer\n");
		return -1;
	}
	*fd = fb_fd;
	return 0;
}

void write_framebuffer(void *fb_buffer,
	  void		*data_buf,
	  int            pitch,
	  unsigned int   width,
	  unsigned int   height) {
	int i;
	int copy_height, copy_width;

	copy_height = height > fb_vinfo.yres ? fb_vinfo.yres : height;
	copy_width = width > (fb_vinfo.xres / 2) ? (fb_vinfo.xres / 2): width;
	for (i = 0; i < height; i++) {
		memcpy(fb_buffer, data_buf, copy_width *
					fb_vinfo.bits_per_pixel / 8);
		fb_buffer += fb_info.line_length;
		data_buf += pitch;
	}
}

void fill_pattern(char *buffer, int width, int height, int index,
		int direction) {
	int linenum;
	int pitch = width * IMG_BPP;
	static char patterns[] = {
		0xe0,
		0x70,
		0x0e,
		0xb2,
		0xff,
	};
	char pattern = patterns[(index / height) % sizeof(patterns)];

	if (direction == 0) // top to bottom
		linenum = index % height;
	else // bottom to top
		linenum = (height - 1) - (index % height);

	memset(buffer + pitch * linenum, pattern, pitch);
}

int decode_jpeg(char *buffer, int pattern, int cnt) {
	size_t bufsize;
	shjpeg_context_t *context;
	int format, pitch;
	char *jpeg_buf;
	void *encode_buffer;
	void *decode_result;
	struct jpeg_buffer jbuf;
	int i;

	if ((context = shjpeg_init(0)) == NULL) {
		fprintf(stderr, "shjpeg_init() failed\n");
		return 1;
	}

	context->width = IMG_WIDTH;
	context->height = IMG_HEIGHT;
	format = SHJPEG_PF_RGB16;
	pitch  = (SHJPEG_PF_PITCH_MULTIPLY(format) * context->width + 7) & ~7;

	encode_buffer = shjpeg_malloc(context, format, context->width,
				      context->height, pitch, &bufsize);
	decode_result = shjpeg_malloc(context, format, context->width,
				      context->height, pitch, &bufsize);

	if (!encode_buffer || !decode_result) {
		fprintf(stderr, "Out of UIO memory\n");
		return 1;
	}

	jpeg_buf = malloc(bufsize);

	jbuf.ptr = jpeg_buf;
	/* set callbacks to context */
	context->sops = &my_sops;
	context->priv_data = (void*)&jbuf;
	context->libjpeg_disabled = 0;

	memset(encode_buffer, 0, bufsize);

	for (i = 0; i < cnt || cnt < 0; i++) {
		/* start of frame */
			fill_pattern(encode_buffer, context->width,
				context->height, i, pattern);
		if (shjpeg_encode(context, format, encode_buffer,
			      context->width, context->height, pitch) < 0) {
			fprintf(stderr, "shjpeg_encode() failed.\n");
			return 1;
		}

		/* init decoding (read jpeg header) */
		if (shjpeg_decode_init(context) < 0) {
			fprintf(stderr, "shjpeg_decode_init() failed\n");
			return 1;
		}

		/* start decoding */
		if (shjpeg_decode_run(context, format, decode_result,
				context->width, context->height, pitch) < 0) {
			fprintf(stderr, "shjpeg_deocde_run() failed\n");
		}
		/* shutdown decoder */
		shjpeg_decode_shutdown(context);
		/* get framebuffer information */
		write_framebuffer(buffer, decode_result, pitch, context->width,
			context->height);
		/* end of frame */
	}
	shjpeg_shutdown(context);
	free(jpeg_buf);
	shjpeg_free(context, encode_buffer, bufsize);
	shjpeg_free(context, decode_result, bufsize);
	return 0;
}

struct decode_data {
	void *fb_buffer;	//Location in FB to draw
	int pattern;		//pattern to encode/decode
	int count;		//repeat count
};

void *decode_thread(void *arg) {
	struct decode_data *dec_data = (struct decode_data *)arg;
	decode_jpeg(dec_data->fb_buffer, dec_data->pattern,
		dec_data->count);
	return 0;
}

void
print_usage() {
    fprintf(stderr,
	    "Usage: %s [OPTION]\n", argv0);
    fprintf(stderr,
	    "- Decode given JPEG file, and then re-encode.\n"
	    "- Default re-encoed JPEG filename is with '.out' as a suffix.\n"
	    "\n"
	    "Options:\n"
	    "  -h, --help                this message.\n"
	    "  -q, --quiet		 no messages from this program.\n"
	    "  -c <cnt>, --count=<cnt>   Frame count (default: infinite)\n");
}

int
main(int argc, char *argv[])
{
    int			   count = -1;
    int			   quiet = 0;
    struct decode_data     decode_inst1, decode_inst2;
    pthread_t		   tid1, tid2;
    void		  *fb_buffer;
    int			   fb_fd;

    argv0 = argv[0];

    /* parse arguments */
    while (1) {
	int c, option_index = 0;
	static struct option   long_options[] = {
	    {"help", 0, 0, 'h'},
	    {"quiet", 0, 0, 'q'},
	    {"count", 1, 0, 'c'},
	    {0, 0, 0, 0}
	};

	if ((c = getopt_long(argc, argv, "hc:q",
			     long_options, &option_index)) == -1)
	    break;

	switch(c) {
	case 'h':	// help
	    print_usage();
	    return 0;

	case 'c':
	    count =  strtol(optarg, NULL, 0);
	    break;

	case 'q':
	    quiet = 1;
	    break;

	default:
	    fprintf(stderr, "unknown option 0%x.\n", c);
	    print_usage();
	    return 1;
	}
    }


    if (optind > argc) {
	print_usage();
	return 1;
    }

    setup_framebuffer("/dev/fb0", &fb_fd, &fb_buffer);

    decode_inst1.fb_buffer = fb_buffer;
    decode_inst1.pattern = 0;
    decode_inst1.count = count;

    decode_inst2.fb_buffer = fb_buffer + fb_info.line_length / 2;
    decode_inst2.pattern = 1;
    decode_inst2.count = count;

    pthread_create(&tid1, NULL, decode_thread, &decode_inst1);
    pthread_create(&tid2, NULL, decode_thread, &decode_inst2);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    printf("done!\n");

    return 0;
}
