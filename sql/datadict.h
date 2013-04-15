#ifndef DATADICT_INCLUDED
#define DATADICT_INCLUDED
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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "handler.h"

/*
  Data dictionary API.
*/

enum frm_type_enum
{
  FRMTYPE_ERROR= 0,
  FRMTYPE_TABLE,
  FRMTYPE_VIEW
};

/*
  Take extra care when using dd_frm_type() - it only checks the .frm file,
  and it won't work for any engine that supports discovery.

  Prefer to use ha_table_exists() instead.
  To check whether it's an frm of a view, use dd_frm_is_view().
*/
frm_type_enum dd_frm_type(THD *thd, char *path, enum legacy_db_type *dbt);

static inline bool dd_frm_is_view(THD *thd, char *path)
{
  enum legacy_db_type not_used;
  return dd_frm_type(thd, path, &not_used) == FRMTYPE_VIEW;
}

bool dd_recreate_table(THD *thd, const char *db, const char *table_name,
                       const char *path = NULL);

#endif // DATADICT_INCLUDED
