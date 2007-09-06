/* Copyright (C) 2006 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

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

/* This file is included by all internal maria files */

#include "maria.h"				/* Structs & some defines */
#include "myisampack.h"				/* packing of keys */
#include <my_tree.h>
#ifdef THREAD
#include <my_pthread.h>
#include <thr_lock.h>
#else
#include <my_no_pthread.h>
#endif

#include "ma_loghandler.h"
#include "ma_control_file.h"

#define MAX_NONMAPPED_INSERTS 1000
#define MARIA_MAX_TREE_LEVELS 32
#define SANITY_CHECKS

struct st_transaction;

/* undef map from my_nosys; We need test-if-disk full */
#undef my_write	

typedef struct st_maria_status_info
{
  ha_rows records;				/* Rows in table */
  ha_rows del;					/* Removed rows */
  my_off_t empty;				/* lost space in datafile */
  my_off_t key_empty;				/* lost space in indexfile */
  my_off_t key_file_length;
  my_off_t data_file_length;
  ha_checksum checksum;
} MARIA_STATUS_INFO;

typedef struct st_maria_state_info
{
  struct
  {					/* Fileheader */
    uchar file_version[4];
    uchar options[2];
    uchar header_length[2];
    uchar state_info_length[2];
    uchar base_info_length[2];
    uchar base_pos[2];
    uchar key_parts[2];			/* Key parts */
    uchar unique_key_parts[2];		/* Key parts + unique parts */
    uchar keys;				/* number of keys in file */
    uchar uniques;			/* number of UNIQUE definitions */
    uchar language;			/* Language for indexes */
    uchar fulltext_keys;
    uchar data_file_type;
    /* Used by mariapack to store the original data_file_type */
    uchar org_data_file_type;
  } header;

  MARIA_STATUS_INFO state;
  ha_rows split;			/* number of split blocks */
  my_off_t dellink;			/* Link to next removed block */
  ulonglong first_bitmap_with_space;
  ulonglong auto_increment;
  ulong process;			/* process that updated table last */
  ulong unique;				/* Unique number for this process */
  ulong update_count;			/* Updated for each write lock */
  ulong status;
  ulong *rec_per_key_part;
  ha_checksum checksum;                 /* Table checksum */
  my_off_t *key_root;			/* Start of key trees */
  my_off_t key_del;			/* delete links for index pages */
  my_off_t rec_per_key_rows;		/* Rows when calculating rec_per_key */

  ulong sec_index_changed;		/* Updated when new sec_index */
  ulong sec_index_used;			/* which extra index are in use */
  ulonglong key_map;			/* Which keys are in use */
  ulong version;			/* timestamp of create */
  time_t create_time;			/* Time when created database */
  time_t recover_time;			/* Time for last recover */
  time_t check_time;			/* Time for last check */
  uint sortkey;				/* sorted by this key (not used) */
  uint open_count;
  uint8 changed;			/* Changed since mariachk */
  LSN create_rename_lsn;    /**< LSN when table was last created/renamed */

  /* the following isn't saved on disk */
  uint state_diff_length;		/* Should be 0 */
  uint state_length;			/* Length of state header in file */
  ulong *key_info;
} MARIA_STATE_INFO;


#define MARIA_STATE_INFO_SIZE	\
  (24 + LSN_STORE_SIZE + 4 + 11*8 + 4*4 + 8 + 3*4 + 5*8)
#define MARIA_STATE_KEY_SIZE	8
#define MARIA_STATE_KEYBLOCK_SIZE  8
#define MARIA_STATE_KEYSEG_SIZE	4
#define MARIA_STATE_EXTRA_SIZE (MARIA_MAX_KEY*MARIA_STATE_KEY_SIZE + MARIA_MAX_KEY*HA_MAX_KEY_SEG*MARIA_STATE_KEYSEG_SIZE)
#define MARIA_KEYDEF_SIZE		(2+ 5*2)
#define MARIA_UNIQUEDEF_SIZE	(2+1+1)
#define HA_KEYSEG_SIZE		(6+ 2*2 + 4*2)
#define MARIA_COLUMNDEF_SIZE	(6+2+2+2+2+2+1+1)
#define MARIA_BASE_INFO_SIZE	(5*8 + 6*4 + 11*2 + 6 + 5*2 + 1 + 16)
#define MARIA_INDEX_BLOCK_MARGIN 16	/* Safety margin for .MYI tables */
/* Internal management bytes needed to store 2 keys on an index page */
#define MARIA_INDEX_MIN_OVERHEAD_SIZE (4 + (TRANSID_SIZE+1) * 2)

/*
  Basic information of the Maria table. This is stored on disk
  and not changed (unless we do DLL changes).
*/

typedef struct st_ma_base_info
{
  my_off_t keystart;                    /* Start of keys */
  my_off_t max_data_file_length;
  my_off_t max_key_file_length;
  my_off_t margin_key_file_length;
  ha_rows records, reloc;               /* Create information */
  ulong mean_row_length;                /* Create information */
  ulong reclength;                      /* length of unpacked record */
  ulong pack_reclength;                 /* Length of full packed rec */
  ulong min_pack_length;
  ulong max_pack_length;                /* Max possibly length of packed rec */
  ulong min_block_length;
  uint fields;                          /* fields in table */
  uint fixed_not_null_fields;
  uint fixed_not_null_fields_length;
  uint max_field_lengths;
  uint pack_fields;                     /* packed fields in table */
  uint varlength_fields;                /* char/varchar/blobs */
  /* Number of bytes in the index used to refer to a row (2-8) */
  uint rec_reflength;
  /* Number of bytes in the index used to refer to another index page (2-8) */
  uint key_reflength;                   /* = 2-8 */
  uint keys;                            /* same as in state.header */
  uint auto_key;                        /* Which key-1 is a auto key */
  uint blobs;                           /* Number of blobs */
  /* Length of packed bits (when table was created first time) */
  uint pack_bytes;
  /* Length of null bits (when table was created first time) */
  uint original_null_bytes;
  uint null_bytes;                      /* Null bytes in record */
  uint field_offsets;                   /* Number of field offsets */
  uint max_key_block_length;            /* Max block length */
  uint max_key_length;                  /* Max key length */
  /* Extra allocation when using dynamic record format */
  uint extra_alloc_bytes;
  uint extra_alloc_procent;
  uint is_nulls_extended;               /* 1 if new null bytes */
  uint min_row_length;			/* Min possible length of a row */
  uint default_row_flag;                /* 0 or ROW_FLAG_NULLS_EXTENDED */
  uint block_size;
  /* Size of initial record buffer */
  uint default_rec_buff_size;
  /* Extra number of bytes the row format require in the record buffer */
  uint extra_rec_buff_size;

  /* The following are from the header */
  uint key_parts, all_key_parts;
  /**
     @brief If false, we disable logging, versioning, transaction etc. Observe
     difference with MARIA_SHARE::now_transactional
  */
  my_bool born_transactional;
} MARIA_BASE_INFO;


/* Structs used intern in database */

typedef struct st_maria_blob            /* Info of record */
{
  ulong offset;                         /* Offset to blob in record */
  uint pack_length;                     /* Type of packed length */
  ulong length;                         /* Calc:ed for each record */
} MARIA_BLOB;


typedef struct st_maria_pack
{
  ulong header_length;
  uint ref_length;
  uchar version;
} MARIA_PACK;

typedef struct st_maria_file_bitmap
{
  uchar *map;
  ulonglong page;                      /* Page number for current bitmap */
  uint used_size;                      /* Size of bitmap head that is not 0 */
  my_bool changed;                     /* 1 if page needs to be flushed */
  PAGECACHE_FILE file;		       /* datafile where bitmap is stored */

#ifdef THREAD
  pthread_mutex_t bitmap_lock;
#endif
  /* Constants, allocated when initiating bitmaps */
  uint sizes[8];                      /* Size per bit combination */
  uint total_size;		      /* Total usable size of bitmap page */
  uint block_size;                    /* Block size of file */
  ulong pages_covered;                /* Pages covered by bitmap + 1 */
} MARIA_FILE_BITMAP;


typedef struct st_maria_share
{					/* Shared between opens */
  MARIA_STATE_INFO state;
  MARIA_BASE_INFO base;
  MARIA_KEYDEF ft2_keyinfo;		/* Second-level ft-key
						   definition */
  MARIA_KEYDEF *keyinfo;		/* Key definitions */
  MARIA_UNIQUEDEF *uniqueinfo;		/* unique definitions */
  HA_KEYSEG *keyparts;			/* key part info */
  MARIA_COLUMNDEF *columndef;		/* Pointer to column information */
  MARIA_PACK pack;			/* Data about packed records */
  MARIA_BLOB *blobs;			/* Pointer to blobs */
  char *unique_file_name;		/* realpath() of index file */
  char *data_file_name;			/* Resolved path names from symlinks */
  char *index_file_name;
  char *open_file_name;			/* parameter to open filename */
  uchar *file_map;			/* mem-map of file if possible */
  PAGECACHE *pagecache;			/* ref to the current key cache */
  MARIA_DECODE_TREE *decode_trees;
  uint16 *decode_tables;
  uint16 id; /**< 2-byte id by which log records refer to the table */
  /* Called the first time the table instance is opened */
  my_bool (*once_init)(struct st_maria_share *, File);
  /* Called when the last instance of the table is closed */
  my_bool (*once_end)(struct st_maria_share *);
  /* Is called for every open of the table */
  my_bool (*init)(struct st_maria_info *);
  /* Is called for every close of the table */
  void (*end)(struct st_maria_info *);
  /* Called when we want to read a record from a specific position */
  int (*read_record)(struct st_maria_info *, uchar *, MARIA_RECORD_POS);
  /* Initialize a scan */
  my_bool (*scan_init)(struct st_maria_info *);
  /* Read next record while scanning */
  int (*scan)(struct st_maria_info *, uchar *, MARIA_RECORD_POS, my_bool);
  /* End scan */
  void (*scan_end)(struct st_maria_info *);
  /* Pre-write of row (some handlers may do the actual write here) */
  MARIA_RECORD_POS (*write_record_init)(struct st_maria_info *, const uchar *);
  /* Write record (or accept write_record_init) */
  my_bool (*write_record)(struct st_maria_info *, const uchar *);
  /* Called when write failed */
  my_bool (*write_record_abort)(struct st_maria_info *);
  my_bool (*update_record)(struct st_maria_info *, MARIA_RECORD_POS,
                           const uchar *, const uchar *);
  my_bool (*delete_record)(struct st_maria_info *, const uchar *record);
  my_bool (*compare_record)(struct st_maria_info *, const uchar *);
  /* calculate checksum for a row */
  ha_checksum(*calc_checksum)(struct st_maria_info *, const uchar *);
  /*
    Calculate checksum for a row during write. May be 0 if we calculate
    the checksum in write_record_init()
  */
  ha_checksum(*calc_write_checksum)(struct st_maria_info *, const uchar *);
  /* calculate checksum for a row during check table */
  ha_checksum(*calc_check_checksum)(struct st_maria_info *, const uchar *);
  /* Compare a row in memory with a row on disk */
  my_bool (*compare_unique)(struct st_maria_info *, MARIA_UNIQUEDEF *,
                            const uchar *record, MARIA_RECORD_POS pos);
  /* Mapings to read/write the data file */
  uint (*file_read)(MARIA_HA *, uchar *, uint, my_off_t, myf);
  uint (*file_write)(MARIA_HA *, uchar *, uint, my_off_t, myf);
  invalidator_by_filename invalidator;	/* query cache invalidator */
  ulong this_process;			/* processid */
  ulong last_process;			/* For table-change-check */
  ulong last_version;			/* Version on start */
  ulong options;			/* Options used */
  ulong min_pack_length;		/* These are used by packed data */
  ulong max_pack_length;
  ulong state_diff_length;
  uint rec_reflength;			/* rec_reflength in use now */
  uint unique_name_length;
  uint32 ftparsers;			/* Number of distinct ftparsers
						   + 1 */
  PAGECACHE_FILE kfile;			/* Shared keyfile */
  File data_file;			/* Shared data file */
  int mode;				/* mode of file on open */
  uint reopen;				/* How many times reopened */
  uint w_locks, r_locks, tot_locks;	/* Number of read/write locks */
  uint block_size;			/* block_size of keyfile & data file*/
  /* Fixed length part of a packed row in BLOCK_RECORD format */
  uint base_length;
  myf write_flag;
  enum data_file_type data_file_type;
  enum pagecache_page_type page_type;   /* value depending transactional */
  my_bool temporary;
  /* Below flag is needed to make log tables work with concurrent insert */
  my_bool is_log_table;

  my_bool changed,			/* If changed since lock */
    global_changed,			/* If changed since open */
    not_flushed, concurrent_insert;
  my_bool delay_key_write;
  my_bool have_rtree;
  /**
     @brief if the table is transactional right now. It may have been created
     transactional (base.born_transactional==TRUE) but with transactionality
     (logging) temporarily disabled (now_transactional==FALSE). The opposite
     (FALSE, TRUE) is impossible.
  */
  my_bool now_transactional;
#ifdef THREAD
  THR_LOCK lock;
  pthread_mutex_t intern_lock;		/* Locking for use with _locking */
  rw_lock_t *key_root_lock;
#endif
  my_off_t mmaped_length;
  uint nonmmaped_inserts;		/* counter of writing in
						   non-mmaped area */
  MARIA_FILE_BITMAP bitmap;
  rw_lock_t mmap_lock;
} MARIA_SHARE;


typedef uchar MARIA_BITMAP_BUFFER;

typedef struct st_maria_bitmap_block
{
  ulonglong page;                       /* Page number */
  /* Number of continuous pages. TAIL_BIT is set if this is a tail page */
  uint page_count;
  uint empty_space;                     /* Set for head and tail pages */
  /*
    Number of BLOCKS for block-region (holds all non-blob-fields or one blob)
  */
  uint sub_blocks;
  /* set to <> 0 in write_record() if this block was actually used */
  uint8 used;
  uint8 org_bitmap_value;
} MARIA_BITMAP_BLOCK;


typedef struct st_maria_bitmap_blocks
{
  MARIA_BITMAP_BLOCK *block;
  uint count;
  my_bool tail_page_skipped;            /* If some tail pages was not used */
  my_bool page_skipped;                 /* If some full pages was not used */
} MARIA_BITMAP_BLOCKS;


/* Data about the currently read row */
typedef struct st_maria_row
{
  MARIA_BITMAP_BLOCKS insert_blocks;
  MARIA_BITMAP_BUFFER *extents;
  MARIA_RECORD_POS lastpos, nextpos;
  MARIA_RECORD_POS *tail_positions;
  ha_checksum checksum;
  uchar *empty_bits, *field_lengths;
  uint *null_field_lengths;             /* All null field lengths */
  ulong *blob_lengths;                  /* Length for each blob */
  ulong base_length, normal_length, char_length, varchar_length, blob_length;
  ulong head_length, total_length;
  size_t extents_buffer_length;         /* Size of 'extents' buffer */
  uint field_lengths_length;            /* Length of data in field_lengths */
  uint extents_count;                   /* number of extents in 'extents' */
  uint full_page_count, tail_count;     /* For maria_chk */
  uint space_on_head_page;
} MARIA_ROW;

/* Data to scan row in blocked format */
typedef struct st_maria_block_scan
{
  uchar *bitmap_buff, *bitmap_pos, *bitmap_end, *page_buff;
  uchar *dir, *dir_end;
  ulong bitmap_page;
  ulonglong bits;
  uint number_of_rows, bit_pos;
  MARIA_RECORD_POS row_base_page;
} MARIA_BLOCK_SCAN;


struct st_maria_info
{
  MARIA_SHARE *s;			/* Shared between open:s */
  struct st_transaction *trn;           /* Pointer to active transaction */
  MARIA_STATUS_INFO *state, save_state;
  MARIA_ROW cur_row;                    /* The active row that we just read */
  MARIA_ROW new_row;			/* Storage for a row during update */
  MARIA_BLOCK_SCAN scan;
  MARIA_BLOB *blobs;			/* Pointer to blobs */
  MARIA_BIT_BUFF bit_buff;
  DYNAMIC_ARRAY bitmap_blocks;
  DYNAMIC_ARRAY pinned_pages;
  /* accumulate indexfile changes between write's */
  TREE *bulk_insert;
  LEX_STRING *log_row_parts;		/* For logging */
  DYNAMIC_ARRAY *ft1_to_ft2;		/* used only in ft1->ft2 conversion */
  MEM_ROOT      ft_memroot;             /* used by the parser               */
  MYSQL_FTPARSER_PARAM *ftparser_param;	/* share info between init/deinit */
  uchar *buff;				/* page buffer */
  uchar *keyread_buff;                   /* Buffer for last key read */
  uchar *lastkey, *lastkey2;		/* Last used search key */
  uchar *first_mbr_key;			/* Searhed spatial key */
  uchar *rec_buff;			/* Temp buffer for recordpack */
  uchar *int_keypos,			/* Save position for next/previous */
   *int_maxpos;				/* -""- */
  uchar *update_field_data;		/* Used by update in rows-in-block */
  uint int_nod_flag;			/* -""- */
  uint32 int_keytree_version;		/* -""- */
  int (*read_record) (struct st_maria_info *, uchar*, MARIA_RECORD_POS);
  invalidator_by_filename invalidator;	/* query cache invalidator */
  ulong this_unique;			/* uniq filenumber or thread */
  ulong last_unique;			/* last unique number */
  ulong this_loop;			/* counter for this open */
  ulong last_loop;			/* last used counter */
  MARIA_RECORD_POS save_lastpos;
  MARIA_RECORD_POS dup_key_pos;
  my_off_t pos;				/* Intern variable */
  my_off_t last_keypage;		/* Last key page read */
  my_off_t last_search_keypage;		/* Last keypage when searching */

  /*
    QQ: the folloing two xxx_length fields should be removed,
     as they are not compatible with parallel repair
  */
  ulong packed_length, blob_length;	/* Length of found, packed record */
  size_t rec_buff_size;
  PAGECACHE_FILE dfile;			/* The datafile */
  IO_CACHE rec_cache;			/* When cacheing records */
  LIST open_list;
  uint opt_flag;			/* Optim. for space/speed */
  uint update;				/* If file changed since open */
  int lastinx;				/* Last used index */
  uint lastkey_length;			/* Length of key in lastkey */
  uint last_rkey_length;		/* Last length in maria_rkey() */
  enum ha_rkey_function last_key_func;	/* CONTAIN, OVERLAP, etc */
  uint save_lastkey_length;
  uint pack_key_length;			/* For MARIAMRG */
  uint16 last_used_keyseg;              /* For MARIAMRG */
  int errkey;				/* Got last error on this key */
  int lock_type;			/* How database was locked */
  int tmp_lock_type;			/* When locked by readinfo */
  uint data_changed;			/* Somebody has changed data */
  uint save_update;			/* When using KEY_READ */
  int save_lastinx;
  uint preload_buff_size;		/* When preloading indexes */
  myf lock_wait;			/* is 0 or MY_DONT_WAIT */
  my_bool was_locked;			/* Was locked in panic */
  my_bool append_insert_at_end;		/* Set if concurrent insert */
  my_bool quick_mode;
  /* If info->keyread_buff can't be used for rnext */
  my_bool page_changed;
  /* If info->keyread_buff has to be re-read for rnext */
  my_bool keyread_buff_used;
  my_bool once_flags;			/* For MARIA_MRG */
#ifdef __WIN__
  my_bool owned_by_merge;               /* This Maria table is part of a merge union */
#endif
#ifdef THREAD
  THR_LOCK_DATA lock;
#endif
  uchar *maria_rtree_recursion_state;	/* For RTREE */
  uchar length_buff[5];			/* temp buff to store blob lengths */
  int maria_rtree_recursion_depth;
};

/* Some defines used by maria-functions */

#define USE_WHOLE_KEY	65535         /* Use whole key in _search() */
#define F_EXTRA_LCK	-1

/* bits in opt_flag */
#define MEMMAP_USED	32
#define REMEMBER_OLD_POS 64

#define WRITEINFO_UPDATE_KEYFILE	1
#define WRITEINFO_NO_UNLOCK		2

/* once_flags */
#define USE_PACKED_KEYS         1
#define RRND_PRESERVE_LASTINX   2

/* bits in state.changed */

#define STATE_CHANGED		1
#define STATE_CRASHED		2
#define STATE_CRASHED_ON_REPAIR 4
#define STATE_NOT_ANALYZED	8
#define STATE_NOT_OPTIMIZED_KEYS 16
#define STATE_NOT_SORTED_PAGES	32
#define STATE_NOT_OPTIMIZED_ROWS 64

/* options to maria_read_cache */

#define READING_NEXT	1
#define READING_HEADER	2

#define maria_data_on_page(x)	((uint) mi_uint2korr(x) & 32767)
#define maria_putint(x,y,nod) { uint16 boh=(nod ? (uint16) 32768 : 0) + (uint16) (y);\
			  mi_int2store(x,boh); }
#define _ma_test_if_nod(x) (x[0] & 128 ? info->s->base.key_reflength : 0)
#define maria_mark_crashed(x) do{(x)->s->state.changed|= STATE_CRASHED; \
    DBUG_PRINT("error", ("Marked table crashed"));                      \
  }while(0)
#define maria_mark_crashed_on_repair(x) do{(x)->s->state.changed|=      \
      STATE_CRASHED|STATE_CRASHED_ON_REPAIR;                            \
    (x)->update|= HA_STATE_CHANGED;                                     \
    DBUG_PRINT("error",                                                 \
               ("Marked table crashed"));                               \
  }while(0)
#define maria_is_crashed(x) ((x)->s->state.changed & STATE_CRASHED)
#define maria_is_crashed_on_repair(x) ((x)->s->state.changed & STATE_CRASHED_ON_REPAIR)
#ifdef EXTRA_DEBUG
#define maria_print_error(SHARE, ERRNO)                     \
        _ma_report_error((ERRNO), (SHARE)->index_file_name)
#else
#define maria_print_error(SHARE, ERRNO) while (0)
#endif


/* Functions to store length of space packed keys, VARCHAR or BLOB keys */

#define store_key_length(key,length) \
{ if ((length) < 255) \
  { *(key)=(length); } \
  else \
  { *(key)=255; mi_int2store((key)+1,(length)); } \
}

#define get_key_full_length(length,key) \
  { if (*(uchar*) (key) != 255)            \
    length= ((uint) *(uchar*) ((key)++))+1; \
  else \
  { length=mi_uint2korr((key)+1)+3; (key)+=3; } \
}

#define get_key_full_length_rdonly(length,key) \
{ if (*(uchar*) (key) != 255) \
    length= ((uint) *(uchar*) ((key)))+1; \
  else \
  { length=mi_uint2korr((key)+1)+3; } \
}

#define maria_max_key_length() ((maria_block_size - MARIA_INDEX_MIN_OVERHEAD_SIZE)/2)
#define get_pack_length(length) ((length) >= 255 ? 3 : 1)

#define MARIA_MIN_BLOCK_LENGTH	20		/* Because of delete-link */
/* Don't use to small record-blocks */
#define MARIA_EXTEND_BLOCK_LENGTH	20
#define MARIA_SPLIT_LENGTH	((MARIA_EXTEND_BLOCK_LENGTH+4)*2)
	/* Max prefix of record-block */
#define MARIA_MAX_DYN_BLOCK_HEADER	20
#define MARIA_BLOCK_INFO_HEADER_LENGTH 20
#define MARIA_DYN_DELETE_BLOCK_HEADER 20    /* length of delete-block-header */
#define MARIA_DYN_MAX_BLOCK_LENGTH	((1L << 24)-4L)
#define MARIA_DYN_MAX_ROW_LENGTH	(MARIA_DYN_MAX_BLOCK_LENGTH - MARIA_SPLIT_LENGTH)
#define MARIA_DYN_ALIGN_SIZE	  4	/* Align blocks on this */
#define MARIA_MAX_DYN_HEADER_BYTE 13	/* max header uchar for dynamic rows */
#define MARIA_MAX_BLOCK_LENGTH	((((ulong) 1 << 24)-1) & (~ (ulong) (MARIA_DYN_ALIGN_SIZE-1)))
#define MARIA_REC_BUFF_OFFSET      ALIGN_SIZE(MARIA_DYN_DELETE_BLOCK_HEADER+sizeof(uint32))

#define MEMMAP_EXTRA_MARGIN	7	/* Write this as a suffix for file */

#define PACK_TYPE_SELECTED	1	/* Bits in field->pack_type */
#define PACK_TYPE_SPACE_FIELDS	2
#define PACK_TYPE_ZERO_FILL	4
#define MARIA_FOUND_WRONG_KEY 32738	/* Impossible value from ha_key_cmp */

#define MARIA_BLOCK_SIZE(key_length,data_pointer,key_pointer,block_size)  (((((key_length)+(data_pointer)+(key_pointer))*4+(key_pointer)+2)/(block_size)+1)*(block_size))
#define MARIA_MAX_KEYPTR_SIZE	5	/* For calculating block lengths */
#define MARIA_MIN_KEYBLOCK_LENGTH 50	/* When to split delete blocks */

#define MARIA_MIN_SIZE_BULK_INSERT_TREE 16384	/* this is per key */
#define MARIA_MIN_ROWS_TO_USE_BULK_INSERT 100
#define MARIA_MIN_ROWS_TO_DISABLE_INDEXES 100
#define MARIA_MIN_ROWS_TO_USE_WRITE_CACHE 10

/* The UNIQUE check is done with a hashed long key */

#define MARIA_UNIQUE_HASH_TYPE	HA_KEYTYPE_ULONG_INT
#define maria_unique_store(A,B)    mi_int4store((A),(B))

#ifdef THREAD
extern pthread_mutex_t THR_LOCK_maria;
#endif
#if !defined(THREAD) || defined(DONT_USE_RW_LOCKS)
#define rw_wrlock(A) {}
#define rw_rdlock(A) {}
#define rw_unlock(A) {}
#endif


/* Some extern variables */
extern LIST *maria_open_list;
extern uchar NEAR maria_file_magic[], NEAR maria_pack_file_magic[];
extern uint NEAR maria_read_vec[], NEAR maria_readnext_vec[];
extern uint maria_quick_table_bits;
extern const char *maria_data_root;
extern uchar maria_zero_string[];
extern my_bool maria_inited;


/* This is used by _ma_calc_xxx_key_length och _ma_store_key */
typedef struct st_maria_s_param
{
  uint ref_length, key_length, n_ref_length;
  uint n_length, totlength, part_of_prev_key, prev_length, pack_marker;
  const uchar *key;
  uchar *prev_key, *next_key_pos;
  bool store_not_null;
} MARIA_KEY_PARAM;


/* Used to store reference to pinned page */
typedef struct st_pinned_page
{
  PAGECACHE_PAGE_LINK link;
  enum pagecache_page_lock unlock;
} MARIA_PINNED_PAGE;


/* Prototypes for intern functions */
extern int _ma_read_dynamic_record(MARIA_HA *, uchar *, MARIA_RECORD_POS);
extern int _ma_read_rnd_dynamic_record(MARIA_HA *, uchar *, MARIA_RECORD_POS,
                                       my_bool);
extern my_bool _ma_write_dynamic_record(MARIA_HA *, const uchar *);
extern my_bool _ma_update_dynamic_record(MARIA_HA *, MARIA_RECORD_POS,
                                         const uchar *, const uchar *);
extern my_bool _ma_delete_dynamic_record(MARIA_HA *info, const uchar *record);
extern my_bool _ma_cmp_dynamic_record(MARIA_HA *info, const uchar *record);
extern my_bool _ma_write_blob_record(MARIA_HA *, const uchar *);
extern my_bool _ma_update_blob_record(MARIA_HA *, MARIA_RECORD_POS,
                                      const uchar *, const uchar *);
extern int _ma_read_static_record(MARIA_HA *info, uchar *, MARIA_RECORD_POS);
extern int _ma_read_rnd_static_record(MARIA_HA *, uchar *, MARIA_RECORD_POS,
                                      my_bool);
extern my_bool _ma_write_static_record(MARIA_HA *, const uchar *);
extern my_bool _ma_update_static_record(MARIA_HA *, MARIA_RECORD_POS,
                                        const uchar *, const uchar *);
extern my_bool _ma_delete_static_record(MARIA_HA *info, const uchar *record);
extern my_bool _ma_cmp_static_record(MARIA_HA *info, const uchar *record);
extern int _ma_ck_write(MARIA_HA *info, uint keynr, uchar *key,
                        uint length);
extern int _ma_ck_real_write_btree(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                   uchar *key, uint key_length,
                                   MARIA_RECORD_POS *root, uint comp_flag);
extern int _ma_enlarge_root(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                            uchar *key, MARIA_RECORD_POS *root);
extern int _ma_insert(MARIA_HA *info, MARIA_KEYDEF *keyinfo, uchar *key,
                      uchar *anc_buff, uchar *key_pos, uchar *key_buff,
                      uchar *father_buff, uchar *father_keypos,
                      my_off_t father_page, my_bool insert_last);
extern int _ma_split_page(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                          uchar *key, uchar *buff, uchar *key_buff,
                          my_bool insert_last);
extern uchar *_ma_find_half_pos(uint nod_flag, MARIA_KEYDEF *keyinfo,
                                uchar *page, uchar *key,
                                uint *return_key_length,
                                uchar ** after_key);
extern int _ma_calc_static_key_length(MARIA_KEYDEF *keyinfo, uint nod_flag,
                                      uchar *key_pos, uchar *org_key,
                                      uchar *key_buff, const uchar *key,
                                      MARIA_KEY_PARAM *s_temp);
extern int _ma_calc_var_key_length(MARIA_KEYDEF *keyinfo, uint nod_flag,
                                   uchar *key_pos, uchar *org_key,
                                   uchar *key_buff, const uchar *key,
                                   MARIA_KEY_PARAM *s_temp);
extern int _ma_calc_var_pack_key_length(MARIA_KEYDEF *keyinfo,
                                        uint nod_flag, uchar *key_pos,
                                        uchar *org_key, uchar *prev_key,
                                        const uchar *key,
                                        MARIA_KEY_PARAM *s_temp);
extern int _ma_calc_bin_pack_key_length(MARIA_KEYDEF *keyinfo,
                                        uint nod_flag, uchar *key_pos,
                                        uchar *org_key, uchar *prev_key,
                                        const uchar *key,
                                        MARIA_KEY_PARAM *s_temp);
void _ma_store_static_key(MARIA_KEYDEF *keyinfo, uchar *key_pos,
                          MARIA_KEY_PARAM *s_temp);
void _ma_store_var_pack_key(MARIA_KEYDEF *keyinfo, uchar *key_pos,
                            MARIA_KEY_PARAM *s_temp);
#ifdef NOT_USED
void _ma_store_pack_key(MARIA_KEYDEF *keyinfo, uchar *key_pos,
                        MARIA_KEY_PARAM *s_temp);
#endif
void _ma_store_bin_pack_key(MARIA_KEYDEF *keyinfo, uchar *key_pos,
                            MARIA_KEY_PARAM *s_temp);

extern int _ma_ck_delete(MARIA_HA *info, uint keynr, uchar *key,
                         uint key_length);
extern int _ma_readinfo(MARIA_HA *info, int lock_flag, int check_keybuffer);
extern int _ma_writeinfo(MARIA_HA *info, uint options);
extern int _ma_test_if_changed(MARIA_HA *info);
extern int _ma_mark_file_changed(MARIA_HA *info);
extern int _ma_decrement_open_count(MARIA_HA *info);
extern int _ma_check_index(MARIA_HA *info, int inx);
extern int _ma_search(MARIA_HA *info, MARIA_KEYDEF *keyinfo, uchar *key,
                      uint key_len, uint nextflag, my_off_t pos);
extern int _ma_bin_search(struct st_maria_info *info, MARIA_KEYDEF *keyinfo,
                          uchar *page, uchar *key, uint key_len,
                          uint comp_flag, uchar **ret_pos, uchar *buff,
                          my_bool *was_last_key);
extern int _ma_seq_search(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                          uchar *page, uchar *key, uint key_len,
                          uint comp_flag, uchar ** ret_pos, uchar *buff,
                          my_bool *was_last_key);
extern int _ma_prefix_search(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                             uchar *page, uchar *key, uint key_len,
                             uint comp_flag, uchar ** ret_pos, uchar *buff,
                             my_bool *was_last_key);
extern my_off_t _ma_kpos(uint nod_flag, uchar *after_key);
extern void _ma_kpointer(MARIA_HA *info, uchar *buff, my_off_t pos);
extern MARIA_RECORD_POS _ma_dpos(MARIA_HA *info, uint nod_flag,
                                 const uchar *after_key);
extern MARIA_RECORD_POS _ma_rec_pos(MARIA_SHARE *info, uchar *ptr);
extern void _ma_dpointer(MARIA_HA *info, uchar *buff, MARIA_RECORD_POS pos);
extern uint _ma_get_static_key(MARIA_KEYDEF *keyinfo, uint nod_flag,
                               uchar **page, uchar *key);
extern uint _ma_get_pack_key(MARIA_KEYDEF *keyinfo, uint nod_flag,
                             uchar **page, uchar *key);
extern uint _ma_get_binary_pack_key(MARIA_KEYDEF *keyinfo, uint nod_flag,
                                    uchar ** page_pos, uchar *key);
extern uchar *_ma_get_last_key(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                               uchar *keypos, uchar *lastkey,
                               uchar *endpos, uint *return_key_length);
extern uchar *_ma_get_key(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                          uchar *page, uchar *key, uchar *keypos,
                          uint *return_key_length);
extern uint _ma_keylength(MARIA_KEYDEF *keyinfo, const uchar *key);
extern uint _ma_keylength_part(MARIA_KEYDEF *keyinfo, register const uchar *key,
                               HA_KEYSEG *end);
extern uchar *_ma_move_key(MARIA_KEYDEF *keyinfo, uchar *to, const uchar *from);
extern int _ma_search_next(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                           uchar *key, uint key_length, uint nextflag,
                           my_off_t pos);
extern int _ma_search_first(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                            my_off_t pos);
extern int _ma_search_last(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                           my_off_t pos);
extern uchar *_ma_fetch_keypage(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                my_off_t page, int level, uchar *buff,
                                int return_buffer);
extern int _ma_write_keypage(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                             my_off_t page, int level, uchar *buff);
extern int _ma_dispose(MARIA_HA *info, MARIA_KEYDEF *keyinfo, my_off_t pos,
                       int level);
extern my_off_t _ma_new(MARIA_HA *info, MARIA_KEYDEF *keyinfo, int level);
extern uint _ma_make_key(MARIA_HA *info, uint keynr, uchar *key,
                         const uchar *record, MARIA_RECORD_POS filepos);
extern uint _ma_pack_key(MARIA_HA *info, uint keynr, uchar *key,
                         const uchar *old, key_part_map keypart_map,
                         HA_KEYSEG ** last_used_keyseg);
extern int _ma_read_key_record(MARIA_HA *info, uchar *buf, MARIA_RECORD_POS);
extern int _ma_read_cache(IO_CACHE *info, uchar *buff, MARIA_RECORD_POS pos,
                          uint length, int re_read_if_possibly);
extern ulonglong ma_retrieve_auto_increment(MARIA_HA *info, const uchar *record);

extern my_bool _ma_alloc_buffer(uchar **old_addr, size_t *old_size,
                                size_t new_size);
extern ulong _ma_rec_unpack(MARIA_HA *info, uchar *to, uchar *from,
                            ulong reclength);
extern my_bool _ma_rec_check(MARIA_HA *info, const uchar *record,
                             uchar *packpos, ulong packed_length,
                             my_bool with_checkum, ha_checksum checksum);
extern int _ma_write_part_record(MARIA_HA *info, my_off_t filepos,
                                 ulong length, my_off_t next_filepos,
                                 uchar ** record, ulong *reclength,
                                 int *flag);
extern void _ma_print_key(FILE *stream, HA_KEYSEG *keyseg,
                          const uchar *key, uint length);
extern my_bool _ma_once_init_pack_row(MARIA_SHARE *share, File dfile);
extern my_bool _ma_once_end_pack_row(MARIA_SHARE *share);
extern int _ma_read_pack_record(MARIA_HA *info, uchar *buf,
                                MARIA_RECORD_POS filepos);
extern int _ma_read_rnd_pack_record(MARIA_HA *, uchar *, MARIA_RECORD_POS,
                                    my_bool);
extern int _ma_pack_rec_unpack(MARIA_HA *info, MARIA_BIT_BUFF *bit_buff,
                               uchar *to, uchar *from, ulong reclength);
extern ulonglong _ma_safe_mul(ulonglong a, ulonglong b);
extern int _ma_ft_update(MARIA_HA *info, uint keynr, uchar *keybuf,
                         const uchar *oldrec, const uchar *newrec,
                         my_off_t pos);

/*
  Parameter to _ma_get_block_info
  The dynamic row header is read into this struct. For an explanation of
  the fields, look at the function _ma_get_block_info().
*/

typedef struct st_maria_block_info
{
  uchar header[MARIA_BLOCK_INFO_HEADER_LENGTH];
  ulong rec_len;
  ulong data_len;
  ulong block_len;
  ulong blob_len;
  MARIA_RECORD_POS filepos;
  MARIA_RECORD_POS next_filepos;
  MARIA_RECORD_POS prev_filepos;
  uint second_read;
  uint offset;
} MARIA_BLOCK_INFO;


/* bits in return from _ma_get_block_info */

#define BLOCK_FIRST	1
#define BLOCK_LAST	2
#define BLOCK_DELETED	4
#define BLOCK_ERROR	8			/* Wrong data */
#define BLOCK_SYNC_ERROR 16			/* Right data at wrong place */
#define BLOCK_FATAL_ERROR 32			/* hardware-error */

#define NEED_MEM	((uint) 10*4*(IO_SIZE+32)+32) /* Nead for recursion */
#define MAXERR			20
#define BUFFERS_WHEN_SORTING	16		/* Alloc for sort-key-tree */
#define WRITE_COUNT		MY_HOW_OFTEN_TO_WRITE
#define INDEX_TMP_EXT		".TMM"
#define DATA_TMP_EXT		".TMD"

#define UPDATE_TIME		1
#define UPDATE_STAT		2
#define UPDATE_SORT		4
#define UPDATE_AUTO_INC		8
#define UPDATE_OPEN_COUNT	16

#define USE_BUFFER_INIT		(((1024L*512L-MALLOC_OVERHEAD)/IO_SIZE)*IO_SIZE)
#define READ_BUFFER_INIT	(1024L*256L-MALLOC_OVERHEAD)
#define SORT_BUFFER_INIT	(2048L*1024L-MALLOC_OVERHEAD)
#define MIN_SORT_BUFFER		(4096-MALLOC_OVERHEAD)

#define fast_ma_writeinfo(INFO) if (!(INFO)->s->tot_locks) (void) _ma_writeinfo((INFO),0)
#define fast_ma_readinfo(INFO) ((INFO)->lock_type == F_UNLCK) && _ma_readinfo((INFO),F_RDLCK,1)

extern uint _ma_get_block_info(MARIA_BLOCK_INFO *, File, my_off_t);
extern uint _ma_rec_pack(MARIA_HA *info, uchar *to, const uchar *from);
extern uint _ma_pack_get_block_info(MARIA_HA *maria, MARIA_BIT_BUFF *bit_buff,
                                    MARIA_BLOCK_INFO *info, uchar **rec_buff_p,
                                    size_t *rec_buff_size,
                                    File file, my_off_t filepos);
extern void _ma_store_blob_length(uchar *pos, uint pack_length, uint length);
extern void _ma_report_error(int errcode, const char *file_name);
extern my_bool _ma_memmap_file(MARIA_HA *info);
extern void _ma_unmap_file(MARIA_HA *info);
extern uint _ma_save_pack_length(uint version, uchar * block_buff,
                                 ulong length);
extern uint _ma_calc_pack_length(uint version, ulong length);
extern ulong _ma_calc_blob_length(uint length, const uchar *pos);
extern uint _ma_mmap_pread(MARIA_HA *info, uchar *Buffer,
                           uint Count, my_off_t offset, myf MyFlags);
extern uint _ma_mmap_pwrite(MARIA_HA *info, uchar *Buffer,
                            uint Count, my_off_t offset, myf MyFlags);
extern uint _ma_nommap_pread(MARIA_HA *info, uchar *Buffer,
                             uint Count, my_off_t offset, myf MyFlags);
extern uint _ma_nommap_pwrite(MARIA_HA *info, uchar *Buffer,
                              uint Count, my_off_t offset, myf MyFlags);

uint _ma_state_info_write(File file, MARIA_STATE_INFO *state, uint pWrite);
uint _ma_state_info_read_dsk(File file, MARIA_STATE_INFO *state);
uint _ma_base_info_write(File file, MARIA_BASE_INFO *base);
int _ma_keyseg_write(File file, const HA_KEYSEG *keyseg);
char *_ma_keyseg_read(char *ptr, HA_KEYSEG *keyseg);
uint _ma_keydef_write(File file, MARIA_KEYDEF *keydef);
char *_ma_keydef_read(char *ptr, MARIA_KEYDEF *keydef);
uint _ma_uniquedef_write(File file, MARIA_UNIQUEDEF *keydef);
char *_ma_uniquedef_read(char *ptr, MARIA_UNIQUEDEF *keydef);
uint _ma_columndef_write(File file, MARIA_COLUMNDEF *columndef);
char *_ma_columndef_read(char *ptr, MARIA_COLUMNDEF *columndef);
ulong _ma_calc_total_blob_length(MARIA_HA *info, const uchar *record);
ha_checksum _ma_checksum(MARIA_HA *info, const uchar *buf);
ha_checksum _ma_static_checksum(MARIA_HA *info, const uchar *buf);
my_bool _ma_check_unique(MARIA_HA *info, MARIA_UNIQUEDEF *def,
                         uchar *record, ha_checksum unique_hash,
                         MARIA_RECORD_POS pos);
ha_checksum _ma_unique_hash(MARIA_UNIQUEDEF *def, const uchar *buf);
my_bool _ma_cmp_static_unique(MARIA_HA *info, MARIA_UNIQUEDEF *def,
                              const uchar *record, MARIA_RECORD_POS pos);
my_bool _ma_cmp_dynamic_unique(MARIA_HA *info, MARIA_UNIQUEDEF *def,
                               const uchar *record, MARIA_RECORD_POS pos);
my_bool _ma_unique_comp(MARIA_UNIQUEDEF *def, const uchar *a, const uchar *b,
                        my_bool null_are_equal);
void _ma_get_status(void *param, int concurrent_insert);
void _ma_update_status(void *param);
void _ma_restore_status(void *param);
void _ma_copy_status(void *to, void *from);
my_bool _ma_check_status(void *param);
void _ma_reset_status(MARIA_HA *maria);
#include "ma_commit.h"

extern MARIA_HA *_ma_test_if_reopen(char *filename);
my_bool _ma_check_table_is_closed(const char *name, const char *where);
int _ma_open_datafile(MARIA_HA *info, MARIA_SHARE *share, File file_to_dup);
int _ma_open_keyfile(MARIA_SHARE *share);
void _ma_setup_functions(register MARIA_SHARE *share);
my_bool _ma_dynmap_file(MARIA_HA *info, my_off_t size);
void _ma_remap_file(MARIA_HA *info, my_off_t size);

MARIA_RECORD_POS _ma_write_init_default(MARIA_HA *info, const uchar *record);
my_bool _ma_write_abort_default(MARIA_HA *info);

C_MODE_START
#define MARIA_FLUSH_DATA  1
#define MARIA_FLUSH_INDEX 2
int _ma_flush_table_files(MARIA_HA *info, uint flush_data_or_index,
                          enum flush_type flush_type_for_data,
                          enum flush_type flush_type_for_index);
/* Functions needed by _ma_check (are overrided in MySQL) */
volatile int *_ma_killed_ptr(HA_CHECK *param);
void _ma_check_print_error _VARARGS((HA_CHECK *param, const char *fmt, ...));
void _ma_check_print_warning _VARARGS((HA_CHECK *param, const char *fmt, ...));
void _ma_check_print_info _VARARGS((HA_CHECK *param, const char *fmt, ...));
C_MODE_END

int _ma_flush_pending_blocks(MARIA_SORT_PARAM *param);
int _ma_sort_ft_buf_flush(MARIA_SORT_PARAM *sort_param);
int _ma_thr_write_keys(MARIA_SORT_PARAM *sort_param);
#ifdef THREAD
pthread_handler_t _ma_thr_find_all_keys(void *arg);
#endif
int _ma_flush_table_files_after_repair(HA_CHECK *param, MARIA_HA *info);

int _ma_sort_write_record(MARIA_SORT_PARAM *sort_param);
int _ma_create_index_by_sort(MARIA_SORT_PARAM *info, my_bool no_messages,
                             ulong);
int _ma_sync_table_files(const MARIA_HA *info);
int _ma_initialize_data_file(MARIA_SHARE *share, File dfile);
int _ma_update_create_rename_lsn_on_disk(MARIA_SHARE *share, my_bool do_sync);

void _ma_unpin_all_pages(MARIA_HA *info, LSN undo_lsn);
#define _ma_tmp_disable_logging_for_table(S) \
  { (S)->now_transactional= FALSE; (S)->page_type= PAGECACHE_PLAIN_PAGE; }
#define _ma_reenable_logging_for_table(S)                        \
  { if (((S)->now_transactional= (S)->base.born_transactional))  \
      (S)->page_type= PAGECACHE_LSN_PAGE; }

extern PAGECACHE *maria_log_pagecache;
