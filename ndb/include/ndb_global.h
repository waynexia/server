
#ifndef NDBGLOBAL_H
#define NDBGLOBAL_H

#include <my_global.h>
#include <m_string.h>
#include <m_ctype.h>
#include <ndb_types.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>

#ifndef NDB_MACOSX
#include <sys/mman.h>
#endif

#ifdef NDB_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define DIR_SEPARATOR "\\"
#define PATH_MAX 256

#pragma warning(disable: 4503 4786)
#else

#define DIR_SEPARATOR "/"

#endif

#ifdef NDB_VC98
#define STATIC_CONST(x) enum { x }
#else
#define STATIC_CONST(x) static const Uint32 x
#endif

#ifdef  __cplusplus
#include <new>
#endif

#ifdef  __cplusplus
extern "C" {
#endif
	
#include <assert.h>

#ifndef HAVE_STRDUP
extern char * strdup(const char *s);
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy (char *dst, const char *src, size_t dst_sz);
#endif

#ifndef HAVE_STRLCAT
extern size_t strlcat (char *dst, const char *src, size_t dst_sz);
#endif

#ifndef HAVE_STRCASECMP
extern int strcasecmp(const char *s1, const char *s2);
extern int strncasecmp(const char *s1, const char *s2, size_t n);
#endif

#ifdef  __cplusplus
}
#endif

#endif
