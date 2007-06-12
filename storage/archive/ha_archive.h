/* Copyright (C) 2003 MySQL AB

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

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include <zlib.h>
#include "azlib.h"

/*
  Please read ha_archive.cc first. If you are looking for more general
  answers on how storage engines work, look at ha_example.cc and
  ha_example.h.
*/

typedef struct st_archive_record_buffer {
  byte *buffer;
  uint32 length;
} archive_record_buffer;


typedef struct st_archive_share {
  char *table_name;
  char data_file_name[FN_REFLEN];
  uint table_name_length,use_count;
  pthread_mutex_t mutex;
  THR_LOCK lock;
  azio_stream archive_write;     /* Archive file we are working with */
  bool archive_write_open;
  bool dirty;               /* Flag for if a flush should occur */
  bool crashed;             /* Meta file is crashed */
  ha_rows rows_recorded;    /* Number of rows in tables */
  ulonglong mean_rec_length;
  char real_path[FN_REFLEN];
} ARCHIVE_SHARE;

/*
  Version for file format.
  1 - Initial Version (Never Released)
  2 - Stream Compression, seperate blobs, no packing
  3 - One steam (row and blobs), with packing
*/
#define ARCHIVE_VERSION 3

class ha_archive: public handler
{
  THR_LOCK_DATA lock;        /* MySQL lock */
  ARCHIVE_SHARE *share;      /* Shared lock info */
  
  azio_stream archive;            /* Archive file we are working with */
  my_off_t current_position;  /* The position of the row we just read */
  byte byte_buffer[IO_SIZE]; /* Initial buffer for our string */
  String buffer;             /* Buffer used for blob storage */
  ha_rows scan_rows;         /* Number of rows left in scan */
  bool delayed_insert;       /* If the insert is delayed */
  bool bulk_insert;          /* If we are performing a bulk insert */
  const byte *current_key;
  uint current_key_len;
  uint current_k_offset;
  archive_record_buffer *record_buffer;
  bool archive_reader_open;

  archive_record_buffer *create_record_buffer(unsigned int length);
  void destroy_record_buffer(archive_record_buffer *r);

public:
  ha_archive(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_archive()
  {
  }
  const char *table_type() const { return "ARCHIVE"; }
  const char *index_type(uint inx) { return "NONE"; }
  const char **bas_ext() const;
  ulonglong table_flags() const
  {
    return (HA_NO_TRANSACTIONS | HA_REC_NOT_IN_SEQ | HA_CAN_BIT_FIELD |
            HA_BINLOG_ROW_CAPABLE | HA_BINLOG_STMT_CAPABLE |
            HA_FILE_BASED | HA_CAN_INSERT_DELAYED | HA_CAN_GEOMETRY);
  }
  ulong index_flags(uint idx, uint part, bool all_parts) const
  {
    return HA_ONLY_WHOLE_INDEX;
  }
  virtual void get_auto_increment(ulonglong offset, ulonglong increment,
                                  ulonglong nb_desired_values,
                                  ulonglong *first_value,
                                  ulonglong *nb_reserved_values);
  uint max_supported_keys()          const { return 1; }
  uint max_supported_key_length()    const { return sizeof(ulonglong); }
  uint max_supported_key_part_length() const { return sizeof(ulonglong); }
  int index_init(uint keynr, bool sorted);
  virtual int index_read(byte * buf, const byte * key,
			 uint key_len, enum ha_rkey_function find_flag);
  virtual int index_read_idx(byte * buf, uint index, const byte * key,
			     uint key_len, enum ha_rkey_function find_flag);
  int index_next(byte * buf);
  int open(const char *name, int mode, uint test_if_locked);
  int close(void);
  int write_row(byte * buf);
  int real_write_row(byte *buf, azio_stream *writer);
  int delete_all_rows();
  int rnd_init(bool scan=1);
  int rnd_next(byte *buf);
  int rnd_pos(byte * buf, byte *pos);
  int get_row(azio_stream *file_to_read, byte *buf);
  int get_row_version2(azio_stream *file_to_read, byte *buf);
  int get_row_version3(azio_stream *file_to_read, byte *buf);
  ARCHIVE_SHARE *get_share(const char *table_name, int *rc);
  int free_share();
  int init_archive_writer();
  int init_archive_reader();
  bool auto_repair() const { return 1; } // For the moment we just do this
  int read_data_header(azio_stream *file_to_read);
  void position(const byte *record);
  int info(uint);
  void update_create_info(HA_CREATE_INFO *create_info);
  int create(const char *name, TABLE *form, HA_CREATE_INFO *create_info);
  int optimize(THD* thd, HA_CHECK_OPT* check_opt);
  int repair(THD* thd, HA_CHECK_OPT* check_opt);
  void start_bulk_insert(ha_rows rows);
  int end_bulk_insert();
  enum row_type get_row_type() const 
  { 
    return ROW_TYPE_COMPRESSED;
  }
  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type);
  bool is_crashed() const;
  int check(THD* thd, HA_CHECK_OPT* check_opt);
  bool check_and_repair(THD *thd);
  uint32 max_row_length(const byte *buf);
  bool fix_rec_buff(unsigned int length);
  int unpack_row(azio_stream *file_to_read, byte *record);
  unsigned int pack_row(byte *record);
};

