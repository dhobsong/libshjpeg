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

#ifndef __shjpeg_types_h__
#define __shjpeg_types_h__

#include <stdbool.h>
#include <stdint.h>

#include <stdio.h>	/* required by jpeglib.h */
#include <jpeglib.h>
#ifdef HAVE_STDLIB_H   
#  undef HAVE_STDLIB_H //conflicts when using libjpeg version 6x
#endif
/**
 * \file shjpeg_types.h
 *
 * A library for controlling SH-Mobile JPEG hardware codec.
 */

//! Use default physically contigous buffer
#define SHJPEG_USE_DEFAULT_BUFFER	0xffffffffUL

/**
 * \brief Encodes pixelformat
 *
 * A macro to encode pixelformat. Internal purpose only.
 *
 * \param id  unique identifier for the pixelformat
 * \param pitch a pitch multiplier
 * \param bpp a number of bits per pixel
 * \param mul  a multiplier for planes calculation (units of 1/2 plane)
 */

#define SHJPEG_PIXELFORMAT(id, pitch, bpp, mul)		\
    ((((id) & 0xffff) << 24) | (((pitch) & 0xff) << 16) | (((bpp) &   0xff) <<  8) | \
     ((mul)  &   0xff))

/**
 * \brief Decodes pixelformat - pitch multiplier
 * 
 * Decodes multiplier used to calculate line pitch. Multiply width
 * by this value gives line pitch.
 *
 * \param format the pixelformat
 */
#define SHJPEG_PF_PITCH_MULTIPLY(format) \
    (((format) & 0x00ff0000) >> 16)

/**
 * \brief Decodes pixelformat - bits-per-pixel
 * 
 * Decodes bits per pixel.
 *
 * \param format the pixelformat
 */
#define SHJPEG_PF_BPP(format) \
    (((format) & 0x0000ff00) >> 8)

/**
 * \brief Decodes pixelformat - plane multiplier
 * 
 * Decodes multiplier used to calculate the total height of the plane.
 * SHJPEG_PF_PLANE_MULTIPLY(pf, height) x SHJPEG_PF_PITCH_MULTIPLY(pf) x width x SHJPEG_PF_BPP(pf) / 8
 * gives you the total number of bytes required to hold the plane.
 *
 * \param format the pixelformat.
 * \param height the height of the plane.
 */
#define SHJPEG_PF_PLANE_MULTIPLY(format, height) \
    (((format) & 0xff) * (height) / 2)

/**
 * \brief Pixel format
 *
 * Supported pixel formats.
 */

typedef enum {
    SHJPEG_PF_NONE = 0,                                         /*!< No format - Error placeholder*/
    SHJPEG_PF_RGB16 = SHJPEG_PIXELFORMAT(1, 2, 16, 2),		/*!< RGB16 pixel format. */
    SHJPEG_PF_RGB24 = SHJPEG_PIXELFORMAT(2, 3, 24, 2),		/*!< RGB24 pixel format. */
    SHJPEG_PF_RGB32 = SHJPEG_PIXELFORMAT(3, 4, 32, 2),		/*!< RGB32 pixel format. */
    SHJPEG_PF_NV12  = SHJPEG_PIXELFORMAT(4, 1, 12, 3),		/*!< NV12 pixel format. */
    SHJPEG_PF_NV16  = SHJPEG_PIXELFORMAT(5, 1, 16, 4),		/*!< NV16 pixel format. */
    SHJPEG_PF_GRAYSCALE = SHJPEG_PIXELFORMAT(6, 1, 12, 3),	/*!< Y8 pixel format. */
    SHJPEG_PF_YCbCr = SHJPEG_PIXELFORMAT(7, 3, 24, 2),	/*!< YUV 4:4:4 pixel format */
} shjpeg_pixelformat;

/**
 * \brief a type definition for shjpeg_context_struct.
 */

typedef struct shjpeg_stream_ops_struct shjpeg_sops;

/**
 * \brief JPEG Stream Operations
 *
 * Defines a set of operations to read/write data from/to JPEG stream.
 */

struct shjpeg_stream_ops_struct {
    //! A method to init JPEG stream.
    /*!
      \param [in] private user data.
      \return should return 0 if success, otherwise non-zero value.
     */
    int (*init)(void *priv_user_data);

    //! A method to read JPEG data.
    /*!
      \param [in] private user data.
      \param [in,out] nbytes number of bytes to read, and returns bytes actually read.
      \param [in] dataptr a pointer to the buffer to be filled.
      \return should return 0 if success, otherwise non-zero value.
     */
    int	(*read)(void *priv_user_data, size_t *nbytes, void *dataptr);

    //! A method to write JPEG data.
    /*!
      \param [in] private user data.
      \param [in,out] nbytes number of bytes to write, and returns bytes actually written.
      \param [in] dataptr a pointer to the buffer to be writen.
      \return should return 0 if success, otherwise non-zero value.
     */
    int	(*write)(void *priv_user_data, size_t *nbytes, void *dataptr);

    //! A method to finalize JPEG data.
    /*!
      \param [in] private user data.
     */
    void (*finalize)(void *priv_user_data);
};

/**
 * \brief a type definition for shjpeg_context_struct.
 */

typedef struct shjpeg_context_struct shjpeg_context_t;

/**
 * \brief JPEG Compressoin/Decompression Context
 * 
 * When the file to be decoed is opened, the details of the JPEG files
 * is stored in this structure.
 */

/**
 * \deprecated
 * The structure was only ever used by the libjpeg wrapper.
 * The libjpeg wrapper now contains is own local version of this information,
 * making this strcuture unnecessary.
 *
 * \brief JPU data buffer information (deprecated)
 *
 * This structure stores the physical and virtual address
 * of the buffer that is used for JPEG encode/decode on the
 * JPU.
 */

struct shjpeg_buffer {
    void                *buffer;
    unsigned long       phys;
    size_t              size;
};

struct shjpeg_context_struct {
    //! Width of the current image.
    int		width;

    //! Height of the current image.
    int		height;

    //! True if the image is YUV420 (valid during decode only).
    bool	mode420;

    //! True if the image is YUV444 (valid during decode only).
    bool	mode444;

    //! Stream operations
    shjpeg_sops	*sops;

    //! User defined private data
    void	*priv_data;

    //! libshjpeg private data
    void	*internal_data;

    //! Set to non-zero, if fallback to libjpeg is NOT desired.
    int		 libjpeg_disabled;

    //! libshjpeg set this to non-zero, if decoding falled back to libjpeg.
    int		 libjpeg_used;

    //! libshjpeg private data - verbose flag
    int		 verbose;
    //! libshjpeg private data - libjpeg compress context
    struct jpeg_compress_struct    jpeg_comp;
    //! libshjpeg private data - libjpeg compress context
    struct jpeg_decompress_struct  jpeg_decomp;

//New for 1.3
    //! Pitch of the current image.
    int         pitch;
    //! Format of the input image for encoding
    shjpeg_pixelformat   format;
    //! the working buffer for JPEG encode/decode (deprecated, see shjpeg_buffer)
    struct shjpeg_buffer buffer;
};

#endif /* !__shjpeg_types_h__ */
