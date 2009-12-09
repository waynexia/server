/* -*- C++ -*- */
/* Copyright (C) 2002 MySQL AB

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

#ifndef _SP_H_
#define _SP_H_

/* Tells what SP_DEFAULT_ACCESS should be mapped to */
#define SP_DEFAULT_ACCESS_MAPPING SP_CONTAINS_SQL

// Return codes from sp_create_*, sp_drop_*, and sp_show_*:
#define SP_OK                 0
#define SP_KEY_NOT_FOUND     -1
#define SP_OPEN_TABLE_FAILED -2
#define SP_WRITE_ROW_FAILED  -3
#define SP_DELETE_ROW_FAILED -4
#define SP_GET_FIELD_FAILED  -5
#define SP_PARSE_ERROR       -6
#define SP_INTERNAL_ERROR    -7
#define SP_NO_DB_ERROR       -8
#define SP_BAD_IDENTIFIER    -9
#define SP_BODY_TOO_LONG    -10
#define SP_FLD_STORE_FAILED -11

/* Drop all routines in database 'db' */
int
sp_drop_db_routines(THD *thd, char *db);

sp_head *
sp_find_routine(THD *thd, int type, sp_name *name,
                sp_cache **cp, bool cache_only);

int
sp_cache_routine(THD *thd, int type, sp_name *name, sp_head **sp);

bool
sp_exist_routines(THD *thd, TABLE_LIST *procs, bool any);

int
sp_routine_exists_in_table(THD *thd, int type, sp_name *name);

bool
sp_show_create_routine(THD *thd, int type, sp_name *name);

int
sp_create_routine(THD *thd, int type, sp_head *sp);

int
sp_update_routine(THD *thd, int type, sp_name *name, st_sp_chistics *chistics);

int
sp_drop_routine(THD *thd, int type, sp_name *name);


/**
  Structure that represents element in the set of stored routines
  used by statement or routine.
*/

class Sroutine_hash_entry
{
public:
  /**
    Metadata lock request for routine.
    MDL_key in this request is also used as a key for set.
  */
  MDL_request mdl_request;
  /**
    Next element in list linking all routines in set. See also comments
    for LEX::sroutine/sroutine_list and sp_head::m_sroutines.
  */
  Sroutine_hash_entry *next;
  /**
    Uppermost view which directly or indirectly uses this routine.
    0 if routine is not used in view. Note that it also can be 0 if
    statement uses routine both via view and directly.
  */
  TABLE_LIST *belong_to_view;
};


/*
  Procedures for handling sets of stored routines used by statement or routine.
*/
void sp_add_used_routine(Query_tables_list *prelocking_ctx, Query_arena *arena,
                         sp_name *rt, char rt_type);
bool sp_add_used_routine(Query_tables_list *prelocking_ctx, Query_arena *arena,
                         const MDL_key *key, TABLE_LIST *belong_to_view);
void sp_remove_not_own_routines(Query_tables_list *prelocking_ctx);
void sp_update_sp_used_routines(HASH *dst, HASH *src);
void sp_update_stmt_used_routines(THD *thd, Query_tables_list *prelocking_ctx,
                                  HASH *src, TABLE_LIST *belong_to_view);
void sp_update_stmt_used_routines(THD *thd, Query_tables_list *prelocking_ctx,
                                  SQL_LIST *src, TABLE_LIST *belong_to_view);

extern "C" uchar* sp_sroutine_key(const uchar *ptr, size_t *plen,
                                  my_bool first);

/*
  Routines which allow open/lock and close mysql.proc table even when
  we already have some tables open and locked.
*/
TABLE *open_proc_table_for_read(THD *thd, Open_tables_state *backup);

#endif /* _SP_H_ */
