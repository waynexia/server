/***************************************************************************
Version control for database, common definitions, and include files

(c) 1994 - 2000 Innobase Oy

Created 1/20/1994 Heikki Tuuri
****************************************************************************/

#ifndef univ_i
#define univ_i

#if (defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)) && !defined(MYSQL_SERVER) && !defined(__WIN__)
#undef __WIN__
#define __WIN__

#include <windows.h>

#if !defined(WIN64) && !defined(_WIN64)
#define UNIV_CAN_USE_X86_ASSEMBLER
#endif

#ifdef _NT_
#define __NT__
#endif

#else
/* The defines used with MySQL */

/* Include two header files from MySQL to make the Unix flavor used
in compiling more Posix-compatible. These headers also define __WIN__
if we are compiling on Windows. */

#include <my_global.h>
#include <my_pthread.h>

/* Include <sys/stat.h> to get S_I... macros defined for os0file.c */
#include <sys/stat.h>

#undef PACKAGE
#undef VERSION

/* Include the header file generated by GNU autoconf */
#include "../ib_config.h"

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

/* When compiling for Itanium IA64, undefine the flag below to prevent use
of the 32-bit x86 assembler in mutex operations. */

#if defined(__WIN__) && !defined(WIN64) && !defined(_WIN64)
#define UNIV_CAN_USE_X86_ASSEMBLER
#endif

/* We only try to do explicit inlining of functions with gcc and
Microsoft Visual C++ */

#if !defined(__GNUC__) && !defined(__WIN__)
#undef  UNIV_MUST_NOT_INLINE			/* Remove compiler warning */
#define UNIV_MUST_NOT_INLINE
#endif

#ifdef HAVE_PREAD
#define HAVE_PWRITE
#endif

#endif /* #if (defined(WIN32) || ... */

/*			DEBUG VERSION CONTROL
			===================== */

/* The following flag will make InnoDB to initialize
all memory it allocates to zero. It hides Purify
warnings about reading unallocated memory unless
memory is read outside the allocated blocks. */
/*
#define UNIV_INIT_MEM_TO_ZERO
*/

/* Make a non-inline debug version */

#ifdef DBUG_ON
# define UNIV_DEBUG
#endif /* DBUG_ON */
/*
#define UNIV_MEM_DEBUG
#define UNIV_IBUF_DEBUG
#define UNIV_SYNC_DEBUG
#define UNIV_SEARCH_DEBUG
#define UNIV_SYNC_PERF_STAT
#define UNIV_SEARCH_PERF_STAT
*/
#define UNIV_LIGHT_MEM_DEBUG

#define YYDEBUG			1

#ifdef HAVE_purify
/* The following sets all new allocated memory to zero before use:
this can be used to eliminate unnecessary Purify warnings, but note that
it also masks many bugs Purify could detect. For detailed Purify analysis it
is best to remove the define below and look through the warnings one
by one. */
#define UNIV_SET_MEM_TO_ZERO
#endif

/*
#define UNIV_SQL_DEBUG
#define UNIV_LOG_DEBUG
*/
			/* the above option prevents forcing of log to disk
			at a buffer page write: it should be tested with this
			option off; also some ibuf tests are suppressed */
/*
#define UNIV_BASIC_LOG_DEBUG
*/
			/* the above option enables basic recovery debugging:
			new allocated file pages are reset */

#if (!defined(UNIV_DEBUG) && !defined(INSIDE_HA_INNOBASE_CC) && !defined(UNIV_MUST_NOT_INLINE))
/* Definition for inline version */

#ifdef __WIN__
#define UNIV_INLINE  	__inline
#else
/* config.h contains the right def for 'inline' for the current compiler */
#if (__GNUC__ == 2)
#define UNIV_INLINE  extern inline
#else
/* extern inline doesn't work with gcc 3.0.2 */
#define UNIV_INLINE static inline
#endif
#endif

#else
/* If we want to compile a noninlined version we use the following macro
definitions: */

#define UNIV_NONINL
#define UNIV_INLINE

#endif	/* UNIV_DEBUG */

#ifdef _WIN32
#define UNIV_WORD_SIZE		4
#elif defined(_WIN64)
#define UNIV_WORD_SIZE		8
#else
/* MySQL config.h generated by GNU autoconf will define SIZEOF_LONG in Posix */
#define UNIV_WORD_SIZE		SIZEOF_LONG
#endif

/* The following alignment is used in memory allocations in memory heap
management to ensure correct alignment for doubles etc. */
#define UNIV_MEM_ALIGNMENT      8

/* The following alignment is used in aligning lints etc. */
#define UNIV_WORD_ALIGNMENT	UNIV_WORD_SIZE

/*
			DATABASE VERSION CONTROL
			========================
*/

/* The universal page size of the database */
#define UNIV_PAGE_SIZE          (2 * 8192) /* NOTE! Currently, this has to be a
					power of 2 */
/* The 2-logarithm of UNIV_PAGE_SIZE: */
#define UNIV_PAGE_SIZE_SHIFT	14					

/* Maximum number of parallel threads in a parallelized operation */
#define UNIV_MAX_PARALLELISM	32

/*
			UNIVERSAL TYPE DEFINITIONS
			==========================
*/

/* Note that inside MySQL 'byte' is defined as char on Linux! */
#define byte			unsigned char

/* Another basic type we use is unsigned long integer which should be equal to
the word size of the machine, that is on a 32-bit platform 32 bits, and on a
64-bit platform 64 bits. We also give the printf format for the type as a
macro PRULINT. */

#ifdef _WIN64
typedef unsigned __int64	ulint;
#define ULINTPF			"%I64u"
typedef __int64			lint;
#else
typedef unsigned long int	ulint;
#define ULINTPF			"%lu"
typedef long int		lint;
#endif

#ifdef __WIN__
typedef __int64			ib_longlong;
#else
typedef longlong		ib_longlong;
#endif

#ifndef __WIN__
#if SIZEOF_LONG != SIZEOF_VOIDP
#error "Error: InnoDB's ulint must be of the same size as void*"
#endif
#endif

/* The following type should be at least a 64-bit floating point number */
typedef double			utfloat;

/* The 'undefined' value for a ulint */
#define ULINT_UNDEFINED		((ulint)(-1))

/* The undefined 32-bit unsigned integer */
#define	ULINT32_UNDEFINED	0xFFFFFFFF

/* Maximum value for a ulint */
#define ULINT_MAX		((ulint)(-2))

/* This 'ibool' type is used within Innobase. Remember that different included
headers may define 'bool' differently. Do not assume that 'bool' is a ulint! */
#define ibool			ulint

#ifndef TRUE

#define TRUE    1
#define FALSE   0

#endif

/* The following number as the length of a logical field means that the field
has the SQL NULL as its value. NOTE that because we assume that the length
of a field is a 32-bit integer when we store it, for example, to an undo log
on disk, we must have also this number fit in 32 bits, also in 64-bit
computers! */

#define UNIV_SQL_NULL 	ULINT32_UNDEFINED

/* Lengths which are not UNIV_SQL_NULL, but bigger than the following
number indicate that a field contains a reference to an externally
stored part of the field in the tablespace. The length field then
contains the sum of the following flag and the locally stored len. */

#define UNIV_EXTERN_STORAGE_FIELD (UNIV_SQL_NULL - UNIV_PAGE_SIZE)

#include <stdio.h>
#include "ut0dbg.h"
#include "ut0ut.h"
#include "db0err.h"

#endif
