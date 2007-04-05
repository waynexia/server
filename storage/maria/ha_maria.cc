/* Copyright (C) 2006,2004 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

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


#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation                          // gcc: Class implementation
#endif

#define MYSQL_SERVER 1
#include "mysql_priv.h"
#include <mysql/plugin.h>
#include <m_ctype.h>
#include <myisampack.h>
#include <my_bit.h>
#include "ha_maria.h"

#include "maria_def.h"
#include "ma_rt_index.h"

ulong maria_recover_options= HA_RECOVER_NONE;

/* bits in maria_recover_options */
const char *maria_recover_names[]=
{
  "DEFAULT", "BACKUP", "FORCE", "QUICK", NullS
};
TYPELIB maria_recover_typelib=
{
  array_elements(maria_recover_names) - 1, "",
  maria_recover_names, NULL
};

const char *maria_stats_method_names[]=
{
  "nulls_unequal", "nulls_equal",
  "nulls_ignored", NullS
};
TYPELIB maria_stats_method_typelib=
{
  array_elements(maria_stats_method_names) - 1, "",
  maria_stats_method_names, NULL
};


/*****************************************************************************
** MARIA tables
*****************************************************************************/

static handler *maria_create_handler(handlerton *hton,
                                     TABLE_SHARE * table,
                                     MEM_ROOT *mem_root)
{
  return new (mem_root) ha_maria(hton, table);
}


// collect errors printed by maria_check routines

static void _ma_check_print_msg(HA_CHECK *param, const char *msg_type,
                                const char *fmt, va_list args)
{
  THD *thd= (THD *) param->thd;
  Protocol *protocol= thd->protocol;
  uint length, msg_length;
  char msgbuf[MARIA_MAX_MSG_BUF];
  char name[NAME_LEN * 2 + 2];

  msg_length= my_vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
  msgbuf[sizeof(msgbuf) - 1]= 0;                // healthy paranoia

  DBUG_PRINT(msg_type, ("message: %s", msgbuf));

  if (!thd->vio_ok())
  {
    sql_print_error(msgbuf);
    return;
  }

  if (param->testflag &
      (T_CREATE_MISSING_KEYS | T_SAFE_REPAIR | T_AUTO_REPAIR))
  {
    my_message(ER_NOT_KEYFILE, msgbuf, MYF(MY_WME));
    return;
  }
  length= (uint) (strxmov(name, param->db_name, ".", param->table_name,
                          NullS) - name);
  protocol->prepare_for_resend();
  protocol->store(name, length, system_charset_info);
  protocol->store(param->op_name, system_charset_info);
  protocol->store(msg_type, system_charset_info);
  protocol->store(msgbuf, msg_length, system_charset_info);
  if (protocol->write())
    sql_print_error("Failed on my_net_write, writing to stderr instead: %s\n",
                    msgbuf);
  return;
}



/*
  Convert TABLE object to Maria key and column definition

  SYNOPSIS
    table2maria()
      table_arg   in     TABLE object.
      keydef_out  out    Maria key definition.
      recinfo_out out    Maria column definition.
      records_out out    Number of fields.

  DESCRIPTION
    This function will allocate and initialize Maria key and column
    definition for further use in ma_create or for a check for underlying
    table conformance in merge engine.

  RETURN VALUE
    0  OK
    # error code
*/

int table2maria(TABLE *table_arg, MARIA_KEYDEF **keydef_out,
                MARIA_COLUMNDEF **recinfo_out, uint *records_out)
{
  uint i, j, recpos, minpos, fieldpos, temp_length, length;
  enum ha_base_keytype type= HA_KEYTYPE_BINARY;
  byte *record;
  KEY *pos;
  MARIA_KEYDEF *keydef;
  MARIA_COLUMNDEF *recinfo, *recinfo_pos;
  HA_KEYSEG *keyseg;
  TABLE_SHARE *share= table_arg->s;
  uint options= share->db_options_in_use;
  DBUG_ENTER("table2maria");

  if (!(my_multi_malloc(MYF(MY_WME),
          recinfo_out, (share->fields * 2 + 2) * sizeof(MARIA_COLUMNDEF),
          keydef_out, share->keys * sizeof(MARIA_KEYDEF),
          &keyseg,
          (share->key_parts + share->keys) * sizeof(HA_KEYSEG),
          NullS)))
    DBUG_RETURN(HA_ERR_OUT_OF_MEM); /* purecov: inspected */
  keydef= *keydef_out;
  recinfo= *recinfo_out;
  pos= table_arg->key_info;
  for (i= 0; i < share->keys; i++, pos++)
  {
    keydef[i].flag= (pos->flags & (HA_NOSAME | HA_FULLTEXT | HA_SPATIAL));
    keydef[i].key_alg= pos->algorithm == HA_KEY_ALG_UNDEF ?
      (pos->flags & HA_SPATIAL ? HA_KEY_ALG_RTREE : HA_KEY_ALG_BTREE) :
      pos->algorithm;
    keydef[i].block_length= pos->block_size;
    keydef[i].seg= keyseg;
    keydef[i].keysegs= pos->key_parts;
    for (j= 0; j < pos->key_parts; j++)
    {
      Field *field= pos->key_part[j].field;
      type= field->key_type();
      keydef[i].seg[j].flag= pos->key_part[j].key_part_flag;

      if (options & HA_OPTION_PACK_KEYS ||
          (pos->flags & (HA_PACK_KEY | HA_BINARY_PACK_KEY |
                         HA_SPACE_PACK_USED)))
      {
        if (pos->key_part[j].length > 8 &&
            (type == HA_KEYTYPE_TEXT ||
             type == HA_KEYTYPE_NUM ||
             (type == HA_KEYTYPE_BINARY && !field->zero_pack())))
        {
          /* No blobs here */
          if (j == 0)
            keydef[i].flag|= HA_PACK_KEY;
          if (!(field->flags & ZEROFILL_FLAG) &&
              (field->type() == MYSQL_TYPE_STRING ||
               field->type() == MYSQL_TYPE_VAR_STRING ||
               ((int) (pos->key_part[j].length - field->decimals())) >= 4))
            keydef[i].seg[j].flag|= HA_SPACE_PACK;
        }
        else if (j == 0 && (!(pos->flags & HA_NOSAME) || pos->key_length > 16))
          keydef[i].flag|= HA_BINARY_PACK_KEY;
      }
      keydef[i].seg[j].type= (int) type;
      keydef[i].seg[j].start= pos->key_part[j].offset;
      keydef[i].seg[j].length= pos->key_part[j].length;
      keydef[i].seg[j].bit_start= keydef[i].seg[j].bit_end=
        keydef[i].seg[j].bit_length= 0;
      keydef[i].seg[j].bit_pos= 0;
      keydef[i].seg[j].language= field->charset()->number;

      if (field->null_ptr)
      {
        keydef[i].seg[j].null_bit= field->null_bit;
        keydef[i].seg[j].null_pos= (uint) (field->null_ptr-
                                           (uchar*) table_arg->record[0]);
      }
      else
      {
        keydef[i].seg[j].null_bit= 0;
        keydef[i].seg[j].null_pos= 0;
      }
      if (field->type() == MYSQL_TYPE_BLOB ||
          field->type() == MYSQL_TYPE_GEOMETRY)
      {
        keydef[i].seg[j].flag|= HA_BLOB_PART;
        /* save number of bytes used to pack length */
        keydef[i].seg[j].bit_start= (uint) (field->pack_length() -
                                            share->blob_ptr_size);
      }
      else if (field->type() == MYSQL_TYPE_BIT)
      {
        keydef[i].seg[j].bit_length= ((Field_bit *) field)->bit_len;
        keydef[i].seg[j].bit_start= ((Field_bit *) field)->bit_ofs;
        keydef[i].seg[j].bit_pos= (uint) (((Field_bit *) field)->bit_ptr -
                                          (uchar*) table_arg->record[0]);
      }
    }
    keyseg+= pos->key_parts;
  }
  if (table_arg->found_next_number_field)
    keydef[share->next_number_index].flag|= HA_AUTO_KEY;
  record= table_arg->record[0];
  recpos= 0;
  recinfo_pos= recinfo;
  while (recpos < (uint) share->reclength)
  {
    Field **field, *found= 0;
    minpos= share->reclength;
    length= 0;

    for (field= table_arg->field; *field; field++)
    {
      if ((fieldpos= (*field)->offset(record)) >= recpos &&
          fieldpos <= minpos)
      {
        /* skip null fields */
        if (!(temp_length= (*field)->pack_length_in_rec()))
          continue; /* Skip null-fields */
        if (! found || fieldpos < minpos ||
            (fieldpos == minpos && temp_length < length))
        {
          minpos= fieldpos;
          found= *field;
          length= temp_length;
        }
      }
    }
    DBUG_PRINT("loop", ("found: 0x%lx  recpos: %d  minpos: %d  length: %d",
                        (long) found, recpos, minpos, length));
    if (recpos != minpos)
    { // Reserved space (Null bits?)
      bzero((char*) recinfo_pos, sizeof(*recinfo_pos));
      recinfo_pos->type= FIELD_NORMAL;
      recinfo_pos++->length= (uint16) (minpos - recpos);
    }
    if (!found)
      break;

    if (found->flags & BLOB_FLAG)
      recinfo_pos->type= FIELD_BLOB;
    else if (found->type() == MYSQL_TYPE_VARCHAR)
      recinfo_pos->type= FIELD_VARCHAR;
    else if (!(options & HA_OPTION_PACK_RECORD) ||
             (found->zero_pack() && (found->flags & PRI_KEY_FLAG)))
      recinfo_pos->type= FIELD_NORMAL;
    else if (found->zero_pack())
      recinfo_pos->type= FIELD_SKIP_ZERO;
    else
      recinfo_pos->type= ((length <= 3 ||
                           (found->flags & ZEROFILL_FLAG)) ?
                          FIELD_NORMAL :
                          found->type() == MYSQL_TYPE_STRING ||
                          found->type() == MYSQL_TYPE_VAR_STRING ?
                          FIELD_SKIP_ENDSPACE :
                          FIELD_SKIP_PRESPACE);
    if (found->null_ptr)
    {
      recinfo_pos->null_bit= found->null_bit;
      recinfo_pos->null_pos= (uint) (found->null_ptr -
                                     (uchar*) table_arg->record[0]);
    }
    else
    {
      recinfo_pos->null_bit= 0;
      recinfo_pos->null_pos= 0;
    }
    (recinfo_pos++)->length= (uint16) length;
    recpos= minpos + length;
    DBUG_PRINT("loop", ("length: %d  type: %d",
                        recinfo_pos[-1].length,recinfo_pos[-1].type));
  }
  *records_out= (uint) (recinfo_pos - recinfo);
  DBUG_RETURN(0);
}


/*
  Check for underlying table conformance

  SYNOPSIS
    maria_check_definition()
      t1_keyinfo       in    First table key definition
      t1_recinfo       in    First table record definition
      t1_keys          in    Number of keys in first table
      t1_recs          in    Number of records in first table
      t2_keyinfo       in    Second table key definition
      t2_recinfo       in    Second table record definition
      t2_keys          in    Number of keys in second table
      t2_recs          in    Number of records in second table
      strict           in    Strict check switch

  DESCRIPTION
    This function compares two Maria definitions. By intention it was done
    to compare merge table definition against underlying table definition.
    It may also be used to compare dot-frm and MAI definitions of Maria
    table as well to compare different Maria table definitions.

    For merge table it is not required that number of keys in merge table
    must exactly match number of keys in underlying table. When calling this
    function for underlying table conformance check, 'strict' flag must be
    set to false, and converted merge definition must be passed as t1_*.

    Otherwise 'strict' flag must be set to 1 and it is not required to pass
    converted dot-frm definition as t1_*.

  RETURN VALUE
    0 - Equal definitions.
    1 - Different definitions.

  NOTES
    This is currently not used. In MyISAM the corresponding function
    (myisam_check_definition()) is used only by MERGE tables
    (in ha_myisammrg.cc).
*/
int maria_check_definition(MARIA_KEYDEF *t1_keyinfo,
                           MARIA_COLUMNDEF *t1_recinfo,
                           uint t1_keys, uint t1_recs,
                           MARIA_KEYDEF *t2_keyinfo,
                           MARIA_COLUMNDEF *t2_recinfo,
                           uint t2_keys, uint t2_recs, bool strict)
{
  uint i, j;
  DBUG_ENTER("maria_check_definition");
  if ((strict ? t1_keys != t2_keys : t1_keys > t2_keys))
  {
    DBUG_PRINT("error", ("Number of keys differs: t1_keys=%u, t2_keys=%u",
                         t1_keys, t2_keys));
    DBUG_RETURN(1);
  }
  if (t1_recs != t2_recs)
  {
    DBUG_PRINT("error", ("Number of recs differs: t1_recs=%u, t2_recs=%u",
                         t1_recs, t2_recs));
    DBUG_RETURN(1);
  }
  for (i= 0; i < t1_keys; i++)
  {
    HA_KEYSEG *t1_keysegs= t1_keyinfo[i].seg;
    HA_KEYSEG *t2_keysegs= t2_keyinfo[i].seg;
    if (t1_keyinfo[i].keysegs != t2_keyinfo[i].keysegs ||
        t1_keyinfo[i].key_alg != t2_keyinfo[i].key_alg)
    {
      DBUG_PRINT("error", ("Key %d has different definition", i));
      DBUG_PRINT("error", ("t1_keysegs=%d, t1_key_alg=%d",
                           t1_keyinfo[i].keysegs, t1_keyinfo[i].key_alg));
      DBUG_PRINT("error", ("t2_keysegs=%d, t2_key_alg=%d",
                           t2_keyinfo[i].keysegs, t2_keyinfo[i].key_alg));
      DBUG_RETURN(1);
    }
    for (j=  t1_keyinfo[i].keysegs; j--;)
    {
      if (t1_keysegs[j].type != t2_keysegs[j].type ||
          t1_keysegs[j].language != t2_keysegs[j].language ||
          t1_keysegs[j].null_bit != t2_keysegs[j].null_bit ||
          t1_keysegs[j].length != t2_keysegs[j].length)
      {
        DBUG_PRINT("error", ("Key segment %d (key %d) has different "
                             "definition", j, i));
        DBUG_PRINT("error", ("t1_type=%d, t1_language=%d, t1_null_bit=%d, "
                             "t1_length=%d",
                             t1_keysegs[j].type, t1_keysegs[j].language,
                             t1_keysegs[j].null_bit, t1_keysegs[j].length));
        DBUG_PRINT("error", ("t2_type=%d, t2_language=%d, t2_null_bit=%d, "
                             "t2_length=%d",
                             t2_keysegs[j].type, t2_keysegs[j].language,
                             t2_keysegs[j].null_bit, t2_keysegs[j].length));

        DBUG_RETURN(1);
      }
    }
  }
  for (i= 0; i < t1_recs; i++)
  {
    MARIA_COLUMNDEF *t1_rec= &t1_recinfo[i];
    MARIA_COLUMNDEF *t2_rec= &t2_recinfo[i];
    if (t1_rec->type != t2_rec->type ||
        t1_rec->length != t2_rec->length ||
        t1_rec->null_bit != t2_rec->null_bit)
    {
      DBUG_PRINT("error", ("Field %d has different definition", i));
      DBUG_PRINT("error", ("t1_type=%d, t1_length=%d, t1_null_bit=%d",
                           t1_rec->type, t1_rec->length, t1_rec->null_bit));
      DBUG_PRINT("error", ("t2_type=%d, t2_length=%d, t2_null_bit=%d",
                           t2_rec->type, t2_rec->length, t2_rec->null_bit));
      DBUG_RETURN(1);
    }
  }
  DBUG_RETURN(0);
}


extern "C" {

volatile int *_ma_killed_ptr(HA_CHECK *param)
{
  /* In theory Unsafe conversion, but should be ok for now */
  return (int*) &(((THD *) (param->thd))->killed);
}


void _ma_check_print_error(HA_CHECK *param, const char *fmt, ...)
{
  param->error_printed |= 1;
  param->out_flag |= O_DATA_LOST;
  va_list args;
  va_start(args, fmt);
  _ma_check_print_msg(param, "error", fmt, args);
  va_end(args);
}


void _ma_check_print_info(HA_CHECK *param, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  _ma_check_print_msg(param, "info", fmt, args);
  va_end(args);
}


void _ma_check_print_warning(HA_CHECK *param, const char *fmt, ...)
{
  param->warning_printed= 1;
  param->out_flag |= O_DATA_LOST;
  va_list args;
  va_start(args, fmt);
  _ma_check_print_msg(param, "warning", fmt, args);
  va_end(args);
}

}


ha_maria::ha_maria(handlerton *hton, TABLE_SHARE *table_arg):
handler(hton, table_arg), file(0),
int_table_flags(HA_NULL_IN_KEY | HA_CAN_FULLTEXT | HA_CAN_SQL_HANDLER |
                HA_DUPLICATE_POS | HA_CAN_INDEX_BLOBS | HA_AUTO_PART_KEY |
                HA_FILE_BASED | HA_CAN_GEOMETRY | HA_NO_TRANSACTIONS |
                HA_CAN_INSERT_DELAYED | HA_CAN_BIT_FIELD | HA_CAN_RTREEKEYS |
                HA_HAS_RECORDS | HA_STATS_RECORDS_IS_EXACT),
can_enable_indexes(1)
{}


handler *ha_maria::clone(MEM_ROOT *mem_root)
{
  ha_maria *new_handler= static_cast <ha_maria *>(handler::clone(mem_root));
  if (new_handler)
    new_handler->file->state= file->state;
  return new_handler;
}


static const char *ha_maria_exts[]=
{
  MARIA_NAME_IEXT,
  MARIA_NAME_DEXT,
  NullS
};


const char **ha_maria::bas_ext() const
{
  return ha_maria_exts;
}


const char *ha_maria::index_type(uint key_number)
{
  return ((table->key_info[key_number].flags & HA_FULLTEXT) ?
          "FULLTEXT" :
          (table->key_info[key_number].flags & HA_SPATIAL) ?
          "SPATIAL" :
          (table->key_info[key_number].algorithm == HA_KEY_ALG_RTREE) ?
          "RTREE" : "BTREE");
}


double ha_maria::scan_time()
{
  if (file->s->data_file_type == BLOCK_RECORD)
    return ulonglong2double(stats.data_file_length - file->s->block_size) / max(file->s->block_size / 2, IO_SIZE) + 2;
  return handler::scan_time();
}


#ifdef HAVE_REPLICATION
int ha_maria::net_read_dump(NET * net)
{
  int data_fd= file->dfile;
  int error= 0;

  my_seek(data_fd, 0L, MY_SEEK_SET, MYF(MY_WME));
  for (;;)
  {
    ulong packet_len= my_net_read(net);
    if (!packet_len)
      break;                                    // end of file
    if (packet_len == packet_error)
    {
      sql_print_error("ha_maria::net_read_dump - read error ");
      error= -1;
      goto err;
    }
    if (my_write(data_fd, (byte *) net->read_pos, (uint) packet_len,
                 MYF(MY_WME | MY_FNABP)))
    {
      error= errno;
      goto err;
    }
  }
err:
  return error;
}


int ha_maria::dump(THD * thd, int fd)
{
  MARIA_SHARE *share= file->s;
  NET *net= &thd->net;
  uint block_size= share->block_size;
  my_off_t bytes_to_read= share->state.state.data_file_length;
  int data_fd= file->dfile;
  byte *buf= (byte *) my_malloc(block_size, MYF(MY_WME));
  if (!buf)
    return ENOMEM;

  int error= 0;
  my_seek(data_fd, 0L, MY_SEEK_SET, MYF(MY_WME));
  for (; bytes_to_read > 0;)
  {
    uint bytes= my_read(data_fd, buf, block_size, MYF(MY_WME));
    if (bytes == MY_FILE_ERROR)
    {
      error= errno;
      goto err;
    }

    if (fd >= 0)
    {
      if (my_write(fd, buf, bytes, MYF(MY_WME | MY_FNABP)))
      {
        error= errno ? errno : EPIPE;
        goto err;
      }
    }
    else
    {
      if (my_net_write(net, (char*) buf, bytes))
      {
        error= errno ? errno : EPIPE;
        goto err;
      }
    }
    bytes_to_read -= bytes;
  }

  if (fd < 0)
  {
    if (my_net_write(net, "", 0))
      error= errno ? errno : EPIPE;
    net_flush(net);
  }

err:
  my_free((gptr) buf, MYF(0));
  return error;
}
#endif                                          /* HAVE_REPLICATION */


bool ha_maria::check_if_locking_is_allowed(uint sql_command,
                                           ulong type, TABLE *table,
                                           uint count,
                                           bool called_by_privileged_thread)
{
  /*
     To be able to open and lock for reading system tables like 'mysql.proc',
     when we already have some tables opened and locked, and avoid deadlocks
     we have to disallow write-locking of these tables with any other tables.
  */
  if (table->s->system_table &&
      table->reginfo.lock_type >= TL_WRITE_ALLOW_WRITE && count != 1)
  {
    my_error(ER_WRONG_LOCK_OF_SYSTEM_TABLE, MYF(0), table->s->db.str,
             table->s->table_name.str);
    return FALSE;
  }

  /*
    Deny locking of the log tables, which is incompatible with
    concurrent insert. Unless called from a logger THD (general_log_thd
    or slow_log_thd) or by a privileged thread.
  */
  if (!called_by_privileged_thread)
    return check_if_log_table_locking_is_allowed(sql_command, type, table);

  return TRUE;
}


        /* Name is here without an extension */

int ha_maria::open(const char *name, int mode, uint test_if_locked)
{
  uint i;

#ifdef NOT_USED
  /*
    If the user wants to have memory mapped data files, add an
    open_flag. Do not memory map temporary tables because they are
    expected to be inserted and thus extended a lot. Memory mapping is
    efficient for files that keep their size, but very inefficient for
    growing files. Using an open_flag instead of calling ma_extra(...
    HA_EXTRA_MMAP ...) after maxs_open() has the advantage that the
    mapping is not repeated for every open, but just done on the initial
    open, when the MyISAM share is created. Everytime the server
    requires to open a new instance of a table it calls this method. We
    will always supply HA_OPEN_MMAP for a permanent table. However, the
    Maria storage engine will ignore this flag if this is a secondary
    open of a table that is in use by other threads already (if the
    Maria share exists already).
  */
  if (!(test_if_locked & HA_OPEN_TMP_TABLE) && opt_maria_use_mmap)
    test_if_locked|= HA_OPEN_MMAP;
#endif

  if (!(file= maria_open(name, mode, test_if_locked | HA_OPEN_FROM_SQL_LAYER)))
    return (my_errno ? my_errno : -1);

  if (test_if_locked & (HA_OPEN_IGNORE_IF_LOCKED | HA_OPEN_TMP_TABLE))
    VOID(maria_extra(file, HA_EXTRA_NO_WAIT_LOCK, 0));

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);
  if (!(test_if_locked & HA_OPEN_WAIT_IF_LOCKED))
    VOID(maria_extra(file, HA_EXTRA_WAIT_LOCK, 0));
  if (file->s->data_file_type != STATIC_RECORD)
    int_table_flags |= HA_REC_NOT_IN_SEQ;
  if (file->s->options & (HA_OPTION_CHECKSUM | HA_OPTION_COMPRESS_RECORD))
    int_table_flags |= HA_HAS_CHECKSUM;

  for (i= 0; i < table->s->keys; i++)
  {
    struct st_plugin_int *parser= table->key_info[i].parser;
    if (table->key_info[i].flags & HA_USES_PARSER)
      file->s->keyinfo[i].parser=
        (struct st_mysql_ftparser *) parser->plugin->info;
    table->key_info[i].block_size= file->s->keyinfo[i].block_length;
  }
  return (0);
}


int ha_maria::close(void)
{
  MARIA_HA *tmp= file;
  file= 0;
  return maria_close(tmp);
}


int ha_maria::write_row(byte * buf)
{
  statistic_increment(table->in_use->status_var.ha_write_count, &LOCK_status);

  /* If we have a timestamp column, update it to the current time */
  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_INSERT)
    table->timestamp_field->set_time();

  /*
     If we have an auto_increment column and we are writing a changed row
     or a new row, then update the auto_increment value in the record.
  */
  if (table->next_number_field && buf == table->record[0])
  {
    int error;
    if ((error= update_auto_increment()))
      return error;
  }
  return maria_write(file, buf);
}


int ha_maria::check(THD * thd, HA_CHECK_OPT * check_opt)
{
  if (!file)
    return HA_ADMIN_INTERNAL_ERROR;
  int error;
  HA_CHECK param;
  MARIA_SHARE *share= file->s;
  const char *old_proc_info= thd->proc_info;

  thd->proc_info= "Checking table";
  maria_chk_init(&param);
  param.thd= thd;
  param.op_name= "check";
  param.db_name= table->s->db.str;
  param.table_name= table->alias;
  param.testflag= check_opt->flags | T_CHECK | T_SILENT;
  param.stats_method= (enum_handler_stats_method) thd->variables.
    maria_stats_method;

  if (!(table->db_stat & HA_READ_ONLY))
    param.testflag |= T_STATISTICS;
  param.using_global_keycache= 1;

  if (!maria_is_crashed(file) &&
      (((param.testflag & T_CHECK_ONLY_CHANGED) &&
        !(share->state.changed & (STATE_CHANGED | STATE_CRASHED |
                                  STATE_CRASHED_ON_REPAIR)) &&
        share->state.open_count == 0) ||
       ((param.testflag & T_FAST) && (share->state.open_count ==
                                      (uint) (share->global_changed ? 1 :
                                              0)))))
    return HA_ADMIN_ALREADY_DONE;

  error= maria_chk_status(&param, file);                // Not fatal
  error= maria_chk_size(&param, file);
  if (!error)
    error |= maria_chk_del(&param, file, param.testflag);
  if (!error)
    error= maria_chk_key(&param, file);
  if (!error)
  {
    if ((!(param.testflag & T_QUICK) &&
         ((share->options &
           (HA_OPTION_PACK_RECORD | HA_OPTION_COMPRESS_RECORD)) ||
          (param.testflag & (T_EXTEND | T_MEDIUM)))) || maria_is_crashed(file))
    {
      uint old_testflag= param.testflag;
      param.testflag |= T_MEDIUM;
      if (!(error= init_io_cache(&param.read_cache, file->dfile,
                                 my_default_record_cache_size, READ_CACHE,
                                 share->pack.header_length, 1, MYF(MY_WME))))
      {
        error= maria_chk_data_link(&param, file, param.testflag & T_EXTEND);
        end_io_cache(&(param.read_cache));
      }
      param.testflag= old_testflag;
    }
  }
  if (!error)
  {
    if ((share->state.changed & (STATE_CHANGED |
                                 STATE_CRASHED_ON_REPAIR |
                                 STATE_CRASHED | STATE_NOT_ANALYZED)) ||
        (param.testflag & T_STATISTICS) || maria_is_crashed(file))
    {
      file->update |= HA_STATE_CHANGED | HA_STATE_ROW_CHANGED;
      pthread_mutex_lock(&share->intern_lock);
      share->state.changed &= ~(STATE_CHANGED | STATE_CRASHED |
                                STATE_CRASHED_ON_REPAIR);
      if (!(table->db_stat & HA_READ_ONLY))
        error= maria_update_state_info(&param, file, UPDATE_TIME | UPDATE_OPEN_COUNT |
                                 UPDATE_STAT);
      pthread_mutex_unlock(&share->intern_lock);
      info(HA_STATUS_NO_LOCK | HA_STATUS_TIME | HA_STATUS_VARIABLE |
           HA_STATUS_CONST);
    }
  }
  else if (!maria_is_crashed(file) && !thd->killed)
  {
    maria_mark_crashed(file);
    file->update |= HA_STATE_CHANGED | HA_STATE_ROW_CHANGED;
  }

  thd->proc_info= old_proc_info;
  return error ? HA_ADMIN_CORRUPT : HA_ADMIN_OK;
}


/*
  Analyze the key distribution in the table
  As the table may be only locked for read, we have to take into account that
  two threads may do an analyze at the same time!
*/

int ha_maria::analyze(THD *thd, HA_CHECK_OPT * check_opt)
{
  int error= 0;
  HA_CHECK param;
  MARIA_SHARE *share= file->s;

  maria_chk_init(&param);
  param.thd= thd;
  param.op_name= "analyze";
  param.db_name= table->s->db.str;
  param.table_name= table->alias;
  param.testflag= (T_FAST | T_CHECK | T_SILENT | T_STATISTICS |
                   T_DONT_CHECK_CHECKSUM);
  param.using_global_keycache= 1;
  param.stats_method= (enum_handler_stats_method) thd->variables.
    maria_stats_method;

  if (!(share->state.changed & STATE_NOT_ANALYZED))
    return HA_ADMIN_ALREADY_DONE;

  error= maria_chk_key(&param, file);
  if (!error)
  {
    pthread_mutex_lock(&share->intern_lock);
    error= maria_update_state_info(&param, file, UPDATE_STAT);
    pthread_mutex_unlock(&share->intern_lock);
  }
  else if (!maria_is_crashed(file) && !thd->killed)
    maria_mark_crashed(file);
  return error ? HA_ADMIN_CORRUPT : HA_ADMIN_OK;
}


int ha_maria::restore(THD * thd, HA_CHECK_OPT *check_opt)
{
  HA_CHECK_OPT tmp_check_opt;
  char *backup_dir= thd->lex->backup_dir;
  char src_path[FN_REFLEN], dst_path[FN_REFLEN];
  char table_name[FN_REFLEN];
  int error;
  const char *errmsg;
  DBUG_ENTER("restore");

  VOID(tablename_to_filename(table->s->table_name.str, table_name,
                             sizeof(table_name)));

  if (fn_format_relative_to_data_home(src_path, table_name, backup_dir,
                                      MARIA_NAME_DEXT))
    DBUG_RETURN(HA_ADMIN_INVALID);

  strxmov(dst_path, table->s->normalized_path.str, MARIA_NAME_DEXT, NullS);
  if (my_copy(src_path, dst_path, MYF(MY_WME)))
  {
    error= HA_ADMIN_FAILED;
    errmsg= "Failed in my_copy (Error %d)";
    goto err;
  }

  tmp_check_opt.init();
  tmp_check_opt.flags |= T_VERY_SILENT | T_CALC_CHECKSUM | T_QUICK;
  DBUG_RETURN(repair(thd, &tmp_check_opt));

err:
  {
    HA_CHECK param;
    maria_chk_init(&param);
    param.thd= thd;
    param.op_name= "restore";
    param.db_name= table->s->db.str;
    param.table_name= table->s->table_name.str;
    param.testflag= 0;
    _ma_check_print_error(&param, errmsg, my_errno);
    DBUG_RETURN(error);
  }
}


int ha_maria::backup(THD * thd, HA_CHECK_OPT *check_opt)
{
  char *backup_dir= thd->lex->backup_dir;
  char src_path[FN_REFLEN], dst_path[FN_REFLEN];
  char table_name[FN_REFLEN];
  int error;
  const char *errmsg;
  DBUG_ENTER("ha_maria::backup");

  VOID(tablename_to_filename(table->s->table_name.str, table_name,
                             sizeof(table_name)));

  if (fn_format_relative_to_data_home(dst_path, table_name, backup_dir,
                                      reg_ext))
  {
    errmsg= "Failed in fn_format() for .frm file (errno: %d)";
    error= HA_ADMIN_INVALID;
    goto err;
  }

  strxmov(src_path, table->s->normalized_path.str, reg_ext, NullS);
  if (my_copy(src_path, dst_path,
              MYF(MY_WME | MY_HOLD_ORIGINAL_MODES | MY_DONT_OVERWRITE_FILE)))
  {
    error= HA_ADMIN_FAILED;
    errmsg= "Failed copying .frm file (errno: %d)";
    goto err;
  }

  /* Change extension */
  if (fn_format_relative_to_data_home(dst_path, table_name, backup_dir,
                                      MARIA_NAME_DEXT))
  {
    errmsg= "Failed in fn_format() for .MYD file (errno: %d)";
    error= HA_ADMIN_INVALID;
    goto err;
  }

  strxmov(src_path, table->s->normalized_path.str, MARIA_NAME_DEXT, NullS);
  if (my_copy(src_path, dst_path,
              MYF(MY_WME | MY_HOLD_ORIGINAL_MODES | MY_DONT_OVERWRITE_FILE)))
  {
    errmsg= "Failed copying .MYD file (errno: %d)";
    error= HA_ADMIN_FAILED;
    goto err;
  }
  DBUG_RETURN(HA_ADMIN_OK);

err:
  {
    HA_CHECK param;
    maria_chk_init(&param);
    param.thd= thd;
    param.op_name= "backup";
    param.db_name= table->s->db.str;
    param.table_name= table->s->table_name.str;
    param.testflag= 0;
    _ma_check_print_error(&param, errmsg, my_errno);
    DBUG_RETURN(error);
  }
}


int ha_maria::repair(THD * thd, HA_CHECK_OPT *check_opt)
{
  int error;
  HA_CHECK param;
  ha_rows start_records;

  if (!file)
    return HA_ADMIN_INTERNAL_ERROR;

  maria_chk_init(&param);
  param.thd= thd;
  param.op_name= "repair";
  param.testflag= ((check_opt->flags & ~(T_EXTEND)) |
                   T_SILENT | T_FORCE_CREATE | T_CALC_CHECKSUM |
                   (check_opt->flags & T_EXTEND ? T_REP : T_REP_BY_SORT));
  param.sort_buffer_length= check_opt->sort_buffer_size;
  start_records= file->state->records;
  while ((error= repair(thd, param, 0)) && param.retry_repair)
  {
    param.retry_repair= 0;
    if (test_all_bits(param.testflag,
                      (uint) (T_RETRY_WITHOUT_QUICK | T_QUICK)))
    {
      param.testflag &= ~T_RETRY_WITHOUT_QUICK;
      sql_print_information("Retrying repair of: '%s' without quick",
                            table->s->path.str);
      continue;
    }
    param.testflag &= ~T_QUICK;
    if ((param.testflag & T_REP_BY_SORT))
    {
      param.testflag= (param.testflag & ~T_REP_BY_SORT) | T_REP;
      sql_print_information("Retrying repair of: '%s' with keycache",
                            table->s->path.str);
      continue;
    }
    break;
  }
  if (!error && start_records != file->state->records &&
      !(check_opt->flags & T_VERY_SILENT))
  {
    char llbuff[22], llbuff2[22];
    sql_print_information("Found %s of %s rows when repairing '%s'",
                          llstr(file->state->records, llbuff),
                          llstr(start_records, llbuff2),
                          table->s->path.str);
  }
  return error;
}

int ha_maria::optimize(THD * thd, HA_CHECK_OPT *check_opt)
{
  int error;
  if (!file)
    return HA_ADMIN_INTERNAL_ERROR;
  HA_CHECK param;

  maria_chk_init(&param);
  param.thd= thd;
  param.op_name= "optimize";
  param.testflag= (check_opt->flags | T_SILENT | T_FORCE_CREATE |
                   T_REP_BY_SORT | T_STATISTICS | T_SORT_INDEX);
  param.sort_buffer_length= check_opt->sort_buffer_size;
  if ((error= repair(thd, param, 1)) && param.retry_repair)
  {
    sql_print_warning("Warning: Optimize table got errno %d, retrying",
                      my_errno);
    param.testflag &= ~T_REP_BY_SORT;
    error= repair(thd, param, 1);
  }
  return error;
}


int ha_maria::repair(THD *thd, HA_CHECK &param, bool do_optimize)
{
  int error= 0;
  uint local_testflag= param.testflag;
  bool optimize_done= !do_optimize, statistics_done= 0;
  const char *old_proc_info= thd->proc_info;
  char fixed_name[FN_REFLEN];
  MARIA_SHARE *share= file->s;
  ha_rows rows= file->state->records;
  DBUG_ENTER("ha_maria::repair");

  param.db_name= table->s->db.str;
  param.table_name= table->alias;
  param.tmpfile_createflag= O_RDWR | O_TRUNC;
  param.using_global_keycache= 1;
  param.thd= thd;
  param.tmpdir= &mysql_tmpdir_list;
  param.out_flag= 0;
  strmov(fixed_name, file->filename);

#ifndef TO_BE_FIXED
  /* QQ: Until we have repair for block format, lie that it succeded */
  if (file->s->data_file_type == BLOCK_RECORD)
  {
    if (optimize)
      DBUG_RETURN(analyze(thd, (HA_CHECK_OPT*) 0));
    DBUG_RETURN(HA_ADMIN_OK);
  }
#endif

  // Don't lock tables if we have used LOCK TABLE
  if (!thd->locked_tables &&
      maria_lock_database(file, table->s->tmp_table ? F_EXTRA_LCK : F_WRLCK))
  {
    _ma_check_print_error(&param, ER(ER_CANT_LOCK), my_errno);
    DBUG_RETURN(HA_ADMIN_FAILED);
  }

  if (!do_optimize ||
      ((file->state->del || share->state.split != file->state->records) &&
       (!(param.testflag & T_QUICK) ||
        (share->state.changed & (STATE_NOT_OPTIMIZED_KEYS |
                                 STATE_NOT_OPTIMIZED_ROWS)))))
  {
    ulonglong key_map= ((local_testflag & T_CREATE_MISSING_KEYS) ?
                        maria_get_mask_all_keys_active(share->base.keys) :
                        share->state.key_map);
    uint testflag= param.testflag;
    if (maria_test_if_sort_rep(file, file->state->records, key_map, 0) &&
        (local_testflag & T_REP_BY_SORT))
    {
      local_testflag |= T_STATISTICS;
      param.testflag |= T_STATISTICS;           // We get this for free
      statistics_done= 1;
      if (thd->variables.maria_repair_threads > 1)
      {
        char buf[40];
        /* TODO: respect maria_repair_threads variable */
        my_snprintf(buf, 40, "Repair with %d threads", my_count_bits(key_map));
        thd->proc_info= buf;
        error= maria_repair_parallel(&param, file, fixed_name,
                                     param.testflag & T_QUICK);
        thd->proc_info= "Repair done";          // to reset proc_info, as
        // it was pointing to local buffer
      }
      else
      {
        thd->proc_info= "Repair by sorting";
        error= maria_repair_by_sort(&param, file, fixed_name,
                                    param.testflag & T_QUICK);
      }
    }
    else
    {
      thd->proc_info= "Repair with keycache";
      param.testflag &= ~T_REP_BY_SORT;
      error= maria_repair(&param, file, fixed_name, param.testflag & T_QUICK);
    }
    param.testflag= testflag;
    optimize_done= 1;
  }
  if (!error)
  {
    if ((local_testflag & T_SORT_INDEX) &&
        (share->state.changed & STATE_NOT_SORTED_PAGES))
    {
      optimize_done= 1;
      thd->proc_info= "Sorting index";
      error= maria_sort_index(&param, file, fixed_name);
    }
    if (!statistics_done && (local_testflag & T_STATISTICS))
    {
      if (share->state.changed & STATE_NOT_ANALYZED)
      {
        optimize_done= 1;
        thd->proc_info= "Analyzing";
        error= maria_chk_key(&param, file);
      }
      else
        local_testflag &= ~T_STATISTICS;        // Don't update statistics
    }
  }
  thd->proc_info= "Saving state";
  if (!error)
  {
    if ((share->state.changed & STATE_CHANGED) || maria_is_crashed(file))
    {
      share->state.changed &= ~(STATE_CHANGED | STATE_CRASHED |
                                STATE_CRASHED_ON_REPAIR);
      file->update |= HA_STATE_CHANGED | HA_STATE_ROW_CHANGED;
    }
    /*
       the following 'if', thought conceptually wrong,
       is a useful optimization nevertheless.
    */
    if (file->state != &file->s->state.state)
      file->s->state.state= *file->state;
    if (file->s->base.auto_key)
      _ma_update_auto_increment_key(&param, file, 1);
    if (optimize_done)
      error= maria_update_state_info(&param, file,
                               UPDATE_TIME | UPDATE_OPEN_COUNT |
                               (local_testflag &
                                T_STATISTICS ? UPDATE_STAT : 0));
    info(HA_STATUS_NO_LOCK | HA_STATUS_TIME | HA_STATUS_VARIABLE |
         HA_STATUS_CONST);
    if (rows != file->state->records && !(param.testflag & T_VERY_SILENT))
    {
      char llbuff[22], llbuff2[22];
      _ma_check_print_warning(&param, "Number of rows changed from %s to %s",
                              llstr(rows, llbuff),
                              llstr(file->state->records, llbuff2));
    }
  }
  else
  {
    maria_mark_crashed_on_repair(file);
    file->update |= HA_STATE_CHANGED | HA_STATE_ROW_CHANGED;
    maria_update_state_info(&param, file, 0);
  }
  thd->proc_info= old_proc_info;
  if (!thd->locked_tables)
    maria_lock_database(file, F_UNLCK);
  DBUG_RETURN(error ? HA_ADMIN_FAILED :
              !optimize_done ? HA_ADMIN_ALREADY_DONE : HA_ADMIN_OK);
}


/*
  Assign table indexes to a specific key cache.
*/

int ha_maria::assign_to_keycache(THD * thd, HA_CHECK_OPT *check_opt)
{
  KEY_CACHE *new_key_cache= check_opt->key_cache;
  const char *errmsg= 0;
  int error= HA_ADMIN_OK;
  ulonglong map= ~(ulonglong) 0;
  TABLE_LIST *table_list= table->pos_in_table_list;
  DBUG_ENTER("ha_maria::assign_to_keycache");

  /* Check validity of the index references */
  if (table_list->use_index)
  {
    /* We only come here when the user did specify an index map */
    key_map kmap;
    if (get_key_map_from_key_list(&kmap, table, table_list->use_index))
    {
      errmsg= thd->net.last_error;
      error= HA_ADMIN_FAILED;
      goto err;
    }
    map= kmap.to_ulonglong();
  }

  if ((error= maria_assign_to_key_cache(file, map, new_key_cache)))
  {
    char buf[STRING_BUFFER_USUAL_SIZE];
    my_snprintf(buf, sizeof(buf),
                "Failed to flush to index file (errno: %d)", error);
    errmsg= buf;
    error= HA_ADMIN_CORRUPT;
  }

err:
  if (error != HA_ADMIN_OK)
  {
    /* Send error to user */
    HA_CHECK param;
    maria_chk_init(&param);
    param.thd= thd;
    param.op_name= "assign_to_keycache";
    param.db_name= table->s->db.str;
    param.table_name= table->s->table_name.str;
    param.testflag= 0;
    _ma_check_print_error(&param, errmsg);
  }
  DBUG_RETURN(error);
}


/*
  Preload pages of the index file for a table into the key cache.
*/

int ha_maria::preload_keys(THD * thd, HA_CHECK_OPT *check_opt)
{
  int error;
  const char *errmsg;
  ulonglong map= ~(ulonglong) 0;
  TABLE_LIST *table_list= table->pos_in_table_list;
  my_bool ignore_leaves= table_list->ignore_leaves;

  DBUG_ENTER("ha_maria::preload_keys");

  /* Check validity of the index references */
  if (table_list->use_index)
  {
    key_map kmap;
    get_key_map_from_key_list(&kmap, table, table_list->use_index);
    if (kmap.is_set_all())
    {
      errmsg= thd->net.last_error;
      error= HA_ADMIN_FAILED;
      goto err;
    }
    if (!kmap.is_clear_all())
      map= kmap.to_ulonglong();
  }

  maria_extra(file, HA_EXTRA_PRELOAD_BUFFER_SIZE,
              (void*) &thd->variables.preload_buff_size);

  if ((error= maria_preload(file, map, ignore_leaves)))
  {
    switch (error) {
    case HA_ERR_NON_UNIQUE_BLOCK_SIZE:
      errmsg= "Indexes use different block sizes";
      break;
    case HA_ERR_OUT_OF_MEM:
      errmsg= "Failed to allocate buffer";
      break;
    default:
      char buf[ERRMSGSIZE + 20];
      my_snprintf(buf, ERRMSGSIZE,
                  "Failed to read from index file (errno: %d)", my_errno);
      errmsg= buf;
    }
    error= HA_ADMIN_FAILED;
    goto err;
  }

  DBUG_RETURN(HA_ADMIN_OK);

err:
  {
    HA_CHECK param;
    maria_chk_init(&param);
    param.thd= thd;
    param.op_name= "preload_keys";
    param.db_name= table->s->db.str;
    param.table_name= table->s->table_name.str;
    param.testflag= 0;
    _ma_check_print_error(&param, errmsg);
    DBUG_RETURN(error);
  }
}


/*
  Disable indexes, making it persistent if requested.

  SYNOPSIS
    disable_indexes()
    mode        mode of operation:
                HA_KEY_SWITCH_NONUNIQ      disable all non-unique keys
                HA_KEY_SWITCH_ALL          disable all keys
                HA_KEY_SWITCH_NONUNIQ_SAVE dis. non-uni. and make persistent
                HA_KEY_SWITCH_ALL_SAVE     dis. all keys and make persistent

  IMPLEMENTATION
    HA_KEY_SWITCH_NONUNIQ       is not implemented.
    HA_KEY_SWITCH_ALL_SAVE      is not implemented.

  RETURN
    0  ok
    HA_ERR_WRONG_COMMAND  mode not implemented.
*/

int ha_maria::disable_indexes(uint mode)
{
  int error;

  if (mode == HA_KEY_SWITCH_ALL)
  {
    /* call a storage engine function to switch the key map */
    error= maria_disable_indexes(file);
  }
  else if (mode == HA_KEY_SWITCH_NONUNIQ_SAVE)
  {
    maria_extra(file, HA_EXTRA_NO_KEYS, 0);
    info(HA_STATUS_CONST);                      // Read new key info
    error= 0;
  }
  else
  {
    /* mode not implemented */
    error= HA_ERR_WRONG_COMMAND;
  }
  return error;
}


/*
  Enable indexes, making it persistent if requested.

  SYNOPSIS
    enable_indexes()
    mode        mode of operation:
                HA_KEY_SWITCH_NONUNIQ      enable all non-unique keys
                HA_KEY_SWITCH_ALL          enable all keys
                HA_KEY_SWITCH_NONUNIQ_SAVE en. non-uni. and make persistent
                HA_KEY_SWITCH_ALL_SAVE     en. all keys and make persistent

  DESCRIPTION
    Enable indexes, which might have been disabled by disable_index() before.
    The modes without _SAVE work only if both data and indexes are empty,
    since the MARIA repair would enable them persistently.
    To be sure in these cases, call handler::delete_all_rows() before.

  IMPLEMENTATION
    HA_KEY_SWITCH_NONUNIQ       is not implemented.
    HA_KEY_SWITCH_ALL_SAVE      is not implemented.

  RETURN
    0  ok
    !=0  Error, among others:
    HA_ERR_CRASHED  data or index is non-empty. Delete all rows and retry.
    HA_ERR_WRONG_COMMAND  mode not implemented.
*/

int ha_maria::enable_indexes(uint mode)
{
  int error;

  if (maria_is_all_keys_active(file->s->state.key_map, file->s->base.keys))
  {
    /* All indexes are enabled already. */
    return 0;
  }

  if (mode == HA_KEY_SWITCH_ALL)
  {
    error= maria_enable_indexes(file);
    /*
       Do not try to repair on error,
       as this could make the enabled state persistent,
       but mode==HA_KEY_SWITCH_ALL forbids it.
    */
  }
  else if (mode == HA_KEY_SWITCH_NONUNIQ_SAVE)
  {
    THD *thd= current_thd;
    HA_CHECK param;
    const char *save_proc_info= thd->proc_info;
    thd->proc_info= "Creating index";
    maria_chk_init(&param);
    param.op_name= "recreating_index";
    param.testflag= (T_SILENT | T_REP_BY_SORT | T_QUICK |
                     T_CREATE_MISSING_KEYS);
    param.myf_rw &= ~MY_WAIT_IF_FULL;
    param.sort_buffer_length= thd->variables.maria_sort_buff_size;
    param.stats_method= (enum_handler_stats_method) thd->variables.
      maria_stats_method;
    param.tmpdir= &mysql_tmpdir_list;
    if ((error= (repair(thd, param, 0) != HA_ADMIN_OK)) && param.retry_repair)
    {
      sql_print_warning("Warning: Enabling keys got errno %d, retrying",
                        my_errno);
      /* Repairing by sort failed. Now try standard repair method. */
      param.testflag &= ~(T_REP_BY_SORT | T_QUICK);
      error= (repair(thd, param, 0) != HA_ADMIN_OK);
      /*
         If the standard repair succeeded, clear all error messages which
         might have been set by the first repair. They can still be seen
         with SHOW WARNINGS then.
      */
      if (!error)
        thd->clear_error();
    }
    info(HA_STATUS_CONST);
    thd->proc_info= save_proc_info;
  }
  else
  {
    /* mode not implemented */
    error= HA_ERR_WRONG_COMMAND;
  }
  return error;
}


/*
  Test if indexes are disabled.


  SYNOPSIS
    indexes_are_disabled()
      no parameters


  RETURN
    0  indexes are not disabled
    1  all indexes are disabled
   [2  non-unique indexes are disabled - NOT YET IMPLEMENTED]
*/

int ha_maria::indexes_are_disabled(void)
{
  return maria_indexes_are_disabled(file);
}


/*
  prepare for a many-rows insert operation
  e.g. - disable indexes (if they can be recreated fast) or
  activate special bulk-insert optimizations

  SYNOPSIS
    start_bulk_insert(rows)
    rows        Rows to be inserted
                0 if we don't know

  NOTICE
    Do not forget to call end_bulk_insert() later!
*/

void ha_maria::start_bulk_insert(ha_rows rows)
{
  DBUG_ENTER("ha_maria::start_bulk_insert");
  THD *thd= current_thd;
  ulong size= min(thd->variables.read_buff_size,
                  table->s->avg_row_length * rows);
  DBUG_PRINT("info", ("start_bulk_insert: rows %lu size %lu",
                      (ulong) rows, size));

  /* don't enable row cache if too few rows */
  if (!rows || (rows > MARIA_MIN_ROWS_TO_USE_WRITE_CACHE))
    maria_extra(file, HA_EXTRA_WRITE_CACHE, (void*) &size);

  can_enable_indexes= maria_is_all_keys_active(file->s->state.key_map,
                                               file->s->base.keys);
  /* TODO: Remove when we have repair() working */
  can_enable_indexes= 0;

  if (!(specialflag & SPECIAL_SAFE_MODE))
  {
    /*
       Only disable old index if the table was empty and we are inserting
       a lot of rows.
       We should not do this for only a few rows as this is slower and
       we don't want to update the key statistics based of only a few rows.
    */
    if (file->state->records == 0 && can_enable_indexes &&
        (!rows || rows >= MARIA_MIN_ROWS_TO_DISABLE_INDEXES))
      maria_disable_non_unique_index(file, rows);
    else
      if (!file->bulk_insert &&
          (!rows || rows >= MARIA_MIN_ROWS_TO_USE_BULK_INSERT))
    {
      maria_init_bulk_insert(file, thd->variables.bulk_insert_buff_size, rows);
    }
  }
  DBUG_VOID_RETURN;
}


/*
  end special bulk-insert optimizations,
  which have been activated by start_bulk_insert().

  SYNOPSIS
    end_bulk_insert()
    no arguments

  RETURN
    0     OK
    != 0  Error
*/

int ha_maria::end_bulk_insert()
{
  int err;
  DBUG_ENTER("ha_maria::end_bulk_insert");
  maria_end_bulk_insert(file);
  err= maria_extra(file, HA_EXTRA_NO_CACHE, 0);
  DBUG_RETURN(err ? err : can_enable_indexes ?
              enable_indexes(HA_KEY_SWITCH_NONUNIQ_SAVE) : 0);
}


bool ha_maria::check_and_repair(THD *thd)
{
  int error= 0;
  int marked_crashed;
  char *old_query;
  uint old_query_length;
  HA_CHECK_OPT check_opt;
  DBUG_ENTER("ha_maria::check_and_repair");

  check_opt.init();
  check_opt.flags= T_MEDIUM | T_AUTO_REPAIR;
  // Don't use quick if deleted rows
  if (!file->state->del && (maria_recover_options & HA_RECOVER_QUICK))
    check_opt.flags |= T_QUICK;
  sql_print_warning("Checking table:   '%s'", table->s->path.str);

  old_query= thd->query;
  old_query_length= thd->query_length;
  pthread_mutex_lock(&LOCK_thread_count);
  thd->query= table->s->table_name.str;
  thd->query_length= table->s->table_name.length;
  pthread_mutex_unlock(&LOCK_thread_count);

  if ((marked_crashed= maria_is_crashed(file)) || check(thd, &check_opt))
  {
    sql_print_warning("Recovering table: '%s'", table->s->path.str);
    check_opt.flags=
      ((maria_recover_options & HA_RECOVER_BACKUP ? T_BACKUP_DATA : 0) |
       (marked_crashed ? 0 : T_QUICK) |
       (maria_recover_options & HA_RECOVER_FORCE ? 0 : T_SAFE_REPAIR) |
       T_AUTO_REPAIR);
    if (repair(thd, &check_opt))
      error= 1;
  }
  pthread_mutex_lock(&LOCK_thread_count);
  thd->query= old_query;
  thd->query_length= old_query_length;
  pthread_mutex_unlock(&LOCK_thread_count);
  DBUG_RETURN(error);
}


bool ha_maria::is_crashed() const
{
  return (file->s->state.changed & STATE_CRASHED ||
          (my_disable_locking && file->s->state.open_count));
}


int ha_maria::update_row(const byte * old_data, byte * new_data)
{
  statistic_increment(table->in_use->status_var.ha_update_count, &LOCK_status);
  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_UPDATE)
    table->timestamp_field->set_time();
  return maria_update(file, old_data, new_data);
}


int ha_maria::delete_row(const byte * buf)
{
  statistic_increment(table->in_use->status_var.ha_delete_count, &LOCK_status);
  return maria_delete(file, buf);
}


int ha_maria::index_read(byte * buf, const byte * key,
                         uint key_len, enum ha_rkey_function find_flag)
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_key_count,
                      &LOCK_status);
  int error= maria_rkey(file, buf, active_index, key, key_len, find_flag);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_read_idx(byte * buf, uint index, const byte * key,
                             uint key_len, enum ha_rkey_function find_flag)
{
  statistic_increment(table->in_use->status_var.ha_read_key_count,
                      &LOCK_status);
  int error= maria_rkey(file, buf, index, key, key_len, find_flag);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_read_last(byte * buf, const byte * key, uint key_len)
{
  DBUG_ENTER("ha_maria::index_read_last");
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_key_count,
                      &LOCK_status);
  int error= maria_rkey(file, buf, active_index, key, key_len,
                        HA_READ_PREFIX_LAST);
  table->status= error ? STATUS_NOT_FOUND : 0;
  DBUG_RETURN(error);
}


int ha_maria::index_next(byte * buf)
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_next_count,
                      &LOCK_status);
  int error= maria_rnext(file, buf, active_index);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_prev(byte * buf)
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_prev_count,
                      &LOCK_status);
  int error= maria_rprev(file, buf, active_index);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_first(byte * buf)
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_first_count,
                      &LOCK_status);
  int error= maria_rfirst(file, buf, active_index);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_last(byte * buf)
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_last_count,
                      &LOCK_status);
  int error= maria_rlast(file, buf, active_index);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::index_next_same(byte * buf,
                              const byte *key __attribute__ ((unused)),
                              uint length __attribute__ ((unused)))
{
  DBUG_ASSERT(inited == INDEX);
  statistic_increment(table->in_use->status_var.ha_read_next_count,
                      &LOCK_status);
  int error= maria_rnext_same(file, buf);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::rnd_init(bool scan)
{
  if (scan)
    return maria_scan_init(file);
  return maria_reset(file);                        // Free buffers
}


int ha_maria::rnd_end()
{
  /* Safe to call even if we don't have started a scan */
  maria_scan_end(file);
  return 0;
}


int ha_maria::rnd_next(byte *buf)
{
  statistic_increment(table->in_use->status_var.ha_read_rnd_next_count,
                      &LOCK_status);
  int error= maria_scan(file, buf);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


int ha_maria::restart_rnd_next(byte *buf, byte *pos)
{
  return rnd_pos(buf, pos);
}


int ha_maria::rnd_pos(byte * buf, byte *pos)
{
  statistic_increment(table->in_use->status_var.ha_read_rnd_count,
                      &LOCK_status);
  int error= maria_rrnd(file, buf, my_get_ptr(pos, ref_length));
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


void ha_maria::position(const byte * record)
{
  my_off_t row_position= maria_position(file);
  my_store_ptr(ref, ref_length, row_position);
}


int ha_maria::info(uint flag)
{
  MARIA_INFO maria_info;
  char name_buff[FN_REFLEN];

  (void) maria_status(file, &maria_info, flag);
  if (flag & HA_STATUS_VARIABLE)
  {
    stats.records=           maria_info.records;
    stats.deleted=           maria_info.deleted;
    stats.data_file_length=  maria_info.data_file_length;
    stats.index_file_length= maria_info.index_file_length;
    stats.delete_length=     maria_info.delete_length;
    stats.check_time=        maria_info.check_time;
    stats.mean_rec_length=   maria_info.mean_reclength;
  }
  if (flag & HA_STATUS_CONST)
  {
    TABLE_SHARE *share= table->s;
    stats.max_data_file_length=  maria_info.max_data_file_length;
    stats.max_index_file_length= maria_info.max_index_file_length;
    stats.create_time= maria_info.create_time;
    ref_length= maria_info.reflength;
    share->db_options_in_use= maria_info.options;
    stats.block_size= maria_block_size;

    /* Update share */
    if (share->tmp_table == NO_TMP_TABLE)
      pthread_mutex_lock(&share->mutex);
    share->keys_in_use.set_prefix(share->keys);
    share->keys_in_use.intersect_extended(maria_info.key_map);
    share->keys_for_keyread.intersect(share->keys_in_use);
    share->db_record_offset= maria_info.record_offset;
    if (share->key_parts)
      memcpy((char*) table->key_info[0].rec_per_key,
             (char*) maria_info.rec_per_key,
             sizeof(table->key_info[0].rec_per_key) * share->key_parts);
    if (share->tmp_table == NO_TMP_TABLE)
      pthread_mutex_unlock(&share->mutex);

    /*
       Set data_file_name and index_file_name to point at the symlink value
       if table is symlinked (Ie;  Real name is not same as generated name)
    */
    data_file_name= index_file_name= 0;
    fn_format(name_buff, file->filename, "", MARIA_NAME_DEXT,
              MY_APPEND_EXT | MY_UNPACK_FILENAME);
    if (strcmp(name_buff, maria_info.data_file_name))
      data_file_name=maria_info.data_file_name;
    fn_format(name_buff, file->filename, "", MARIA_NAME_IEXT,
              MY_APPEND_EXT | MY_UNPACK_FILENAME);
    if (strcmp(name_buff, maria_info.index_file_name))
      index_file_name=maria_info.index_file_name;
  }
  if (flag & HA_STATUS_ERRKEY)
  {
    errkey= maria_info.errkey;
    my_store_ptr(dup_ref, ref_length, maria_info.dup_key_pos);
  }
  /* Faster to always update, than to do it based on flag */
  stats.update_time= maria_info.update_time;
  stats.auto_increment_value= maria_info.auto_increment;

  return 0;
}


int ha_maria::extra(enum ha_extra_function operation)
{
  if ((specialflag & SPECIAL_SAFE_MODE) && operation == HA_EXTRA_KEYREAD)
    return 0;
  return maria_extra(file, operation, 0);
}

int ha_maria::reset(void)
{
  return maria_reset(file);
}

/* To be used with WRITE_CACHE and EXTRA_CACHE */

int ha_maria::extra_opt(enum ha_extra_function operation, ulong cache_size)
{
  if ((specialflag & SPECIAL_SAFE_MODE) && operation == HA_EXTRA_WRITE_CACHE)
    return 0;
  return maria_extra(file, operation, (void*) &cache_size);
}


int ha_maria::delete_all_rows()
{
  return maria_delete_all_rows(file);
}


int ha_maria::delete_table(const char *name)
{
  return maria_delete_table(name);
}


int ha_maria::external_lock(THD *thd, int lock_type)
{
  return maria_lock_database(file, !table->s->tmp_table ?
                             lock_type : ((lock_type == F_UNLCK) ?
                                          F_UNLCK : F_EXTRA_LCK));
}


THR_LOCK_DATA **ha_maria::store_lock(THD *thd,
                                     THR_LOCK_DATA **to,
                                     enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && file->lock.type == TL_UNLOCK)
    file->lock.type= lock_type;
  *to++= &file->lock;
  return to;
}


void ha_maria::update_create_info(HA_CREATE_INFO *create_info)
{
  ha_maria::info(HA_STATUS_AUTO | HA_STATUS_CONST);
  if (!(create_info->used_fields & HA_CREATE_USED_AUTO))
  {
    create_info->auto_increment_value= stats.auto_increment_value;
  }
  create_info->data_file_name= data_file_name;
  create_info->index_file_name= index_file_name;
}


enum row_type ha_maria::get_row_type() const
{
  switch (file->s->data_file_type) {
  case STATIC_RECORD:     return ROW_TYPE_FIXED;
  case DYNAMIC_RECORD:    return ROW_TYPE_DYNAMIC;
  case BLOCK_RECORD:      return ROW_TYPE_PAGES;
  case COMPRESSED_RECORD: return ROW_TYPE_COMPRESSED;
  default:                return ROW_TYPE_NOT_USED;
  }
}


static enum data_file_type maria_row_type(HA_CREATE_INFO *info)
{
  switch (info->row_type) {
  case ROW_TYPE_FIXED:   return STATIC_RECORD;
  case ROW_TYPE_DYNAMIC: return DYNAMIC_RECORD;
  default:               return BLOCK_RECORD;
  }
}


int ha_maria::create(const char *name, register TABLE *table_arg,
                     HA_CREATE_INFO *ha_create_info)
{
  int error;
  uint create_flags= 0, records, i;
  char buff[FN_REFLEN];
  MARIA_KEYDEF *keydef;
  MARIA_COLUMNDEF *recinfo;
  MARIA_CREATE_INFO create_info;
  TABLE_SHARE *share= table_arg->s;
  uint options= share->db_options_in_use;
  enum data_file_type row_type;
  DBUG_ENTER("ha_maria::create");

  for (i= 0; i < share->keys; i++)
  {
    if (table_arg->key_info[i].flags & HA_USES_PARSER)
    {
      create_flags|= HA_CREATE_RELIES_ON_SQL_LAYER;
      break;
    }
  }
  row_type= maria_row_type(ha_create_info);
  if ((error= table2maria(table_arg, &keydef, &recinfo, &records)))
    DBUG_RETURN(error); /* purecov: inspected */
  bzero((char*) &create_info, sizeof(create_info));
  create_info.max_rows= share->max_rows;
  create_info.reloc_rows= share->min_rows;
  create_info.with_auto_increment= share->next_number_key_offset == 0;
  create_info.auto_increment= (ha_create_info->auto_increment_value ?
                               ha_create_info->auto_increment_value -1 :
                               (ulonglong) 0);
  create_info.data_file_length= ((ulonglong) share->max_rows *
                                 share->avg_row_length);
  create_info.data_file_name= ha_create_info->data_file_name;
  create_info.index_file_name= ha_create_info->index_file_name;

  if (ha_create_info->options & HA_LEX_CREATE_TMP_TABLE)
    create_flags|= HA_CREATE_TMP_TABLE;
  if (options & HA_OPTION_PACK_RECORD)
    create_flags|= HA_PACK_RECORD;
  if (options & HA_OPTION_CHECKSUM)
    create_flags|= HA_CREATE_CHECKSUM;
  if (options & HA_OPTION_DELAY_KEY_WRITE)
    create_flags|= HA_CREATE_DELAY_KEY_WRITE;

  /* TODO: Check that the following fn_format is really needed */
  error=
    maria_create(fn_format(buff, name, "", "",
                           MY_UNPACK_FILENAME | MY_APPEND_EXT),
                 row_type, share->keys, keydef,
                 records,  recinfo,
                 0, (MARIA_UNIQUEDEF *) 0,
                 &create_info, create_flags);

  my_free((gptr) recinfo, MYF(0));
  DBUG_RETURN(error);
}


int ha_maria::rename_table(const char *from, const char *to)
{
  return maria_rename(from, to);
}


void ha_maria::get_auto_increment(ulonglong offset, ulonglong increment,
                                  ulonglong nb_desired_values,
                                  ulonglong *first_value,
                                  ulonglong *nb_reserved_values)
{
  ulonglong nr;
  int error;
  byte key[HA_MAX_KEY_LENGTH];

  if (!table->s->next_number_key_offset)
  {                                             // Autoincrement at key-start
    ha_maria::info(HA_STATUS_AUTO);
    *first_value= stats.auto_increment_value;
    /* Maria has only table-level lock for now, so reserves to +inf */
    *nb_reserved_values= ULONGLONG_MAX;
    return;
  }

  /* it's safe to call the following if bulk_insert isn't on */
  maria_flush_bulk_insert(file, table->s->next_number_index);

  (void) extra(HA_EXTRA_KEYREAD);
  key_copy(key, table->record[0],
           table->key_info + table->s->next_number_index,
           table->s->next_number_key_offset);
  error= maria_rkey(file, table->record[1], (int) table->s->next_number_index,
                    key, table->s->next_number_key_offset, HA_READ_PREFIX_LAST);
  if (error)
    nr= 1;
  else
  {
    /* Get data from record[1] */
    nr= ((ulonglong) table->next_number_field->
         val_int_offset(table->s->rec_buff_length) + 1);
  }
  extra(HA_EXTRA_NO_KEYREAD);
  *first_value= nr;
  /*
    MySQL needs to call us for next row: assume we are inserting ("a",null)
    here, we return 3, and next this statement will want to insert ("b",null):
    there is no reason why ("b",3+1) would be the good row to insert: maybe it
    already exists, maybe 3+1 is too large...
  */
  *nb_reserved_values= 1;
}


/*
  Find out how many rows there is in the given range

  SYNOPSIS
    records_in_range()
    inx                 Index to use
    min_key             Start of range.  Null pointer if from first key
    max_key             End of range. Null pointer if to last key

  NOTES
    min_key.flag can have one of the following values:
      HA_READ_KEY_EXACT         Include the key in the range
      HA_READ_AFTER_KEY         Don't include key in range

    max_key.flag can have one of the following values:
      HA_READ_BEFORE_KEY        Don't include key in range
      HA_READ_AFTER_KEY         Include all 'end_key' values in the range

  RETURN
   HA_POS_ERROR         Something is wrong with the index tree.
   0                    There is no matching keys in the given range
   number > 0           There is approximately 'number' matching rows in
                        the range.
*/

ha_rows ha_maria::records_in_range(uint inx, key_range *min_key,
                                   key_range *max_key)
{
  return (ha_rows) maria_records_in_range(file, (int) inx, min_key, max_key);
}


int ha_maria::ft_read(byte * buf)
{
  int error;

  if (!ft_handler)
    return -1;

  thread_safe_increment(table->in_use->status_var.ha_read_next_count,
                        &LOCK_status);  // why ?

  error= ft_handler->please->read_next(ft_handler, (char*) buf);

  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}


uint ha_maria::checksum() const
{
  return (uint) file->state->checksum;
}


bool ha_maria::check_if_incompatible_data(HA_CREATE_INFO *info,
                                          uint table_changes)
{
  uint options= table->s->db_options_in_use;

  if (info->auto_increment_value != stats.auto_increment_value ||
      info->data_file_name != data_file_name ||
      info->index_file_name != index_file_name ||
      maria_row_type(info) != data_file_type ||
      table_changes == IS_EQUAL_NO ||
      table_changes & IS_EQUAL_PACK_LENGTH) // Not implemented yet
    return COMPATIBLE_DATA_NO;

  if ((options & (HA_OPTION_PACK_RECORD | HA_OPTION_CHECKSUM |
                  HA_OPTION_DELAY_KEY_WRITE)) !=
      (info->table_options & (HA_OPTION_PACK_RECORD | HA_OPTION_CHECKSUM |
                              HA_OPTION_DELAY_KEY_WRITE)))
    return COMPATIBLE_DATA_NO;
  return COMPATIBLE_DATA_YES;
}

extern int maria_panic(enum ha_panic_function flag);
int maria_panic(handlerton *hton, ha_panic_function flag)
{
  return maria_panic(flag);
}

static int ha_maria_init(void *p)
{
  handlerton *maria_hton;

  maria_hton= (handlerton *)p;
  maria_hton->state= SHOW_OPTION_YES;
  maria_hton->db_type= DB_TYPE_MARIA;
  maria_hton->create= maria_create_handler;
  maria_hton->panic= maria_panic;
  /* TODO: decide if we support Maria being used for log tables */
  maria_hton->flags= HTON_CAN_RECREATE | HTON_SUPPORT_LOG_TABLES;
  return test(maria_init());
}

struct st_mysql_storage_engine maria_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(maria)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &maria_storage_engine,
  "Maria",
  "MySQL AB",
  "Traditional transactional MySQL tables",
  PLUGIN_LICENSE_GPL,
  ha_maria_init, /* Plugin Init */
  NULL, /* Plugin Deinit */
  0x0100, /* 1.0 */
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  NULL                        /* config options                  */
}
mysql_declare_plugin_end;
