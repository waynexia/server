/*
   Copyright (c) 2013 Monty Program Ab

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; version 2 of
   the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*
  a engine that auto-creates tables with rows filled with sequential values
*/

#include <ctype.h>
#include <mysql_version.h>
#include <handler.h>
#include <table.h>
#include <field.h>

class Sequence_share : public Handler_share {
public:
  const char *name;
  THR_LOCK lock;

  ulonglong from, to, step;
  bool reverse;

  Sequence_share(const char *name_arg, ulonglong from_arg, ulonglong to_arg,
                 ulonglong step_arg, bool reverse_arg):
    name(name_arg), from(from_arg), to(to_arg), step(step_arg),
    reverse(reverse_arg)
  {
    thr_lock_init(&lock);
  }
  ~Sequence_share()
  {
    thr_lock_delete(&lock);
  }
};

class ha_seq: public handler
{
private:
  THR_LOCK_DATA lock;
  Sequence_share *seqs;
  Sequence_share *get_share();
  ulonglong cur;

public:
  ha_seq(handlerton *hton, TABLE_SHARE *table_arg)
    : handler(hton, table_arg), seqs(0) { }
  ulonglong table_flags() const
  { return HA_BINLOG_ROW_CAPABLE | HA_BINLOG_STMT_CAPABLE; }

  /* open/close/locking */
  int create(const char *name, TABLE *table_arg,
             HA_CREATE_INFO *create_info) { return HA_ERR_WRONG_COMMAND; }

  int open(const char *name, int mode, uint test_if_locked);
  int close(void);
  THR_LOCK_DATA **store_lock(THD *, THR_LOCK_DATA **, enum thr_lock_type);

  /* table scan */
  int rnd_init(bool scan);
  int rnd_next(unsigned char *buf);
  void position(const uchar *record);
  int rnd_pos(uchar *buf, uchar *pos);
  int info(uint flag);

  /* indexes */
  ulong index_flags(uint inx, uint part, bool all_parts) const
  { return HA_READ_NEXT | HA_READ_PREV | HA_READ_ORDER |
           HA_READ_RANGE | HA_KEYREAD_ONLY; }
  uint max_supported_keys() const { return 1; }
  int index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map,
                     enum ha_rkey_function find_flag);
  int index_next(uchar *buf);
  int index_prev(uchar *buf);
  int index_first(uchar *buf);
  int index_last(uchar *buf);
  ha_rows records_in_range(uint inx, key_range *min_key,
                                   key_range *max_key);

  double scan_time() { return nvalues(); }
  double read_time(uint index, uint ranges, ha_rows rows) { return rows; }
  double keyread_time(uint index, uint ranges, ha_rows rows) { return rows; }

private:
  void set(uchar *buf);
  ulonglong nvalues() { return (seqs->to - seqs->from)/seqs->step; }
};

THR_LOCK_DATA **ha_seq::store_lock(THD *thd, THR_LOCK_DATA **to,
                           enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type= TL_WRITE_ALLOW_WRITE;
  *to ++= &lock;
  return to;
}

void ha_seq::set(unsigned char *buf)
{
  my_bitmap_map *old_map = dbug_tmp_use_all_columns(table, table->write_set);
  my_ptrdiff_t offset = (my_ptrdiff_t) (buf - table->record[0]);
  Field *field = table->field[0];
  field->move_field_offset(offset);
  field->store(cur, true);
  field->move_field_offset(-offset);
  dbug_tmp_restore_column_map(table->write_set, old_map);
}

int ha_seq::rnd_init(bool scan)
{
  cur= seqs->reverse ? seqs->to : seqs->from;
  return 0;
}

int ha_seq::rnd_next(unsigned char *buf)
{
  if (seqs->reverse)
    return index_prev(buf);
  else
    return index_next(buf);
}

void ha_seq::position(const uchar *record)
{
  *(ulonglong*)ref= cur;
}

int ha_seq::rnd_pos(uchar *buf, uchar *pos)
{
  cur= *(ulonglong*)pos;
  return rnd_next(buf);
}

int ha_seq::info(uint flag)
{
  if (flag & HA_STATUS_VARIABLE)
    stats.records = nvalues();
  return 0;
}

int ha_seq::index_read_map(uchar *buf, const uchar *key_arg,
                           key_part_map keypart_map,
                           enum ha_rkey_function find_flag)
{
  ulonglong key= uint8korr(key_arg);
  switch (find_flag) {
  case HA_READ_AFTER_KEY:
    key++;
    // fall through
  case HA_READ_KEY_OR_NEXT:
    if (key <= seqs->from)
      cur= seqs->from;
    else
    {
      cur= (key - seqs->from + seqs->step - 1) / seqs->step * seqs->step + seqs->from;
      if (cur >= seqs->to)
        return HA_ERR_KEY_NOT_FOUND;
    }
    return index_next(buf);

  case HA_READ_KEY_EXACT:
    if ((key - seqs->from) % seqs->step != 0 || key < seqs->from || key >= seqs->to)
      return HA_ERR_KEY_NOT_FOUND;
    cur= key;
    return index_next(buf);

  case HA_READ_BEFORE_KEY:
    key--;
    // fall through
  case HA_READ_PREFIX_LAST_OR_PREV:
    if (key >= seqs->to)
      cur= seqs->to;
    else
    {
      if (key < seqs->from)
        return HA_ERR_KEY_NOT_FOUND;
      cur= (key - seqs->from) / seqs->step * seqs->step + seqs->from;
    }
    return index_prev(buf);
  default: return HA_ERR_WRONG_COMMAND;
  }
}


int ha_seq::index_next(uchar *buf)
{
  if (cur == seqs->to)
    return HA_ERR_END_OF_FILE;
  set(buf);
  cur+= seqs->step;
  return 0;
}


int ha_seq::index_prev(uchar *buf)
{
  if (cur == seqs->from)
    return HA_ERR_END_OF_FILE;
  cur-= seqs->step;
  set(buf);
  return 0;
}


int ha_seq::index_first(uchar *buf)
{
  cur= seqs->from;
  return index_next(buf);
}


int ha_seq::index_last(uchar *buf)
{
  cur= seqs->to;
  return index_prev(buf);
}

ha_rows ha_seq::records_in_range(uint inx, key_range *min_key,
                                 key_range *max_key)
{
  ulonglong kmin= min_key ? uint8korr(min_key->key) : seqs->from;
  ulonglong kmax= max_key ? uint8korr(max_key->key) : seqs->to - 1;
  if (kmin >= seqs->to || kmax < seqs->from || kmin > kmax)
    return 0;
  return (kmax - seqs->from) / seqs->step -
         (kmin - seqs->from + seqs->step - 1) / seqs->step + 1;
}


int ha_seq::open(const char *name, int mode, uint test_if_locked)
{
  if (!(seqs= get_share()))
    return HA_ERR_OUT_OF_MEM;
  DBUG_ASSERT(my_strcasecmp(table_alias_charset, name, seqs->name) == 0);

  ref_length= sizeof(cur);
  thr_lock_data_init(&seqs->lock,&lock,NULL);
  return 0;
}

int ha_seq::close(void)
{
  return 0;
}

static handler *create_handler(handlerton *hton, TABLE_SHARE *table,
                               MEM_ROOT *mem_root)
{
  return new (mem_root) ha_seq(hton, table);
}


static bool parse_table_name(const char *name, size_t name_length,
                             ulonglong *from, ulonglong *to, ulonglong *step)
{
  uint n0=0, n1= 0, n2= 0;
  *step= 1;

  // the table is discovered if its name matches the pattern of seq_1_to_10 or 
  // seq_1_to_10_step_3
  sscanf(name, "seq_%llu_to_%n%llu%n_step_%llu%n",
         from, &n0, to, &n1, step, &n2);
  // I consider this a bug in sscanf() - when an unsigned number
  // is requested, -5 should *not* be accepted. But is is :(
  // hence the additional check below:
  return
    n0 == 0 || !isdigit(name[4]) || !isdigit(name[n0]) || // reject negative numbers
    (n1 != name_length && n2 != name_length);
}


Sequence_share *ha_seq::get_share()
{
  Sequence_share *tmp_share;
  lock_shared_ha_data();
  if (!(tmp_share= static_cast<Sequence_share*>(get_ha_share_ptr())))
  {
    bool reverse;
    ulonglong from, to, step;

    parse_table_name(table_share->table_name.str,
                     table_share->table_name.length, &from, &to, &step);
    
    if ((reverse = from > to))
    {
      if (step > from - to)
        to = from;
      else
        swap_variables(ulonglong, from, to);
      /*
        when keyread is allowed, optimizer will always prefer an index to a
        table scan for our tables, and we'll never see the range reversed.
      */
      table_share->keys_for_keyread.clear_all();
    }

    to= (to - from) / step * step + step + from;

    tmp_share= new Sequence_share(table_share->normalized_path.str, from, to, step, reverse);

    if (!tmp_share)
      goto err;
    set_ha_share_ptr(static_cast<Handler_share*>(tmp_share));
  }
err:
  unlock_shared_ha_data();
  return tmp_share;
}


static int discover_table(handlerton *hton, THD *thd, TABLE_SHARE *share)
{
  ulonglong from, to, step;
  if (parse_table_name(share->table_name.str, share->table_name.length,
                       &from, &to, &step))
    return HA_ERR_NO_SUCH_TABLE;

  if (step == 0)
    return HA_WRONG_CREATE_OPTION;

  const char *sql="create table seq (seq bigint unsigned primary key)";
  return share->init_from_sql_statement_string(thd, 0, sql, strlen(sql));
}


static int discover_table_existence(handlerton *hton, const char *db,
                                    const char *table_name)
{
  ulonglong from, to, step;
  return !parse_table_name(table_name, strlen(table_name), &from, &to, &step);
}

static int dummy_ret_int() { return 0; }

static int init(void *p)
{
  handlerton *hton= (handlerton *)p;
  hton->create= create_handler;
  hton->discover_table= discover_table;
  hton->discover_table_existence= discover_table_existence;
  hton->commit= hton->rollback= hton->prepare=
   (int (*)(handlerton *, THD *, bool)) &dummy_ret_int;
  hton->savepoint_set= hton->savepoint_rollback= hton->savepoint_release=
   (int  (*)(handlerton *, THD *, void *)) &dummy_ret_int;
    
  return 0;
}

static struct st_mysql_storage_engine descriptor =
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

maria_declare_plugin(sequence)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &descriptor,
  "SEQUENCE",
  "Sergei Golubchik",
  "Generated tables filled with sequential values",
  PLUGIN_LICENSE_GPL,
  init,
  NULL,
  0x0100,
  NULL,
  NULL,
  "0.1",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
}
maria_declare_plugin_end;

