// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * utf.c (13.09.09)
 * exFAT file system implementation library.
 *
 * Free exFAT implementation.
 * Copyright (C) 2010-2018  Andrew Nayenko
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <errno.h>
#include <stdio.h>
#include "exfat_ondisk.h"
#include "exfat_tools.h"

static char *wchar_to_utf8(char *output, wchar_t wc, size_t outsize)
{
	if (wc <= 0x7f) {
		if (outsize < 1)
			return NULL;
		*output++ = (char) wc;
	} else if (wc <= 0x7ff) {
		if (outsize < 2)
			return NULL;
		*output++ = 0xc0 | (wc >> 6);
		*output++ = 0x80 | (wc & 0x3f);
	} else if (wc <= 0xffff) {
		if (outsize < 3)
			return NULL;
		*output++ = 0xe0 | (wc >> 12);
		*output++ = 0x80 | ((wc >> 6) & 0x3f);
		*output++ = 0x80 | (wc & 0x3f);
	} else if (wc <= 0x1fffff) {
		if (outsize < 4)
			return NULL;
		*output++ = 0xf0 | (wc >> 18);
		*output++ = 0x80 | ((wc >> 12) & 0x3f);
		*output++ = 0x80 | ((wc >> 6) & 0x3f);
		*output++ = 0x80 | (wc & 0x3f);
	} else if (wc <= 0x3ffffff) {
		if (outsize < 5)
			return NULL;
		*output++ = 0xf8 | (wc >> 24);
		*output++ = 0x80 | ((wc >> 18) & 0x3f);
		*output++ = 0x80 | ((wc >> 12) & 0x3f);
		*output++ = 0x80 | ((wc >> 6) & 0x3f);
		*output++ = 0x80 | (wc & 0x3f);
	} else if (wc <= 0x7fffffff) {
		if (outsize < 6)
			return NULL;
		*output++ = 0xfc | (wc >> 30);
		*output++ = 0x80 | ((wc >> 24) & 0x3f);
		*output++ = 0x80 | ((wc >> 18) & 0x3f);
		*output++ = 0x80 | ((wc >> 12) & 0x3f);
		*output++ = 0x80 | ((wc >> 6) & 0x3f);
		*output++ = 0x80 | (wc & 0x3f);
	} else
		return NULL;

	return output;
}

static const __le16 *utf16_to_wchar(const __le16 *input, wchar_t *wc,
		size_t insize)
{
	if ((le16_to_cpu(input[0]) & 0xfc00) == 0xd800) {
		if (insize < 2 || (le16_to_cpu(input[1]) & 0xfc00) != 0xdc00)
			return NULL;
		*wc = ((wchar_t) (le16_to_cpu(input[0]) & 0x3ff) << 10);
		*wc |= (le16_to_cpu(input[1]) & 0x3ff);
		*wc += 0x10000;
		return input + 2;
	}

	*wc = le16_to_cpu(*input);
	return input + 1;
}

int utf16_to_utf8(char *output, const __le16 *input, size_t outsize,
		size_t insize)
{
	const __le16 *iptr = input;
	const __le16 *iend = input + insize;
	char *optr = output;
	const char *oend = output + outsize;
	wchar_t wc;

	while (iptr < iend) {
		iptr = utf16_to_wchar(iptr, &wc, iend - iptr);
		if (iptr == NULL) {
			exfat_err("illegal UTF-16 sequence");
			return -EILSEQ;
		}
		optr = wchar_to_utf8(optr, wc, oend - optr);
		if (optr == NULL) {
			exfat_err("name is too long");
			return -ENAMETOOLONG;
		}
		if (wc == 0)
			return 0;
	}
	if (optr >= oend) {
		exfat_err("name is too long");
		return -ENAMETOOLONG;
	}
	*optr = '\0';
	return 0;
}

static const char *utf8_to_wchar(const char *input, wchar_t *wc,
		size_t insize)
{
	if ((input[0] & 0x80) == 0 && insize >= 1) {
		*wc = (wchar_t) input[0];
		return input + 1;
	}
	if ((input[0] & 0xe0) == 0xc0 && insize >= 2) {
		*wc = (((wchar_t) input[0] & 0x1f) << 6) |
		       ((wchar_t) input[1] & 0x3f);
		return input + 2;
	}
	if ((input[0] & 0xf0) == 0xe0 && insize >= 3) {
		*wc = (((wchar_t) input[0] & 0x0f) << 12) |
		      (((wchar_t) input[1] & 0x3f) << 6) |
		       ((wchar_t) input[2] & 0x3f);
		return input + 3;
	}
	if ((input[0] & 0xf8) == 0xf0 && insize >= 4) {
		*wc = (((wchar_t) input[0] & 0x07) << 18) |
		      (((wchar_t) input[1] & 0x3f) << 12) |
		      (((wchar_t) input[2] & 0x3f) << 6) |
		       ((wchar_t) input[3] & 0x3f);
		return input + 4;
	}
	if ((input[0] & 0xfc) == 0xf8 && insize >= 5) {
		*wc = (((wchar_t) input[0] & 0x03) << 24) |
		      (((wchar_t) input[1] & 0x3f) << 18) |
		      (((wchar_t) input[2] & 0x3f) << 12) |
		      (((wchar_t) input[3] & 0x3f) << 6) |
		       ((wchar_t) input[4] & 0x3f);
		return input + 5;
	}
	if ((input[0] & 0xfe) == 0xfc && insize >= 6) {
		*wc = (((wchar_t) input[0] & 0x01) << 30) |
		      (((wchar_t) input[1] & 0x3f) << 24) |
		      (((wchar_t) input[2] & 0x3f) << 18) |
		      (((wchar_t) input[3] & 0x3f) << 12) |
		      (((wchar_t) input[4] & 0x3f) << 6) |
		       ((wchar_t) input[5] & 0x3f);
		return input + 6;
	}
	return NULL;
}

static __le16 *wchar_to_utf16(__le16 *output, wchar_t wc, size_t outsize)
{
	 /* if character is from BMP */
	if (wc <= 0xffff) {
		if (outsize == 0)
			return NULL;
		output[0] = cpu_to_le16(wc);
		return output + 1;
	}
	if (outsize < 2)
		return NULL;
	wc -= 0x10000;
	output[0] = cpu_to_le16(0xd800 | ((wc >> 10) & 0x3ff));
	output[1] = cpu_to_le16(0xdc00 | (wc & 0x3ff));
	return output + 2;
}

int utf8_to_utf16(__le16 *output, const char *input, size_t outsize,
		size_t insize)
{
	const char *iptr = input;
	const char *iend = input + insize;
	__le16 *optr = output;
	const __le16 *oend = output + outsize;
	wchar_t wc;

	while (iptr < iend) {
		iptr = utf8_to_wchar(iptr, &wc, iend - iptr);
		if (iptr == NULL) {
			exfat_err("illegal UTF-8 sequence");
			return -EILSEQ;
		}
		optr = wchar_to_utf16(optr, wc, oend - optr);
		if (optr == NULL) {
			exfat_err("name is too long");
			return -ENAMETOOLONG;
		}
		if (wc == 0)
			break;
	}
	if (optr >= oend) {
		exfat_err("name is too long");
		return -ENAMETOOLONG;
	}
	*optr = cpu_to_le16(0);
	return 0;
}

size_t utf16_length(const __le16 *str)
{
	size_t i = 0;

	while (le16_to_cpu(str[i]))
		i++;
	return i;
}