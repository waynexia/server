AC_DEFUN([MYSQL_CHECK_READLINE_DECLARES_HIST_ENTRY], [
    AC_CACHE_CHECK([HIST_ENTRY is declared in readline/readline.h], mysql_cv_hist_entry_declared,
	AC_TRY_COMPILE(
	    [
		#include "stdio.h"
		#include "readline/readline.h"
	    ],
	    [ 
		HIST_ENTRY entry;
	    ],
	    [
		mysql_cv_hist_entry_declared=yes
		AC_DEFINE_UNQUOTED(HAVE_HIST_ENTRY, [1],
                                   [HIST_ENTRY is defined in the outer libeditreadline])
	    ],
	    [mysql_cv_libedit_interface=no]
        )
    )
])

AC_DEFUN([MYSQL_CHECK_LIBEDIT_INTERFACE], [
    AC_CACHE_CHECK([libedit variant of rl_completion_entry_function], mysql_cv_libedit_interface,
	AC_TRY_COMPILE(
	    [
		#include "stdio.h"
		#include "readline/readline.h"
	    ],
	    [ 
		char res= *(*rl_completion_entry_function)(0,0);
		completion_matches(0,0);
	    ],
	    [
		mysql_cv_libedit_interface=yes
                AC_DEFINE_UNQUOTED([USE_LIBEDIT_INTERFACE], [1],
                                   [used libedit interface (can we dereference result of rl_completion_entry_function)])
	    ],
	    [mysql_cv_libedit_interface=no]
        )
    )
])

AC_DEFUN([MYSQL_CHECK_NEW_RL_INTERFACE], [
    AC_CACHE_CHECK([for system libreadline], mysql_cv_new_rl_interface,
	AC_COMPILE_IFELSE(
	    [AC_LANG_SOURCE([
		#include "stdio.h"
		#include "readline/readline.h"
		rl_completion_func_t *func1= (rl_completion_func_t*)0;
		rl_compentry_func_t *func2= (rl_compentry_func_t*)0;
	    ])],
	    [
                AC_PREPROC_IFELSE(
                    [AC_LANG_SOURCE([
                        #include "stdio.h"
                        #include "readline/readline.h"
                        #if RL_VERSION_MAJOR > 5
                        #error
                        #endif
                    ])], [ rl_v5=yes ], [ rl_v5=no ],
                )
                if [test "$rl_v5" = "yes"]
                then
                  mysql_cv_new_rl_interface=yes
                else
                  if [test "$enable_distribution" = "yes"]
                  then
                    mysql_cv_new_rl_interface=no
                  else
                    mysql_cv_new_rl_interface=yes
                    enable_distribution=warn
                  fi
                fi
	    ],
	    [mysql_cv_new_rl_interface=no]
        )
        if [test "$mysql_cv_new_rl_interface" = yes]
        then
          AC_DEFINE_UNQUOTED([USE_NEW_READLINE_INTERFACE], [1],
                             [used new readline interface (are rl_completion_func_t and rl_compentry_func_t defined)])
        fi
    )
])

dnl
dnl check for availability of multibyte characters and functions
dnl (Based on BASH_CHECK_MULTIBYTE in aclocal.m4 of readline-5.0)
dnl
AC_DEFUN([MYSQL_CHECK_MULTIBYTE],
[
AC_CHECK_HEADERS(wctype.h)
AC_CHECK_HEADERS(wchar.h)
AC_CHECK_HEADERS(langinfo.h)

AC_CHECK_FUNC(mbrlen, AC_DEFINE(HAVE_MBRLEN,[],[Define if you have mbrlen]))
AC_CHECK_FUNC(mbscmp, AC_DEFINE(HAVE_MBSCMP,[],[Define if you have mbscmp]))
AC_CHECK_FUNC(mbsrtowcs, AC_DEFINE(HAVE_MBSRTOWCS,[],[Define if you have mbsrtowcs]))

AC_CHECK_FUNC(wcrtomb, AC_DEFINE(HAVE_WCRTOMB,[],[Define if you have wcrtomb]))
AC_CHECK_FUNC(mbrtowc, AC_DEFINE(HAVE_MBRTOWC,[],[Define if you have mbrtowc]))
AC_CHECK_FUNC(wcscoll, AC_DEFINE(HAVE_WCSCOLL,[],[Define if you have wcscoll]))
AC_CHECK_FUNC(wcsdup, AC_DEFINE(HAVE_WCSDUP,[],[Define if you have wcsdup]))
AC_CHECK_FUNC(wcwidth, AC_DEFINE(HAVE_WCWIDTH,[],[Define if you have wcwidth]))
AC_CHECK_FUNC(wctype, AC_DEFINE(HAVE_WCTYPE,[],[Define if you have wctype]))

AC_CACHE_CHECK([for mbstate_t], mysql_cv_have_mbstate_t,
[AC_TRY_COMPILE([
#include <wchar.h>], [
  mbstate_t ps;
  mbstate_t *psp;
  psp = (mbstate_t *)0;
], mysql_cv_have_mbstate_t=yes,  mysql_cv_have_mbstate_t=no)])
if test $mysql_cv_have_mbstate_t = yes; then
        AC_DEFINE([HAVE_MBSTATE_T],[],[Define if mysql_cv_have_mbstate_t=yes])
fi

AC_CHECK_FUNCS(iswlower iswupper towlower towupper iswctype)

AC_CACHE_CHECK([for nl_langinfo and CODESET], mysql_cv_langinfo_codeset,
[AC_TRY_LINK(
[#include <langinfo.h>],
[char* cs = nl_langinfo(CODESET);],
mysql_cv_langinfo_codeset=yes, mysql_cv_langinfo_codeset=no)])
if test $mysql_cv_langinfo_codeset = yes; then
  AC_DEFINE([HAVE_LANGINFO_CODESET],[],[Define if mysql_cv_langinfo_codeset=yes])
fi

dnl check for wchar_t in <wchar.h>
AC_CACHE_CHECK([for wchar_t in wchar.h], bash_cv_type_wchar_t,
[AC_TRY_COMPILE(
[#include <wchar.h>
],
[
        wchar_t foo;
        foo = 0;
], bash_cv_type_wchar_t=yes, bash_cv_type_wchar_t=no)])
if test $bash_cv_type_wchar_t = yes; then
        AC_DEFINE(HAVE_WCHAR_T, 1, [systems should define this type here])
fi

dnl check for wctype_t in <wctype.h>
AC_CACHE_CHECK([for wctype_t in wctype.h], bash_cv_type_wctype_t,
[AC_TRY_COMPILE(
[#include <wctype.h>],
[
        wctype_t foo;
        foo = 0;
], bash_cv_type_wctype_t=yes, bash_cv_type_wctype_t=no)])
if test $bash_cv_type_wctype_t = yes; then
        AC_DEFINE(HAVE_WCTYPE_T, 1, [systems should define this type here])
fi

dnl check for wint_t in <wctype.h>
AC_CACHE_CHECK([for wint_t in wctype.h], bash_cv_type_wint_t,
[AC_TRY_COMPILE(
[#include <wctype.h>],
[
        wint_t foo;
        foo = 0;
], bash_cv_type_wint_t=yes, bash_cv_type_wint_t=no)])
if test $bash_cv_type_wint_t = yes; then
        AC_DEFINE(HAVE_WINT_T, 1, [systems should define this type here])
fi

])
