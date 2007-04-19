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

/* Create a MARIA table */

#include "ma_ftdefs.h"
#include "ma_sp_defs.h"
#include <my_bit.h>
#include "ma_blockrec.h"

#if defined(MSDOS) || defined(__WIN__)
#ifdef __WIN__
#include <fcntl.h>
#else
#include <process.h>			/* Prototype for getpid */
#endif
#endif
#include <m_ctype.h>

static int compare_columns(MARIA_COLUMNDEF **a, MARIA_COLUMNDEF **b);

/*
  Old options is used when recreating database, from maria_chk
*/

int maria_create(const char *name, enum data_file_type datafile_type,
                 uint keys,MARIA_KEYDEF *keydefs,
                 uint columns, MARIA_COLUMNDEF *columndef,
                 uint uniques, MARIA_UNIQUEDEF *uniquedefs,
                 MARIA_CREATE_INFO *ci,uint flags)
{
  register uint i,j;
  File dfile,file;
  int errpos,save_errno, create_mode= O_RDWR | O_TRUNC, res;
  myf create_flag;
  uint length,max_key_length,packed,pack_bytes,pointer,real_length_diff,
       key_length,info_length,key_segs,options,min_key_length_skip,
       base_pos,long_varchar_count,varchar_length,
       unique_key_parts,fulltext_keys,offset;
  uint max_field_lengths, extra_header_size;
  ulong reclength, real_reclength,min_pack_length;
  char filename[FN_REFLEN],linkname[FN_REFLEN], *linkname_ptr;
  ulong pack_reclength;
  ulonglong tot_length,max_rows, tmp;
  enum en_fieldtype type;
  enum data_file_type org_datafile_type= datafile_type;
  MARIA_SHARE share;
  MARIA_KEYDEF *keydef,tmp_keydef;
  MARIA_UNIQUEDEF *uniquedef;
  HA_KEYSEG *keyseg,tmp_keyseg;
  MARIA_COLUMNDEF *column, *end_column;
  ulong *rec_per_key_part;
  my_off_t key_root[HA_MAX_POSSIBLE_KEY];
  MARIA_CREATE_INFO tmp_create_info;
  my_bool tmp_table= FALSE; /* cache for presence of HA_OPTION_TMP_TABLE */
  myf     sync_dir=  MY_SYNC_DIR;
  DBUG_ENTER("maria_create");
  DBUG_PRINT("enter", ("keys: %u  columns: %u  uniques: %u  flags: %u",
                      keys, columns, uniques, flags));

  DBUG_ASSERT(maria_block_size && maria_block_size % IO_SIZE == 0);
  LINT_INIT(dfile);
  LINT_INIT(file);

  if (!ci)
  {
    bzero((char*) &tmp_create_info,sizeof(tmp_create_info));
    ci=&tmp_create_info;
  }

  if (keys + uniques > MARIA_MAX_KEY || columns == 0)
  {
    DBUG_RETURN(my_errno=HA_WRONG_CREATE_OPTION);
  }
  errpos=0;
  options=0;
  bzero((byte*) &share,sizeof(share));

  if (flags & HA_DONT_TOUCH_DATA)
  {
    org_datafile_type= ci->org_data_file_type;
    if (!(ci->old_options & HA_OPTION_TEMP_COMPRESS_RECORD))
      options=ci->old_options &
	(HA_OPTION_COMPRESS_RECORD | HA_OPTION_PACK_RECORD |
	 HA_OPTION_READ_ONLY_DATA | HA_OPTION_CHECKSUM |
	 HA_OPTION_TMP_TABLE | HA_OPTION_DELAY_KEY_WRITE);
    else
    {
      /* Uncompressing rows */
      options=ci->old_options &
	(HA_OPTION_CHECKSUM | HA_OPTION_TMP_TABLE | HA_OPTION_DELAY_KEY_WRITE);
    }
  }

  if (ci->reloc_rows > ci->max_rows)
    ci->reloc_rows=ci->max_rows;		/* Check if wrong parameter */

  if (!(rec_per_key_part=
	(ulong*) my_malloc((keys + uniques)*HA_MAX_KEY_SEG*sizeof(long),
			   MYF(MY_WME | MY_ZEROFILL))))
    DBUG_RETURN(my_errno);

	/* Start by checking fields and field-types used */

  varchar_length=long_varchar_count=packed=
    pack_reclength= max_field_lengths= 0;
  reclength= min_pack_length= ci->null_bytes;

  for (column= columndef, end_column= column + columns ;
       column != end_column ;
       column++)
  {
    /* Fill in not used struct parts */
    column->offset= reclength;
    column->empty_pos= 0;
    column->empty_bit= 0;
    column->fill_length= column->length;

    reclength+= column->length;
    type= column->type;
    if (type == FIELD_SKIP_PRESPACE && datafile_type == BLOCK_RECORD)
      type= FIELD_NORMAL;                /* SKIP_PRESPACE not supported */

    if (type != FIELD_NORMAL && type != FIELD_CHECK)
    {
      column->empty_pos= packed/8;
      column->empty_bit= (1 << (packed & 7));
      if (type == FIELD_BLOB)
      {
        packed++;
	share.base.blobs++;
	if (pack_reclength != INT_MAX32)
	{
	  if (column->length == 4+portable_sizeof_char_ptr)
	    pack_reclength= INT_MAX32;
	  else
          {
            /* Add max possible blob length */
	    pack_reclength+= (1 << ((column->length-
                                     portable_sizeof_char_ptr)*8));
          }
	}
        max_field_lengths+= (column->length - portable_sizeof_char_ptr);
      }
      else if (type == FIELD_SKIP_PRESPACE ||
	       type == FIELD_SKIP_ENDSPACE)
      {
        max_field_lengths+= column->length > 255 ? 2 : 1;
        if (datafile_type != BLOCK_RECORD)
          min_pack_length++;
        packed++;
      }
      else if (type == FIELD_VARCHAR)
      {
	varchar_length+= column->length-1; /* Used for min_pack_length */
	pack_reclength++;
        if (datafile_type != BLOCK_RECORD)
          min_pack_length++;
        max_field_lengths++;
        packed++;
        column->fill_length= 1;
        /* We must test for 257 as length includes pack-length */
        if (test(column->length >= 257))
	{
	  long_varchar_count++;
          max_field_lengths++;
          column->fill_length= 2;
	}
      }
      else if (type == FIELD_SKIP_ZERO)
        packed++;
      else
      {
        if (datafile_type != BLOCK_RECORD || !column->null_bit)
          min_pack_length+= column->length;
        column->empty_pos= 0;
        column->empty_bit= 0;
      }
    }
    else					/* FIELD_NORMAL */
    {
      if (datafile_type != BLOCK_RECORD || !column->null_bit)
        min_pack_length+= column->length;
      if (!column->null_bit)
      {
        share.base.fixed_not_null_fields++;
        share.base.fixed_not_null_fields_length+= column->length;
      }
    }
  }
  if ((packed & 7) == 1)
  {
    /*
      Not optimal packing, try to remove a 1 byte length zero-field as
      this will get same record length, but smaller pack overhead
    */
    while (column != columndef)
    {
      column--;
      if (column->type == (int) FIELD_SKIP_ZERO && column->length == 1)
      {
	column->type=(int) FIELD_NORMAL;
        column->empty_pos= 0;
        column->empty_bit= 0;
	packed--;
	min_pack_length++;
	break;
      }
    }
  }
  share.base.null_bytes= ci->null_bytes;
  share.base.original_null_bytes= ci->null_bytes;
  share.base.transactional= ci->transactional;
  share.base.max_field_lengths= max_field_lengths;
  share.base.field_offsets= 0;                  /* for future */
  
  if (pack_reclength != INT_MAX32)
    pack_reclength+= max_field_lengths + long_varchar_count;

  if (packed && datafile_type == STATIC_RECORD)
    datafile_type= BLOCK_RECORD;
  if (datafile_type == DYNAMIC_RECORD)
    options|= HA_OPTION_PACK_RECORD;	/* Must use packed records */

  if (datafile_type == STATIC_RECORD)
  {
    /* We can't use checksum with static length rows */
    flags&= ~HA_CREATE_CHECKSUM;
    options&= ~HA_OPTION_CHECKSUM;
    min_pack_length+= varchar_length;
  }
  if (flags & HA_CREATE_TMP_TABLE)
  {
    options|= HA_OPTION_TMP_TABLE;
    create_mode|= O_EXCL | O_NOFOLLOW;
  }
  if (flags & HA_CREATE_CHECKSUM || (options & HA_OPTION_CHECKSUM))
  {
    options|= HA_OPTION_CHECKSUM;
    min_pack_length++;
    pack_reclength++;
  }
  if (flags & HA_CREATE_DELAY_KEY_WRITE)
    options|= HA_OPTION_DELAY_KEY_WRITE;
  if (flags & HA_CREATE_RELIES_ON_SQL_LAYER)
    options|= HA_OPTION_RELIES_ON_SQL_LAYER;

  pack_bytes= (packed + 7) / 8;
  if (pack_reclength != INT_MAX32)
    pack_reclength+= reclength+pack_bytes +
      test(test_all_bits(options, HA_OPTION_CHECKSUM | HA_PACK_RECORD));
  min_pack_length+= pack_bytes;
  /* Calculate min possible row length for rows-in-block */
  extra_header_size= MAX_FIXED_HEADER_SIZE;
  if (ci->transactional)
    extra_header_size= TRANS_MAX_FIXED_HEADER_SIZE;
  share.base.min_row_length= (extra_header_size + share.base.null_bytes +
                              pack_bytes);
  if (!ci->data_file_length && ci->max_rows)
  {
    if (pack_reclength == INT_MAX32 ||
             (~(ulonglong) 0)/ci->max_rows < (ulonglong) pack_reclength)
      ci->data_file_length= ~(ulonglong) 0;
    else
      ci->data_file_length=(ulonglong) ci->max_rows*pack_reclength;
  }
  else if (!ci->max_rows)
  {
    if (datafile_type == BLOCK_RECORD)
    {
      uint rows_per_page= ((maria_block_size - PAGE_OVERHEAD_SIZE) /
                           (min_pack_length + extra_header_size +
                            DIR_ENTRY_SIZE));
      ulonglong data_file_length= ci->data_file_length;
      if (!data_file_length)
        data_file_length= ((((ulonglong) 1 << ((BLOCK_RECORD_POINTER_SIZE-1) *
                                               8)) -1));
      if (rows_per_page > 0)
      {
        set_if_smaller(rows_per_page, MAX_ROWS_PER_PAGE);
        ci->max_rows= data_file_length / maria_block_size * rows_per_page;
      }
      else
        ci->max_rows= data_file_length / (min_pack_length +
                                          extra_header_size +
                                          DIR_ENTRY_SIZE);
    }
    else
      ci->max_rows=(ha_rows) (ci->data_file_length/(min_pack_length +
                                                    ((options &
                                                      HA_OPTION_PACK_RECORD) ?
                                                     3 : 0)));
  }
  max_rows= (ulonglong) ci->max_rows;
  if (datafile_type == BLOCK_RECORD)
  {
    /* The + 1 is for record position withing page */
    pointer= maria_get_pointer_length((ci->data_file_length /
                                       maria_block_size), 3) + 1;
    set_if_smaller(pointer, BLOCK_RECORD_POINTER_SIZE);

    if (!max_rows)
      max_rows= (((((ulonglong) 1 << ((pointer-1)*8)) -1) * maria_block_size) /
                 min_pack_length);
  }
  else
  {
    if (datafile_type != STATIC_RECORD)
      pointer= maria_get_pointer_length(ci->data_file_length,
                                        maria_data_pointer_size);
    else
      pointer= maria_get_pointer_length(ci->max_rows, maria_data_pointer_size);
    if (!max_rows)
      max_rows= ((((ulonglong) 1 << (pointer*8)) -1) / min_pack_length);
  }

  real_reclength=reclength;
  if (datafile_type == STATIC_RECORD)
  {
    if (reclength <= pointer)
      reclength=pointer+1;		/* reserve place for delete link */
  }
  else
    reclength+= long_varchar_count;	/* We need space for varchar! */

  max_key_length=0; tot_length=0 ; key_segs=0;
  fulltext_keys=0;
  share.state.rec_per_key_part=rec_per_key_part;
  share.state.key_root=key_root;
  share.state.key_del= HA_OFFSET_ERROR;
  if (uniques)
    max_key_length= MARIA_UNIQUE_HASH_LENGTH + pointer;

  for (i=0, keydef=keydefs ; i < keys ; i++ , keydef++)
  {
    share.state.key_root[i]= HA_OFFSET_ERROR;
    min_key_length_skip=length=real_length_diff=0;
    key_length=pointer;
    if (keydef->flag & HA_SPATIAL)
    {
#ifdef HAVE_SPATIAL
      /* BAR TODO to support 3D and more dimensions in the future */
      uint sp_segs=SPDIMS*2;
      keydef->flag=HA_SPATIAL;

      if (flags & HA_DONT_TOUCH_DATA)
      {
        /*
          Called by maria_chk - i.e. table structure was taken from
          MYI file and SPATIAL key *does have* additional sp_segs keysegs.
          keydef->seg here points right at the GEOMETRY segment,
          so we only need to decrease keydef->keysegs.
          (see maria_recreate_table() in _ma_check.c)
        */
        keydef->keysegs-=sp_segs-1;
      }

      for (j=0, keyseg=keydef->seg ; (int) j < keydef->keysegs ;
	   j++, keyseg++)
      {
        if (keyseg->type != HA_KEYTYPE_BINARY &&
	    keyseg->type != HA_KEYTYPE_VARBINARY1 &&
            keyseg->type != HA_KEYTYPE_VARBINARY2)
        {
          my_errno=HA_WRONG_CREATE_OPTION;
          goto err_no_lock;
        }
      }
      keydef->keysegs+=sp_segs;
      key_length+=SPLEN*sp_segs;
      length++;                              /* At least one length byte */
      min_key_length_skip+=SPLEN*2*SPDIMS;
#else
      my_errno= HA_ERR_UNSUPPORTED;
      goto err_no_lock;
#endif /*HAVE_SPATIAL*/
    }
    else if (keydef->flag & HA_FULLTEXT)
    {
      keydef->flag=HA_FULLTEXT | HA_PACK_KEY | HA_VAR_LENGTH_KEY;
      options|=HA_OPTION_PACK_KEYS;             /* Using packed keys */

      for (j=0, keyseg=keydef->seg ; (int) j < keydef->keysegs ;
	   j++, keyseg++)
      {
        if (keyseg->type != HA_KEYTYPE_TEXT &&
	    keyseg->type != HA_KEYTYPE_VARTEXT1 &&
            keyseg->type != HA_KEYTYPE_VARTEXT2)
        {
          my_errno=HA_WRONG_CREATE_OPTION;
          goto err_no_lock;
        }
        if (!(keyseg->flag & HA_BLOB_PART) &&
	    (keyseg->type == HA_KEYTYPE_VARTEXT1 ||
             keyseg->type == HA_KEYTYPE_VARTEXT2))
        {
          /* Make a flag that this is a VARCHAR */
          keyseg->flag|= HA_VAR_LENGTH_PART;
          /* Store in bit_start number of bytes used to pack the length */
          keyseg->bit_start= ((keyseg->type == HA_KEYTYPE_VARTEXT1)?
                              1 : 2);
        }
      }

      fulltext_keys++;
      key_length+= HA_FT_MAXBYTELEN+HA_FT_WLEN;
      length++;                              /* At least one length byte */
      min_key_length_skip+=HA_FT_MAXBYTELEN;
      real_length_diff=HA_FT_MAXBYTELEN-FT_MAX_WORD_LEN_FOR_SORT;
    }
    else
    {
      /* Test if prefix compression */
      if (keydef->flag & HA_PACK_KEY)
      {
	/* Can't use space_compression on number keys */
	if ((keydef->seg[0].flag & HA_SPACE_PACK) &&
	    keydef->seg[0].type == (int) HA_KEYTYPE_NUM)
	  keydef->seg[0].flag&= ~HA_SPACE_PACK;

	/* Only use HA_PACK_KEY when first segment is a variable length key */
	if (!(keydef->seg[0].flag & (HA_SPACE_PACK | HA_BLOB_PART |
				     HA_VAR_LENGTH_PART)))
	{
	  /* pack relative to previous key */
	  keydef->flag&= ~HA_PACK_KEY;
	  keydef->flag|= HA_BINARY_PACK_KEY | HA_VAR_LENGTH_KEY;
	}
	else
	{
	  keydef->seg[0].flag|=HA_PACK_KEY;	/* for easyer intern test */
	  keydef->flag|=HA_VAR_LENGTH_KEY;
	  options|=HA_OPTION_PACK_KEYS;		/* Using packed keys */
	}
      }
      if (keydef->flag & HA_BINARY_PACK_KEY)
	options|=HA_OPTION_PACK_KEYS;		/* Using packed keys */

      if (keydef->flag & HA_AUTO_KEY && ci->with_auto_increment)
	share.base.auto_key=i+1;
      for (j=0, keyseg=keydef->seg ; j < keydef->keysegs ; j++, keyseg++)
      {
	/* numbers are stored with high by first to make compression easier */
	switch (keyseg->type) {
	case HA_KEYTYPE_SHORT_INT:
	case HA_KEYTYPE_LONG_INT:
	case HA_KEYTYPE_FLOAT:
	case HA_KEYTYPE_DOUBLE:
	case HA_KEYTYPE_USHORT_INT:
	case HA_KEYTYPE_ULONG_INT:
	case HA_KEYTYPE_LONGLONG:
	case HA_KEYTYPE_ULONGLONG:
	case HA_KEYTYPE_INT24:
	case HA_KEYTYPE_UINT24:
	case HA_KEYTYPE_INT8:
	  keyseg->flag|= HA_SWAP_KEY;
          break;
        case HA_KEYTYPE_VARTEXT1:
        case HA_KEYTYPE_VARTEXT2:
        case HA_KEYTYPE_VARBINARY1:
        case HA_KEYTYPE_VARBINARY2:
          if (!(keyseg->flag & HA_BLOB_PART))
          {
            /* Make a flag that this is a VARCHAR */
            keyseg->flag|= HA_VAR_LENGTH_PART;
            /* Store in bit_start number of bytes used to pack the length */
            keyseg->bit_start= ((keyseg->type == HA_KEYTYPE_VARTEXT1 ||
                                 keyseg->type == HA_KEYTYPE_VARBINARY1) ?
                                1 : 2);
          }
          break;
	default:
	  break;
	}
	if (keyseg->flag & HA_SPACE_PACK)
	{
          DBUG_ASSERT(!(keyseg->flag & HA_VAR_LENGTH_PART));
	  keydef->flag |= HA_SPACE_PACK_USED | HA_VAR_LENGTH_KEY;
	  options|=HA_OPTION_PACK_KEYS;		/* Using packed keys */
	  length++;				/* At least one length byte */
	  min_key_length_skip+=keyseg->length;
	  if (keyseg->length >= 255)
	  {					/* prefix may be 3 bytes */
	    min_key_length_skip+=2;
	    length+=2;
	  }
	}
	if (keyseg->flag & (HA_VAR_LENGTH_PART | HA_BLOB_PART))
	{
          DBUG_ASSERT(!test_all_bits(keyseg->flag,
                                    (HA_VAR_LENGTH_PART | HA_BLOB_PART)));
	  keydef->flag|=HA_VAR_LENGTH_KEY;
	  length++;				/* At least one length byte */
	  options|=HA_OPTION_PACK_KEYS;		/* Using packed keys */
	  min_key_length_skip+=keyseg->length;
	  if (keyseg->length >= 255)
	  {					/* prefix may be 3 bytes */
	    min_key_length_skip+=2;
	    length+=2;
	  }
	}
	key_length+= keyseg->length;
	if (keyseg->null_bit)
	{
	  key_length++;
	  options|=HA_OPTION_PACK_KEYS;
	  keyseg->flag|=HA_NULL_PART;
	  keydef->flag|=HA_VAR_LENGTH_KEY | HA_NULL_PART_KEY;
	}
      }
    } /* if HA_FULLTEXT */
    key_segs+=keydef->keysegs;
    if (keydef->keysegs > HA_MAX_KEY_SEG)
    {
      my_errno=HA_WRONG_CREATE_OPTION;
      goto err_no_lock;
    }
    /*
      key_segs may be 0 in the case when we only want to be able to
      add on row into the table. This can happen with some DISTINCT queries
      in MySQL
    */
    if ((keydef->flag & (HA_NOSAME | HA_NULL_PART_KEY)) == HA_NOSAME &&
	key_segs)
      share.state.rec_per_key_part[key_segs-1]=1L;
    length+=key_length;
    /*
      A key can't be longer than than half a index block (as we have
      to be able to put at least 2 keys on an index block for the key
      algorithms to work).
    */
    if (length > maria_max_key_length())
    {
      my_errno=HA_WRONG_CREATE_OPTION;
      goto err_no_lock;
    }
    keydef->block_length= maria_block_size;
    keydef->keylength= (uint16) key_length;
    keydef->minlength= (uint16) (length-min_key_length_skip);
    keydef->maxlength= (uint16) length;

    if (length > max_key_length)
      max_key_length= length;
    tot_length+= ((max_rows/(ulong) (((uint) maria_block_size-5)/
                                     (length*2))) *
                  maria_block_size);
  }

  unique_key_parts=0;
  offset=reclength-uniques*MARIA_UNIQUE_HASH_LENGTH;
  for (i=0, uniquedef=uniquedefs ; i < uniques ; i++ , uniquedef++)
  {
    uniquedef->key=keys+i;
    unique_key_parts+=uniquedef->keysegs;
    share.state.key_root[keys+i]= HA_OFFSET_ERROR;
    tot_length+= (max_rows/(ulong) (((uint) maria_block_size-5)/
                         ((MARIA_UNIQUE_HASH_LENGTH + pointer)*2)))*
                         (ulong) maria_block_size;
  }
  keys+=uniques;				/* Each unique has 1 key */
  key_segs+=uniques;				/* Each unique has 1 key seg */

  base_pos=(MARIA_STATE_INFO_SIZE + keys * MARIA_STATE_KEY_SIZE +
	    key_segs * MARIA_STATE_KEYSEG_SIZE);
  info_length= base_pos+(uint) (MARIA_BASE_INFO_SIZE+
                                keys * MARIA_KEYDEF_SIZE+
                                uniques * MARIA_UNIQUEDEF_SIZE +
                                (key_segs + unique_key_parts)*HA_KEYSEG_SIZE+
                                columns*MARIA_COLUMNDEF_SIZE);

 DBUG_PRINT("info", ("info_length: %u", info_length));
  /* There are only 16 bits for the total header length. */
  if (info_length > 65535)
  {
    my_printf_error(0, "Maria table '%s' has too many columns and/or "
                    "indexes and/or unique constraints.",
                    MYF(0), name + dirname_length(name));
    my_errno= HA_WRONG_CREATE_OPTION;
    goto err_no_lock;
  }

  bmove(share.state.header.file_version,(byte*) maria_file_magic,4);
  ci->old_options=options| (ci->old_options & HA_OPTION_TEMP_COMPRESS_RECORD ?
			HA_OPTION_COMPRESS_RECORD |
			HA_OPTION_TEMP_COMPRESS_RECORD: 0);
  mi_int2store(share.state.header.options,ci->old_options);
  mi_int2store(share.state.header.header_length,info_length);
  mi_int2store(share.state.header.state_info_length,MARIA_STATE_INFO_SIZE);
  mi_int2store(share.state.header.base_info_length,MARIA_BASE_INFO_SIZE);
  mi_int2store(share.state.header.base_pos,base_pos);
  share.state.header.data_file_type= datafile_type;
  share.state.header.org_data_file_type= org_datafile_type;
  share.state.header.language= (ci->language ?
				ci->language : default_charset_info->number);

  share.state.dellink = HA_OFFSET_ERROR;
  share.state.first_bitmap_with_space= 0;
  share.state.process=	(ulong) getpid();
  share.state.unique=	(ulong) 0;
  share.state.update_count=(ulong) 0;
  share.state.version=	(ulong) time((time_t*) 0);
  share.state.sortkey=  (ushort) ~0;
  share.state.auto_increment=ci->auto_increment;
  share.options=options;
  share.base.rec_reflength=pointer;
  share.base.block_size= maria_block_size;

  /* Get estimate for index file length (this may be wrong for FT keys) */
  tmp= (tot_length + maria_block_size * keys *
	MARIA_INDEX_BLOCK_MARGIN) / maria_block_size;
  /*
    use maximum of key_file_length we calculated and key_file_length value we
    got from MYI file header (see also mariapack.c:save_state)
  */
  share.base.key_reflength=
    maria_get_pointer_length(max(ci->key_file_length,tmp),3);
  share.base.keys= share.state.header.keys= keys;
  share.state.header.uniques= uniques;
  share.state.header.fulltext_keys= fulltext_keys;
  mi_int2store(share.state.header.key_parts,key_segs);
  mi_int2store(share.state.header.unique_key_parts,unique_key_parts);

  maria_set_all_keys_active(share.state.key_map, keys);

  share.base.keystart = share.state.state.key_file_length=
    MY_ALIGN(info_length, maria_block_size);
  share.base.max_key_block_length= maria_block_size;
  share.base.max_key_length=ALIGN_SIZE(max_key_length+4);
  share.base.records=ci->max_rows;
  share.base.reloc=  ci->reloc_rows;
  share.base.reclength=real_reclength;
  share.base.pack_reclength=reclength+ test(options & HA_OPTION_CHECKSUM);
  share.base.max_pack_length=pack_reclength;
  share.base.min_pack_length=min_pack_length;
  share.base.pack_bytes= pack_bytes;
  share.base.fields= columns;
  share.base.pack_fields= packed;
#ifdef USE_RAID
  share.base.raid_type=ci->raid_type;
  share.base.raid_chunks=ci->raid_chunks;
  share.base.raid_chunksize=ci->raid_chunksize;
#endif

  /* max_data_file_length and max_key_file_length are recalculated on open */
  if (options & HA_OPTION_TMP_TABLE)
  {
    tmp_table= TRUE;
    sync_dir= 0;
    share.base.max_data_file_length= (my_off_t) ci->data_file_length;
  }

  if (datafile_type == BLOCK_RECORD)
    share.base.min_block_length= share.base.min_row_length;
  else
  {
    share.base.min_block_length=
      (share.base.pack_reclength+3 < MARIA_EXTEND_BLOCK_LENGTH &&
       ! share.base.blobs) ?
      max(share.base.pack_reclength,MARIA_MIN_BLOCK_LENGTH) :
      MARIA_EXTEND_BLOCK_LENGTH;
  }
  if (! (flags & HA_DONT_TOUCH_DATA))
    share.state.create_time= (long) time((time_t*) 0);

  pthread_mutex_lock(&THR_LOCK_maria);

  if (ci->index_file_name)
  {
    char *iext= strrchr(ci->index_file_name, '.');
    int have_iext= iext && !strcmp(iext, MARIA_NAME_IEXT);
    if (tmp_table)
    {
      char *path;
      /* chop off the table name, tempory tables use generated name */
      if ((path= strrchr(ci->index_file_name, FN_LIBCHAR)))
        *path= '\0';
      fn_format(filename, name, ci->index_file_name, MARIA_NAME_IEXT,
                MY_REPLACE_DIR | MY_UNPACK_FILENAME | MY_APPEND_EXT);
    }
    else
    {
      fn_format(filename, ci->index_file_name, "", MARIA_NAME_IEXT,
                MY_UNPACK_FILENAME | (have_iext ? MY_REPLACE_EXT :
                                      MY_APPEND_EXT));
    }
    fn_format(linkname, name, "", MARIA_NAME_IEXT,
              MY_UNPACK_FILENAME|MY_APPEND_EXT);
    linkname_ptr=linkname;
    /*
      Don't create the table if the link or file exists to ensure that one
      doesn't accidently destroy another table.
      Don't sync dir now if the data file has the same path.
    */
    create_flag=
      (ci->data_file_name &&
       !strcmp(ci->index_file_name, ci->data_file_name)) ? 0 : sync_dir;
  }
  else
  {
    fn_format(filename, name, "", MARIA_NAME_IEXT,
              (MY_UNPACK_FILENAME |
               (flags & HA_DONT_TOUCH_DATA) ? MY_RETURN_REAL_PATH : 0) |
                MY_APPEND_EXT);
    linkname_ptr=0;
    /*
      Replace the current file.
      Don't sync dir now if the data file has the same path.
    */
    create_flag= MY_DELETE_OLD | (!ci->data_file_name ? 0 : sync_dir);
  }

  /*
    If a MRG_MARIA table is in use, the mapped MARIA tables are open,
    but no entry is made in the table cache for them.
    A TRUNCATE command checks for the table in the cache only and could
    be fooled to believe, the table is not open.
    Pull the emergency brake in this situation. (Bug #8306)
  */
  if (_ma_test_if_reopen(filename))
  {
    my_printf_error(0, "MARIA table '%s' is in use "
                    "(most likely by a MERGE table). Try FLUSH TABLES.",
                    MYF(0), name + dirname_length(name));
    goto err;
  }

  if ((file= my_create_with_symlink(linkname_ptr, filename, 0, create_mode,
				    MYF(MY_WME|create_flag))) < 0)
    goto err;
  errpos=1;

  if (!(flags & HA_DONT_TOUCH_DATA))
  {
    if (ci->data_file_name)
    {
      char *dext= strrchr(ci->data_file_name, '.');
      int have_dext= dext && !strcmp(dext, MARIA_NAME_DEXT);

      if (tmp_table)
      {
        char *path;
        /* chop off the table name, tempory tables use generated name */
        if ((path= strrchr(ci->data_file_name, FN_LIBCHAR)))
          *path= '\0';
        fn_format(filename, name, ci->data_file_name, MARIA_NAME_DEXT,
                  MY_REPLACE_DIR | MY_UNPACK_FILENAME | MY_APPEND_EXT);
      }
      else
      {
        fn_format(filename, ci->data_file_name, "", MARIA_NAME_DEXT,
                  MY_UNPACK_FILENAME |
                  (have_dext ? MY_REPLACE_EXT : MY_APPEND_EXT));
      }
      fn_format(linkname, name, "",MARIA_NAME_DEXT,
                MY_UNPACK_FILENAME | MY_APPEND_EXT);
      linkname_ptr=linkname;
      create_flag=0;
    }
    else
    {
      fn_format(filename,name,"", MARIA_NAME_DEXT,
                MY_UNPACK_FILENAME | MY_APPEND_EXT);
      linkname_ptr=0;
      create_flag=MY_DELETE_OLD;
    }
    if ((dfile=
         my_create_with_symlink(linkname_ptr, filename, 0, create_mode,
                                MYF(MY_WME | create_flag | sync_dir))) < 0)
      goto err;
    errpos=3;

    if (_ma_initialize_data_file(dfile, &share))
      goto err;
  }
  DBUG_PRINT("info", ("write state info and base info"));
  if (_ma_state_info_write(file, &share.state, 2) ||
      _ma_base_info_write(file, &share.base))
    goto err;
  DBUG_PRINT("info", ("base_pos: %d  base_info_size: %d",
                      base_pos, MARIA_BASE_INFO_SIZE));
  DBUG_ASSERT(my_tell(file,MYF(0)) == base_pos+ MARIA_BASE_INFO_SIZE);

  /* Write key and keyseg definitions */
  DBUG_PRINT("info", ("write key and keyseg definitions"));
  for (i=0 ; i < share.base.keys - uniques; i++)
  {
    uint sp_segs=(keydefs[i].flag & HA_SPATIAL) ? 2*SPDIMS : 0;

    if (_ma_keydef_write(file, &keydefs[i]))
      goto err;
    for (j=0 ; j < keydefs[i].keysegs-sp_segs ; j++)
      if (_ma_keyseg_write(file, &keydefs[i].seg[j]))
       goto err;
#ifdef HAVE_SPATIAL
    for (j=0 ; j < sp_segs ; j++)
    {
      HA_KEYSEG sseg;
      sseg.type=SPTYPE;
      sseg.language= 7;                         /* Binary */
      sseg.null_bit=0;
      sseg.bit_start=0;
      sseg.bit_end=0;
      sseg.bit_length= 0;
      sseg.bit_pos= 0;
      sseg.length=SPLEN;
      sseg.null_pos=0;
      sseg.start=j*SPLEN;
      sseg.flag= HA_SWAP_KEY;
      if (_ma_keyseg_write(file, &sseg))
        goto err;
    }
#endif
  }
  /* Create extra keys for unique definitions */
  offset=reclength-uniques*MARIA_UNIQUE_HASH_LENGTH;
  bzero((char*) &tmp_keydef,sizeof(tmp_keydef));
  bzero((char*) &tmp_keyseg,sizeof(tmp_keyseg));
  for (i=0; i < uniques ; i++)
  {
    tmp_keydef.keysegs=1;
    tmp_keydef.flag=		HA_UNIQUE_CHECK;
    tmp_keydef.block_length=	(uint16) maria_block_size;
    tmp_keydef.keylength=	MARIA_UNIQUE_HASH_LENGTH + pointer;
    tmp_keydef.minlength=tmp_keydef.maxlength=tmp_keydef.keylength;
    tmp_keyseg.type=		MARIA_UNIQUE_HASH_TYPE;
    tmp_keyseg.length=		MARIA_UNIQUE_HASH_LENGTH;
    tmp_keyseg.start=		offset;
    offset+=			MARIA_UNIQUE_HASH_LENGTH;
    if (_ma_keydef_write(file,&tmp_keydef) ||
	_ma_keyseg_write(file,(&tmp_keyseg)))
      goto err;
  }

  /* Save unique definition */
  DBUG_PRINT("info", ("write unique definitions"));
  for (i=0 ; i < share.state.header.uniques ; i++)
  {
    HA_KEYSEG *keyseg_end;
    keyseg= uniquedefs[i].seg;
    if (_ma_uniquedef_write(file, &uniquedefs[i]))
      goto err;
    for (keyseg= uniquedefs[i].seg, keyseg_end= keyseg+ uniquedefs[i].keysegs;
         keyseg < keyseg_end;
         keyseg++)
    {
      switch (keyseg->type) {
      case HA_KEYTYPE_VARTEXT1:
      case HA_KEYTYPE_VARTEXT2:
      case HA_KEYTYPE_VARBINARY1:
      case HA_KEYTYPE_VARBINARY2:
        if (!(keyseg->flag & HA_BLOB_PART))
        {
          keyseg->flag|= HA_VAR_LENGTH_PART;
          keyseg->bit_start= ((keyseg->type == HA_KEYTYPE_VARTEXT1 ||
                               keyseg->type == HA_KEYTYPE_VARBINARY1) ?
                              1 : 2);
        }
        break;
      default:
        DBUG_ASSERT((keyseg->flag & HA_VAR_LENGTH_PART) == 0);
        break;
      }
      if (_ma_keyseg_write(file, keyseg))
	goto err;
    }
  }
  DBUG_PRINT("info", ("write field definitions"));
  if (datafile_type == BLOCK_RECORD)
  {
    /* Store columns in a more efficent order */
    MARIA_COLUMNDEF **col_order, **pos;
    if (!(col_order= (MARIA_COLUMNDEF**) my_malloc(share.base.fields *
                                                   sizeof(MARIA_COLUMNDEF*),
                                                   MYF(MY_WME))))
      goto err;
    for (column= columndef, pos= col_order ;
         column != end_column ;
         column++, pos++)
      *pos= column;
    qsort(col_order, share.base.fields, sizeof(*col_order),
          (qsort_cmp) compare_columns);
    for (i=0 ; i < share.base.fields ; i++)
    {
      if (_ma_columndef_write(file, col_order[i]))
      {
        my_free((gptr) col_order, MYF(0));
        goto err;
      }
    }
    my_free((gptr) col_order, MYF(0));
  }
  else
  {
    for (i=0 ; i < share.base.fields ; i++)
      if (_ma_columndef_write(file, &columndef[i]))
        goto err;
  }

#ifndef DBUG_OFF
  if ((uint) my_tell(file,MYF(0)) != info_length)
  {
    uint pos= (uint) my_tell(file,MYF(0));
    DBUG_PRINT("warning",("info_length: %d  != used_length: %d",
			  info_length, pos));
  }
#endif

	/* Enlarge files */
  DBUG_PRINT("info", ("enlarge to keystart: %lu",
                      (ulong) share.base.keystart));
  if (my_chsize(file,(ulong) share.base.keystart,0,MYF(0)))
    goto err;

  if (! (flags & HA_DONT_TOUCH_DATA))
  {
#ifdef USE_RELOC
    if (my_chsize(dfile,share.base.min_pack_length*ci->reloc_rows,0,MYF(0)))
      goto err;
    if (!tmp_table && my_sync(file, MYF(0)))
      goto err;
#endif
    /* if !USE_RELOC, there was no write to the file, no need to sync it */
    errpos=2;
    if (my_close(dfile,MYF(0)))
      goto err;
  }
  errpos=0;
  pthread_mutex_unlock(&THR_LOCK_maria);
  res= 0;
  if (my_close(file,MYF(0)))
    res= my_errno;
  /*
    RECOVERY TODO
    Write a log record describing the CREATE operation (just the file
    names, link names, and the full header's content).
    For this record to be of any use for Recovery, we need the upper
    MySQL layer to be crash-safe, which it is not now (that would require work
    using the ddl_log of sql/sql_table.cc); when is is, we should reconsider
    the moment of writing this log record (before or after op, under
    THR_LOCK_maria or not...), how to use it in Recovery, and force the log.
    For now this record is just informative.
    If operation failed earlier, we clean up in "err:" and the MySQL layer
    will clean up the frm, so we needn't write anything to the log.
  */
  my_free((char*) rec_per_key_part,MYF(0));
  DBUG_RETURN(res);

err:
  pthread_mutex_unlock(&THR_LOCK_maria);

err_no_lock:
  save_errno=my_errno;
  switch (errpos) {
  case 3:
    VOID(my_close(dfile,MYF(0)));
    /* fall through */
  case 2:
  if (! (flags & HA_DONT_TOUCH_DATA))
    my_delete_with_symlink(fn_format(filename,name,"",MARIA_NAME_DEXT,
                                     MY_UNPACK_FILENAME | MY_APPEND_EXT),
			   sync_dir);
    /* fall through */
  case 1:
    VOID(my_close(file,MYF(0)));
    if (! (flags & HA_DONT_TOUCH_DATA))
      my_delete_with_symlink(fn_format(filename,name,"",MARIA_NAME_IEXT,
                                       MY_UNPACK_FILENAME | MY_APPEND_EXT),
			     sync_dir);
  }
  my_free((char*) rec_per_key_part, MYF(0));
  DBUG_RETURN(my_errno=save_errno);		/* return the fatal errno */
}


uint maria_get_pointer_length(ulonglong file_length, uint def)
{
  DBUG_ASSERT(def >= 2 && def <= 7);
  if (file_length)				/* If not default */
  {
#ifdef NOT_YET_READY_FOR_8_BYTE_POINTERS
    if (file_length >= (ULL(1) << 56))
      def=8;
    else
#endif
      if (file_length >= (ULL(1) << 48))
      def=7;
    else if (file_length >= (ULL(1) << 40))
      def=6;
    else if (file_length >= (ULL(1) << 32))
      def=5;
    else if (file_length >= (ULL(1) << 24))
      def=4;
    else if (file_length >= (ULL(1) << 16))
      def=3;
    else
      def=2;
  }
  return def;
}


/*
  Sort columns for records-in-block

  IMPLEMENTATION
   Sort columns in following order:

   Fixed size, not null columns
   Fixed length, null fields
   Variable length fields (CHAR, VARCHAR)
   Blobs

   For same kind of fields, keep fields in original order
*/

static inline int sign(longlong a)
{
  return a < 0 ? -1 : (a > 0 ? 1 : 0);
}


static int compare_columns(MARIA_COLUMNDEF **a_ptr, MARIA_COLUMNDEF **b_ptr)
{
  MARIA_COLUMNDEF *a= *a_ptr, *b= *b_ptr;
  enum en_fieldtype a_type, b_type;

  a_type= ((a->type == FIELD_NORMAL || a->type == FIELD_CHECK) ?
           FIELD_NORMAL : a->type);
  b_type= ((b->type == FIELD_NORMAL || b->type == FIELD_CHECK) ?
           FIELD_NORMAL : b->type);

  if (a_type == FIELD_NORMAL && !a->null_bit)
  {
    if (b_type != FIELD_NORMAL || b->null_bit)
      return -1;
    return sign((long) (a->offset - b->offset));
  }
  if (b_type == FIELD_NORMAL && !b->null_bit)
    return 1;
  if (a_type == b_type)
    return sign((long) (a->offset - b->offset));
  if (a_type == FIELD_NORMAL)
    return -1;
  if (b_type == FIELD_NORMAL)
    return 1;
  if (a_type == FIELD_BLOB)
    return 1;
  if (b_type == FIELD_BLOB)
    return -1;
  return sign((long) (a->offset - b->offset));
}


/* Initialize data file */

int _ma_initialize_data_file(File dfile, MARIA_SHARE *share)
{
  if (share->data_file_type == BLOCK_RECORD)
  {
    if (my_chsize(dfile, maria_block_size, 0, MYF(MY_WME)))
      return 1;
    share->state.state.data_file_length= maria_block_size;
    _ma_bitmap_delete_all(share);
  }
  return 0;
}
