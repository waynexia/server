/* Copyright (c) 2005 PrimeBase Technologies GmbH
 *
 * PrimeBase XT
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * 2006-03-22	Paul McCullagh
 *
 * H&G2JCtL
 *
 * This header file should be included in every source, before all other
 * headers.
 *
 * In particular: BEFORE THE SYSTEM HEADERS
 */

#ifndef __xt_config_h__
#define __xt_config_h__

#define MYSQL_SERVER		1

#ifdef DRIZZLED
#include "drizzled/global.h"
const int max_connections = 500;
#else
#include <mysql_version.h>
#include "my_global.h"
#endif

/*
 * This enables everything that GNU can do. The macro is actually
 * recommended for new programs.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * Make sure we use the thread safe version of the library.
 */
#define _THREAD_SAFE

/*
 * This causes things to be defined like stuff in inttypes.h
 * which is used in printf()
 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

/*
 * This define is not required by Linux because the _GNU_SOURCE
 * definition includes POSIX complience. But I need it for
 * Mac OS X.
 */
//#define _POSIX_C_SOURCE	2
//#define _ANSI_SOURCE

#ifdef __APPLE__
#define XT_MAC
#endif

#if defined(MSDOS) || defined(__WIN__)
#define XT_WIN
#endif

#ifdef XT_WIN
#ifdef _DEBUG
#define DEBUG
#endif // _DEBUG
#else
#define XT_STREAMING
#endif

#ifdef __FreeBSD__
#define XT_FREEBSD
#endif

#ifdef __NetBSD__
#define XT_NETBSD
#endif

#ifdef __sun
#define XT_SOLARIS
#endif

#endif
