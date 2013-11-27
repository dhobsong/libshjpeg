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

#ifndef __shjpeg_h__
#define __shjpeg_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <shjpeg/shjpeg_types.h>

/**
 * \file shjpeg.h
 *
 * A library for controlling SH-Mobile JPEG hardware codec.
 */

/**
 * \mainpage
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
 *
 * Encoding can only be from/to NV12/NV16 pixel format.
 */

/**
 * \brief Initialize SH-Mobile JPEG library
 *
 * Initialize the SH-Mobile JPEG library. This must be called before
 * calling any APIs in this library.
 *
 * \param verbose [in] if non-zero value is set, verbose debug message is enabled.
 *
 * \retval 0 success
 * \retval -1 failed
 */

shjpeg_context_t *shjpeg_init(int verbose);

/**
 * \brief Shutdown SH-Mobile JPEG library
 *
 * De-initialize the SH7722 JPEG library. This must be called before
 * closing the process.
 *
 * \retval 0 success
 * \retval -1 failed
 */

void shjpeg_shutdown(shjpeg_context_t *context);

/**
 * \brief Allocate memory for output buffer.
 *
 * Allocate memory. This could be called only after
 * shjpeg_decode_init() is called.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        decoded. Pass the value set by shjpeg_open().
 *
 * \param format [in] desired pixelformat of the decoded image.
 *
 * \param width [in] width of the destination frame buffer.
 *
 * \param height [in] height of the destination frame buffer.
 *
 * \param pitch [in] pitch of the frame buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_init().
 */
void *shjpeg_malloc(shjpeg_context_t	*context,
		    shjpeg_pixelformat	 format,
		    int			 width,
		    int			 height,
		    int			 pitch,
		    size_t		*allocated_size);

/**
 * \brief Release memory of output buffer.
 *
 * Release memory of specified buffer.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        decoded. Pass the value set by shjpeg_open().
 *
 * \param address [in] address of the buffer.
 *
 * \param size [in] size of the buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_init().
 */
void shjpeg_free(shjpeg_context_t	*context,
		 void			*address,
		 size_t			 size);

/**
 * \brief Get frame buffer information.
 *
 * Kernel allocated contiguous memory that could be used to place
 * uncompressed image for decoding and encoding is returned. This
 * physcal contiguous memory will be default memory used by
 * encoder/decoder when SHJPEG_USE_DEFAULT_BUFFER is passed as the physcial
 * memory address to shjpeg_decode() or shjpeg_encode(). As the memory is made
 * accessible from user space only after calling shjpeg_init(), you cannot
 * call this function before calling shjpeg_init().
 *
 * \param context [in] a pointer to the JPEG image context.
 *        Pass the value set by shjpeg_open().
 *
 * \param buffer pointer to the memory mapped physical contiguous
 *	 memory is set.
 *
 * \param size size of the physical contiguous memory region is set.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode(), and shjpeg_encode().
 */

int shjpeg_get_frame_buffer(shjpeg_context_t	 *context,
			    void		**buffer,
			    size_t		 *size );

/**
 * \brief Open JPEG file.
 *
 * Parse the passed JPEG file stream, and returns the context required
 * for decompression. Subsequently after calling this function,
 * shjpeg_decode_run() can be called.
 *
 * Width and height of the JPEG image is returned in the context.
 *
 * \param [in,out] context a pointer to the JPEG image context to be returned.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_run(), shjpeg_decode_shutdown()
 */

int shjpeg_decode_init(shjpeg_context_t *context);

/**
 * \brief Decode JPEG stream.
 *
 * Start decoding JPEG file. This could be called only after
 * shjpeg_decode_init() is called.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        decoded. Pass the value set by shjpeg_open().
 *
 * \param format [in] desired pixelformat of the decoded image.
 *
 * \param virt [in] virttual memory address for decoded image.
 *
 * \param width [in] width of the destination frame buffer.
 *
 * \param height [in] height of the destination frame buffer.
 *
 * \param pitch [in] pitch of the frame buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_init(), and shpeg_get_frame_buffer().
 */
int shjpeg_decode_run(shjpeg_context_t		*context,
		      shjpeg_pixelformat	 format,
		      void          	        *virt,
		      int			 width,
		      int			 height,
		      int                    	 pitch);

/**
 * \brief Close JPEG stream context.
 *
 * You should call this function after decompression of image is
 * completed.
 *
 * \param context [in] a pointer to the JPEG image decompression
 * context.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_open().
 */
void shjpeg_decode_shutdown(shjpeg_context_t *context);

/**
 * \brief Encode the image to JPEG file.
 *
 * Image passed to this function is encoded and written as a file.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        encoded. Pass the value set by shjpeg_open().
 *
 * \param format pixelformat of the image. Only NV12 and NV16 are
 *	  supported.
 *
 * \param virt virtual memory address for input image.
 *
 * \param width width of the input image. 
 *
 * \param height height of the input image.
 *
 * \param pitch pitch of the input image buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_get_frame_buffer().
 */
int shjpeg_encode(shjpeg_context_t	*context,
		  shjpeg_pixelformat	 format,
		  void			*virt,
		  int			 width,
		  int			 height,
		  int			 pitch);

#ifdef __cplusplus
}
#endif

#endif /* !__shjpeg_h__ */
