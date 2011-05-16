/* Copyright (c) 2000 TXT DataKonsult Ab & Monty Program Ab
   Copyright (c) 2009-2011, Monty Program Ab

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.

   THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND ANY
   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
   OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE.
*/
/*  File   : strinstr.c
    Author : Monty & David
    Updated: 1986.12.08
    Defines: strinstr()

    strinstr(src, pat) looks for an instance of pat in src.  pat is not a
    regex(3) pattern, it is a literal string which must be matched exactly.
    The result 0 if the pattern was not found else it is the start char of
    the pattern counted from the beginning of the string, where the first
    char is 1.
*/

#include "strings_def.h"

size_t strinstr(reg1 const char *str,reg4 const char *search)
{
  reg2 const char *i, *j;
  const char *start= str;

 skip:
  while (*str != '\0')
  {
    if (*str++ == *search)
    {
      i= str; j= search+1;
      while (*j)
	if (*i++ != *j++) goto skip;
      return ((size_t) (str - start));
    }
  }
  return (0);
}
