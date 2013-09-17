/* Copyright (C) 2012-2013 Kentoku Shiba

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

class spider_db_mysql_util: public spider_db_util
{
public:
  spider_db_mysql_util();
  ~spider_db_mysql_util();
  int append_name(
    spider_string *str,
    const char *name,
    uint name_length
  );
  int append_name_with_charset(
    spider_string *str,
    const char *name,
    uint name_length,
    CHARSET_INFO *name_charset
  );
  bool is_name_quote(
    const char head_code
  );
  int append_escaped_name_quote(
    spider_string *str
  );
  int append_column_value(
    ha_spider *spider,
    spider_string *str,
    Field *field,
    const uchar *new_ptr,
    CHARSET_INFO *access_charset
  );
  int append_from_with_alias(
    spider_string *str,
    const char **table_names,
    uint *table_name_lengths,
    const char **table_aliases,
    uint *table_alias_lengths,
    uint table_count,
    int *table_name_pos,
    bool over_write
  );
  int append_trx_isolation(
    spider_string *str,
    int trx_isolation
  );
  int append_autocommit(
    spider_string *str,
    bool autocommit
  );
  int append_sql_log_off(
    spider_string *str,
    bool sql_log_off
  );
  int append_time_zone(
    spider_string *str,
    Time_zone *time_zone
  );
  int append_start_transaction(
    spider_string *str
  );
  int append_xa_start(
    spider_string *str,
    XID *xid
  );
  int append_lock_table_head(
    spider_string *str
  );
  int append_lock_table_body(
    spider_string *str,
    const char *db_name,
    uint db_name_length,
    CHARSET_INFO *db_name_charset,
    const char *table_name,
    uint table_name_length,
    CHARSET_INFO *table_name_charset,
    int lock_type
  );
  int append_lock_table_tail(
    spider_string *str
  );
  int append_unlock_table(
    spider_string *str
  );
  int open_item_func(
    Item_func *item_func,
    ha_spider *spider,
    spider_string *str,
    const char *alias,
    uint alias_length
  );
#ifdef HANDLER_HAS_DIRECT_AGGREGATE
  int open_item_sum_func(
    Item_sum *item_sum,
    ha_spider *spider,
    spider_string *str,
    const char *alias,
    uint alias_length
  );
#endif
  int append_escaped_util(
    spider_string *to,
    String *from
  );
};

class spider_db_mysql_row: public spider_db_row
{
public:
  MYSQL_ROW           row;
  MYSQL_ROW           row_first;
  ulong               *lengths;
  ulong               *lengths_first;
  uint                field_count;
  bool                cloned;
  spider_db_mysql_row();
  ~spider_db_mysql_row();
  int store_to_field(
    Field *field,
    CHARSET_INFO *access_charset
  );
  int append_to_str(
    spider_string *str
  );
  int append_escaped_to_str(
    spider_string *str,
    uint dbton_id
  );
  void first();
  void next();
  bool is_null();
  int val_int();
  double val_real();
  my_decimal *val_decimal(
    my_decimal *decimal_value,
    CHARSET_INFO *access_charset
  );
  SPIDER_DB_ROW *clone();
  int store_to_tmp_table(
    TABLE *tmp_table,
    spider_string *str
  );
};

class spider_db_mysql_result: public spider_db_result
{
public:
  MYSQL_RES           *db_result;
  spider_db_mysql_row row;
  MYSQL_ROW_OFFSET    first_row;
  int                 store_error_num;
  spider_db_mysql_result();
  ~spider_db_mysql_result();
  bool has_result();
  void free_result();
  SPIDER_DB_ROW *current_row();
  SPIDER_DB_ROW *fetch_row();
  SPIDER_DB_ROW *fetch_row_from_result_buffer(
    spider_db_result_buffer *spider_res_buf
  );
  SPIDER_DB_ROW *fetch_row_from_tmp_table(
    TABLE *tmp_table
  );
  int fetch_table_status(
    int mode,
    ha_rows &records,
    ulong &mean_rec_length,
    ulonglong &data_file_length,
    ulonglong &max_data_file_length,
    ulonglong &index_file_length,
    ulonglong &auto_increment_value,
    time_t &create_time,
    time_t &update_time,
    time_t &check_time
  );
  int fetch_table_records(
    int mode,
    ha_rows &records
  );
  int fetch_table_cardinality(
    int mode,
    TABLE *table,
    longlong *cardinality,
    uchar *cardinality_upd,
    int bitmap_size
  );
  int fetch_table_mon_status(
    int &status
  );
  longlong num_rows();
  uint num_fields();
  void move_to_pos(
    longlong pos
  );
  int get_errno();
#ifdef SPIDER_HAS_DISCOVER_TABLE_STRUCTURE
  int fetch_columns_for_discover_table_structure(
    spider_string *str,
    CHARSET_INFO *access_charset
  );
  int fetch_index_for_discover_table_structure(
    spider_string *str,
    CHARSET_INFO *access_charset
  );
#endif
};

class spider_db_mysql: public spider_db_conn
{
  MYSQL          *db_conn;
  int            stored_error;
public:
  HASH           lock_table_hash;
  bool           lock_table_hash_inited;
  uint           lock_table_hash_id;
  const char     *lock_table_hash_func_name;
  const char     *lock_table_hash_file_name;
  ulong          lock_table_hash_line_no;
  DYNAMIC_ARRAY  handler_open_array;
  bool           handler_open_array_inited;
  uint           handler_open_array_id;
  const char     *handler_open_array_func_name;
  const char     *handler_open_array_file_name;
  ulong          handler_open_array_line_no;
  spider_db_mysql(
    SPIDER_CONN *conn
  );
  ~spider_db_mysql();
  int init();
  bool is_connected();
  void bg_connect();
  int connect(
    char *tgt_host,
    char *tgt_username,
    char *tgt_password,
    long tgt_port,
    char *tgt_socket,
    char *server_name,
    int connect_retry_count,
    longlong connect_retry_interval
  );
  int ping();
  void bg_disconnect();
  void disconnect();
  int set_net_timeout();
  int exec_query(
    const char *query,
    uint length,
    int quick_mode
  );
  int get_errno();
  const char *get_error();
  bool is_server_gone_error(
    int error_num
  );
  bool is_dup_entry_error(
    int error_num
  );
  bool is_xa_nota_error(
    int error_num
  );
  spider_db_result *store_result(
    spider_db_result_buffer **spider_res_buf,
    st_spider_db_request_key *request_key,
    int *error_num
  );
  spider_db_result *use_result(
    st_spider_db_request_key *request_key,
    int *error_num
  );
  int next_result();
  uint affected_rows();
  ulonglong last_insert_id();
  int set_character_set(
    const char *csname
  );
  int select_db(
    const char *dbname
  );
  int consistent_snapshot(
    int *need_mon
  );
  bool trx_start_in_bulk_sql();
  int start_transaction(
    int *need_mon
  );
  int commit(
    int *need_mon
  );
  int rollback(
    int *need_mon
  );
  bool xa_start_in_bulk_sql();
  int xa_start(
    XID *xid,
    int *need_mon
  );
  int xa_end(
    XID *xid,
    int *need_mon
  );
  int xa_prepare(
    XID *xid,
    int *need_mon
  );
  int xa_commit(
    XID *xid,
    int *need_mon
  );
  int xa_rollback(
    XID *xid,
    int *need_mon
  );
  bool set_trx_isolation_in_bulk_sql();
  int set_trx_isolation(
    int trx_isolation,
    int *need_mon
  );
  bool set_autocommit_in_bulk_sql();
  int set_autocommit(
    bool autocommit,
    int *need_mon
  );
  bool set_sql_log_off_in_bulk_sql();
  int set_sql_log_off(
    bool sql_log_off,
    int *need_mon
  );
  bool set_time_zone_in_bulk_sql();
  int set_time_zone(
    Time_zone *time_zone,
    int *need_mon
  );
#if defined(HS_HAS_SQLCOM) && defined(HAVE_HANDLERSOCKET)
  int append_sql(
    char *sql,
    ulong sql_length,
    st_spider_db_request_key *request_key
  );
  int append_open_handler(
    uint handler_id,
    const char *db_name,
    const char *table_name,
    const char *index_name,
    const char *sql,
    st_spider_db_request_key *request_key
  );
  int append_select(
    uint handler_id,
    spider_string *sql,
    SPIDER_DB_HS_STRING_REF_BUFFER *keys,
    int limit,
    int skip,
    st_spider_db_request_key *request_key
  );
  int append_insert(
    uint handler_id,
    SPIDER_DB_HS_STRING_REF_BUFFER *upds,
    st_spider_db_request_key *request_key
  );
  int append_update(
    uint handler_id,
    spider_string *sql,
    SPIDER_DB_HS_STRING_REF_BUFFER *keys,
    SPIDER_DB_HS_STRING_REF_BUFFER *upds,
    int limit,
    int skip,
    bool increment,
    bool decrement,
    st_spider_db_request_key *request_key
  );
  int append_delete(
    uint handler_id,
    spider_string *sql,
    SPIDER_DB_HS_STRING_REF_BUFFER *keys,
    int limit,
    int skip,
    st_spider_db_request_key *request_key
  );
  void reset_request_queue();
#endif
  size_t escape_string(
    char *to,
    const char *from,
    size_t from_length
  );
  bool have_lock_table_list();
  int append_lock_tables(
    spider_string *str
  );
  int append_unlock_tables(
    spider_string *str
  );
  uint get_lock_table_hash_count();
  void reset_lock_table_hash();
  uint get_opened_handler_count();
  void reset_opened_handler();
  void set_dup_key_idx(
    ha_spider *spider,
    int link_idx
  );
  bool cmp_request_key_to_snd(
    st_spider_db_request_key *request_key
  );
};

class spider_mysql_share: public spider_db_share
{
public:
  spider_string      *table_select;
  int                table_select_pos;
  spider_string      *key_select;
  int                *key_select_pos;
  spider_string      *key_hint;
  spider_string      *show_table_status;
  spider_string      *show_records;
  spider_string      *show_index;
  spider_string      *table_names_str;
  spider_string      *db_names_str;
  spider_string      *db_table_str;
#ifdef SPIDER_HAS_HASH_VALUE_TYPE
  my_hash_value_type *db_table_str_hash_value;
#endif
  uint               table_nm_max_length;
  uint               db_nm_max_length;
  spider_string      *column_name_str;
  bool               same_db_table_name;
  int                first_all_link_idx;

  spider_mysql_share(
    st_spider_share *share
  );
  ~spider_mysql_share();
  int init();
  uint get_column_name_length(
    uint field_index
  );
  int append_column_name(
    spider_string *str,
    uint field_index
  );
  int append_column_name_with_alias(
    spider_string *str,
    uint field_index,
    const char *alias,
    uint alias_length
  );
  int append_table_name(
    spider_string *str,
    int all_link_idx
  );
  int append_table_name_with_adjusting(
    spider_string *str,
    int all_link_idx
  );
  int append_from_with_adjusted_table_name(
    spider_string *str,
    int *table_name_pos
  );
  bool need_change_db_table_name();
#ifdef SPIDER_HAS_DISCOVER_TABLE_STRUCTURE
  int discover_table_structure(
    SPIDER_TRX *trx,
    SPIDER_SHARE *spider_share,
    spider_string *str
  );
#endif
private:
  int create_table_names_str();
  void free_table_names_str();
  int create_column_name_str();
  void free_column_name_str();
  int convert_key_hint_str();
  int append_show_table_status();
  void free_show_table_status();
  int append_show_records();
  void free_show_records();
  int append_show_index();
  void free_show_index();
  int append_table_select();
  int append_key_select(
    uint idx
  );
};

class spider_mysql_handler: public spider_db_handler
{
  spider_string           sql;
  spider_string           sql_part;
  spider_string           sql_part2;
  spider_string           ha_sql;
  int                     where_pos;
  int                     order_pos;
  int                     limit_pos;
  int                     table_name_pos;
  int                     ha_read_pos;
  int                     ha_next_pos;
  int                     ha_where_pos;
  int                     ha_limit_pos;
  int                     ha_table_name_pos;
  uint                    ha_sql_handler_id;
  spider_string           insert_sql;
  int                     insert_pos;
  int                     insert_table_name_pos;
  spider_string           update_sql;
  TABLE                   *upd_tmp_tbl;
  TMP_TABLE_PARAM         upd_tmp_tbl_prm;
  spider_string           tmp_sql;
  int                     tmp_sql_pos1; /* drop db nm pos at tmp_table_join */
  int                     tmp_sql_pos2; /* create db nm pos at tmp_table_join */
  int                     tmp_sql_pos3; /* insert db nm pos at tmp_table_join */
  int                     tmp_sql_pos4; /* insert val pos at tmp_table_join */
  int                     tmp_sql_pos5; /* end of drop tbl at tmp_table_join */
  spider_string           dup_update_sql;
  spider_string           *exec_sql;
  spider_string           *exec_insert_sql;
  spider_string           *exec_update_sql;
  spider_string           *exec_tmp_sql;
  spider_string           *exec_ha_sql;
  bool                    reading_from_bulk_tmp_table;
  bool                    filled_up;
#if defined(HS_HAS_SQLCOM) && defined(HAVE_HANDLERSOCKET)
  SPIDER_DB_HS_STRING_REF_BUFFER hs_upds;
#endif
  SPIDER_INT_HLD          *union_table_name_pos_first;
  SPIDER_INT_HLD          *union_table_name_pos_current;
public:
  spider_mysql_share      *mysql_share;
  SPIDER_LINK_FOR_HASH    *link_for_hash;
  spider_mysql_handler(
    ha_spider *spider,
    spider_mysql_share *share
  );
  ~spider_mysql_handler();
  int init();
  int append_table_name_with_adjusting(
    spider_string *str,
    int link_idx,
    ulong sql_type
  );
  int append_key_column_types(
    const key_range *start_key,
    spider_string *str
  );
  int append_key_join_columns_for_bka(
    const key_range *start_key,
    spider_string *str,
    const char **table_aliases,
    uint *table_alias_lengths
  );
  int append_tmp_table_and_sql_for_bka(
    const key_range *start_key
  );
  int reuse_tmp_table_and_sql_for_bka();
  void create_tmp_bka_table_name(
    char *tmp_table_name,
    int *tmp_table_name_length,
    int link_idx
  );
  int append_create_tmp_bka_table(
    const key_range *start_key,
    spider_string *str,
    char *tmp_table_name,
    int tmp_table_name_length,
    int *db_name_pos,
    CHARSET_INFO *table_charset
  );
  int append_drop_tmp_bka_table(
    spider_string *str,
    char *tmp_table_name,
    int tmp_table_name_length,
    int *db_name_pos,
    int *drop_table_end_pos,
    bool with_semicolon
  );
  int append_insert_tmp_bka_table(
    const key_range *start_key,
    spider_string *str,
    char *tmp_table_name,
    int tmp_table_name_length,
    int *db_name_pos
  );
  int append_union_table_and_sql_for_bka(
    const key_range *start_key
  );
  int reuse_union_table_and_sql_for_bka();
  int append_insert_for_recovery(
    ulong sql_type,
    int link_idx
  );
  int append_update(
    const TABLE *table,
    my_ptrdiff_t ptr_diff
  );
  int append_update(
    const TABLE *table,
    my_ptrdiff_t ptr_diff,
    int link_idx
  );
  int append_delete(
    const TABLE *table,
    my_ptrdiff_t ptr_diff
  );
  int append_delete(
    const TABLE *table,
    my_ptrdiff_t ptr_diff,
    int link_idx
  );
  int append_insert_part();
  int append_insert(
    spider_string *str,
    int link_idx
  );
  int append_update_part();
  int append_update(
    spider_string *str,
    int link_idx
  );
  int append_delete_part();
  int append_delete(
    spider_string *str
  );
  #if defined(HS_HAS_SQLCOM) && defined(HAVE_HANDLERSOCKET)
  #ifdef HANDLER_HAS_DIRECT_UPDATE_ROWS
  int append_increment_update_set_part();
  int append_increment_update_set(
    spider_string *str
  );
  #endif
  #endif
  int append_update_set_part();
  int append_update_set(
    spider_string *str
  );
  #ifdef HANDLER_HAS_DIRECT_UPDATE_ROWS
  int append_direct_update_set_part();
  int append_direct_update_set(
    spider_string *str
  );
  int append_dup_update_pushdown_part(
    const char *alias,
    uint alias_length
  );
  int append_update_columns_part(
    const char *alias,
    uint alias_length
  );
  int check_update_columns_part();
  int append_update_columns(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  #endif
  int append_select_part(
    ulong sql_type
  );
  int append_select(
    spider_string *str,
    ulong sql_type
  );
  int append_table_select_part(
    ulong sql_type
  );
  int append_table_select(
    spider_string *str
  );
  int append_key_select_part(
    ulong sql_type,
    uint idx
  );
  int append_key_select(
    spider_string *str,
    uint idx
  );
  int append_minimum_select_part(
    ulong sql_type
  );
  int append_minimum_select(
    spider_string *str,
    ulong sql_type
  );
  int append_table_select_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_key_select_with_alias(
    spider_string *str,
    const KEY *key_info,
    const char *alias,
    uint alias_length
  );
  int append_minimum_select_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_select_columns_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_hint_after_table_part(
    ulong sql_type
  );
  int append_hint_after_table(
    spider_string *str
  );
  void set_where_pos(
    ulong sql_type
  );
  void set_where_to_pos(
    ulong sql_type
  );
  int check_item_type(
    Item *item
  );
  int append_values_connector_part(
    ulong sql_type
  );
  int append_values_connector(
    spider_string *str
  );
  int append_values_terminator_part(
    ulong sql_type
  );
  int append_values_terminator(
    spider_string *str
  );
  int append_union_table_connector_part(
    ulong sql_type
  );
  int append_union_table_connector(
    spider_string *str
  );
  int append_union_table_terminator_part(
    ulong sql_type
  );
  int append_union_table_terminator(
    spider_string *str
  );
  int append_key_column_values_part(
    const key_range *start_key,
    ulong sql_type
  );
  int append_key_column_values(
    spider_string *str,
    const key_range *start_key
  );
  int append_key_column_values_with_name_part(
    const key_range *start_key,
    ulong sql_type
  );
  int append_key_column_values_with_name(
    spider_string *str,
    const key_range *start_key
  );
  int append_key_where_part(
    const key_range *start_key,
    const key_range *end_key,
    ulong sql_type
  );
  int append_key_where(
    spider_string *str,
    spider_string *str_part,
    spider_string *str_part2,
    const key_range *start_key,
    const key_range *end_key,
    ulong sql_type,
    bool set_order
  );
  int append_is_null_part(
    ulong sql_type,
    KEY_PART_INFO *key_part,
    const key_range *key,
    const uchar **ptr,
    bool key_eq
  );
  int append_is_null(
    ulong sql_type,
    spider_string *str,
    spider_string *str_part,
    spider_string *str_part2,
    KEY_PART_INFO *key_part,
    const key_range *key,
    const uchar **ptr,
    bool key_eq
  );
  int append_where_terminator_part(
    ulong sql_type,
    bool set_order,
    int key_count
  );
  int append_where_terminator(
    ulong sql_type,
    spider_string *str,
    spider_string *str_part,
    spider_string *str_part2,
    bool set_order,
    int key_count
  );
  int append_match_where_part(
    ulong sql_type
  );
  int append_match_where(
    spider_string *str
  );
  int append_update_where(
    spider_string *str,
    const TABLE *table,
    my_ptrdiff_t ptr_diff
  );
  int append_condition_part(
    const char *alias,
    uint alias_length,
    ulong sql_type,
    bool test_flg
  );
  int append_condition(
    spider_string *str,
    const char *alias,
    uint alias_length,
    bool start_where,
    ulong sql_type
  );
  int append_match_against_part(
    ulong sql_type,
    st_spider_ft_info *ft_info,
    const char *alias,
    uint alias_length
  );
  int append_match_against(
    spider_string *str,
    st_spider_ft_info  *ft_info,
    const char *alias,
    uint alias_length
  );
  int append_match_select_part(
    ulong sql_type,
    const char *alias,
    uint alias_length
  );
  int append_match_select(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
#ifdef HANDLER_HAS_DIRECT_AGGREGATE
  int append_sum_select_part(
    ulong sql_type,
    const char *alias,
    uint alias_length
  );
  int append_sum_select(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
#endif
  void set_order_pos(
    ulong sql_type
  );
  void set_order_to_pos(
    ulong sql_type
  );
#ifdef HANDLER_HAS_DIRECT_AGGREGATE
  int append_group_by(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
#endif
  int append_key_order_for_merge_with_alias_part(
    const char *alias,
    uint alias_length,
    ulong sql_type
  );
  int append_key_order_for_merge_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_key_order_for_direct_order_limit_with_alias_part(
    const char *alias,
    uint alias_length,
    ulong sql_type
  );
  int append_key_order_for_direct_order_limit_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_key_order_with_alias_part(
    const char *alias,
    uint alias_length,
    ulong sql_type
  );
  int append_key_order_for_handler(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_key_order_with_alias(
    spider_string *str,
    const char *alias,
    uint alias_length
  );
  int append_limit_part(
    longlong offset,
    longlong limit,
    ulong sql_type
  );
  int reappend_limit_part(
    longlong offset,
    longlong limit,
    ulong sql_type
  );
  int append_limit(
    spider_string *str,
    longlong offset,
    longlong limit
  );
  int append_select_lock_part(
    ulong sql_type
  );
  int append_select_lock(
    spider_string *str
  );
  int append_union_all_start_part(
    ulong sql_type
  );
  int append_union_all_start(
    spider_string *str
  );
  int append_union_all_part(
    ulong sql_type
  );
  int append_union_all(
    spider_string *str
  );
  int append_union_all_end_part(
    ulong sql_type
  );
  int append_union_all_end(
    spider_string *str
  );
  int append_multi_range_cnt_part(
    ulong sql_type,
    uint multi_range_cnt,
    bool with_comma
  );
  int append_multi_range_cnt(
    spider_string *str,
    uint multi_range_cnt,
    bool with_comma
  );
  int append_multi_range_cnt_with_name_part(
    ulong sql_type,
    uint multi_range_cnt
  );
  int append_multi_range_cnt_with_name(
    spider_string *str,
    uint multi_range_cnt
  );
  int append_open_handler_part(
    ulong sql_type,
    uint handler_id,
    SPIDER_CONN *conn,
    int link_idx
  );
  int append_open_handler(
    spider_string *str,
    uint handler_id,
    SPIDER_CONN *conn,
    int link_idx
  );
  int append_close_handler_part(
    ulong sql_type,
    int link_idx
  );
  int append_close_handler(
    spider_string *str,
    int link_idx
  );
  int append_insert_terminator_part(
    ulong sql_type
  );
  int append_insert_terminator(
    spider_string *str
  );
  int append_insert_values_part(
    ulong sql_type
  );
  int append_insert_values(
    spider_string *str
  );
  int append_into_part(
    ulong sql_type
  );
  int append_into(
    spider_string *str
  );
  void set_insert_to_pos(
    ulong sql_type
  );
  int append_from_part(
    ulong sql_type,
    int link_idx
  );
  int append_from(
    spider_string *str,
    ulong sql_type,
    int link_idx
  );
  int append_flush_tables_part(
    ulong sql_type,
    int link_idx,
    bool lock
  );
  int append_flush_tables(
    spider_string *str,
    int link_idx,
    bool lock
  );
  int append_optimize_table_part(
    ulong sql_type,
    int link_idx
  );
  int append_optimize_table(
    spider_string *str,
    int link_idx
  );
  int append_analyze_table_part(
    ulong sql_type,
    int link_idx
  );
  int append_analyze_table(
    spider_string *str,
    int link_idx
  );
  int append_repair_table_part(
    ulong sql_type,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int append_repair_table(
    spider_string *str,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int append_check_table_part(
    ulong sql_type,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int append_check_table(
    spider_string *str,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int append_enable_keys_part(
    ulong sql_type,
    int link_idx
  );
  int append_enable_keys(
    spider_string *str,
    int link_idx
  );
  int append_disable_keys_part(
    ulong sql_type,
    int link_idx
  );
  int append_disable_keys(
    spider_string *str,
    int link_idx
  );
  int append_delete_all_rows_part(
    ulong sql_type
  );
  int append_delete_all_rows(
    spider_string *str,
    ulong sql_type
  );
  int append_truncate(
    spider_string *str,
    ulong sql_type,
    int link_idx
  );
  int append_explain_select_part(
    key_range *start_key,
    key_range *end_key,
    ulong sql_type,
    int link_idx
  );
  int append_explain_select(
    spider_string *str,
    key_range *start_key,
    key_range *end_key,
    ulong sql_type,
    int link_idx
  );
  bool is_bulk_insert_exec_period(
    bool bulk_end
  );
  bool sql_is_filled_up(
    ulong sql_type
  );
  bool sql_is_empty(
    ulong sql_type
  );
  bool support_multi_split_read();
  bool support_bulk_update();
  int bulk_tmp_table_insert();
  int bulk_tmp_table_insert(
    int link_idx
  );
  int bulk_tmp_table_end_bulk_insert();
  int bulk_tmp_table_rnd_init();
  int bulk_tmp_table_rnd_next();
  int bulk_tmp_table_rnd_end();
  bool need_copy_for_update(
    int link_idx
  );
  bool bulk_tmp_table_created();
  int mk_bulk_tmp_table_and_bulk_start();
  void rm_bulk_tmp_table();
  int store_sql_to_bulk_tmp_table(
    spider_string *str,
    TABLE *tmp_table
  );
  int restore_sql_from_bulk_tmp_table(
    spider_string *str,
    TABLE *tmp_table
  );
  int insert_lock_tables_list(
    SPIDER_CONN *conn,
    int link_idx
  );
  int append_lock_tables_list(
    SPIDER_CONN *conn,
    int link_idx,
    int *appended
  );
  int realloc_sql(
    ulong *realloced
  );
  int reset_sql(
    ulong sql_type
  );
#if defined(HS_HAS_SQLCOM) && defined(HAVE_HANDLERSOCKET)
  int reset_keys(
    ulong sql_type
  );
  int reset_upds(
    ulong sql_type
  );
  int reset_strs(
    ulong sql_type
  );
  int reset_strs_pos(
    ulong sql_type
  );
  int push_back_upds(
    SPIDER_HS_STRING_REF &info
  );
#endif
  bool need_lock_before_set_sql_for_exec(
    ulong sql_type
  );
  int set_sql_for_exec(
    ulong sql_type,
    int link_idx
  );
  int set_sql_for_exec(
    spider_db_copy_table *tgt_ct,
    ulong sql_type
  );
  int execute_sql(
    ulong sql_type,
    SPIDER_CONN *conn,
    int quick_mode,
    int *need_mon
  );
  int reset();
  int sts_mode_exchange(
    int sts_mode
  );
  int show_table_status(
    int link_idx,
    int sts_mode,
    uint flag
  );
  int crd_mode_exchange(
    int crd_mode
  );
  int show_index(
    int link_idx,
    int crd_mode
  );
  int show_records(
    int link_idx
  );
  int show_last_insert_id(
    int link_idx,
    ulonglong &last_insert_id
  );
  ha_rows explain_select(
    key_range *start_key,
    key_range *end_key,
    int link_idx
  );
  int lock_tables(
    int link_idx
  );
  int unlock_tables(
    int link_idx
  );
  int disable_keys(
    SPIDER_CONN *conn,
    int link_idx
  );
  int enable_keys(
    SPIDER_CONN *conn,
    int link_idx
  );
  int check_table(
    SPIDER_CONN *conn,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int repair_table(
    SPIDER_CONN *conn,
    int link_idx,
    HA_CHECK_OPT* check_opt
  );
  int analyze_table(
    SPIDER_CONN *conn,
    int link_idx
  );
  int optimize_table(
    SPIDER_CONN *conn,
    int link_idx
  );
  int flush_tables(
    SPIDER_CONN *conn,
    int link_idx,
    bool lock
  );
  int flush_logs(
    SPIDER_CONN *conn,
    int link_idx
  );
  int insert_opened_handler(
    SPIDER_CONN *conn,
    int link_idx
  );
  int delete_opened_handler(
    SPIDER_CONN *conn,
    int link_idx
  );
  int sync_from_clone_source(
    spider_db_handler *dbton_hdl
  );
  bool support_use_handler(
    int use_handler
  );
  bool minimum_select_bit_is_set(
    uint field_index
  );
  void copy_minimum_select_bitmap(
    uchar *bitmap
  );
  int init_union_table_name_pos();
  int set_union_table_name_pos();
  int reset_union_table_name(
    spider_string *str,
    int link_idx,
    ulong sql_type
  );
};

class spider_mysql_copy_table: public spider_db_copy_table
{
public:
  spider_mysql_share      *mysql_share;
  spider_string           sql;
  uint                    pos;
  spider_mysql_copy_table(
    spider_mysql_share *db_share
  );
  ~spider_mysql_copy_table();
  int init();
  void set_sql_charset(
    CHARSET_INFO *cs
  );
  int append_select_str();
  int append_insert_str(
    int insert_flg
  );
  int append_table_columns(
    TABLE_SHARE *table_share
  );
  int append_from_str();
  int append_table_name(
    int link_idx
  );
  void set_sql_pos();
  void set_sql_to_pos();
  int append_copy_where(
    spider_db_copy_table *source_ct,
    KEY *key_info,
    ulong *last_row_pos,
    ulong *last_lengths
  );
  int append_key_order_str(
    KEY *key_info,
    int start_pos,
    bool desc_flg
  );
  int append_limit(
    longlong offset,
    longlong limit
  );
  int append_into_str();
  int append_open_paren_str();
  int append_values_str();
  int append_select_lock_str(
    int lock_mode
  );
  int exec_query(
    SPIDER_CONN *conn,
    int quick_mode,
    int *need_mon
  );
  int copy_key_row(
    spider_db_copy_table *source_ct,
    Field *field,
    ulong *row_pos,
    ulong *length,
    const char *joint_str,
    const int joint_length
  );
  int copy_row(
    Field *field,
    SPIDER_DB_ROW *row
  );
  int copy_rows(
    TABLE *table,
    SPIDER_DB_ROW *row,
    ulong **last_row_pos,
    ulong **last_lengths
  );
  int copy_rows(
    TABLE *table,
    SPIDER_DB_ROW *row
  );
  int append_insert_terminator();
  int copy_insert_values(
    spider_db_copy_table *source_ct
  );
};
