/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef INNODB_PRIV_INCLUDED
#define INNODB_PRIV_INCLUDED

/** @file Declaring server-internal functions that are used by InnoDB. */

#include <sql_priv.h>

class THD;

uint filename_to_tablename(const char *from, char *to, uint to_length);
int get_quote_char_for_identifier(THD *thd, const char *name, uint length);
bool check_global_access(THD *thd, ulong want_access);

uint strconvert(CHARSET_INFO *from_cs, const char *from,
                CHARSET_INFO *to_cs, char *to, uint to_length,
                uint *errors);
void sql_print_error(const char *format, ...);


#endif /* INNODB_PRIV_INCLUDED */
