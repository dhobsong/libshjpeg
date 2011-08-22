#
#
#

AM_CPPFLAGS = $(INCLTDL) -I$(top_srcdir)/include
if MAKE_JPEG_WRAPPER
AM_CPPFLAGS +=	-I$(libjpeg_inc_path)
endif
AM_CFLAGS = -Wall -Werror $(UIOMUX_CFLAGS) $(SHVEU_CFLAGS)

lib_LTLIBRARIES = libshjpeg.la

libshjpeg_la_LIBADD = $(LIBS) $(UIOMUX_LIBS) $(SHVEU_LIBS)

libshjpeg_la_LDFLAGS = -version-info $(LIBSHJPEG_VERSION_INFO)

libshjpeg_la_SOURCES = \
	shjpeg_common.c \
	shjpeg_config.c \
	shjpeg_veu.c \
	shjpeg_jpu.c \
	shjpeg_decode.c \
	shjpeg_encode.c \
	shjpeg_softhelper.c \
	shjpeg_softhelper.h \
	shjpeg_internal.h \
	shjpeg_utils.h \
	shjpeg_regs.h \
	shjpeg_veu.h \
	shjpeg_jpu.h

if MAKE_JPEG_WRAPPER
libshjpeg_la_SOURCES += \
	libjpeg_wrap/init.c \
	libjpeg_wrap/override.c \
	libjpeg_wrap/override.h \
	libjpeg_wrap/jpeg_io.c \
	libjpeg_wrap/jpeg_io.h \
	libjpeg_wrap/libmessage.h \
	libjpeg_wrap/hooks/hooks.c \
	libjpeg_wrap/libjpeg_api/jpegapi.c
endif
#	libjpeg_wrap/libjpeg_api/jerror.c \
#	libjpeg_wrap/libjpeg_api/jconfig.c \
#	libjpeg_wrap/libjpeg_api/jmorecfg.c
