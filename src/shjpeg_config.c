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

#include "shjpeg_regs.h"

/*
 * For Debug Purpose
 */

const char *jpu_reg_str[] = {
	"JCMOD",		// 0x00
	"JCCMD",
	"JCSTS",
	"JCQTN",

	"JCHTN",		// 0x10
	"JCDRIU",
	"JCDRID",
	"JCVSZU",

	"JCVSZD",		// 0x20
	"JCHSZU",
	"JCHSZD",
	"JCDTCU",

	"JCDTCM",		// 0x30
	"JCDTCD",
	"JINTE",
	"JINTS",

	"JCDERR",		// 0x40
	"JCRST",
	"-",
	"-",

	"-", "-", "-", "-",	// 0x50

	"JIFCNT",		// 0x60
	"-", "-", "-",

	"JIFECNT",		// 0x70
	"JIFESYA1",
	"JIFESCA1",
	"JIFESYA2",

	"JIFESCA2",		// 0x80
	"JIFESMW",
	"JIFESVSZ",
	"JIFESHSZ",

	"JIFEDA1",		// 0x90
	"JIFEDA2",
	"JIFEDRSZ", "-",

	"JIFDCNT",		// 0xa0
	"JIFDSA1",
	"JIFDSA2",
	"JIFDDRSZ",

	"JIFDDMW",		// 0xb0
	"JIFDDVSZ",
	"JIFDDHSZ",
	"JIFDDYA1",

	"JIFDDCA1",		// 0xc0
	"JIFDDYA2",
	"JIFDDCA2", "-",
};
