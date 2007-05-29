/* Copyright (C) 2007 Michael Widenius

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

/*
  Storage of records in block

  Some clarifications about the abbrev used:

  NULL fields      -> Fields that may have contain a NULL value.
  Not null fields  -> Fields that may not contain a NULL value.
  Critical fields  -> Fields that can't be null and can't be dropped without
		      causing a table reorganization.


  Maria will have a LSN at start of each page (excluding the bitmap pages)

  The different page types that are in a data file are:

  Bitmap pages     Map of free pages in the next extent (8192 page size
                   gives us 256M of mapped pages / bitmap)
  Head page        Start of rows are stored on this page.
                   A rowid always points to a head page
  Blob page        This page is totally filled with data from one blob or by
                   a set of long VARCHAR/CHAR fields
  Tail page        This contains the last part from different rows, blobs
                   or varchar fields.

  The data file starts with a bitmap page, followed by as many data
  pages as the bitmap can cover. After this there is a new bitmap page
  and more data pages etc.

  For information about the bitmap page, see ma_bitmap.c

  Structure of data and tail page:

  The page has a row directory at end of page to allow us to do deletes
  without having to reorganize the page.  It also allows us to later store
  some more bytes after each row to allow them to grow without having to move
  around other rows.

  Page header:

  LSN          7 bytes  Log position for last page change
  PAGE_TYPE    1 byte   1 for head / 2 for tail / 3 for blob
  NO           1 byte   Number of row/tail entries on page
  empty space  2 bytes  Empty space on page

  The most significant bit in PAGE_TYPE is set to 1 if the data on the page
  can be compacted to get more space. (PAGE_CAN_BE_COMPACTED)

  Row data

  Row directory of NO entries, that consist of the following for each row
  (in reverse order; i.e., first record is stored last):

  Position      2 bytes Position of row on page
  Length        2 bytes Length of entry

  For Position and Length, the 1 most significant bit of the position and
  the 1 most significant bit of the length could be used for some states of
  the row (in other words, we should try to keep these reserved)

  eof flag  1 byte   Reserved for full page read testing. (Ie, did the
		     previous write get the whole block on disk.

  ----------------

  Structure of blob pages:

  LSN       7 bytes  Log position for last page change
  PAGE_TYPE 1 byte   3

  data

  -----------------

  Row data structure:

  Flag                          1 byte   Marker of which header field exists
  TRANSID                       6 bytes  TRANSID of changing transaction
                                         (optional, added on insert and first
                                         update/delete)
  VER_PTR                       7 bytes  Pointer to older version in log
                                         (undo record)
                                         (optional, added after first
                                         update/delete)
  DELETE_TRANSID                6 bytes  (optional). TRANSID of original row.
                                         Added on delete.
  Nulls_extended                1 byte   To allow us to add new DEFAULT NULL
                                         fields (optional, added after first
                                         change of row after alter table)
  Number of ROW_EXTENT's        1-3 byte Length encoded, optional
                                         This is the number of extents the
                                         row is split into
  First row_extent              7 byte   Pointer to first row extent (optional)

  Total length of length array  1-3 byte Only used if we have
                                         char/varchar/blob fields.
  Row checksum		        1 byte   Only if table created with checksums
  Null_bits             ..      One bit for each NULL field (a field that may
				have the value NULL)
  Empty_bits            ..      One bit for each field that may be 'empty'.
				(Both for null and not null fields).
                                This bit is 1 if the value for the field is
                                0 or empty string.

  field_offsets                 2 byte/offset
                                  For each 32'th field, there is one offset
                                  that points to where the field information
                                  starts in the block. This is to provide
                                  fast access to later field in the row
                                  when we only need to return a small
                                  set of fields.
                                  TODO: Implement this.

  Things marked above as 'optional' will only be present if the
  corresponding bit is set in 'Flag' field.  Flag gives us a way to
  get more space on a page when doing page compaction as we don't need
  to store TRANSID that have committed before the smallest running
  transaction we have in memory.

  Data in the following order:
  (Field order is precalculated when table is created)

  Critical fixed length, not null, fields. (Note, these can't be dropped)
  Fixed length, null fields

  Length array, 1-4 byte per field for all CHAR/VARCHAR/BLOB fields.
  Number of bytes used in length array per entry is depending on max length
  for field.

  ROW_EXTENT's
  CHAR data (space stripped)
  VARCHAR data
  BLOB data

  Fields marked in null_bits or empty_bits are not stored in data part or
  length array.

  If row doesn't fit into the given block, then the first EXTENT will be
  stored last on the row. This is done so that we don't break any field
  data in the middle.

  We first try to store the full row into one block. If that's not possible
  we move out each big blob into their own extents. If this is not enough we
  move out a concatenation of all varchars to their own extent.

  Each blob and the concatenated char/varchar fields are stored the following
  way:
  - Store the parts in as many full-contiguous pages as possible.
  - The last part, that doesn't fill a full page, is stored in tail page.

  When doing an insert of a new row, we don't have to have
  VER_PTR in the row. This will make rows that are not changed stored
  efficiently. On update and delete we would add TRANSID (if it was an old
  committed row) and VER_PTR to
  the row. On row page compaction we can easily detect rows where
  TRANSID was committed before the longest running transaction
  started and we can then delete TRANSID and VER_PTR from the row to
  gain more space.

  If a row is deleted in Maria, we change TRANSID to current transid and
  change VER_PTR to point to the undo record for the delete. The undo
  record must contain the original TRANSID, so that another transaction
  can use this to check if they should use the found row or go to the
  previous row pointed to by the VER_PTR in the undo row.

  Description of the different parts:

  Flag is coded as:

  Description           bit
  TRANS_ID_exists       0
  VER_PTR_exists        1
  Row is deleted        2       (Means that DELETE_TRANSID exists)
  Nulls_extended_exists 3
  Row is split          7       This means that 'Number_of_row_extents' exists

  Nulls_extended is the number of new DEFAULT NULL fields in the row
  compared to the number of DEFAULT NULL fields when the first version
  of the table was created.  If Nulls_extended doesn't exist in the row,
  we know it's 0 as this must be one of the original rows from when the
  table was created first time.  This coding allows us to add 255*8 =
  2048 new fields without requiring a full alter table.

  Empty_bits is used to allow us to store 0, 0.0, empty string, empty
  varstring and empty blob efficiently. (This is very good for data
  warehousing where NULL's are often regarded as evil). Having this
  bitmap also allows us to drop information of a field during a future
  delete if field was deleted with ALTER TABLE DROP COLUMN.  To be able
  to handle DROP COLUMN, we must store in the index header the fields
  that has been dropped. When unpacking a row we will ignore dropped
  fields. When storing a row, we will mark a dropped field either with a
  null in the null bit map or in the empty_bits and not store any data
  for it.
  TODO: Add code for handling dropped fields.


  A ROW EXTENT is range of pages. One ROW_EXTENT is coded as:

  START_PAGE            5 bytes
  PAGE_COUNT            2 bytes.  High bit is used to indicate tail page/
                                  end of blob
  With 8K pages, we can cover 256M in one extent. This coding gives us a
  maximum file size of 2^40*8192 = 8192 tera

  As an example of ROW_EXTENT handling, assume a row with one integer
  field (value 5), two big VARCHAR fields (size 250 and 8192*3), and 2
  big BLOB fields that we have updated.

  The record format for storing this into an empty file would be:

  Page 1:

  00 00 00 00 00 00 00          LSN
  01                            Only one row in page
  xx xx                         Empty space on page

  10                            Flag: row split, VER_PTR exists
  01 00 00 00 00 00             TRANSID 1
  00 00 00 00 00 01 00          VER_PTR to first block in LOG file 1
  5                             Number of row extents
  02 00 00 00 00 03 00          VARCHAR's are stored in full pages 2,3,4
  0                             No null fields
  0                             No empty fields
  05 00 00 00 00 00 80          Tail page for VARCHAR, rowid 0
  06 00 00 00 00 80 00          First blob, stored at page 6-133
  05 00 00 00 00 01 80          Tail of first blob (896 bytes) at page 5
  86 00 00 00 00 80 00          Second blob, stored at page 134-262
  05 00 00 00 00 02 80          Tail of second blob (896 bytes) at page 5
  05 00                         5 integer
  FA                            Length of first varchar field (size 250)
  00 60                         Length of second varchar field (size 8192*3)
  00 60 10                      First medium BLOB, 1M
  01 00 10 00                   Second BLOB, 1M
  xx xx xx xx xx xx             Varchars are stored here until end of page

  ..... until end of page

  09 00 F4 1F 00                (Start position 9, length 8180, end byte)
*/

#define SANITY_CHECKS

#include "maria_def.h"
#include "ma_blockrec.h"
#include <lf.h>
#include "trnman.h"

/*
  Struct for having a cursor over a set of extent.
  This is used to loop over all extents for a row when reading
  the row data. It's also used to store the tail positions for
  a read row to be used by a later update/delete command.
*/

typedef struct st_maria_extent_cursor
{
  /*
    Pointer to packed byte array of extents for the row.
    Format is described above in the header
  */
  byte *extent;
  /* Where data starts on page; Only for debugging */
  byte *data_start;
  /* Position to all tails in the row. Updated when reading a row */
  MARIA_RECORD_POS *tail_positions;
  /* Current page */
  my_off_t page;
  /* How many pages in the page region */
  uint page_count;
  /* Total number of extents (i.e., entries in the 'extent' slot) */
  uint extent_count;
  /* <> 0 if current extent is a tail page; Set while using cursor */
  uint tail;
  /*
    <> 1 if we are working on the first extent (i.e., the one that is store in
    the row header, not an extent that is stored as part of the row data).
  */
  my_bool first_extent;
} MARIA_EXTENT_CURSOR;


static my_bool delete_tails(MARIA_HA *info, MARIA_RECORD_POS *tails);
static my_bool delete_head_or_tail(MARIA_HA *info,
                                  ulonglong page, uint record_number,
                                  my_bool head);
static void _ma_print_directory(byte *buff, uint block_size);
static void compact_page(byte *buff, uint block_size, uint rownr,
                         my_bool extend_block);
static uchar *store_page_range(uchar *to, MARIA_BITMAP_BLOCK *block,
                               uint block_size, ulong length);
static size_t fill_insert_undo_parts(MARIA_HA *info, const byte *record,
                                     LEX_STRING *log_parts,
                                     uint *log_parts_count);
static size_t fill_update_undo_parts(MARIA_HA *info, const byte *oldrec,
                                     const byte *newrec,
                                     LEX_STRING *log_parts,
                                     uint *log_parts_count);

/****************************************************************************
  Initialization
****************************************************************************/

/*
  Initialize data needed for block structures
*/


/* Size of the different header elements for a row */

static uchar header_sizes[]=
{
  TRANSID_SIZE,
  VERPTR_SIZE,
  TRANSID_SIZE,                                 /* Delete transid */
  1                                             /* Null extends */
};

/*
  Calculate array of all used headers

  Used to speed up:

  size= 1;
  if (flag & 1)
    size+= TRANSID_SIZE;
  if (flag & 2)
    size+= VERPTR_SIZE;
  if (flag & 4)
    size+= TRANSID_SIZE
  if (flag & 8)
    size+= 1;

   NOTES
     This is called only once at startup of Maria
*/

static uchar total_header_size[1 << array_elements(header_sizes)];
#define PRECALC_HEADER_BITMASK (array_elements(total_header_size) -1)

void _ma_init_block_record_data(void)
{
  uint i;
  bzero(total_header_size, sizeof(total_header_size));
  total_header_size[0]= FLAG_SIZE;              /* Flag byte */
  for (i= 1; i < array_elements(total_header_size); i++)
  {
    uint size= FLAG_SIZE, j, bit;
    for (j= 0; (bit= (1 << j)) <= i; j++)
    {
      if (i & bit)
        size+= header_sizes[j];
    }
    total_header_size[i]= size;
  }
}


my_bool _ma_once_init_block_record(MARIA_SHARE *share, File data_file)
{

  share->base.max_data_file_length=
    (((ulonglong) 1 << ((share->base.rec_reflength-1)*8))-1) *
    share->block_size;
#if SIZEOF_OFF_T == 4
    set_if_smaller(share->base.max_data_file_length, INT_MAX32);
#endif
  return _ma_bitmap_init(share, data_file);
}


my_bool _ma_once_end_block_record(MARIA_SHARE *share)
{
  int res= _ma_bitmap_end(share);
  if (share->bitmap.file.file >= 0)
  {
    if (flush_pagecache_blocks(share->pagecache, &share->bitmap.file,
                               share->temporary ? FLUSH_IGNORE_CHANGED :
                               FLUSH_RELEASE))
      res= 1;
    if (my_close(share->bitmap.file.file, MYF(MY_WME)))
      res= 1;
    /*
      Trivial assignment to guard against multiple invocations
      (May happen if file are closed but we want to keep the maria object
      around a bit longer)
    */
    share->bitmap.file.file= -1;
  }
  return res;
}


/* Init info->cur_row structure */

my_bool _ma_init_block_record(MARIA_HA *info)
{
  MARIA_ROW *row= &info->cur_row, *new_row= &info->new_row;
  DBUG_ENTER("_ma_init_block_record");

  if (!my_multi_malloc(MY_WME,
                       &row->empty_bits, info->s->base.pack_bytes,
                       &row->field_lengths,
                       info->s->base.max_field_lengths + 2,
                       &row->blob_lengths, sizeof(ulong) * info->s->base.blobs,
                       &row->null_field_lengths, (sizeof(uint) *
                                                  (info->s->base.fields -
                                                   info->s->base.blobs +
                                                   EXTRA_LENGTH_FIELDS)),
                       &row->tail_positions, (sizeof(MARIA_RECORD_POS) *
                                              (info->s->base.blobs + 2)),
                       &new_row->empty_bits, info->s->base.pack_bytes,
                       &new_row->field_lengths,
                       info->s->base.max_field_lengths + 2,
                       &new_row->blob_lengths,
                       sizeof(ulong) * info->s->base.blobs,
                       &new_row->null_field_lengths, (sizeof(uint) *
                                                      (info->s->base.fields -
                                                       info->s->base.blobs +
                                                       EXTRA_LENGTH_FIELDS)),
                       &info->log_row_parts,
                       sizeof(*info->log_row_parts) *
                       (TRANSLOG_INTERNAL_PARTS + 2 +
                        info->s->base.fields + 2),
                       &info->update_field_data,
                       (info->s->base.fields * 4 +
                        info->s->base.max_field_lengths + 1 + 4),
                       NullS, 0))
    DBUG_RETURN(1);
  /* Skip over bytes used to store length of field length for logging */
  row->field_lengths+= 2;
  new_row->field_lengths+= 2;
  if (my_init_dynamic_array(&info->bitmap_blocks,
                            sizeof(MARIA_BITMAP_BLOCK),
                            ELEMENTS_RESERVED_FOR_MAIN_PART, 16))
    goto err;
  /* The following should be big enough for all purposes */
  if (my_init_dynamic_array(&info->pinned_pages,
                            sizeof(MARIA_PINNED_PAGE),
                            max(info->s->base.blobs + 2,
                                MARIA_MAX_TREE_LEVELS*2), 16))
    goto err;
  row->base_length= new_row->base_length= info->s->base_length;

  /*
    We need to reserve 'EXTRA_LENGTH_FIELDS' number of parts in
    null_field_lengths to allow splitting of rows in 'find_where_to_split_row'
  */

  row->null_field_lengths+= EXTRA_LENGTH_FIELDS;
  new_row->null_field_lengths+= EXTRA_LENGTH_FIELDS;

  DBUG_RETURN(0);

err:
  _ma_end_block_record(info);
  DBUG_RETURN(1);
}


void _ma_end_block_record(MARIA_HA *info)
{
  DBUG_ENTER("_ma_end_block_record");
  my_free((gptr) info->cur_row.empty_bits, MYF(MY_ALLOW_ZERO_PTR));
  delete_dynamic(&info->bitmap_blocks);
  delete_dynamic(&info->pinned_pages);
  my_free((gptr) info->cur_row.extents, MYF(MY_ALLOW_ZERO_PTR));
  /*
    The data file is closed, when needed, in ma_once_end_block_record().
    The following protects us from doing an extra, not allowed, close
    in maria_close()
  */
  info->dfile.file= -1;
  DBUG_VOID_RETURN;
}


/****************************************************************************
  Helper functions
****************************************************************************/

/*
  Return the next used byte on the page after a directory entry.

  SYNOPSIS
    start_of_next_entry()
    dir		Directory entry to be used

  RETURN
    #   Position in page where next entry starts.
        Everything between the '*dir' and this are free to be used.
*/

static inline uint start_of_next_entry(byte *dir)
{
  byte *prev;
  /*
     Find previous used entry. (There is always a previous entry as
     the directory never starts with a deleted entry)
  */
  for (prev= dir - DIR_ENTRY_SIZE ;
       prev[0] == 0 && prev[1] == 0 ;
       prev-= DIR_ENTRY_SIZE)
  {}
  return (uint) uint2korr(prev);
}


/*
  Check that a region is all zero

  SYNOPSIS
    check_if_zero()
    pos		Start of memory to check
    length	length of memory region

  NOTES
    Used mainly to detect rows with wrong extent information
*/

static my_bool check_if_zero(byte *pos, uint length)
{
  byte *end;
  for (end= pos+ length; pos != end ; pos++)
    if (pos[0] != 0)
      return 1;
  return 0;
}


/*
  Unpin all pinned pages

  SYNOPSIS
    _ma_unpin_all_pages()
    info	Maria handler
    undo_lsn	LSN for undo pages. 0 if we shouldn't write undo (error)

  NOTE
    We unpin pages in the reverse order as they where pinned; This may not
    be strictly necessary but may simplify things in the future.

  RETURN
    0   ok
    1   error (fatal disk error)

*/

void _ma_unpin_all_pages(MARIA_HA *info, LSN undo_lsn)
{
  MARIA_PINNED_PAGE *page_link= ((MARIA_PINNED_PAGE*)
                                 dynamic_array_ptr(&info->pinned_pages, 0));
  MARIA_PINNED_PAGE *pinned_page= page_link + info->pinned_pages.elements;
  DBUG_ENTER("_ma_unpin_all_pages");
  DBUG_PRINT("info", ("undo_lsn: %lu", (ulong) undo_lsn));

  /* True if not disk error */
  DBUG_ASSERT(undo_lsn != 0 || info->s->base.transactional == 0);

  while (pinned_page-- != page_link)
    pagecache_unlock(info->s->pagecache, pinned_page->link,
                     pinned_page->unlock, PAGECACHE_UNPIN, 0, undo_lsn);

  info->pinned_pages.elements= 0;
  DBUG_VOID_RETURN;
}


/*
  Find free position in directory

  SYNOPSIS
  find_free_position()
    buff                Page
    block_size          Size of page
    res_rownr           Store index to free position here
    res_length		Store length of found segment here
    empty_space		Store length of empty space on disk here. This is
		        all empty space, including the found block.

  NOTES
    If there is a free directory entry (entry with position == 0),
    then use it and change it to be the size of the empty block
    after the previous entry. This guarantees that all row entries
    are stored on disk in inverse directory order, which makes life easier for
    'compact_page()' and to know if there is free space after any block.

    If there is no free entry (entry with position == 0), then we create
    a new one. If there is not space for the directory entry (because
    the last block overlapps with the directory), we compact the page.

    We will update the offset and the length of the found dir entry to
    match the position and empty space found.

    buff[EMPTY_SPACE_OFFSET] is NOT updated but left up to the caller

  RETURN
    0      Error (directory full or last block goes over directory)
    #      Pointer to directory entry on page
*/

static byte *find_free_position(byte *buff, uint block_size, uint *res_rownr,
                                uint *res_length, uint *empty_space)
{
  uint max_entry= (uint) ((uchar*) buff)[DIR_COUNT_OFFSET];
  uint entry, length, first_pos;
  byte *dir, *end;
  DBUG_ENTER("find_free_position");
  DBUG_PRINT("info", ("max_entry: %u", max_entry));

  dir= (buff + block_size - DIR_ENTRY_SIZE * max_entry - PAGE_SUFFIX_SIZE);
  end= buff + block_size - PAGE_SUFFIX_SIZE - DIR_ENTRY_SIZE;

  *empty_space= uint2korr(buff + EMPTY_SPACE_OFFSET);

  /* Search after first empty position */
  first_pos= PAGE_HEADER_SIZE;
  for (entry= 0 ; dir <= end ; end-= DIR_ENTRY_SIZE, entry++)
  {
    uint tmp= uint2korr(end);
    if (!tmp)             /* Found not used entry */
    {
      length= start_of_next_entry(end) - first_pos;
      int2store(end, first_pos);                /* Update dir entry */
      int2store(end + 2, length);
      *res_rownr= entry;
      *res_length= length;
      DBUG_RETURN(end);
    }
    first_pos= tmp + uint2korr(end + 2);
  }
  /* No empty places in dir; create a new one */
  dir= end;
  /* Check if there is place for the directory entry */
  if (max_entry == MAX_ROWS_PER_PAGE)
    DBUG_RETURN(0);
  /* Check if there is place for the directory entry */
  if ((uint) (dir - buff) < first_pos)
  {
    /* Create place for directory */
    compact_page(buff, block_size, max_entry-1, 0);
    first_pos= (uint2korr(end + DIR_ENTRY_SIZE) +
                uint2korr(end + DIR_ENTRY_SIZE+ 2));
    *empty_space= uint2korr(buff + EMPTY_SPACE_OFFSET);
  }
  buff[DIR_COUNT_OFFSET]= (byte) (uchar) max_entry+1;
  length= (uint) (dir - buff - first_pos);
  DBUG_ASSERT(length <= *empty_space - DIR_ENTRY_SIZE);
  int2store(dir, first_pos);
  int2store(dir+2, length);                     /* Current max length */
  *res_rownr= max_entry;
  *res_length= length;

  /* Reduce directory entry size from free space size */
  (*empty_space)-= DIR_ENTRY_SIZE;
  DBUG_RETURN(dir);
}


/****************************************************************************
  Updating records
****************************************************************************/

/*
  Calculate length of all the different field parts

  SYNOPSIS
    calc_record_size()
    info	Maria handler
    record      Row to store
    row		Store statistics about row here

  NOTES
    The statistics is used to find out how much space a row will need
    and also where we can split a row when we need to split it into several
    extents.
*/

static void calc_record_size(MARIA_HA *info, const byte *record,
                             MARIA_ROW *row)
{
  MARIA_SHARE *share= info->s;
  byte *field_length_data;
  MARIA_COLUMNDEF *column, *end_column;
  uint *null_field_lengths= row->null_field_lengths;
  ulong *blob_lengths= row->blob_lengths;

  row->normal_length= row->char_length= row->varchar_length=
    row->blob_length= row->extents_count= 0;

  /* Create empty bitmap and calculate length of each varlength/char field */
  bzero(row->empty_bits, share->base.pack_bytes);
  field_length_data= row->field_lengths;
  for (column= share->columndef + share->base.fixed_not_null_fields,
       end_column= share->columndef + share->base.fields;
       column < end_column; column++, null_field_lengths++)
  {
    if ((record[column->null_pos] & column->null_bit))
    {
      if (column->type != FIELD_BLOB)
        *null_field_lengths= 0;
      else
        *blob_lengths++= 0;
      continue;
    }
    switch ((enum en_fieldtype) column->type) {
    case FIELD_CHECK:
    case FIELD_NORMAL:                          /* Fixed length field */
    case FIELD_ZERO:
      DBUG_ASSERT(column->empty_bit == 0);
      /* fall through */
    case FIELD_SKIP_PRESPACE:                   /* Not packed */
      row->normal_length+= column->length;
      *null_field_lengths= column->length;
      break;
    case FIELD_SKIP_ZERO:                       /* Fixed length field */
      if (memcmp(record+ column->offset, maria_zero_string,
                 column->length) == 0)
      {
        row->empty_bits[column->empty_pos] |= column->empty_bit;
        *null_field_lengths= 0;
      }
      else
      {
        row->normal_length+= column->length;
        *null_field_lengths= column->length;
      }
      break;
    case FIELD_SKIP_ENDSPACE:                   /* CHAR */
    {
      const char *pos, *end;
      for (pos= record + column->offset, end= pos + column->length;
           end > pos && end[-1] == ' '; end--)
        ;
      if (pos == end)                           /* If empty string */
      {
        row->empty_bits[column->empty_pos]|= column->empty_bit;
        *null_field_lengths= 0;
      }
      else
      {
        uint length= (end - pos);
        if (column->length <= 255)
          *field_length_data++= (byte) (uchar) length;
        else
        {
          int2store(field_length_data, length);
          field_length_data+= 2;
        }
        row->char_length+= length;
        *null_field_lengths= length;
      }
      break;
    }
    case FIELD_VARCHAR:
    {
      uint length, field_length_data_length;
      const byte *field_pos= record + column->offset;
      /* 256 is correct as this includes the length byte */

      field_length_data[0]= field_pos[0];
      if (column->length <= 256)
      {
        length= (uint) (uchar) *field_pos;
        field_length_data_length= 1;
      }
      else
      {
        length= uint2korr(field_pos);
        field_length_data[1]= field_pos[1];
        field_length_data_length= 2;
      }
      *null_field_lengths= length;
      if (!length)
      {
        row->empty_bits[column->empty_pos]|= column->empty_bit;
        break;
      }
      row->varchar_length+= length;
      *null_field_lengths= length;
      field_length_data+= field_length_data_length;
      break;
    }
    case FIELD_BLOB:
    {
      const byte *field_pos= record + column->offset;
      uint size_length= column->length - portable_sizeof_char_ptr;
      ulong blob_length= _ma_calc_blob_length(size_length, field_pos);

      *blob_lengths++= blob_length;
      if (!blob_length)
        row->empty_bits[column->empty_pos]|= column->empty_bit;
      else
      {
        row->blob_length+= blob_length;
        memcpy(field_length_data, field_pos, size_length);
        field_length_data+= size_length;
      }
      break;
    }
    default:
      DBUG_ASSERT(0);
    }
  }
  row->field_lengths_length= (uint) (field_length_data - row->field_lengths);
  row->head_length= (row->base_length +
                     share->base.fixed_not_null_fields_length +
                     row->field_lengths_length +
                     size_to_store_key_length(row->field_lengths_length) +
                     row->normal_length +
                     row->char_length + row->varchar_length);
  row->total_length= (row->head_length + row->blob_length);
  if (row->total_length < share->base.min_row_length)
    row->total_length= share->base.min_row_length;
}


/*
  Compact page by removing all space between rows

  IMPLEMENTATION
    Move up all rows to start of page.
    Move blocks that are directly after each other with one memmove.

  TODO LATER
    Remove TRANSID from rows that are visible to all transactions

  SYNOPSIS
    compact_page()
    buff                Page to compact
    block_size          Size of page
    recnr               Put empty data after this row
    extend_block	If 1, extend the block at 'rownr' to cover the
                        whole block.
*/


static void compact_page(byte *buff, uint block_size, uint rownr,
                         my_bool extend_block)
{
  uint max_entry= (uint) ((uchar *) buff)[DIR_COUNT_OFFSET];
  uint page_pos, next_free_pos, start_of_found_block, diff, end_of_found_block;
  byte *dir, *end;
  DBUG_ENTER("compact_page");
  DBUG_PRINT("enter", ("rownr: %u", rownr));
  DBUG_ASSERT(max_entry > 0 &&
              max_entry < (block_size - PAGE_HEADER_SIZE -
                           PAGE_SUFFIX_SIZE) / DIR_ENTRY_SIZE);

  /* Move all entries before and including rownr up to start of page */
  dir= buff + block_size - DIR_ENTRY_SIZE * (rownr+1) - PAGE_SUFFIX_SIZE;
  end= buff + block_size - DIR_ENTRY_SIZE - PAGE_SUFFIX_SIZE;
  page_pos= next_free_pos= start_of_found_block= PAGE_HEADER_SIZE;
  diff= 0;
  for (; dir <= end ; end-= DIR_ENTRY_SIZE)
  {
    uint offset= uint2korr(end);

    if (offset)
    {
      uint row_length= uint2korr(end + 2);
      DBUG_ASSERT(offset >= page_pos);
      DBUG_ASSERT(buff + offset + row_length <= dir);

      if (offset != next_free_pos)
      {
        uint length= (next_free_pos - start_of_found_block);
        /*
           There was empty space before this and prev block
           Check if we have to move previous block up to page start
        */
        if (page_pos != start_of_found_block)
        {
          /* move up previous block */
          memmove(buff + page_pos, buff + start_of_found_block, length);
        }
        page_pos+= length;
        /* next continuous block starts here */
        start_of_found_block= offset;
        diff= offset - page_pos;
      }
      int2store(end, offset - diff);            /* correct current pos */
      next_free_pos= offset + row_length;
    }
  }
  if (page_pos != start_of_found_block)
  {
    uint length= (next_free_pos - start_of_found_block);
    memmove(buff + page_pos, buff + start_of_found_block, length);
  }
  start_of_found_block= uint2korr(dir);

  if (rownr != max_entry - 1)
  {
    /* Move all entries after rownr to end of page */
    uint rownr_length;
    next_free_pos= end_of_found_block= page_pos=
      block_size - DIR_ENTRY_SIZE * max_entry - PAGE_SUFFIX_SIZE;
    diff= 0;
    /* End points to entry before 'rownr' */
    for (dir= buff + end_of_found_block ; dir <= end ; dir+= DIR_ENTRY_SIZE)
    {
      uint offset= uint2korr(dir);
      uint row_length= uint2korr(dir + 2);
      uint row_end= offset + row_length;
      if (!offset)
        continue;
      DBUG_ASSERT(offset >= start_of_found_block && row_end <= next_free_pos);

      if (row_end != next_free_pos)
      {
        uint length= (end_of_found_block - next_free_pos);
        if (page_pos != end_of_found_block)
        {
          /* move next block down */
          memmove(buff + page_pos - length, buff + next_free_pos, length);
        }
        page_pos-= length;
        /* next continuous block starts here */
        end_of_found_block= row_end;
        diff= page_pos - row_end;
      }
      int2store(dir, offset + diff);            /* correct current pos */
      next_free_pos= offset;
    }
    if (page_pos != end_of_found_block)
    {
      uint length= (end_of_found_block - next_free_pos);
      memmove(buff + page_pos - length, buff + next_free_pos, length);
      next_free_pos= page_pos- length;
    }
    /* Extend rownr block to cover hole */
    rownr_length= next_free_pos - start_of_found_block;
    int2store(dir+2, rownr_length);
  }
  else
  {
    if (extend_block)
    {
      /* Extend last block cover whole page */
      uint length= (uint) (dir - buff) - start_of_found_block;
      int2store(dir+2, length);
    }
    buff[PAGE_TYPE_OFFSET]&= ~(byte) PAGE_CAN_BE_COMPACTED;
  }
  DBUG_EXECUTE("directory", _ma_print_directory(buff, block_size););
  DBUG_VOID_RETURN;
}



/*
  Read or initialize new head or tail page

  SYNOPSIS
    get_head_or_tail_page()
    info                        Maria handler
    block                       Block to read
    buff                        Suggest this buffer to key cache
    length                      Minimum space needed
    page_type			HEAD_PAGE || TAIL_PAGE
    res                         Store result position here

  NOTES
    We don't decremented buff[EMPTY_SPACE_OFFSET] with the allocated data
    as we don't know how much data the caller will actually use.

  RETURN
    0  ok     All slots in 'res' are updated
    1  error  my_errno is set
*/

struct st_row_pos_info
{
  byte *buff;                                   /* page buffer */
  byte *data;                                   /* Place for data */
  byte *dir;                                    /* Directory */
  uint length;                                  /* Length for data */
  uint rownr;                                   /* Offset in directory */
  uint empty_space;                             /* Space left on page */
};

static my_bool get_head_or_tail_page(MARIA_HA *info,
                                     MARIA_BITMAP_BLOCK *block,
                                     byte *buff, uint length, uint page_type,
                                     enum pagecache_page_lock lock,
                                     struct st_row_pos_info *res)
{
  uint block_size;
  MARIA_PINNED_PAGE page_link;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("get_head_or_tail_page");
  DBUG_PRINT("enter", ("length: %u", length));

  block_size= share->block_size;
  if (block->org_bitmap_value == 0)             /* Empty block */
  {
    /* New page */
    bzero(buff, PAGE_HEADER_SIZE);

    /*
      We zero the rest of the block to avoid getting old memory information
      to disk and to allow the file to be compressed better if archived.
      The rest of the code does not assume the block is zeroed above
      PAGE_OVERHEAD_SIZE
    */
    bzero(buff+ PAGE_HEADER_SIZE, block_size - PAGE_HEADER_SIZE);

    buff[PAGE_TYPE_OFFSET]= (byte) page_type;
    buff[DIR_COUNT_OFFSET]= 1;
    res->buff= buff;
    res->empty_space= res->length= (block_size - PAGE_OVERHEAD_SIZE);
    res->data= (buff + PAGE_HEADER_SIZE);
    res->dir= res->data + res->length;
    res->rownr= 0;
    /* Store position to the first row */
    int2store(res->dir, PAGE_HEADER_SIZE);
    DBUG_ASSERT(length <= res->length);
  }
  else
  {
    byte *dir;
    /* TODO: lock the page */
    /* Read old page */
    DBUG_ASSERT(share->pagecache->block_size == block_size);
    if (!(res->buff= pagecache_read(share->pagecache,
                                    &info->dfile,
                                    (my_off_t) block->page, 0,
                                    buff, share->page_type,
                                    lock, &page_link.link)))
      DBUG_RETURN(1);
    if (lock != PAGECACHE_LOCK_LEFT_UNLOCKED)
    {
      page_link.unlock= (lock == PAGECACHE_LOCK_READ ?
                         PAGECACHE_LOCK_READ_UNLOCK :
                         PAGECACHE_LOCK_WRITE_UNLOCK);
      push_dynamic(&info->pinned_pages, (void*) &page_link);
    }

    DBUG_ASSERT((res->buff[PAGE_TYPE_OFFSET] & PAGE_TYPE_MASK) == page_type);
    if (!(dir= find_free_position(res->buff, block_size, &res->rownr,
                                  &res->length, &res->empty_space)))
      goto crashed;

    if (res->length < length)
    {
      if (res->empty_space + res->length < length)
      {
        compact_page(res->buff, block_size, res->rownr, 1);
        /* All empty space are now after current position */
        dir= (res->buff + block_size - DIR_ENTRY_SIZE * res->rownr -
              PAGE_SUFFIX_SIZE);
        res->length= res->empty_space= uint2korr(dir+2);
      }
      if (res->length < length)
        goto crashed;                         /* Wrong bitmap information */
    }
    res->dir= dir;
    res->data= res->buff + uint2korr(dir);
  }
  DBUG_RETURN(0);

crashed:
  my_errno= HA_ERR_WRONG_IN_RECORD;             /* File crashed */
  DBUG_RETURN(1);
}


/*
  Write tail for head data or blob

  SYNOPSIS
    write_tail()
    info                Maria handler
    block               Block to tail page
    row_part            Data to write to page
    length              Length of data

  NOTES
    block->page_count is updated to the directory offset for the tail
    so that we can store the position in the row extent information

  RETURN
    0  ok
       block->page_count is set to point (dir entry + TAIL_BIT)

    1  error; In this case my_errno is set to the error
*/

static my_bool write_tail(MARIA_HA *info,
                          MARIA_BITMAP_BLOCK *block,
                          byte *row_part, uint length)
{
  MARIA_SHARE *share= share= info->s;
  MARIA_PINNED_PAGE page_link;
  uint block_size= share->block_size, empty_space;
  struct st_row_pos_info row_pos;
  my_off_t position;
  my_bool res, block_is_read;
  DBUG_ENTER("write_tail");
  DBUG_PRINT("enter", ("page: %lu  length: %u",
                       (ulong) block->page, length));

  info->keyread_buff_used= 1;

  /* page will be pinned & locked by get_head_or_tail_page */
  if (get_head_or_tail_page(info, block, info->keyread_buff, length,
                            TAIL_PAGE, PAGECACHE_LOCK_WRITE,
                            &row_pos))
    DBUG_RETURN(1);
  block_is_read= block->org_bitmap_value != 0;

  memcpy(row_pos.data, row_part, length);

  {
    /* Log changes in tail block */
    uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + DIRPOS_STORE_SIZE];
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
    LSN lsn;

    /* Log REDO changes of tail page */
    fileid_store(log_data, info->dfile.file);
    page_store(log_data+ FILEID_STORE_SIZE, block->page);
    dirpos_store(log_data+ FILEID_STORE_SIZE + PAGE_STORE_SIZE,
                 row_pos.rownr);
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    (char*) row_pos.data;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= length;
    if (translog_write_record(&lsn, LOGREC_REDO_INSERT_ROW_TAIL,
                              info->trn->short_id, NULL, share,
                              sizeof(log_data) + length,
                              TRANSLOG_INTERNAL_PARTS + 2,
                              log_array))
      DBUG_RETURN(1);
  }

  /*
    Don't allocate smaller block than MIN_TAIL_SIZE (we want to give rows
    some place to grow in the future)
  */
  if (length < MIN_TAIL_SIZE)
    length= MIN_TAIL_SIZE;
  int2store(row_pos.dir + 2, length);
  empty_space= row_pos.empty_space - length;
  int2store(row_pos.buff + EMPTY_SPACE_OFFSET, empty_space);
  block->page_count= row_pos.rownr + TAIL_BIT;
  /*
    If there is less directory entries free than number of possible tails
    we can write for a row, we mark the page full to ensure that we don't
    during _ma_bitmap_find_place() allocate more entries on the tail page
    than it can hold
  */
  block->empty_space= ((uint) ((uchar*) row_pos.buff)[DIR_COUNT_OFFSET] <=
                       MAX_ROWS_PER_PAGE - 1 - share->base.blobs ?
                       empty_space : 0);
  block->used= BLOCKUSED_USED | BLOCKUSED_TAIL;

  /* Increase data file size, if extended */
  position= (my_off_t) block->page * block_size;
  if (info->state->data_file_length <= position)
    info->state->data_file_length= position + block_size;

  DBUG_ASSERT(share->pagecache->block_size == block_size);
  if (!(res= pagecache_write(share->pagecache,
                             &info->dfile, block->page, 0,
                             row_pos.buff,share->page_type,
                             block_is_read ? PAGECACHE_LOCK_WRITE_TO_READ :
                             PAGECACHE_LOCK_READ,
                             block_is_read ? PAGECACHE_PIN_LEFT_PINNED :
                             PAGECACHE_PIN,
                             PAGECACHE_WRITE_DELAY, &page_link.link)))
  {
    page_link.unlock= PAGECACHE_LOCK_READ_UNLOCK;
    if (block_is_read)
    {
      /* Change the lock used when we read the page */
      set_dynamic(&info->pinned_pages, (void*) &page_link,
                  info->pinned_pages.elements-1);
    }
    else
      push_dynamic(&info->pinned_pages, (void*) &page_link);
  }
  DBUG_RETURN(res);
}


/*
  Write full pages

  SYNOPSIS
    write_full_pages()
    info                Maria handler
    lsn			LSN for the undo record
    block               Where to write data
    data                Data to write
    length              Length of data

  NOTES
    Logging of the changes to the full pages are done in the caller
    write_block_record().

  RETURN
    0  ok
    1  error on write
*/

static my_bool write_full_pages(MARIA_HA *info,
                                LSN lsn,
                                MARIA_BITMAP_BLOCK *block,
                                byte *data, ulong length)
{
  my_off_t page;
  MARIA_SHARE *share= share= info->s;
  uint block_size= share->block_size;
  uint data_size= FULL_PAGE_SIZE(block_size);
  byte *buff= info->keyread_buff;
  uint page_count;
  my_off_t position;
  DBUG_ENTER("write_full_pages");
  DBUG_PRINT("enter", ("length: %lu  page: %lu  page_count: %lu",
                       (ulong) length, (ulong) block->page,
                       (ulong) block->page_count));

  info->keyread_buff_used= 1;
  page=       block->page;
  page_count= block->page_count;

  position= (my_off_t) (page + page_count) * block_size;
  if (info->state->data_file_length < position)
    info->state->data_file_length= position;

  /* Increase data file size, if extended */

  for (; length; data+= data_size)
  {
    uint copy_length;
    if (!page_count--)
    {
      block++;
      page= block->page;
      page_count= block->page_count - 1;
      DBUG_PRINT("info", ("page: %lu  page_count: %lu",
                          (ulong) block->page, (ulong) block->page_count));

      position= (page + page_count + 1) * block_size;
      if (info->state->data_file_length < position)
        info->state->data_file_length= position;
    }
    lsn_store(buff, lsn);
    buff[PAGE_TYPE_OFFSET]= (byte) BLOB_PAGE;
    copy_length= min(data_size, length);
    memcpy(buff + LSN_SIZE + PAGE_TYPE_SIZE, data, copy_length);
    length-= copy_length;

    DBUG_ASSERT(share->pagecache->block_size == block_size);
    if (pagecache_write(share->pagecache,
                        &info->dfile, page, 0,
                        buff, share->page_type,
                        PAGECACHE_LOCK_LEFT_UNLOCKED,
                        PAGECACHE_PIN_LEFT_UNPINNED,
                        PAGECACHE_WRITE_DELAY,
                        0))
      DBUG_RETURN(1);
    page++;
    block->used= BLOCKUSED_USED;
  }
  DBUG_RETURN(0);
}


/*
  Store ranges of full pages in compact format for logging

  SYNOPSIS
    store_page_range()
    to		Store data here
    block       Where pages are to be written
    block_size  block size
    length	Length of data to be written
		Normally this is full pages, except for the last
                tail block that may only partly fit the last page.

  RETURN
    #  end position for 'to'
*/

static uchar *store_page_range(uchar *to, MARIA_BITMAP_BLOCK *block,
                               uint block_size, ulong length)
{
  uint data_size= FULL_PAGE_SIZE(block_size);
  ulong pages_left= (length + data_size -1) / data_size;
  uint page_count;
  DBUG_ENTER("store_page_range");

  do
  {
    ulonglong page;
    page=       block->page;
    page_count= block->page_count;
    block++;
    if (page_count > pages_left)
      page_count= pages_left;

    page_store(to, page);
    to+= PAGE_STORE_SIZE;
    pagerange_store(to, page_count);
    to+= PAGERANGE_STORE_SIZE;
  } while ((pages_left-= page_count));
  DBUG_RETURN(to);
}


/*
  Store packed extent data

  SYNOPSIS
   store_extent_info()
   to				Store first packed data here
   row_extents_second_part	Store rest here
   first_block		        First block to store
   count			Number of blocks

  NOTES
    We don't have to store the position for the head block
*/

static void store_extent_info(byte *to,
                              byte *row_extents_second_part,
                              MARIA_BITMAP_BLOCK *first_block,
                              uint count)
{
  MARIA_BITMAP_BLOCK *block, *end_block;
  uint copy_length;
  my_bool first_found= 0;

  for (block= first_block, end_block= first_block+count ;
       block < end_block; block++)
  {
    /* The following is only false for marker blocks */
    if (likely(block->used & BLOCKUSED_USED))
    {
      DBUG_ASSERT(block->page_count != 0);
      page_store(to, block->page);
      pagerange_store(to + PAGE_STORE_SIZE, block->page_count);
      to+= ROW_EXTENT_SIZE;
      if (!first_found)
      {
        first_found= 1;
        to= row_extents_second_part;
      }
    }
  }
  copy_length= (count - 1) * ROW_EXTENT_SIZE;
  /*
    In some unlikely cases we have allocated to many blocks. Clear this
    data.
  */
  bzero(to, (my_size_t) (row_extents_second_part + copy_length - to));
}


/*
  Free regions of pages with logging

  RETURN
    0   ok
    1   error
*/

static my_bool free_full_pages(MARIA_HA *info, MARIA_ROW *row)
{
  uchar log_data[FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE];
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
  LSN lsn;
  size_t extents_length= row->extents_count * ROW_EXTENT_SIZE;
  DBUG_ENTER("free_full_pages");

  fileid_store(log_data, info->dfile.file);
  pagerange_store(log_data + FILEID_STORE_SIZE,
                  row->extents_count);
  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    row->extents;
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= extents_length;
  if (translog_write_record(&lsn, LOGREC_REDO_PURGE_BLOCKS,
                            info->trn->short_id, NULL, info->s,
                            sizeof(log_data) + extents_length,
                            TRANSLOG_INTERNAL_PARTS + 2, log_array))
    DBUG_RETURN(1);

  DBUG_RETURN (_ma_bitmap_free_full_pages(info, row->extents,
                                          row->extents_count));
}


/*
  Free one page range

  NOTES
    This is very similar to free_full_pages()

  RETURN
    0   ok
    1   error
*/

static my_bool free_full_page_range(MARIA_HA *info, ulonglong page, uint count)
{
  uchar log_data[FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE +
                 ROW_EXTENT_SIZE];
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 1];
  LSN lsn;
  my_bool res= 0;

  if (pagecache_delete_pages(info->s->pagecache, &info->dfile,
                             page, count, PAGECACHE_LOCK_WRITE, 0))
    res= 1;

  fileid_store(log_data, info->dfile.file);
  pagerange_store(log_data + FILEID_STORE_SIZE, 1);
  int5store(log_data + FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE,
            page);
  int2store(log_data + FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE + 5,
            count);
  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);

  if (translog_write_record(&lsn, LOGREC_REDO_PURGE_BLOCKS,
                            info->trn->short_id, NULL, info->s,
                            sizeof(log_data),
                            TRANSLOG_INTERNAL_PARTS + 1, log_array))
    res= 1;

  pthread_mutex_lock(&info->s->bitmap.bitmap_lock);
  if (_ma_reset_full_page_bits(info, &info->s->bitmap, page,
                               count))
    res= 1;
  pthread_mutex_unlock(&info->s->bitmap.bitmap_lock);
  return res;
}


/*
  Write a record to a (set of) pages

  SYNOPSIS
    write_block_record()
    info	Maria handler
    old_record  Orignal record in case of update; NULL in case of insert
    record	Record we should write
    row		Statistics about record (calculated by calc_record_size())
    map_blocks  On which pages the record should be stored
    row_pos	Position on head page where to put head part of record

  NOTES
    On return all pinned pages are released.

  RETURN
    0 ok
    1 error
*/

static my_bool write_block_record(MARIA_HA *info,
                                  const byte *old_record, const byte *record,
                                  MARIA_ROW *row,
                                  MARIA_BITMAP_BLOCKS *bitmap_blocks,
                                  my_bool head_block_is_read,
                                  struct st_row_pos_info *row_pos)
{
  byte *data, *end_of_data, *tmp_data_used, *tmp_data;
  byte *row_extents_first_part, *row_extents_second_part;
  byte *field_length_data;
  byte *page_buff;
  MARIA_BITMAP_BLOCK *block, *head_block;
  MARIA_SHARE *share= info->s;
  MARIA_COLUMNDEF *column, *end_column;
  MARIA_PINNED_PAGE page_link;
  uint block_size, flag;
  ulong *blob_lengths;
  my_bool row_extents_in_use, blob_full_pages_exists;
  LSN lsn;
  my_off_t position;
  DBUG_ENTER("write_block_record");

  LINT_INIT(row_extents_first_part);
  LINT_INIT(row_extents_second_part);

  head_block= bitmap_blocks->block;
  block_size= share->block_size;

  page_buff= row_pos->buff;
  /* Position on head page where we should store the head part */
  data= row_pos->data;
  end_of_data= data + row_pos->length;

  /* Write header */
  flag= share->base.default_row_flag;
  row_extents_in_use= 0;
  if (unlikely(row->total_length > row_pos->length))
  {
    /* Need extent */
    if (bitmap_blocks->count <= 1)
      goto crashed;                             /* Wrong in bitmap */
    flag|= ROW_FLAG_EXTENTS;
    row_extents_in_use= 1;
  }
  /* For now we have only a minimum header */
  *data++= (uchar) flag;
  if (unlikely(flag & ROW_FLAG_NULLS_EXTENDED))
    *data++= (uchar) (share->base.null_bytes -
                      share->base.original_null_bytes);
  if (row_extents_in_use)
  {
    /* Store first extent in header */
    store_key_length_inc(data, bitmap_blocks->count - 1);
    row_extents_first_part= data;
    data+= ROW_EXTENT_SIZE;
  }
  if (share->base.pack_fields)
    store_key_length_inc(data, row->field_lengths_length);
  if (share->calc_checksum)
    *(data++)= (byte) info->cur_row.checksum;
  memcpy(data, record, share->base.null_bytes);
  data+= share->base.null_bytes;
  memcpy(data, row->empty_bits, share->base.pack_bytes);
  data+= share->base.pack_bytes;

  /*
    Allocate a buffer of rest of data (except blobs)

    To avoid double copying of data, we copy as many columns that fits into
    the page. The rest goes into info->packed_row.

    Using an extra buffer, instead of doing continuous writes to different
    pages, uses less code and we don't need to have to do a complex call
    for every data segment we want to store.
  */
  if (_ma_alloc_buffer(&info->rec_buff, &info->rec_buff_size,
                       row->head_length))
    DBUG_RETURN(1);

  tmp_data_used= 0;                  /* Either 0 or last used byte in 'data' */
  tmp_data= data;

  if (row_extents_in_use)
  {
    uint copy_length= (bitmap_blocks->count - 2) * ROW_EXTENT_SIZE;
    if (!tmp_data_used && tmp_data + copy_length > end_of_data)
    {
      tmp_data_used= tmp_data;
      tmp_data= info->rec_buff;
    }
    row_extents_second_part= tmp_data;
    /*
       We will copy the extents here when we have figured out the tail
       positions.
    */
    tmp_data+= copy_length;
  }

  /* Copy fields that has fixed lengths (primary key etc) */
  for (column= share->columndef,
         end_column= column + share->base.fixed_not_null_fields;
       column < end_column; column++)
  {
    if (!tmp_data_used && tmp_data + column->length > end_of_data)
    {
      tmp_data_used= tmp_data;
      tmp_data= info->rec_buff;
    }
    memcpy(tmp_data, record + column->offset, column->length);
    tmp_data+= column->length;
  }

  /* Copy length of data for variable length fields */
  if (!tmp_data_used && tmp_data + row->field_lengths_length > end_of_data)
  {
    tmp_data_used= tmp_data;
    tmp_data= info->rec_buff;
  }
  field_length_data= row->field_lengths;
  memcpy(tmp_data, field_length_data, row->field_lengths_length);
  tmp_data+= row->field_lengths_length;

  /* Copy variable length fields and fields with null/zero */
  for (end_column= share->columndef + share->base.fields - share->base.blobs;
       column < end_column ;
       column++)
  {
    const byte *field_pos;
    ulong length;
    if ((record[column->null_pos] & column->null_bit) ||
        (row->empty_bits[column->empty_pos] & column->empty_bit))
      continue;

    field_pos= record + column->offset;
    switch ((enum en_fieldtype) column->type) {
    case FIELD_NORMAL:                          /* Fixed length field */
    case FIELD_SKIP_PRESPACE:
    case FIELD_SKIP_ZERO:                       /* Fixed length field */
      length= column->length;
      break;
    case FIELD_SKIP_ENDSPACE:                   /* CHAR */
      /* Char that is space filled */
      if (column->length <= 255)
        length= (uint) (uchar) *field_length_data++;
      else
      {
        length= uint2korr(field_length_data);
        field_length_data+= 2;
      }
      break;
    case FIELD_VARCHAR:
      if (column->length <= 256)
      {
        length= (uint) (uchar) *field_length_data++;
        field_pos++;                            /* Skip length byte */
      }
      else
      {
        length= uint2korr(field_length_data);
        field_length_data+= 2;
        field_pos+= 2;
      }
      break;
    default:                                    /* Wrong data */
      DBUG_ASSERT(0);
      break;
    }
    if (!tmp_data_used && tmp_data + length > end_of_data)
    {
      /* Data didn't fit in page; Change to use tmp buffer */
      tmp_data_used= tmp_data;
      tmp_data= info->rec_buff;
    }
    memcpy((char*) tmp_data, (char*) field_pos, length);
    tmp_data+= length;
  }

  block= head_block + head_block->sub_blocks;   /* Point to first blob data */

  end_column= column + share->base.blobs;
  blob_lengths= row->blob_lengths;
  if (!tmp_data_used)
  {
    /* Still room on page; Copy as many blobs we can into this page */
    data= tmp_data;
    for (; column < end_column && *blob_lengths < (ulong) (end_of_data - data);
         column++, blob_lengths++)
    {
      byte *tmp_pos;
      uint length;
      if (!*blob_lengths)                       /* Null or "" */
        continue;
      length= column->length - portable_sizeof_char_ptr;
      memcpy_fixed((byte*) &tmp_pos, record + column->offset + length,
                   sizeof(char*));
      memcpy(data, tmp_pos, *blob_lengths);
      data+= *blob_lengths;
      /* Skip over tail page that was to be used to store blob */
      block++;
      bitmap_blocks->tail_page_skipped= 1;
    }
    if (head_block->sub_blocks > 1)
    {
      /* We have allocated pages that where not used */
      bitmap_blocks->page_skipped= 1;
    }
  }
  else
    data= tmp_data_used;                        /* Get last used on page */

  {
    /* Update page directory */
    uint length= (uint) (data - row_pos->data);
    DBUG_PRINT("info", ("head length: %u", length));
    if (length < info->s->base.min_row_length)
      length= info->s->base.min_row_length;

    int2store(row_pos->dir + 2, length);
    /* update empty space at start of block */
    row_pos->empty_space-= length;
    int2store(page_buff + EMPTY_SPACE_OFFSET, row_pos->empty_space);
    /* Mark in bitmaps how the current page was actually used */
    head_block->empty_space= row_pos->empty_space;
    if (page_buff[DIR_COUNT_OFFSET] == (char) MAX_ROWS_PER_PAGE)
      head_block->empty_space= 0;               /* Page is full */
    head_block->used= BLOCKUSED_USED;
  }

  /*
     Now we have to write tail pages, as we need to store the position
     to them in the row extent header.

     We first write out all blob tails, to be able to store them in
     the current page or 'tmp_data'.

     Then we write the tail of the non-blob fields (The position to the
     tail page is stored either in row header, the extents in the head
     page or in the first full page of the non-blob data. It's never in
     the tail page of the non-blob data)
  */

  blob_full_pages_exists= 0;
  if (row_extents_in_use)
  {
    if (column != end_column)                   /* If blob fields */
    {
      MARIA_COLUMNDEF    *save_column=       column;
      MARIA_BITMAP_BLOCK *save_block=        block;
      MARIA_BITMAP_BLOCK *end_block;
      ulong              *save_blob_lengths= blob_lengths;

      for (; column < end_column; column++, blob_lengths++)
      {
        byte *blob_pos;
        if (!*blob_lengths)                     /* Null or "" */
          continue;
        if (block[block->sub_blocks - 1].used & BLOCKUSED_TAIL)
        {
          uint length;
          length= column->length - portable_sizeof_char_ptr;
          memcpy_fixed((byte *) &blob_pos, record + column->offset + length,
                       sizeof(char*));
          length= *blob_lengths % FULL_PAGE_SIZE(block_size);   /* tail size */
          if (length != *blob_lengths)
            blob_full_pages_exists= 1;
          if (write_tail(info, block + block->sub_blocks-1,
                         blob_pos + *blob_lengths - length,
                         length))
            goto disk_err;
        }
        else
          blob_full_pages_exists= 1;

        for (end_block= block + block->sub_blocks; block < end_block; block++)
        {
          /*
            Set only a bit, to not cause bitmap code to believe a block is full
            when there is still a lot of entries in it
          */
          block->used|= BLOCKUSED_USED;
        }
      }
      column= save_column;
      block= save_block;
      blob_lengths= save_blob_lengths;
    }

    if (tmp_data_used)                          /* non blob data overflows */
    {
      MARIA_BITMAP_BLOCK *cur_block, *end_block, *last_head_block;
      MARIA_BITMAP_BLOCK *head_tail_block= 0;
      ulong length;
      ulong data_length= (tmp_data - info->rec_buff);

#ifdef SANITY_CHECK
      if (cur_block->sub_blocks == 1)
        goto crashed;                           /* no reserved full or tails */
#endif

      /*
        Find out where to write tail for non-blob fields.

        Problem here is that the bitmap code may have allocated more
        space than we need. We have to handle the following cases:

        - Bitmap code allocated a tail page we don't need.
        - The last full page allocated needs to be changed to a tail page
        (Because we where able to put more data on the head page than
        the bitmap allocation assumed)

        The reserved pages in bitmap_blocks for the main page has one of
        the following allocations:
        - Full pages, with following blocks:
        # * full pages
        empty page  ; To be used if we change last full to tail page. This
        has 'count' = 0.
        tail page  (optional, if last full page was part full)
        - One tail page
      */

      cur_block= head_block + 1;
      end_block= head_block + head_block->sub_blocks;
      /*
        Loop until we have find a block bigger than we need or
        we find the empty page block.
      */
      while (data_length >= (length= (cur_block->page_count *
                                      FULL_PAGE_SIZE(block_size))) &&
             cur_block->page_count)
      {
#ifdef SANITY_CHECK
        if ((cur_block == end_block) || (cur_block->used & BLOCKUSED_BIT))
          goto crashed;
#endif
        data_length-= length;
        (cur_block++)->used= BLOCKUSED_USED;
      }
      last_head_block= cur_block;
      if (data_length)
      {
        if (cur_block->page_count == 0)
        {
          /* Skip empty filler block */
          cur_block++;
        }
#ifdef SANITY_CHECK
        if ((cur_block >= end_block))
          goto crashed;
#endif
        if (cur_block->used & BLOCKUSED_TAIL)
        {
          DBUG_ASSERT(data_length < MAX_TAIL_SIZE(block_size));
          /* tail written to full tail page */
          cur_block->used= BLOCKUSED_USED;
          head_tail_block= cur_block;
        }
        else if (data_length > length - MAX_TAIL_SIZE(block_size))
        {
          /* tail written to full page */
          cur_block->used= BLOCKUSED_USED;
          if ((cur_block != end_block - 1) &&
              (end_block[-1].used & BLOCKUSED_TAIL))
            bitmap_blocks->tail_page_skipped= 1;
        }
        else
        {
          /*
            cur_block is a full block, followed by an empty and optional
            tail block. Change cur_block to a tail block or split it
            into full blocks and tail blocks.

            TODO:
             If there is enough space on the following tail block, use
             this instead of creating a new tail block.
          */
          DBUG_ASSERT(cur_block[1].page_count == 0);
          if (cur_block->page_count == 1)
          {
            /* convert full block to tail block */
            cur_block->used= BLOCKUSED_USED | BLOCKUSED_TAIL;
            head_tail_block= cur_block;
          }
          else
          {
            DBUG_ASSERT(data_length < length - FULL_PAGE_SIZE(block_size));
            DBUG_PRINT("info", ("Splitting blocks into full and tail"));
            cur_block[1].page= (cur_block->page + cur_block->page_count - 1);
            cur_block[1].page_count= 1;         /* Avoid DBUG_ASSERT */
            cur_block[1].used= BLOCKUSED_USED | BLOCKUSED_TAIL;
            cur_block->page_count--;
            cur_block->used= BLOCKUSED_USED;
            last_head_block= head_tail_block= cur_block+1;
          }
          if (end_block[-1].used & BLOCKUSED_TAIL)
            bitmap_blocks->tail_page_skipped= 1;
        }
      }
      else
      {
        /* Must be an empty or tail page */
        DBUG_ASSERT(cur_block->page_count == 0 ||
                    cur_block->used & BLOCKUSED_TAIL);
        if (end_block[-1].used & BLOCKUSED_TAIL)
          bitmap_blocks->tail_page_skipped= 1;
      }

      /*
        Write all extents into page or tmp_data

        Note that we still don't have a correct position for the tail
        of the non-blob fields.
      */
      store_extent_info(row_extents_first_part,
                        row_extents_second_part,
                        head_block+1, bitmap_blocks->count - 1);
      if (head_tail_block)
      {
        ulong data_length= (tmp_data - info->rec_buff);
        uint length;
        byte *extent_data;

        length= (uint) (data_length % FULL_PAGE_SIZE(block_size));
        if (write_tail(info, head_tail_block,
                       info->rec_buff + data_length - length,
                       length))
          goto disk_err;
        tmp_data-= length;                      /* Remove the tail */

        /* Store the tail position for the non-blob fields */
        if (head_tail_block == head_block + 1)
        {
          /*
            We had a head block + tail block, which means that the
            tail block is the first extent
          */
          extent_data= row_extents_first_part;
        }
        else
        {
          /*
            We have a head block + some full blocks + tail block
            last_head_block is pointing after the last used extent
            for the head block.
          */
          extent_data= row_extents_second_part +
            ((last_head_block - head_block) - 2) * ROW_EXTENT_SIZE;
        }
        DBUG_ASSERT(uint2korr(extent_data+5) & TAIL_BIT);
        int5store(extent_data, head_tail_block->page);
        int2store(extent_data + 5, head_tail_block->page_count);
      }
    }
    else
      store_extent_info(row_extents_first_part,
                        row_extents_second_part,
                        head_block+1, bitmap_blocks->count - 1);
  }

  if (share->base.transactional)
  {
    uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + DIRPOS_STORE_SIZE];
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
    size_t data_length= (size_t) (data - row_pos->data);

    /* Log REDO changes of head page */
    fileid_store(log_data, info->dfile.file);
    page_store(log_data+ FILEID_STORE_SIZE, head_block->page);
    dirpos_store(log_data+ FILEID_STORE_SIZE + PAGE_STORE_SIZE,
                 row_pos->rownr);
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    (char*) row_pos->data;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= data_length;
    if (translog_write_record(&lsn, LOGREC_REDO_INSERT_ROW_HEAD,
                              info->trn->short_id, NULL, share,
                              sizeof(log_data) + data_length,
                              TRANSLOG_INTERNAL_PARTS + 2, log_array))
      goto disk_err;
  }

  /* Increase data file size, if extended */
  position= (my_off_t) head_block->page * block_size;
  if (info->state->data_file_length <= position)
    info->state->data_file_length= position + block_size;

  DBUG_ASSERT(share->pagecache->block_size == block_size);
  if (pagecache_write(share->pagecache,
                      &info->dfile, head_block->page, 0,
                      page_buff, share->page_type,
                      head_block_is_read ? PAGECACHE_LOCK_WRITE_TO_READ :
                      PAGECACHE_LOCK_READ,
                      head_block_is_read ? PAGECACHE_PIN_LEFT_PINNED :
                      PAGECACHE_PIN,
                      PAGECACHE_WRITE_DELAY, &page_link.link))
    goto disk_err;
  page_link.unlock= PAGECACHE_LOCK_READ_UNLOCK;
  if (head_block_is_read)
  {
    /* Head page is always the first pinned page */
    set_dynamic(&info->pinned_pages, (void*) &page_link, 0);
  }
  else
    push_dynamic(&info->pinned_pages, (void*) &page_link);

  if (share->base.transactional && (tmp_data_used || blob_full_pages_exists))
  {
    /*
      Log REDO writes for all full pages (head part and all blobs)
      We write all here to be able to generate the UNDO record early
      so that we can write the LSN for the UNDO record to all full pages.
    */
    uchar tmp_log_data[FILEID_STORE_SIZE + LSN_STORE_SIZE + PAGE_STORE_SIZE +
                       ROW_EXTENT_SIZE * ROW_EXTENTS_ON_STACK];
    uchar *log_data, *log_pos;
    LEX_STRING tmp_log_array[TRANSLOG_INTERNAL_PARTS + 2 +
                             ROW_EXTENTS_ON_STACK];
    LEX_STRING *log_array_pos, *log_array;
    int error;
    ulong log_entry_length= 0;

    /* If few extents, then allocate things on stack to avoid a malloc call */
    if (bitmap_blocks->count < ROW_EXTENTS_ON_STACK)
    {
      log_array= tmp_log_array;
      log_data= tmp_log_data;
    }
    else
    {
      if (my_multi_malloc(MY_WME, &log_array,
                          (uint) ((bitmap_blocks->count +
                                   TRANSLOG_INTERNAL_PARTS + 2) *
                                  sizeof(*log_array)),
                          &log_data, bitmap_blocks->count * ROW_EXTENT_SIZE,
                          NullS))
        goto disk_err;
    }
    fileid_store(log_data, info->dfile.file);
    log_pos= log_data + FILEID_STORE_SIZE;
    log_array_pos= log_array+ TRANSLOG_INTERNAL_PARTS+1;

    if (tmp_data_used)
    {
      /* Full head pages */
      size_t data_length= (ulong) (tmp_data - info->rec_buff);
      log_pos= store_page_range(log_pos, head_block+1, block_size,
                                data_length);
      log_array_pos->str=    (char*) info->rec_buff;
      log_array_pos->length= data_length;
      log_entry_length+= data_length;
      log_array_pos++;
    }
    if (blob_full_pages_exists)
    {
      MARIA_COLUMNDEF *tmp_column= column;
      ulong *tmp_blob_lengths= blob_lengths;
      MARIA_BITMAP_BLOCK *tmp_block= block;

      /* Full blob pages */
      for (; tmp_column < end_column; tmp_column++, tmp_blob_lengths++)
      {
        ulong blob_length;
        uint length;

        if (!*tmp_blob_lengths)                 /* Null or "" */
          continue;
        length= tmp_column->length - portable_sizeof_char_ptr;
        blob_length= *tmp_blob_lengths;
        if (tmp_block[tmp_block->sub_blocks - 1].used & BLOCKUSED_TAIL)
          blob_length-= (blob_length % FULL_PAGE_SIZE(block_size));
        if (blob_length)
        {
          log_array_pos->str=    (char*) record + column->offset + length;
          log_array_pos->length= blob_length;
          log_entry_length+= blob_length;
          log_array_pos++;

          log_pos= store_page_range(log_pos, tmp_block, block_size,
                                    blob_length);
          tmp_block+= tmp_block->sub_blocks;
        }
      }
    }

    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (size_t) (log_pos -
                                                             log_data);
    log_entry_length+= (log_pos - log_data);

    error= translog_write_record(&lsn, LOGREC_REDO_INSERT_ROW_BLOBS,
                                 info->trn->short_id, NULL, share,
                                 log_entry_length, (uint) (log_array_pos -
                                                           log_array),
                                 log_array);
    if (log_array != tmp_log_array)
      my_free((gptr) log_array, MYF(0));
    if (error)
      goto disk_err;
  }

  /* Write UNDO record */
  if (share->base.transactional)
  {
    uchar log_data[LSN_STORE_SIZE + FILEID_STORE_SIZE +
                   PAGE_STORE_SIZE + DIRPOS_STORE_SIZE];
    LEX_STRING *log_array= info->log_row_parts;

    /* LOGREC_UNDO_ROW_INSERT & LOGREC_UNDO_ROW_INSERT share same header */
    lsn_store(log_data, info->trn->undo_lsn);
    fileid_store(log_data + LSN_STORE_SIZE, info->dfile.file);
    page_store(log_data+ LSN_STORE_SIZE + FILEID_STORE_SIZE,
               head_block->page);
    dirpos_store(log_data+ LSN_STORE_SIZE + FILEID_STORE_SIZE +
                 PAGE_STORE_SIZE,
                 row_pos->rownr);

    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);

    if (!old_record)
    {
      /* Write UNDO log record for the INSERT */
      if (translog_write_record(&info->trn->undo_lsn, LOGREC_UNDO_ROW_INSERT,
                                info->trn->short_id, NULL, share,
                                sizeof(log_data),
                                TRANSLOG_INTERNAL_PARTS + 1,
                                log_array))
        goto disk_err;
    }
    else
    {
      /* Write UNDO log record for the UPDATE */
      size_t row_length;
      uint row_parts_count;
      row_length= fill_update_undo_parts(info, old_record, record,
                                         info->log_row_parts +
                                         TRANSLOG_INTERNAL_PARTS + 1,
                                         &row_parts_count);
      if (translog_write_record(&info->trn->undo_lsn, LOGREC_UNDO_ROW_UPDATE,
                                info->trn->short_id, NULL, share,
                                sizeof(log_data) + row_length,
                                TRANSLOG_INTERNAL_PARTS + 1 + row_parts_count,
                                log_array))
      goto disk_err;
    }
  }

  _ma_unpin_all_pages(info, info->trn->undo_lsn);

  if (tmp_data_used)
  {
    /*
      Write data stored in info->rec_buff to pages
      This is the char/varchar data that didn't fit into the head page.
    */
    DBUG_ASSERT(bitmap_blocks->count != 0);
    if (write_full_pages(info, info->trn->undo_lsn, head_block + 1,
                         info->rec_buff, (ulong) (tmp_data - info->rec_buff)))
      goto disk_err;
  }

  /* Write rest of blobs (data, but no tails as they are already written) */
  for (; column < end_column; column++, blob_lengths++)
  {
    byte *blob_pos;
    uint length;
    ulong blob_length;
    if (!*blob_lengths)                         /* Null or "" */
      continue;
    length= column->length - portable_sizeof_char_ptr;
    memcpy_fixed((byte*) &blob_pos, record + column->offset + length,
                 sizeof(char*));
    /* remove tail part */
    blob_length= *blob_lengths;
    if (block[block->sub_blocks - 1].used & BLOCKUSED_TAIL)
      blob_length-= (blob_length % FULL_PAGE_SIZE(block_size));

    if (write_full_pages(info, info->trn->undo_lsn, block,
                         blob_pos, blob_length))
      goto disk_err;
    block+= block->sub_blocks;
  }
  /* Release not used space in used pages */
  if (_ma_bitmap_release_unused(info, bitmap_blocks))
    goto disk_err;
  DBUG_RETURN(0);

crashed:
  /* Something was wrong with data on page */
  my_errno= HA_ERR_WRONG_IN_RECORD;

disk_err:
  /* Unpin all pinned pages to not cause problems for disk cache */
  _ma_unpin_all_pages(info, 0);

  DBUG_RETURN(1);
}


/*
  Write a record (to get the row id for it)

  SYNOPSIS
    _ma_write_init_block_record()
    info                Maria handler
    record              Record to write

  NOTES
    This is done BEFORE we write the keys to the row!

  RETURN
    HA_OFFSET_ERROR     Something went wrong
    #                   Rowid for row
*/

MARIA_RECORD_POS _ma_write_init_block_record(MARIA_HA *info,
                                             const byte *record)
{
  MARIA_BITMAP_BLOCKS *blocks= &info->cur_row.insert_blocks;
  struct st_row_pos_info row_pos;
  DBUG_ENTER("_ma_write_init_block_record");

  calc_record_size(info, record, &info->cur_row);
  if (_ma_bitmap_find_place(info, &info->cur_row, blocks))
    DBUG_RETURN(HA_OFFSET_ERROR);               /* Error reading bitmap */
  /* page will be pinned & locked by get_head_or_tail_page */
  if (get_head_or_tail_page(info, blocks->block, info->buff,
                            info->s->base.min_row_length, HEAD_PAGE,
                            PAGECACHE_LOCK_WRITE, &row_pos))
    DBUG_RETURN(HA_OFFSET_ERROR);
  info->cur_row.lastpos= ma_recordpos(blocks->block->page, row_pos.rownr);
  if (info->s->calc_checksum)
    info->cur_row.checksum= (info->s->calc_checksum)(info,record);
  if (write_block_record(info, (byte*) 0, record, &info->cur_row,
                         blocks, blocks->block->org_bitmap_value != 0,
                         &row_pos))
    DBUG_RETURN(HA_OFFSET_ERROR);               /* Error reading bitmap */
  DBUG_PRINT("exit", ("Rowid: %lu", (ulong) info->cur_row.lastpos));
  info->s->state.split++;
  DBUG_RETURN(info->cur_row.lastpos);
}


/*
  Dummy function for (*info->s->write_record)()

  Nothing to do here, as we already wrote the record in
  _ma_write_init_block_record()
*/

my_bool _ma_write_block_record(MARIA_HA *info __attribute__ ((unused)),
                               const byte *record __attribute__ ((unused)))
{
  return 0;                                     /* Row already written */
}


/*
  Remove row written by _ma_write_block_record

  SYNOPSIS
    _ma_abort_write_block_record()
    info                Maria handler

  INFORMATION
    This is called in case we got a duplicate unique key while
    writing keys.

  RETURN
    0  ok
    1  error
*/

my_bool _ma_write_abort_block_record(MARIA_HA *info)
{
  my_bool res= 0;
  MARIA_BITMAP_BLOCKS *blocks= &info->cur_row.insert_blocks;
  MARIA_BITMAP_BLOCK *block, *end;
  DBUG_ENTER("_ma_abort_write_block_record");

  if (delete_head_or_tail(info,
                          ma_recordpos_to_page(info->cur_row.lastpos),
                          ma_recordpos_to_dir_entry(info->cur_row.lastpos), 1))
    res= 1;
  for (block= blocks->block + 1, end= block + blocks->count - 1; block < end;
       block++)
  {
    if (block->used & BLOCKUSED_TAIL)
    {
      /*
         block->page_count is set to the tail directory entry number in
         write_block_record()
      */
      if (delete_head_or_tail(info, block->page, block->page_count & ~TAIL_BIT,
                              0))
        res= 1;
    }
    else if (block->used & BLOCKUSED_USED)
    {
      if (free_full_page_range(info, block->page, block->page_count))
        res= 1;
    }
  }

  if (info->s->base.transactional)
  {
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 1];
    uchar log_data[LSN_STORE_SIZE];

    /*
      Write UNDO record
      This entry is just an end marker for the abort_insert as we will never
      really undo a failed insert. Note that this UNDO will cause recover
      to ignore the LOGREC_UNDO_ROW_INSERT that is the previous entry
      in the UNDO chain.
    */
    lsn_store(log_data, info->trn->undo_lsn);
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    if (translog_write_record(&info->trn->undo_lsn, LOGREC_UNDO_ROW_PURGE,
                              info->trn->short_id, NULL, info->s,
                              sizeof(log_data),
                              TRANSLOG_INTERNAL_PARTS + 1,
                              log_array))
      res= 1;
  }
  _ma_unpin_all_pages(info, info->trn->undo_lsn);
  DBUG_RETURN(res);
}


/*
  Update a record

  NOTES
    For the moment, we assume that info->curr_row.extents is always updated
    when a row is read. In the future we may decide to read this on demand
    for rows split into many extents.
*/

my_bool _ma_update_block_record(MARIA_HA *info, MARIA_RECORD_POS record_pos,
                                const byte *oldrec, const byte *record)
{
  MARIA_BITMAP_BLOCKS *blocks= &info->cur_row.insert_blocks;
  byte *buff;
  MARIA_ROW *cur_row= &info->cur_row, *new_row= &info->new_row;
  MARIA_PINNED_PAGE page_link;
  uint rownr, org_empty_size, head_length;
  uint block_size= info->s->block_size;
  byte *dir;
  ulonglong page;
  struct st_row_pos_info row_pos;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_update_block_record");
  DBUG_PRINT("enter", ("rowid: %lu", (long) record_pos));

  calc_record_size(info, record, new_row);
  page= ma_recordpos_to_page(record_pos);

  DBUG_ASSERT(share->pagecache->block_size == block_size);
  if (!(buff= pagecache_read(share->pagecache,
                             &info->dfile, (my_off_t) page, 0,
                             info->buff, share->page_type,
                             PAGECACHE_LOCK_WRITE, &page_link.link)))
    DBUG_RETURN(1);
  page_link.unlock= PAGECACHE_LOCK_WRITE_UNLOCK;
  push_dynamic(&info->pinned_pages, (void*) &page_link);

  org_empty_size= uint2korr(buff + EMPTY_SPACE_OFFSET);
  rownr= ma_recordpos_to_dir_entry(record_pos);
  dir= (buff + block_size - DIR_ENTRY_SIZE * rownr -
        DIR_ENTRY_SIZE - PAGE_SUFFIX_SIZE);

  if ((org_empty_size + cur_row->head_length) >= new_row->total_length)
  {
    uint empty, offset, length;
    MARIA_BITMAP_BLOCK block;

    /*
      We can fit the new row in the same page as the original head part
      of the row
    */
    block.org_bitmap_value= _ma_free_size_to_head_pattern(&share->bitmap,
                                                          org_empty_size);
    offset= uint2korr(dir);
    length= uint2korr(dir + 2);
    empty= 0;
    if (new_row->total_length > length)
    {
      /* See if there is empty space after */
      if (rownr != (uint) ((uchar *) buff)[DIR_COUNT_OFFSET] - 1)
        empty= start_of_next_entry(dir) - (offset + length);
      if (new_row->total_length > length + empty)
      {
        compact_page(buff, share->block_size, rownr, 1);
        org_empty_size= 0;
        length= uint2korr(dir + 2);
      }
    }
    row_pos.buff= buff;
    row_pos.rownr= rownr;
    row_pos.empty_space= org_empty_size + length;
    row_pos.dir= dir;
    row_pos.data= buff + uint2korr(dir);
    row_pos.length= length + empty;
    blocks->block= &block;
    blocks->count= 1;
    block.page= page;
    block.sub_blocks= 1;
    block.used= BLOCKUSED_USED | BLOCKUSED_USE_ORG_BITMAP;
    block.empty_space= row_pos.empty_space;
    /* Update cur_row, if someone calls update at once again */
    cur_row->head_length= new_row->total_length;

    if (free_full_pages(info, cur_row))
      goto err;
    DBUG_RETURN(write_block_record(info, oldrec, record, new_row, blocks,
                                   1, &row_pos));
  }
  /*
    Allocate all size in block for record
    QQ: Need to improve this to do compact if we can fit one more blob into
    the head page
  */
  head_length= uint2korr(dir + 2);
  if (buff[PAGE_TYPE_OFFSET] & PAGE_CAN_BE_COMPACTED && org_empty_size &&
      (head_length < new_row->head_length ||
       (new_row->total_length <= head_length &&
        org_empty_size + head_length >= new_row->total_length)))
  {
    compact_page(buff, share->block_size, rownr, 1);
    org_empty_size= 0;
    head_length= uint2korr(dir + 2);
  }

  /* Delete old row */
  if (delete_tails(info, cur_row->tail_positions))
    goto err;
  if (free_full_pages(info, cur_row))
    goto err;
  if (_ma_bitmap_find_new_place(info, new_row, page, head_length, blocks))
    goto err;

  row_pos.buff= buff;
  row_pos.rownr= rownr;
  row_pos.empty_space= org_empty_size + head_length;
  row_pos.dir= dir;
  row_pos.data= buff + uint2korr(dir);
  row_pos.length= head_length;
  DBUG_RETURN(write_block_record(info, oldrec, record, new_row, blocks, 1,
                                 &row_pos));

err:
  _ma_unpin_all_pages(info, 0);
  DBUG_RETURN(1);
}


/*
  Delete a head a tail part

  SYNOPSIS
    delete_head_or_tail()
    info                Maria handler
    page                Page (not file offset!) on which the row is
    head                1 if this is a head page

  NOTES
    Uses info->keyread_buff

  RETURN
    0  ok
    1  error
*/

static my_bool delete_head_or_tail(MARIA_HA *info,
                                   ulonglong page, uint record_number,
                                   my_bool head)
{
  MARIA_SHARE *share= info->s;
  uint number_of_records, empty_space, length;
  uint block_size= share->block_size;
  byte *buff, *dir;
  LSN lsn;
  MARIA_PINNED_PAGE page_link;
  DBUG_ENTER("delete_head_or_tail");

  info->keyread_buff_used= 1;
  DBUG_ASSERT(info->s->pagecache->block_size == block_size);
  if (!(buff= pagecache_read(share->pagecache,
                             &info->dfile, page, 0,
                             info->keyread_buff,
                             info->s->page_type,
                             PAGECACHE_LOCK_WRITE, &page_link.link)))
    DBUG_RETURN(1);
  page_link.unlock= PAGECACHE_LOCK_WRITE_UNLOCK;
  push_dynamic(&info->pinned_pages, (void*) &page_link);

  number_of_records= (uint) ((uchar *) buff)[DIR_COUNT_OFFSET];
#ifdef SANITY_CHECKS
  if (record_number >= number_of_records ||
      record_number > ((block_size - LSN_SIZE - PAGE_TYPE_SIZE - 1 -
                        PAGE_SUFFIX_SIZE) / DIR_ENTRY_SIZE))
  {
    DBUG_PRINT("error", ("record_number: %u  number_of_records: %u",
                         record_number, number_of_records));
    DBUG_RETURN(1);
  }
#endif

  dir= (buff + block_size - DIR_ENTRY_SIZE * record_number -
        DIR_ENTRY_SIZE - PAGE_SUFFIX_SIZE);
  dir[0]= dir[1]= 0;                            /* Delete entry */
  length= uint2korr(dir + 2);
  empty_space= uint2korr(buff + EMPTY_SPACE_OFFSET);

  if (record_number == number_of_records - 1)
  {
    /* Delete this entry and all following empty directory entries */
    byte *end= buff + block_size - PAGE_SUFFIX_SIZE;
    do
    {
      number_of_records--;
      dir+= DIR_ENTRY_SIZE;
      empty_space+= DIR_ENTRY_SIZE;
    } while (dir < end && dir[0] == 0 && dir[1] == 0);
    buff[DIR_COUNT_OFFSET]= (byte) (uchar) number_of_records;
  }
  empty_space+= length;
  if (number_of_records != 0)
  {
    uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + DIRPOS_STORE_SIZE];
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 1];

    /* Update directory */
    int2store(buff + EMPTY_SPACE_OFFSET, empty_space);
    buff[PAGE_TYPE_OFFSET]|= (byte) PAGE_CAN_BE_COMPACTED;
    DBUG_ASSERT(share->pagecache->block_size == block_size);

    /* Log REDO data */
    fileid_store(log_data, info->dfile.file);
    page_store(log_data+ FILEID_STORE_SIZE, page);
    dirpos_store(log_data+ FILEID_STORE_SIZE + PAGE_STORE_SIZE,
                   record_number);

    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    if (translog_write_record(&lsn,
                              (head ? LOGREC_REDO_PURGE_ROW_HEAD :
                               LOGREC_REDO_PURGE_ROW_TAIL),
                              info->trn->short_id, NULL, share,
                              sizeof(log_data), TRANSLOG_INTERNAL_PARTS + 1,
                              log_array))
      DBUG_RETURN(1);
    if (pagecache_write(share->pagecache,
                        &info->dfile, page, 0,
                        buff, share->page_type,
                        PAGECACHE_LOCK_WRITE_TO_READ,
                        PAGECACHE_PIN_LEFT_PINNED,
                        PAGECACHE_WRITE_DELAY, &page_link.link))
      DBUG_RETURN(1);

    /* Change the lock used when we read the page */
    page_link.unlock= PAGECACHE_LOCK_READ_UNLOCK;
    set_dynamic(&info->pinned_pages, (void*) &page_link,
                info->pinned_pages.elements-1);
  }
  else
  {
    uchar log_data[FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE +
                   PAGE_STORE_SIZE + PAGERANGE_STORE_SIZE];
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 1];

    fileid_store(log_data, info->dfile.file);
    pagerange_store(log_data + FILEID_STORE_SIZE, 1);
    page_store(log_data+ FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE, page);
    pagerange_store(log_data + FILEID_STORE_SIZE + PAGERANGE_STORE_SIZE +
                    PAGERANGE_STORE_SIZE, 1);
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    if (translog_write_record(&lsn, LOGREC_REDO_PURGE_BLOCKS,
                              info->trn->short_id, NULL, share,
                              sizeof(log_data), TRANSLOG_INTERNAL_PARTS + 1,
                              log_array))
      DBUG_RETURN(1);
    DBUG_ASSERT(empty_space >= info->s->bitmap.sizes[0]);
  }
  DBUG_PRINT("info", ("empty_space: %u", empty_space));
  DBUG_RETURN(_ma_bitmap_set(info, page, head, empty_space));
}


/*
  delete all tails

  SYNOPSIS
    delete_tails()
    info                Handler
    tails               Pointer to vector of tail positions, ending with 0

  NOTES
    Uses info->keyread_buff

  RETURN
    0  ok
    1  error
*/

static my_bool delete_tails(MARIA_HA *info, MARIA_RECORD_POS *tails)
{
  my_bool res= 0;
  DBUG_ENTER("delete_tails");
  for (; *tails; tails++)
  {
    if (delete_head_or_tail(info,
                            ma_recordpos_to_page(*tails),
                            ma_recordpos_to_dir_entry(*tails), 0))
      res= 1;
  }
  DBUG_RETURN(res);
}


/*
  Delete a record

  NOTES
   For the moment, we assume that info->cur_row.extents is always updated
   when a row is read. In the future we may decide to read this on demand
   for rows with many splits.
*/

my_bool _ma_delete_block_record(MARIA_HA *info, const byte *record)
{
  ulonglong page;
  uint record_number;
  DBUG_ENTER("_ma_delete_block_record");

  page=          ma_recordpos_to_page(info->cur_row.lastpos);
  record_number= ma_recordpos_to_dir_entry(info->cur_row.lastpos);

  if (delete_head_or_tail(info, page, record_number, 1) ||
      delete_tails(info, info->cur_row.tail_positions))
    goto err;

  info->s->state.split--;

  if (info->cur_row.extents && free_full_pages(info, &info->cur_row))
    goto err;

  {
    uchar log_data[LSN_STORE_SIZE + FILEID_STORE_SIZE + PAGE_STORE_SIZE +
                   DIR_COUNT_SIZE];
    size_t row_length;
    uint row_parts_count;

    /* Write UNDO record */
    lsn_store(log_data, info->trn->undo_lsn);
    fileid_store(log_data+ LSN_STORE_SIZE, info->dfile.file);
    page_store(log_data+ LSN_STORE_SIZE + FILEID_STORE_SIZE, page);
    dirpos_store(log_data+ LSN_STORE_SIZE + FILEID_STORE_SIZE +
                 PAGE_STORE_SIZE, record_number);

    info->log_row_parts[TRANSLOG_INTERNAL_PARTS].str= (char*) log_data;
    info->log_row_parts[TRANSLOG_INTERNAL_PARTS].length= sizeof(log_data);
    row_length= fill_insert_undo_parts(info, record, info->log_row_parts +
                                       TRANSLOG_INTERNAL_PARTS + 1,
                                       &row_parts_count);

    if (translog_write_record(&info->trn->undo_lsn, LOGREC_UNDO_ROW_DELETE,
                              info->trn->short_id, NULL, info->s,
                              sizeof(log_data) + row_length,
                              TRANSLOG_INTERNAL_PARTS + 1 + row_parts_count,
                              info->log_row_parts))
      goto err;

  }

  _ma_unpin_all_pages(info, info->trn->undo_lsn);
  DBUG_RETURN(0);

err:
  _ma_unpin_all_pages(info, 0);
  DBUG_RETURN(1);
}


/****************************************************************************
  Reading of records
****************************************************************************/

/*
  Read position to record from record directory at end of page

  SYNOPSIS
   get_record_position()
   buff                 page buffer
   block_size           block size for page
   record_number        Record number in index
   end_of_data          pointer to end of data for record

  RETURN
    0  Error in data
    #  Pointer to start of record.
       In this case *end_of_data is set.
*/

static byte *get_record_position(byte *buff, uint block_size,
                                 uint record_number, byte **end_of_data)
{
  uint number_of_records= (uint) ((uchar *) buff)[DIR_COUNT_OFFSET];
  byte *dir;
  byte *data;
  uint offset, length;

#ifdef SANITY_CHECKS
  if (record_number >= number_of_records ||
      record_number > ((block_size - PAGE_HEADER_SIZE - PAGE_SUFFIX_SIZE) /
                       DIR_ENTRY_SIZE))
  {
    DBUG_PRINT("error",
               ("Wrong row number: record_number: %u  number_of_records: %u",
                record_number, number_of_records));
    return 0;
  }
#endif

  dir= (buff + block_size - DIR_ENTRY_SIZE * record_number -
        DIR_ENTRY_SIZE - PAGE_SUFFIX_SIZE);
  offset= uint2korr(dir);
  length= uint2korr(dir + 2);
#ifdef SANITY_CHECKS
  if (offset < PAGE_HEADER_SIZE ||
      offset + length > (block_size -
                         number_of_records * DIR_ENTRY_SIZE -
                         PAGE_SUFFIX_SIZE))
  {
    DBUG_PRINT("error",
               ("Wrong row position:  record_number: %u  offset: %u  "
                "length: %u  number_of_records: %u",
                record_number, offset, length, number_of_records));
    return 0;
  }
#endif
  data= buff + offset;
  *end_of_data= data + length;
  return data;
}


/*
  Init extent

  NOTES
    extent is a cursor over which pages to read
*/

static void init_extent(MARIA_EXTENT_CURSOR *extent, byte *extent_info,
                        uint extents, MARIA_RECORD_POS *tail_positions)
{
  uint page_count;
  extent->extent=       extent_info;
  extent->extent_count= extents;
  extent->page=         uint5korr(extent_info);         /* First extent */
  page_count=           uint2korr(extent_info + ROW_EXTENT_PAGE_SIZE);
  extent->page_count=   page_count & ~TAIL_BIT;
  extent->tail=         page_count & TAIL_BIT;
  extent->tail_positions= tail_positions;
}


/*
  Read next extent

  SYNOPSIS
    read_next_extent()
    info                Maria handler
    extent              Pointer to current extent (this is updated to point
                        to next)
    end_of_data         Pointer to end of data in read block (out)

  NOTES
    New block is read into info->buff

  RETURN
    0   Error;  my_errno is set
    #   Pointer to start of data in read block
        In this case end_of_data is updated to point to end of data.
*/

static byte *read_next_extent(MARIA_HA *info, MARIA_EXTENT_CURSOR *extent,
                              byte **end_of_data)
{
  MARIA_SHARE *share= info->s;
  byte *buff, *data;
  DBUG_ENTER("read_next_extent");

  if (!extent->page_count)
  {
    uint page_count;
    if (!--extent->extent_count)
      goto crashed;
    extent->extent+=    ROW_EXTENT_SIZE;
    extent->page=       uint5korr(extent->extent);
    page_count=         uint2korr(extent->extent+ROW_EXTENT_PAGE_SIZE);
    DBUG_ASSERT(page_count != 0);
    extent->tail=       page_count & TAIL_BIT;
    extent->page_count= (page_count & ~TAIL_BIT);
    extent->first_extent= 0;
    DBUG_PRINT("info",("New extent.  Page: %lu  page_count: %u  tail_flag: %d",
                       (ulong) extent->page, extent->page_count,
                       extent->tail != 0));
  }

  DBUG_ASSERT(share->pagecache->block_size == share->block_size);
  if (!(buff= pagecache_read(share->pagecache,
                             &info->dfile, extent->page, 0,
                             info->buff, share->page_type,
                             PAGECACHE_LOCK_LEFT_UNLOCKED, 0)))
  {
    /* check if we tried to read over end of file (ie: bad data in record) */
    if ((extent->page + 1) * share->block_size > info->state->data_file_length)
      goto crashed;
    DBUG_RETURN(0);
  }
  if (!extent->tail)
  {
    /* Full data page */
    DBUG_ASSERT((buff[PAGE_TYPE_OFFSET] & PAGE_TYPE_MASK) == BLOB_PAGE);
    extent->page++;                             /* point to next page */
    extent->page_count--;
    *end_of_data= buff + share->block_size;
    info->cur_row.full_page_count++;            /* For maria_chk */
    DBUG_RETURN(extent->data_start= buff + LSN_SIZE + PAGE_TYPE_SIZE);
  }
  /* Found tail. page_count is in this case the position in the tail page */

  DBUG_ASSERT((buff[PAGE_TYPE_OFFSET] & PAGE_TYPE_MASK) == TAIL_PAGE);
  *(extent->tail_positions++)= ma_recordpos(extent->page,
                                            extent->page_count);
  info->cur_row.tail_count++;                   /* For maria_chk */

  if (!(data= get_record_position(buff, share->block_size,
                                  extent->page_count,
                                  end_of_data)))
    goto crashed;
  extent->data_start= data;
  extent->page_count= 0;                        /* No more data in extent */
  DBUG_RETURN(data);


crashed:
  my_errno= HA_ERR_WRONG_IN_RECORD;             /* File crashed */
  DBUG_PRINT("error", ("wrong extent information"));
  DBUG_RETURN(0);
}


/*
  Read data that may be split over many blocks

  SYNOPSIS
    read_long_data()
    info                Maria handler
    to                  Store result string here (this is allocated)
    extent              Pointer to current extent position
    data                Current position in buffer
    end_of_data         End of data in buffer

  NOTES
    When we have to read a new buffer, it's read into info->buff

    This loop is implemented by goto's instead of a for() loop as
    the code is notable smaller and faster this way (and it's not nice
    to jump into a for loop() or into a 'then' clause)

  RETURN
    0   ok
    1   error
*/

static my_bool read_long_data(MARIA_HA *info, byte *to, ulong length,
                              MARIA_EXTENT_CURSOR *extent,
                              byte **data, byte **end_of_data)
{
  DBUG_ENTER("read_long_data");
  DBUG_PRINT("enter", ("length: %lu", length));
  DBUG_ASSERT(*data <= *end_of_data);

  /*
    Fields are never split in middle. This means that if length > rest-of-data
    we should start reading from the next extent.  The reason we may have
    data left on the page is that there fixed part of the row was less than
    min_row_length and in this case the head block was extended to
    min_row_length.

    This may change in the future, which is why we have the loop written
    the way it's written.
  */
  if (length > (ulong) (*end_of_data - *data))
    *end_of_data= *data;

  for(;;)
  {
    uint left_length;
    left_length= (uint) (*end_of_data - *data);
    if (likely(left_length >= length))
    {
      memcpy(to, *data, length);
      (*data)+= length;
      DBUG_RETURN(0);
    }
    memcpy(to, *data, left_length);
    to+= left_length;
    length-= left_length;
    if (!(*data= read_next_extent(info, extent, end_of_data)))
      break;
  }
  DBUG_RETURN(1);
}


/*
  Read a record from page (helper function for _ma_read_block_record())

  SYNOPSIS
    _ma_read_block_record2()
    info                Maria handler
    record              Store record here
    data                Start of head data for row
    end_of_data         End of data for row

  NOTES
    The head page is already read by caller
    Following data is update in info->cur_row:

    cur_row.head_length is set to size of entry in head block
    cur_row.tail_positions is set to point to all tail blocks
    cur_row.extents points to extents data
    cur_row.extents_counts contains number of extents
    cur_row.empty_bits is set to empty bits
    cur_row.field_lengths contains packed length of all fields

   RETURN
     0  ok
     #  Error code
*/

int _ma_read_block_record2(MARIA_HA *info, byte *record,
                           byte *data, byte *end_of_data)
{
  MARIA_SHARE *share= info->s;
  byte *field_length_data, *blob_buffer, *start_of_data;
  uint flag, null_bytes, cur_null_bytes, row_extents, field_lengths;
  my_bool found_blob= 0;
  MARIA_EXTENT_CURSOR extent;
  MARIA_COLUMNDEF *column, *end_column;
  DBUG_ENTER("_ma_read_block_record2");

  LINT_INIT(field_lengths);
  LINT_INIT(field_length_data);
  LINT_INIT(blob_buffer);

  start_of_data= data;
  flag= (uint) (uchar) data[0];
  cur_null_bytes= share->base.original_null_bytes;
  null_bytes=     share->base.null_bytes;
  info->cur_row.head_length= (uint) (end_of_data - data);
  info->cur_row.full_page_count= info->cur_row.tail_count= 0;

  /* Skip trans header (for now, until we have MVCC csupport) */
  data+= total_header_size[(flag & PRECALC_HEADER_BITMASK)];
  if (flag & ROW_FLAG_NULLS_EXTENDED)
    cur_null_bytes+= data[-1];

  row_extents= 0;
  if (flag & ROW_FLAG_EXTENTS)
  {
    uint row_extent_size;
    /*
      Record is split over many data pages.
      Get number of extents and first extent
    */
    get_key_length(row_extents, data);
    info->cur_row.extents_count= row_extents;
    row_extent_size= row_extents * ROW_EXTENT_SIZE;
    if (info->cur_row.extents_buffer_length < row_extent_size &&
        _ma_alloc_buffer(&info->cur_row.extents,
                         &info->cur_row.extents_buffer_length,
                         row_extent_size))
      DBUG_RETURN(my_errno);
    memcpy(info->cur_row.extents, data, ROW_EXTENT_SIZE);
    data+= ROW_EXTENT_SIZE;
    init_extent(&extent, info->cur_row.extents, row_extents,
                info->cur_row.tail_positions);
  }
  else
  {
    info->cur_row.extents_count= 0;
    (*info->cur_row.tail_positions)= 0;
    extent.page_count= 0;
    extent.extent_count= 1;
  }
  extent.first_extent= 1;

  if (share->base.max_field_lengths)
  {
    get_key_length(field_lengths, data);
    info->cur_row.field_lengths_length= field_lengths;
#ifdef SANITY_CHECKS
    if (field_lengths > share->base.max_field_lengths)
      goto err;
#endif
  }

  if (share->calc_checksum)
    info->cur_row.checksum= (uint) (uchar) *data++;
  /* data now points on null bits */
  memcpy(record, data, cur_null_bytes);
  if (unlikely(cur_null_bytes != null_bytes))
  {
    /*
      This only happens if we have added more NULL columns with
      ALTER TABLE and are fetching an old, not yet modified old row
    */
    bzero(record + cur_null_bytes, (uint) (null_bytes - cur_null_bytes));
  }
  data+= null_bytes;
  /* We copy the empty bits to be able to use them for delete/update */
  memcpy(info->cur_row.empty_bits, data, share->base.pack_bytes);
  data+= share->base.pack_bytes;

  /* TODO: Use field offsets, instead of just skipping them */
  data+= share->base.field_offsets * FIELD_OFFSET_SIZE;

  /*
    Read row extents (note that first extent was already read into
    info->cur_row.extents above)
  */
  if (row_extents)
  {
    if (read_long_data(info, info->cur_row.extents + ROW_EXTENT_SIZE,
                       (row_extents - 1) * ROW_EXTENT_SIZE,
                       &extent, &data, &end_of_data))
      DBUG_RETURN(my_errno);
  }

  /*
    Data now points to start of fixed length field data that can't be null
    or 'empty'. Note that these fields can't be split over blocks.
  */
  for (column= share->columndef,
         end_column= column + share->base.fixed_not_null_fields;
       column < end_column; column++)
  {
    uint column_length= column->length;
    if (data >= end_of_data &&
        !(data= read_next_extent(info, &extent, &end_of_data)))
      goto err;
    memcpy(record + column->offset, data, column_length);
    data+= column_length;
  }

  /* Read array of field lengths. This may be stored in several extents */
  if (share->base.max_field_lengths)
  {
    field_length_data= info->cur_row.field_lengths;
    if (read_long_data(info, field_length_data, field_lengths, &extent,
                       &data, &end_of_data))
      DBUG_RETURN(my_errno);
  }

  /* Read variable length data. Each of these may be split over many extents */
  for (end_column= share->columndef + share->base.fields;
       column < end_column; column++)
  {
    enum en_fieldtype type= (enum en_fieldtype) column->type;
    byte *field_pos= record + column->offset;
    /* First check if field is present in record */
    if ((record[column->null_pos] & column->null_bit) ||
        (info->cur_row.empty_bits[column->empty_pos] & column->empty_bit))
    {
      if (type == FIELD_SKIP_ENDSPACE)
        bfill(record + column->offset, column->length, ' ');
      else
        bzero(record + column->offset, column->fill_length);
      continue;
    }
    switch (type) {
    case FIELD_NORMAL:                          /* Fixed length field */
    case FIELD_SKIP_PRESPACE:
    case FIELD_SKIP_ZERO:                       /* Fixed length field */
      if (data >= end_of_data &&
          !(data= read_next_extent(info, &extent, &end_of_data)))
        goto err;
      memcpy(field_pos, data, column->length);
      data+= column->length;
      break;
    case FIELD_SKIP_ENDSPACE:                   /* CHAR */
    {
      /* Char that is space filled */
      uint length;
      if (column->length <= 255)
        length= (uint) (uchar) *field_length_data++;
      else
      {
        length= uint2korr(field_length_data);
        field_length_data+= 2;
      }
#ifdef SANITY_CHECKS
      if (length > column->length)
        goto err;
#endif
      if (read_long_data(info, field_pos, length, &extent, &data,
                         &end_of_data))
        DBUG_RETURN(my_errno);
      bfill(field_pos + length, column->length - length, ' ');
      break;
    }
    case FIELD_VARCHAR:
    {
      ulong length;
      if (column->length <= 256)
      {
        length= (uint) (uchar) (*field_pos++= *field_length_data++);
      }
      else
      {
        length= uint2korr(field_length_data);
        field_pos[0]= field_length_data[0];
        field_pos[1]= field_length_data[1];
        field_pos+= 2;
        field_length_data+= 2;
      }
      if (read_long_data(info, field_pos, length, &extent, &data,
                         &end_of_data))
        DBUG_RETURN(my_errno);
      break;
    }
    case FIELD_BLOB:
    {
      uint size_length= column->length - portable_sizeof_char_ptr;
      ulong blob_length= _ma_calc_blob_length(size_length, field_length_data);

      if (!found_blob)
      {
        /* Calculate total length for all blobs */
        ulong blob_lengths= 0;
        byte *length_data= field_length_data;
        MARIA_COLUMNDEF *blob_field= column;

        found_blob= 1;
        for (; blob_field < end_column; blob_field++)
        {
          uint size_length;
          if ((record[blob_field->null_pos] & blob_field->null_bit) ||
              (info->cur_row.empty_bits[blob_field->empty_pos] &
               blob_field->empty_bit))
            continue;
          size_length= blob_field->length - portable_sizeof_char_ptr;
          blob_lengths+= _ma_calc_blob_length(size_length, length_data);
          length_data+= size_length;
        }
        DBUG_PRINT("info", ("Total blob length: %lu", blob_lengths));
        if (_ma_alloc_buffer(&info->rec_buff, &info->rec_buff_size,
                             blob_lengths))
          DBUG_RETURN(my_errno);
        blob_buffer= info->rec_buff;
      }

      memcpy(field_pos, field_length_data, size_length);
      memcpy_fixed(field_pos + size_length, (byte *) & blob_buffer,
                   sizeof(char*));
      field_length_data+= size_length;

      /*
        After we have read one extent, then each blob is in it's own extent
      */
      if (extent.first_extent && (ulong) (end_of_data - data) < blob_length)
        end_of_data= data;                      /* Force read of next extent */

      if (read_long_data(info, blob_buffer, blob_length, &extent, &data,
                         &end_of_data))
        DBUG_RETURN(my_errno);
      blob_buffer+= blob_length;
      break;
    }
    default:
#ifdef EXTRA_DEBUG
      DBUG_ASSERT(0);                           /* purecov: deadcode */
#endif
      goto err;
    }
    continue;
  }

  if (row_extents)
  {
    DBUG_PRINT("info", ("Row read:  page_count: %u  extent_count: %u",
                        extent.page_count, extent.extent_count));
    *extent.tail_positions= 0;                  /* End marker */
    if (extent.page_count)
      goto err;
    if (extent.extent_count > 1)
      if (check_if_zero(extent.extent + ROW_EXTENT_SIZE,
                        (extent.extent_count-1) * ROW_EXTENT_SIZE))
        goto err;
  }
  else
  {
    DBUG_PRINT("info", ("Row read"));
    /*
      data should normally point to end_of_date. The only exception is if
      the row is very short in which case we allocated 'min_row_length' data
      for allowing the row to expand.
    */
    if (data != end_of_data && (uint) (end_of_data - start_of_data) >
        info->s->base.min_row_length)
      goto err;
  }

  info->update|= HA_STATE_AKTIV;	/* We have an active record */
  DBUG_RETURN(0);

err:
  /* Something was wrong with data on record */
  DBUG_PRINT("error", ("Found record with wrong data"));
  DBUG_RETURN((my_errno= HA_ERR_WRONG_IN_RECORD));
}


/*
  Read a record based on record position

  SYNOPSIS
    _ma_read_block_record()
    info                Maria handler
    record              Store record here
    record_pos          Record position
*/

int _ma_read_block_record(MARIA_HA *info, byte *record,
                          MARIA_RECORD_POS record_pos)
{
  byte *data, *end_of_data, *buff;
  uint offset;
  uint block_size= info->s->block_size;
  DBUG_ENTER("_ma_read_block_record");
  DBUG_PRINT("enter", ("rowid: %lu", (long) record_pos));

  info->cur_row.lastpos= record_pos;
  offset= ma_recordpos_to_dir_entry(record_pos);

  DBUG_ASSERT(info->s->pagecache->block_size == block_size);
  if (!(buff= pagecache_read(info->s->pagecache,
                             &info->dfile, ma_recordpos_to_page(record_pos), 0,
                             info->buff, info->s->page_type,
                             PAGECACHE_LOCK_LEFT_UNLOCKED, 0)))
    DBUG_RETURN(1);
  DBUG_ASSERT((buff[PAGE_TYPE_OFFSET] & PAGE_TYPE_MASK) == HEAD_PAGE);
  if (!(data= get_record_position(buff, block_size, offset, &end_of_data)))
  {
    my_errno= HA_ERR_WRONG_IN_RECORD;           /* File crashed */
    DBUG_PRINT("error", ("Wrong directory entry in data block"));
    DBUG_RETURN(1);
  }
  DBUG_RETURN(_ma_read_block_record2(info, record, data, end_of_data));
}


/* compare unique constraint between stored rows */

my_bool _ma_cmp_block_unique(MARIA_HA *info, MARIA_UNIQUEDEF *def,
                             const byte *record, MARIA_RECORD_POS pos)
{
  byte *org_rec_buff, *old_record;
  my_size_t org_rec_buff_size;
  int error;
  DBUG_ENTER("_ma_cmp_block_unique");

  if (!(old_record= my_alloca(info->s->base.reclength)))
    DBUG_RETURN(1);

  /* Don't let the compare destroy blobs that may be in use */
  org_rec_buff=      info->rec_buff;
  org_rec_buff_size= info->rec_buff_size;
  if (info->s->base.blobs)
  {
    /* Force realloc of record buffer*/
    info->rec_buff= 0;
    info->rec_buff_size= 0;
  }
  error= _ma_read_block_record(info, old_record, pos);
  if (!error)
    error= _ma_unique_comp(def, record, old_record, def->null_are_equal);
  if (info->s->base.blobs)
  {
    my_free(info->rec_buff, MYF(MY_ALLOW_ZERO_PTR));
    info->rec_buff=      org_rec_buff;
    info->rec_buff_size= org_rec_buff_size;
  }
  DBUG_PRINT("exit", ("result: %d", error));
  my_afree(old_record);
  DBUG_RETURN(error != 0);
}


/****************************************************************************
  Table scan
****************************************************************************/

/*
  Allocate buffers for table scan

  SYNOPSIS
   _ma_scan_init_block_record(MARIA_HA *info)

  IMPLEMENTATION
    We allocate one buffer for the current bitmap and one buffer for the
    current page

  RETURN
    0  ok
    1  error (couldn't allocate memory or disk error)
*/

my_bool _ma_scan_init_block_record(MARIA_HA *info)
{
  byte *ptr;
  DBUG_ENTER("_ma_scan_init_block_record");
  if (!(ptr= (byte *) my_malloc(info->s->block_size * 2, MYF(MY_WME))))
    DBUG_RETURN(1);
  info->scan.bitmap_buff= ptr;
  info->scan.page_buff= ptr + info->s->block_size;
  info->scan.bitmap_end= info->scan.bitmap_buff + info->s->bitmap.total_size;

  /* Set scan variables to get _ma_scan_block() to start with reading bitmap */
  info->scan.number_of_rows= 0;
  info->scan.bitmap_pos= info->scan.bitmap_end;
  info->scan.bitmap_page= (ulong) - (long) info->s->bitmap.pages_covered;
  /*
    We have to flush bitmap as we will read the bitmap from the page cache
    while scanning rows
  */
  DBUG_RETURN(_ma_flush_bitmap(info->s));
}


/* Free buffers allocated by _ma_scan_block_init() */

void _ma_scan_end_block_record(MARIA_HA *info)
{
  DBUG_ENTER("_ma_scan_end_block_record");
  my_free(info->scan.bitmap_buff, MYF(MY_ALLOW_ZERO_PTR));
  info->scan.bitmap_buff= 0;
  DBUG_VOID_RETURN;
}


/*
  Read next record while scanning table

  SYNOPSIS
    _ma_scan_block_record()
    info                Maria handler
    record              Store found here
    record_pos          Value stored in info->cur_row.next_pos after last call
    skip_deleted

  NOTES
    - One must have called mi_scan() before this
    - In this version, we don't actually need record_pos, we as easily
      use a variable in info->scan

  IMPLEMENTATION
    Current code uses a lot of goto's to separate the different kind of
    states we may be in. This gives us a minimum of executed if's for
    the normal cases.  I tried several different ways to code this, but
    the current one was in the end the most readable and fastest.

  RETURN
    0   ok
    #   Error code
*/

int _ma_scan_block_record(MARIA_HA *info, byte *record,
                          MARIA_RECORD_POS record_pos,
                          my_bool skip_deleted __attribute__ ((unused)))
{
  uint block_size;
  my_off_t filepos;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_scan_block_record");

restart_record_read:
  /* Find next row in current page */
  if (likely(record_pos < info->scan.number_of_rows))
  {
    uint length, offset;
    byte *data, *end_of_data;

    while (!(offset= uint2korr(info->scan.dir)))
    {
      info->scan.dir-= DIR_ENTRY_SIZE;
      record_pos++;
#ifdef SANITY_CHECKS
      if (info->scan.dir < info->scan.dir_end)
        goto err;
#endif
    }
    /* found row */
    info->cur_row.lastpos= info->scan.row_base_page + record_pos;
    info->cur_row.nextpos= record_pos + 1;
    data= info->scan.page_buff + offset;
    length= uint2korr(info->scan.dir + 2);
    end_of_data= data + length;
    info->scan.dir-= DIR_ENTRY_SIZE;          /* Point to previous row */
#ifdef SANITY_CHECKS
    if (end_of_data > info->scan.dir_end ||
        offset < PAGE_HEADER_SIZE || length < share->base.min_block_length)
      goto err;
#endif
    DBUG_PRINT("info", ("rowid: %lu", (ulong) info->cur_row.lastpos));
    DBUG_RETURN(_ma_read_block_record2(info, record, data, end_of_data));
  }

  /* Find next head page in current bitmap */
restart_bitmap_scan:
  block_size= share->block_size;
  if (likely(info->scan.bitmap_pos < info->scan.bitmap_end))
  {
    byte *data=    info->scan.bitmap_pos;
    longlong bits= info->scan.bits;
    uint bit_pos=  info->scan.bit_pos;

    do
    {
      while (likely(bits))
      {
        uint pattern= bits & 7;
        bits >>= 3;
        bit_pos++;
        if (pattern > 0 && pattern <= 4)
        {
          /* Found head page; Read it */
          ulong page;
          info->scan.bitmap_pos= data;
          info->scan.bits= bits;
          info->scan.bit_pos= bit_pos;
          page= (info->scan.bitmap_page + 1 +
                 (data - info->scan.bitmap_buff) / 6 * 16 + bit_pos - 1);
          info->scan.row_base_page= ma_recordpos(page, 0);
          if (!(pagecache_read(share->pagecache,
                               &info->dfile,
                               page, 0, info->scan.page_buff,
                               share->page_type,
                               PAGECACHE_LOCK_LEFT_UNLOCKED, 0)))
            DBUG_RETURN(my_errno);
          if (((info->scan.page_buff[PAGE_TYPE_OFFSET] & PAGE_TYPE_MASK) !=
               HEAD_PAGE) ||
              (info->scan.number_of_rows=
               (uint) (uchar) info->scan.page_buff[DIR_COUNT_OFFSET]) == 0)
          {
            DBUG_PRINT("error", ("Wrong page header"));
            DBUG_RETURN((my_errno= HA_ERR_WRONG_IN_RECORD));
          }
          info->scan.dir= (info->scan.page_buff + block_size -
                           PAGE_SUFFIX_SIZE - DIR_ENTRY_SIZE);
          info->scan.dir_end= (info->scan.dir -
                               (info->scan.number_of_rows - 1) *
                               DIR_ENTRY_SIZE);
          record_pos= 0;
          goto restart_record_read;
        }
      }
      for (data+= 6; data < info->scan.bitmap_end; data+= 6)
      {
        bits= uint6korr(data);
        if (bits && ((bits & LL(04444444444444444)) != LL(04444444444444444)))
          break;
      }
      bit_pos= 0;
    } while (data < info->scan.bitmap_end);
  }

  /* Read next bitmap */
  info->scan.bitmap_page+= share->bitmap.pages_covered;
  filepos= (my_off_t) info->scan.bitmap_page * block_size;
  if (unlikely(filepos >= info->state->data_file_length))
  {
    DBUG_RETURN((my_errno= HA_ERR_END_OF_FILE));
  }
  if (!(pagecache_read(share->pagecache, &info->dfile,
                       info->scan.bitmap_page,
                       0, info->scan.bitmap_buff, PAGECACHE_PLAIN_PAGE,
                       PAGECACHE_LOCK_LEFT_UNLOCKED, 0)))
    DBUG_RETURN(my_errno);
  /* Skip scanning 'bits' in bitmap scan code */
  info->scan.bitmap_pos= info->scan.bitmap_buff - 6;
  info->scan.bits= 0;
  goto restart_bitmap_scan;

err:
  DBUG_PRINT("error", ("Wrong data on page"));
  DBUG_RETURN((my_errno= HA_ERR_WRONG_IN_RECORD));
}


/*
  Compare a row against a stored one

  NOTES
    Not implemented, as block record is not supposed to be used in a shared
    global environment
*/

my_bool _ma_compare_block_record(MARIA_HA *info __attribute__ ((unused)),
                                 const byte *record __attribute__ ((unused)))
{
  return 0;
}


#ifndef DBUG_OFF

static void _ma_print_directory(byte *buff, uint block_size)
{
  uint max_entry= (uint) ((uchar *) buff)[DIR_COUNT_OFFSET], row= 0;
  uint end_of_prev_row= PAGE_HEADER_SIZE;
  byte *dir, *end;

  dir= buff + block_size - DIR_ENTRY_SIZE * max_entry - PAGE_SUFFIX_SIZE;
  end= buff + block_size - DIR_ENTRY_SIZE - PAGE_SUFFIX_SIZE;

  DBUG_LOCK_FILE;
  fprintf(DBUG_FILE,"Directory dump (pos:length):\n");

  for (row= 1; dir <= end ; end-= DIR_ENTRY_SIZE, row++)
  {
    uint offset= uint2korr(end);
    uint length= uint2korr(end+2);
    fprintf(DBUG_FILE, "   %4u:%4u", offset, offset ? length : 0);
    if (!(row % (80/12)))
      fputc('\n', DBUG_FILE);
    if (offset)
    {
      DBUG_ASSERT(offset >= end_of_prev_row);
      end_of_prev_row= offset + length;
    }
  }
  fputc('\n', DBUG_FILE);
  fflush(DBUG_FILE);
  DBUG_UNLOCK_FILE;
}
#endif /* DBUG_OFF */


/*
  Store an integer with simple packing

  SYNOPSIS
    ma_store_integer()
    to                  Store the packed integer here
    nr                  Integer to store

  NOTES
    This is mostly used to store field numbers and lengths of strings.
    We have to cast the result for the LL() becasue of a bug in Forte CC
    compiler.

    Packing used is:
    nr < 251 is stored as is (in 1 byte)
    Numbers that require 1-4 bytes are stored as char(250+byte_length), data
    Bigger numbers are stored as 255, data as ulonglong (not yet done).

  RETURN
    Position in 'to' after the packed length
*/

uchar *ma_store_length(uchar *to, ulong nr)
{
  if (nr < 251)
  {
    *to=(uchar) nr;
    return to+1;
  }
  if (nr < 65536)
  {
    if (nr <= 255)
    {
      to[0]= (uchar) 251;
      to[1]= (uchar) nr;
      return to+2;
    }
    to[0]= (uchar) 252;
    int2store(to+1, nr);
    return to+3;
  }
  if (nr < 16777216)
  {
    *to++= (uchar) 253;
    int3store(to, nr);
    return to+3;
  }
  *to++= (uchar) 254;
  int4store(to, nr);
  return to+4;
}


/* Calculate how many bytes needed to store a number */

uint ma_calc_length_for_store_length(ulong nr)
{
  if (nr < 251)
    return 1;
  if (nr < 65536)
  {
    if (nr <= 255)
      return 2;
    return 3;
  }
  if (nr < 16777216)
    return 4;
  return 5;
}


/*
  Fill array with pointers to field parts to be stored in log for insert

  SYNOPSIS
    fill_insert_undo_parts()
    info                Maria handler
    record              Inserted row
    log_parts           Store pointers to changed memory areas here
    log_parts_count     See RETURN

  NOTES
    We have information in info->cur_row about the read row.

  RETURN
    length of data in log_parts.
    log_parts_count contains number of used log_parts
*/

static size_t fill_insert_undo_parts(MARIA_HA *info, const byte *record,
                                     LEX_STRING *log_parts,
                                     uint *log_parts_count)
{
  MARIA_SHARE *share= info->s;
  MARIA_COLUMNDEF *column, *end_column;
  uchar *field_lengths= info->cur_row.field_lengths;
  size_t row_length;
  MARIA_ROW *cur_row= &info->cur_row;
  LEX_STRING *start_log_parts;
  DBUG_ENTER("fill_insert_undo_parts");

  start_log_parts= log_parts;

  /* Store null bits */
  log_parts->str=      (char*) record;
  log_parts->length=   share->base.null_bytes;
  row_length=          log_parts->length;
  log_parts++;

  /* Stored bitmap over packed (zero length or all-zero fields) */
  start_log_parts=   log_parts;
  log_parts->str=    info->cur_row.empty_bits;
  log_parts->length= share->base.pack_bytes;
  row_length+=       log_parts->length;
  log_parts++;

  if (share->base.max_field_lengths)
  {
    /* Store field lenghts, with a prefix of number of bytes */
    log_parts->str=      field_lengths-2;
    log_parts->length=   info->cur_row.field_lengths_length+2;
    int2store(log_parts->str, info->cur_row.field_lengths_length);
    row_length+= log_parts->length;
    log_parts++;
  }

  /* Handle constant length fields that are always present */
  for (column= share->columndef,
       end_column= column+ share->base.fixed_not_null_fields;
       column < end_column;
       column++)
  {
    log_parts->str= (char*) record + column->offset;
    log_parts->length= column->length;
    row_length+= log_parts->length;
    log_parts++;
  }

  /* Handle NULL fields and CHAR/VARCHAR fields */
  for (end_column= share->columndef + share->base.fields - share->base.blobs;
       column < end_column;
       column++)
  {
    const uchar *column_pos;
    size_t column_length;
    if ((record[column->null_pos] & column->null_bit) ||
        cur_row->empty_bits[column->empty_pos] & column->empty_bit)
      continue;

    column_pos=    record+ column->offset;
    column_length= column->length;

    switch ((enum en_fieldtype) column->type) {
    case FIELD_CHECK:
    case FIELD_NORMAL:                          /* Fixed length field */
    case FIELD_ZERO:
    case FIELD_SKIP_PRESPACE:                   /* Not packed */
    case FIELD_SKIP_ZERO:                       /* Fixed length field */
      break;
    case FIELD_SKIP_ENDSPACE:                   /* CHAR */
    {
      if (column->length <= 255)
        column_length= *field_lengths++;
      else
      {
        column_length= uint2korr(field_lengths);
        field_lengths+= 2;
      }
      break;
    }
    case FIELD_VARCHAR:
    {
      if (column->length <= 256)
      {
        column_length= *field_lengths;
        field_lengths++;
      }
      else
      {
        column_length= uint2korr(field_lengths);
        field_lengths+= 2;
      }
      break;
    }
    default:
      DBUG_ASSERT(0);
    }
    log_parts->str= (char*) column_pos;
    log_parts->length= column_length;
    row_length+= log_parts->length;
    log_parts++;
  }

  /* Add blobs */
  for (end_column+= share->base.blobs; column < end_column; column++)
  {
    const byte *field_pos= record + column->offset;
    uint size_length= column->length - portable_sizeof_char_ptr;
    ulong blob_length= _ma_calc_blob_length(size_length, field_pos);

    /*
      We don't have to check for null, as blob_length is guranteed to be 0
      if the blob is null
    */
    if (blob_length)
    {
      char *blob_pos;
      memcpy_fixed((byte*) &blob_pos, record + column->offset + size_length,
                   sizeof(blob_pos));
      log_parts->str= blob_pos;
      log_parts->length= blob_length;
      row_length+= log_parts->length;
      log_parts++;
    }
  }
  *log_parts_count= (log_parts - start_log_parts);
  DBUG_RETURN(row_length);
}


/*
   Fill array with pointers to field parts to be stored in log for update

  SYNOPSIS
    fill_update_undo_parts()
    info                Maria handler
    oldrec		Original row
    newrec              New row
    log_parts           Store pointers to changed memory areas here
    log_parts_count     See RETURN

  IMPLEMENTATION
    Format of undo record:

    Fields are stored in same order as the field array.

    Number of changed fields (packed)

    For each changed field
      Fieldnumber (packed)
      Length, if variable length field (packed)

    For each changed field
     Data

   Packing is using ma_store_integer()

   The reason we store field numbers & length separated from data (ie, not
   after each other) is to get better cpu caching when we loop over
   fields (as we probably don't have to access data for each field when we
   want to read and old row through the undo log record).

   As a special case, we use '255' for the field number of the null bitmap.

  RETURN
    length of data in log_parts.
    log_parts_count contains number of used log_parts
*/

static size_t fill_update_undo_parts(MARIA_HA *info, const byte *oldrec,
                                     const byte *newrec,
                                     LEX_STRING *log_parts,
                                     uint *log_parts_count)
{
  MARIA_SHARE *share= info->s;
  MARIA_COLUMNDEF *column, *end_column;
  MARIA_ROW *old_row= &info->cur_row, *new_row= &info->new_row;
  uchar *field_data, *start_field_data;
  uchar *old_field_lengths= old_row->field_lengths;
  uchar *new_field_lengths= new_row->field_lengths;
  size_t row_length;
  uint field_count= 0;
  LEX_STRING *start_log_parts;
  my_bool new_column_is_empty;
  DBUG_ENTER("fill_update_undo_parts");

  start_log_parts= log_parts;

  /*
    First log part is for number of fields, field numbers and lengths
    The +4 is to reserve place for the number of changed fields.
  */
  start_field_data= field_data= info->update_field_data + 4;
  log_parts++;
  
  if (memcmp(oldrec, newrec, share->base.null_bytes))
  {
    /* Store changed null bits */
    *field_data++=       (uchar) 255;           /* Special case */
    field_count++;
    log_parts->str=      (char*) oldrec;
    log_parts->length=   share->base.null_bytes;
    row_length=          log_parts->length;
    log_parts++;
  }

  /* Handle constant length fields */
  for (column= share->columndef,
       end_column= column+ share->base.fixed_not_null_fields;
       column < end_column;
       column++)
  {
    if (memcmp(oldrec + column->offset, newrec + column->offset,
               column->length))
    {
      field_data= ma_store_length(field_data, 
                                  (uint) (column - share->columndef));
      field_count++;
      log_parts->str= (char*) oldrec + column->offset;
      log_parts->length= column->length;
      row_length+= log_parts->length;
      log_parts++;
    }
  }

  /* Handle the rest: NULL fields and CHAR/VARCHAR fields and BLOB's */
  for (end_column= share->columndef + share->base.fields;
       column < end_column;
       column++)
  {
    const uchar *new_column_pos, *old_column_pos;
    size_t new_column_length, old_column_length;

    /* First check if old column is null or empty */
    if (oldrec[column->null_pos] & column->null_bit)
    {
      /*
        It's safe to skip this one as either the new column is also null
        (no change) or the new_column is not null, in which case the null-bit
        maps differed and we have already stored the null bitmap.
      */
      continue;
    }
    if (old_row->empty_bits[column->empty_pos] & column->empty_bit)
    {
      if (new_row->empty_bits[column->empty_pos] & column->empty_bit)
        continue;                               /* Both are empty; skip */

      /* Store null length column */
      field_data= ma_store_length(field_data, 
                                  (uint) (column - share->columndef));
      field_data= ma_store_length(field_data, 0);
      field_count++;
      continue;
    }
    /*
      Remember if the 'new' value is empty (as in this case we must always
      log the original value
    */
    new_column_is_empty= ((newrec[column->null_pos] & column->null_bit) ||
                          (new_row->empty_bits[column->empty_pos] &
                           column->empty_bit));

    old_column_pos=      oldrec + column->offset;
    new_column_pos=      newrec + column->offset;
    old_column_length= new_column_length= column->length;

    switch ((enum en_fieldtype) column->type) {
    case FIELD_CHECK:
    case FIELD_NORMAL:                          /* Fixed length field */
    case FIELD_ZERO:
    case FIELD_SKIP_PRESPACE:                   /* Not packed */
    case FIELD_SKIP_ZERO:                       /* Fixed length field */
      break;
    case FIELD_VARCHAR:
      new_column_length--;                      /* Skip length prefix */
      /* Fall through */
    case FIELD_SKIP_ENDSPACE:                   /* CHAR */
    {
      if (new_column_length <= 255)
      {
        old_column_length= *old_field_lengths++;
        if (!new_column_is_empty)
          new_column_length= *new_field_lengths++;
      }
      else
      {
        old_column_length= uint2korr(old_field_lengths);
        old_field_lengths+= 2;
        if (!new_column_is_empty)
        {
          new_column_length= uint2korr(new_field_lengths);
          new_field_lengths+= 2;
        }
      }
      break;
    }
    case FIELD_BLOB:
    {
      uint size_length= column->length - portable_sizeof_char_ptr;
      old_column_length= _ma_calc_blob_length(size_length, old_column_pos);
      memcpy_fixed((byte*) &old_column_pos,
                   oldrec + column->offset + size_length,
                   sizeof(old_column_pos));
      if (!new_column_is_empty)
      {
        new_column_length= _ma_calc_blob_length(size_length, new_column_pos);
        memcpy_fixed((byte*) &new_column_pos,
                     newrec + column->offset + size_length,
                     sizeof(old_column_pos));
      }
      break;
    }
    default:
      DBUG_ASSERT(0);
    }

    if (new_column_is_empty || new_column_length != old_column_length ||
        memcmp(old_column_pos, new_column_pos, new_column_length))
    {
      field_data= ma_store_length(field_data, 
                                  (uint) (column - share->columndef));
      field_data= ma_store_length(field_data, old_column_length);
      field_count++;

      log_parts->str=     (char*) old_column_pos;
      log_parts->length=  old_column_length;
      row_length+=        log_parts->length;
      log_parts++;
    }
  }

  *log_parts_count= (log_parts - start_log_parts);

  /* Store number of fields before the field/field_lengths */
  start_log_parts->str=  ((char*)
                          (start_field_data -
                           ma_calc_length_for_store_length(field_count)));
  ma_store_length(start_log_parts->str, field_count);
  start_log_parts->length= (size_t) ((char*) field_data -
                                     start_log_parts->str);
  row_length+= start_log_parts->length;
  DBUG_RETURN(row_length);
}
