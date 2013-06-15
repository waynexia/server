/* Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef SQL_TABLE_INCLUDED
#define SQL_TABLE_INCLUDED

#include "my_global.h"                          /* my_bool */
#include "my_sys.h"                             // pthread_mutex_t

class Alter_info;
class Alter_table_ctx;
class Create_field;
struct TABLE_LIST;
class THD;
struct TABLE;
struct handlerton;
typedef struct st_ha_check_opt HA_CHECK_OPT;
typedef struct st_ha_create_information HA_CREATE_INFO;
typedef struct st_key KEY;
typedef struct st_key_cache KEY_CACHE;
typedef struct st_lock_param_type ALTER_PARTITION_PARAM_TYPE;
typedef struct st_order ORDER;

enum ddl_log_entry_code
{
  /*
    DDL_LOG_EXECUTE_CODE:
      This is a code that indicates that this is a log entry to
      be executed, from this entry a linked list of log entries
      can be found and executed.
    DDL_LOG_ENTRY_CODE:
      An entry to be executed in a linked list from an execute log
      entry.
    DDL_IGNORE_LOG_ENTRY_CODE:
      An entry that is to be ignored
  */
  DDL_LOG_EXECUTE_CODE = 'e',
  DDL_LOG_ENTRY_CODE = 'l',
  DDL_IGNORE_LOG_ENTRY_CODE = 'i'
};

enum ddl_log_action_code
{
  /*
    The type of action that a DDL_LOG_ENTRY_CODE entry is to
    perform.
    DDL_LOG_DELETE_ACTION:
      Delete an entity
    DDL_LOG_RENAME_ACTION:
      Rename an entity
    DDL_LOG_REPLACE_ACTION:
      Rename an entity after removing the previous entry with the
      new name, that is replace this entry.
    DDL_LOG_EXCHANGE_ACTION:
      Exchange two entities by renaming them a -> tmp, b -> a, tmp -> b.
  */
  DDL_LOG_DELETE_ACTION = 'd',
  DDL_LOG_RENAME_ACTION = 'r',
  DDL_LOG_REPLACE_ACTION = 's',
  DDL_LOG_EXCHANGE_ACTION = 'e'
};

enum enum_ddl_log_exchange_phase {
  EXCH_PHASE_NAME_TO_TEMP= 0,
  EXCH_PHASE_FROM_TO_NAME= 1,
  EXCH_PHASE_TEMP_TO_FROM= 2
};


typedef struct st_ddl_log_entry
{
  const char *name;
  const char *from_name;
  const char *handler_name;
  const char *tmp_name;
  uint next_entry;
  uint entry_pos;
  enum ddl_log_entry_code entry_type;
  enum ddl_log_action_code action_type;
  /*
    Most actions have only one phase. REPLACE does however have two
    phases. The first phase removes the file with the new name if
    there was one there before and the second phase renames the
    old name to the new name.
  */
  char phase;
} DDL_LOG_ENTRY;

typedef struct st_ddl_log_memory_entry
{
  uint entry_pos;
  struct st_ddl_log_memory_entry *next_log_entry;
  struct st_ddl_log_memory_entry *prev_log_entry;
  struct st_ddl_log_memory_entry *next_active_log_entry;
} DDL_LOG_MEMORY_ENTRY;


enum enum_explain_filename_mode
{
  EXPLAIN_ALL_VERBOSE= 0,
  EXPLAIN_PARTITIONS_VERBOSE,
  EXPLAIN_PARTITIONS_AS_COMMENT
};

/* depends on errmsg.txt Database `db`, Table `t` ... */
#define EXPLAIN_FILENAME_MAX_EXTRA_LENGTH 63

#define WFRM_WRITE_SHADOW 1
#define WFRM_INSTALL_SHADOW 2
#define WFRM_PACK_FRM 4
#define WFRM_KEEP_SHARE 8

/* Flags for conversion functions. */
static const uint FN_FROM_IS_TMP=  1 << 0;
static const uint FN_TO_IS_TMP=    1 << 1;
static const uint FN_IS_TMP=       FN_FROM_IS_TMP | FN_TO_IS_TMP;
static const uint NO_FRM_RENAME=   1 << 2;
static const uint FRM_ONLY=        1 << 3;
/** Don't remove table in engine. Remove only .FRM and maybe .PAR files. */
static const uint NO_HA_TABLE=     1 << 4;
/** Don't resolve MySQL's fake "foo.sym" symbolic directory names. */
static const uint SKIP_SYMDIR_ACCESS= 1 << 5;

uint filename_to_tablename(const char *from, char *to, uint to_length
#ifndef DBUG_OFF
                           , bool stay_quiet = false
#endif /* DBUG_OFF */
                           );
uint tablename_to_filename(const char *from, char *to, uint to_length);
uint check_n_cut_mysql50_prefix(const char *from, char *to, uint to_length);
bool check_mysql50_prefix(const char *name);
uint build_table_filename(char *buff, size_t bufflen, const char *db,
                          const char *table, const char *ext, uint flags);
uint build_table_shadow_filename(char *buff, size_t bufflen,
                                 ALTER_PARTITION_PARAM_TYPE *lpt);
uint build_tmptable_filename(THD* thd, char *buff, size_t bufflen);
bool check_table_file_presence(char *old_path, char *path, const char *db,
                               const char *table_name, const char *alias,
                               bool issue_error);
bool mysql_create_table(THD *thd, TABLE_LIST *create_table,
                        HA_CREATE_INFO *create_info,
                        Alter_info *alter_info);
/*
  mysql_create_table_no_lock can be called in one of the following
  mutually exclusive situations:

  - Just a normal ordinary CREATE TABLE statement that explicitly
    defines the table structure.

  - CREATE TABLE ... SELECT. It is special, because only in this case,
    the list of fields is allowed to have duplicates, as long as one of the
    duplicates comes from the select list, and the other doesn't. For
    example in

       CREATE TABLE t1 (a int(5) NOT NUL) SELECT b+10 as a FROM t2;

    the list in alter_info->create_list will have two fields `a`.

  - ALTER TABLE, that creates a temporary table #sql-xxx, which will be later
    renamed to replace the original table.

  - ALTER TABLE as above, but which only modifies the frm file, it only
    creates an frm file for the #sql-xxx, the table in the engine is not
    created.

  - Assisted discovery, CREATE TABLE statement without the table structure.

  These situations are distinguished by the following "create table mode"
  values, where a CREATE ... SELECT is denoted by any non-negative number
  (which should be the number of fields in the SELECT ... part), and other
  cases use constants as defined below.
*/
#define C_CREATE_SELECT(X)        ((X) > 0 ? (X) : 0)
#define C_ORDINARY_CREATE         0
#define C_ALTER_TABLE            -1
#define C_ALTER_TABLE_FRM_ONLY   -2
#define C_ASSISTED_DISCOVERY     -3

bool mysql_create_table_no_lock(THD *thd, const char *db,
                                const char *table_name,
                                HA_CREATE_INFO *create_info,
                                Alter_info *alter_info, bool *is_trans,
                                int create_table_mode);

int mysql_discard_or_import_tablespace(THD *thd,
                                       TABLE_LIST *table_list,
                                       bool discard);
bool mysql_prepare_alter_table(THD *thd, TABLE *table,
                               HA_CREATE_INFO *create_info,
                               Alter_info *alter_info,
                               Alter_table_ctx *alter_ctx);
bool mysql_trans_prepare_alter_copy_data(THD *thd);
bool mysql_trans_commit_alter_copy_data(THD *thd);
bool mysql_alter_table(THD *thd, char *new_db, char *new_name,
                       HA_CREATE_INFO *create_info,
                       TABLE_LIST *table_list,
                       Alter_info *alter_info,
                       uint order_num, ORDER *order, bool ignore);
bool mysql_compare_tables(TABLE *table,
                          Alter_info *alter_info,
                          HA_CREATE_INFO *create_info,
                          bool *metadata_equal);
bool mysql_recreate_table(THD *thd, TABLE_LIST *table_list);
bool mysql_create_like_table(THD *thd, TABLE_LIST *table,
                             TABLE_LIST *src_table,
                             HA_CREATE_INFO *create_info);
bool mysql_rename_table(handlerton *base, const char *old_db,
                        const char * old_name, const char *new_db,
                        const char * new_name, uint flags);

bool mysql_backup_table(THD* thd, TABLE_LIST* table_list);
bool mysql_restore_table(THD* thd, TABLE_LIST* table_list);

bool mysql_checksum_table(THD* thd, TABLE_LIST* table_list,
                          HA_CHECK_OPT* check_opt);
bool mysql_rm_table(THD *thd,TABLE_LIST *tables, my_bool if_exists,
                    my_bool drop_temporary);
int mysql_rm_table_no_locks(THD *thd, TABLE_LIST *tables, bool if_exists,
                            bool drop_temporary, bool drop_view,
                            bool log_query);
bool quick_rm_table(THD *thd, handlerton *base, const char *db,
                    const char *table_name, uint flags);
void close_cached_table(THD *thd, TABLE *table);
void sp_prepare_create_field(THD *thd, Create_field *sql_field);
int prepare_create_field(Create_field *sql_field,
			 uint *blob_columns,
			 longlong table_flags);
CHARSET_INFO* get_sql_field_charset(Create_field *sql_field,
                                    HA_CREATE_INFO *create_info);
bool mysql_write_frm(ALTER_PARTITION_PARAM_TYPE *lpt, uint flags);
int write_bin_log(THD *thd, bool clear_error,
                  char const *query, ulong query_length,
                  bool is_trans= FALSE);
bool write_ddl_log_entry(DDL_LOG_ENTRY *ddl_log_entry,
                           DDL_LOG_MEMORY_ENTRY **active_entry);
bool write_execute_ddl_log_entry(uint first_entry,
                                   bool complete,
                                   DDL_LOG_MEMORY_ENTRY **active_entry);
bool deactivate_ddl_log_entry(uint entry_no);
void release_ddl_log_memory_entry(DDL_LOG_MEMORY_ENTRY *log_entry);
bool sync_ddl_log();
void release_ddl_log();
void execute_ddl_log_recovery();
bool execute_ddl_log_entry(THD *thd, uint first_entry);
bool validate_comment_length(THD *thd, const char *comment_str,
                             size_t *comment_len, uint max_len,
                             uint err_code, const char *comment_name);
bool check_duplicate_warning(THD *thd, char *msg, ulong length);

template<typename T> class List;
void promote_first_timestamp_column(List<Create_field> *column_definitions);

/*
  These prototypes where under INNODB_COMPATIBILITY_HOOKS.
*/
uint explain_filename(THD* thd, const char *from, char *to, uint to_length,
                      enum_explain_filename_mode explain_mode);


extern MYSQL_PLUGIN_IMPORT const char *primary_key_name;
extern mysql_mutex_t LOCK_gdl;

#endif /* SQL_TABLE_INCLUDED */
