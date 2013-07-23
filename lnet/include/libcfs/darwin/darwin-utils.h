/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.lustre.org/lustre/docs/GPLv2.pdf
 *
 * Please contact Xyratex Technology, Ltd., Langstone Road, Havant, Hampshire.
 * PO9 1SA, U.K. or visit www.xyratex.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2013, Xyratex Technology, Ltd . All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Some portions of Lustre® software are subject to copyrights help by Intel Corp.
 * Copyright (c) 2011-2013 Intel Corporation, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre® and the Lustre logo are registered trademarks of
 * Xyratex Technology, Ltd  in the United States and/or other countries.
 */

#ifndef __LIBCFS_DARWIN_UTILS_H__
#define __LIBCFS_DARWIN_UTILS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#include <sys/random.h> 

#ifdef __KERNEL__
inline int isspace(char c);
char *strpbrk(const char *cs, const char *ct);
char * strsep(char **s, const char *ct);
size_t strnlen(const char * s, size_t count);
char * strstr(const char *in, const char *str);
char * strrchr(const char *p, int ch);
char * ul2dstr(unsigned long address, char *buf, int len);

#define simple_strtol(a1, a2, a3)               strtol(a1, a2, a3)
#define simple_strtoul(a1, a2, a3)              strtoul(a1, a2, a3)
#define simple_strtoll(a1, a2, a3)              strtoq(a1, a2, a3)
#define simple_strtoull(a1, a2, a3)             strtouq(a1, a2, a3)

#define test_bit(i, a)                          isset(a, i)
#define set_bit(i, a)                           setbit(a, i)
#define clear_bit(i, a)                         clrbit(a, i)

#define get_random_bytes(buf, len)              read_random(buf, len)

#endif  /* __KERNEL__ */

#ifndef min_t
#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif
#ifndef max_t
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif

#define do_div(n,base)                          \
	({                                      \
	 __u64 __n = (n);                       \
	 __u32 __base = (base);                 \
	 __u32 __mod;                           \
						\
	 __mod = __n % __base;                  \
	 n = __n / __base;                      \
	 __mod;                                 \
	 })

#define NIPQUAD(addr)			\
	((unsigned char *)&addr)[0],	\
	((unsigned char *)&addr)[1],	\
	((unsigned char *)&addr)[2],	\
	((unsigned char *)&addr)[3]

#define HIPQUAD NIPQUAD

#ifndef LIST_CIRCLE
#define LIST_CIRCLE(elm, field)                                 \
	do {                                                    \
		(elm)->field.le_prev = &(elm)->field.le_next;   \
	} while (0)
#endif

#endif /* __XNU_UTILS_H__ */
