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

/* Functions to handle keys */

#include "maria_def.h"
#include "m_ctype.h"
#include "ma_sp_defs.h"
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#define CHECK_KEYS                              /* Enable safety checks */

#define FIX_LENGTH(cs, pos, length, char_length)                            \
            do {                                                            \
              if (length > char_length)                                     \
                char_length= my_charpos(cs, pos, pos+length, char_length);  \
              set_if_smaller(char_length,length);                           \
            } while(0)

static int _ma_put_key_in_record(MARIA_HA *info,uint keynr,uchar *record);

/*
  Make a intern key from a record

  SYNOPSIS
    _ma_make_key()
    info		MyiSAM handler
    keynr		key number
    key			Store created key here
    record		Record
    filepos		Position to record in the data file

  RETURN
    Length of key
*/

uint _ma_make_key(register MARIA_HA *info, uint keynr, uchar *key,
		  const uchar *record, MARIA_RECORD_POS filepos)
{
  const uchar *pos;
  uchar *start;
  reg1 HA_KEYSEG *keyseg;
  my_bool is_ft= info->s->keyinfo[keynr].flag & HA_FULLTEXT;
  DBUG_ENTER("_ma_make_key");

  if (info->s->keyinfo[keynr].flag & HA_SPATIAL)
  {
    /*
      TODO: nulls processing
    */
#ifdef HAVE_SPATIAL
    DBUG_RETURN(_ma_sp_make_key(info,keynr, key,record,filepos));
#else
    DBUG_ASSERT(0); /* maria_open should check that this never happens*/
#endif
  }

  start=key;
  for (keyseg=info->s->keyinfo[keynr].seg ; keyseg->type ;keyseg++)
  {
    enum ha_base_keytype type=(enum ha_base_keytype) keyseg->type;
    uint length=keyseg->length;
    uint char_length;
    CHARSET_INFO *cs=keyseg->charset;

    if (keyseg->null_bit)
    {
      if (record[keyseg->null_pos] & keyseg->null_bit)
      {
	*key++= 0;				/* NULL in key */
	continue;
      }
      *key++=1;					/* Not NULL */
    }

    char_length= ((!is_ft && cs && cs->mbmaxlen > 1) ? length/cs->mbmaxlen :
                  length);

    pos= record+keyseg->start;
    if (type == HA_KEYTYPE_BIT)
    {
      if (keyseg->bit_length)
      {
        uchar bits= get_rec_bits((uchar*) record + keyseg->bit_pos,
                                 keyseg->bit_start, keyseg->bit_length);
        *key++= (char) bits;
        length--;
      }
      memcpy(key, pos, length);
      key+= length;
      continue;
    }
    if (keyseg->flag & HA_SPACE_PACK)
    {
      if (type != HA_KEYTYPE_NUM)
      {
        length= cs->cset->lengthsp(cs, pos, length);
      }
      else
      {
        const uchar *end= pos + length;
	while (pos < end && pos[0] == ' ')
	  pos++;
	length= (uint) (end-pos);
      }
      FIX_LENGTH(cs, pos, length, char_length);
      store_key_length_inc(key,char_length);
      memcpy(key, pos, (size_t) char_length);
      key+=char_length;
      continue;
    }
    if (keyseg->flag & HA_VAR_LENGTH_PART)
    {
      uint pack_length= (keyseg->bit_start == 1 ? 1 : 2);
      uint tmp_length= (pack_length == 1 ? (uint) *(uchar*) pos :
                        uint2korr(pos));
      pos+= pack_length;			/* Skip VARCHAR length */
      set_if_smaller(length,tmp_length);
      FIX_LENGTH(cs, pos, length, char_length);
      store_key_length_inc(key,char_length);
      memcpy(key,pos,(size_t) char_length);
      key+= char_length;
      continue;
    }
    else if (keyseg->flag & HA_BLOB_PART)
    {
      uint tmp_length= _ma_calc_blob_length(keyseg->bit_start,pos);
      memcpy_fixed(&pos,pos+keyseg->bit_start,sizeof(char*));
      set_if_smaller(length,tmp_length);
      FIX_LENGTH(cs, pos, length, char_length);
      store_key_length_inc(key,char_length);
      memcpy(key,pos,(size_t) char_length);
      key+= char_length;
      continue;
    }
    else if (keyseg->flag & HA_SWAP_KEY)
    {						/* Numerical column */
#ifdef HAVE_ISNAN
      if (type == HA_KEYTYPE_FLOAT)
      {
	float nr;
	float4get(nr,pos);
	if (isnan(nr))
	{
	  /* Replace NAN with zero */
	  bzero(key,length);
	  key+=length;
	  continue;
	}
      }
      else if (type == HA_KEYTYPE_DOUBLE)
      {
	double nr;
	float8get(nr,pos);
	if (isnan(nr))
	{
	  bzero(key,length);
	  key+=length;
	  continue;
	}
      }
#endif
      pos+=length;
      while (length--)
      {
	*key++ = *--pos;
      }
      continue;
    }
    FIX_LENGTH(cs, pos, length, char_length);
    memcpy(key, pos, char_length);
    if (length > char_length)
      cs->cset->fill(cs, (char*) key+char_length, length-char_length, ' ');
    key+= length;
  }
  _ma_dpointer(info,key,filepos);
  DBUG_PRINT("exit",("keynr: %d",keynr));
  DBUG_DUMP("key",start,(uint) (key-start)+keyseg->length);
  DBUG_EXECUTE("key",
	       _ma_print_key(DBUG_FILE,info->s->keyinfo[keynr].seg,start,
			     (uint) (key-start)););
  DBUG_RETURN((uint) (key-start));		/* Return keylength */
} /* _ma_make_key */


/*
  Pack a key to intern format from given format (c_rkey)

  SYNOPSIS
    _ma_pack_key()
    info		MARIA handler
    uint keynr		key number
    key			Store packed key here
    old			Not packed key
    k_length		Length of 'old' to use
    last_used_keyseg	out parameter.  May be NULL

   RETURN
     length of packed key

     last_use_keyseg    Store pointer to the keyseg after the last used one
*/

uint _ma_pack_key(register MARIA_HA *info, uint keynr, uchar *key,
                  const uchar *old, uint k_length, HA_KEYSEG **last_used_keyseg)
{
  uchar *start_key=key;
  HA_KEYSEG *keyseg;
  my_bool is_ft= info->s->keyinfo[keynr].flag & HA_FULLTEXT;
  DBUG_ENTER("_ma_pack_key");

  for (keyseg=info->s->keyinfo[keynr].seg ;
       keyseg->type && (int) k_length > 0;
       old+=keyseg->length, keyseg++)
  {
    enum ha_base_keytype type=(enum ha_base_keytype) keyseg->type;
    uint length=min((uint) keyseg->length,(uint) k_length);
    uint char_length;
    const uchar *pos;
    CHARSET_INFO *cs=keyseg->charset;

    if (keyseg->null_bit)
    {
      k_length--;
      if (!(*key++= (char) 1-*old++))			/* Copy null marker */
      {
	k_length-=length;
        if (keyseg->flag & (HA_VAR_LENGTH_PART | HA_BLOB_PART))
        {
          k_length-=2;                                  /* Skip length */
          old+= 2;
        }
	continue;					/* Found NULL */
      }
    }
    char_length= ((!is_ft && cs && cs->mbmaxlen > 1) ? length/cs->mbmaxlen :
                  length);
    pos= old;
    if (keyseg->flag & HA_SPACE_PACK)
    {
      const uchar *end= pos + length;
      if (type != HA_KEYTYPE_NUM)
      {
	while (end > pos && end[-1] == ' ')
	  end--;
      }
      else
      {
	while (pos < end && pos[0] == ' ')
	  pos++;
      }
      k_length-=length;
      length=(uint) (end-pos);
      FIX_LENGTH(cs, pos, length, char_length);
      store_key_length_inc(key,char_length);
      memcpy(key,pos,(size_t) char_length);
      key+= char_length;
      continue;
    }
    else if (keyseg->flag & (HA_VAR_LENGTH_PART | HA_BLOB_PART))
    {
      /* Length of key-part used with maria_rkey() always 2 */
      uint tmp_length=uint2korr(pos);
      k_length-= 2+length;
      pos+=2;
      set_if_smaller(length,tmp_length);	/* Safety */
      FIX_LENGTH(cs, pos, length, char_length);
      store_key_length_inc(key,char_length);
      old+=2;					/* Skip length */
      memcpy(key, pos,(size_t) char_length);
      key+= char_length;
      continue;
    }
    else if (keyseg->flag & HA_SWAP_KEY)
    {						/* Numerical column */
      pos+=length;
      k_length-=length;
      while (length--)
      {
	*key++ = *--pos;
      }
      continue;
    }
    FIX_LENGTH(cs, pos, length, char_length);
    memcpy(key, pos, char_length);
    if (length > char_length)
      cs->cset->fill(cs, (char*) key+char_length, length-char_length, ' ');
    key+= length;
    k_length-=length;
  }
  if (last_used_keyseg)
    *last_used_keyseg= keyseg;

#ifdef NOT_USED
  if (keyseg->type)
  {
    /* Part-key ; fill with ASCII 0 for easier searching */
    length= (uint) -k_length;			/* unused part of last key */
    do
    {
      if (keyseg->flag & HA_NULL_PART)
	length++;
      if (keyseg->flag & HA_SPACE_PACK)
	length+=2;
      else
	length+= keyseg->length;
      keyseg++;
    } while (keyseg->type);
    bzero(key,length);
    key+=length;
  }
#endif
  DBUG_PRINT("exit", ("length: %u", (uint) (key-start_key)));
  DBUG_RETURN((uint) (key-start_key));
} /* _ma_pack_key */



/*
  Store found key in record

  SYNOPSIS
    _ma_put_key_in_record()
    info		MARIA handler
    keynr		Key number that was used
    record 		Store key here

    Last read key is in info->lastkey

 NOTES
   Used when only-keyread is wanted

 RETURN
   0   ok
   1   error
*/

static int _ma_put_key_in_record(register MARIA_HA *info, uint keynr,
				 uchar *record)
{
  reg2 uchar *key;
  uchar *pos,*key_end;
  reg1 HA_KEYSEG *keyseg;
  uchar *blob_ptr;
  DBUG_ENTER("_ma_put_key_in_record");

  blob_ptr= info->lastkey2;             /* Place to put blob parts */
  key=info->lastkey;                    /* KEy that was read */
  key_end=key+info->lastkey_length;
  for (keyseg=info->s->keyinfo[keynr].seg ; keyseg->type ;keyseg++)
  {
    if (keyseg->null_bit)
    {
      if (!*key++)
      {
	record[keyseg->null_pos]|= keyseg->null_bit;
	continue;
      }
      record[keyseg->null_pos]&= ~keyseg->null_bit;
    }
    if (keyseg->type == HA_KEYTYPE_BIT)
    {
      uint length= keyseg->length;

      if (keyseg->bit_length)
      {
        uchar bits= *key++;
        set_rec_bits(bits, record + keyseg->bit_pos, keyseg->bit_start,
                     keyseg->bit_length);
        length--;
      }
      else
      {
        clr_rec_bits(record + keyseg->bit_pos, keyseg->bit_start,
                     keyseg->bit_length);
      }
      memcpy(record + keyseg->start, key, length);
      key+= length;
      continue;
    }
    if (keyseg->flag & HA_SPACE_PACK)
    {
      uint length;
      get_key_length(length,key);
#ifdef CHECK_KEYS
      if (length > keyseg->length || key+length > key_end)
	goto err;
#endif
      pos= record+keyseg->start;
      if (keyseg->type != (int) HA_KEYTYPE_NUM)
      {
        memcpy(pos,key,(size_t) length);
        keyseg->charset->cset->fill(keyseg->charset,
                                    pos + length, keyseg->length - length,
                                    ' ');
      }
      else
      {
	bfill(pos,keyseg->length-length,' ');
	memcpy(pos+keyseg->length-length,key,(size_t) length);
      }
      key+=length;
      continue;
    }

    if (keyseg->flag & HA_VAR_LENGTH_PART)
    {
      uint length;
      get_key_length(length,key);
#ifdef CHECK_KEYS
      if (length > keyseg->length || key+length > key_end)
	goto err;
#endif
      /* Store key length */
      if (keyseg->bit_start == 1)
        *(uchar*) (record+keyseg->start)= (uchar) length;
      else
        int2store(record+keyseg->start, length);
      /* And key data */
      memcpy(record+keyseg->start + keyseg->bit_start, key, length);
      key+= length;
    }
    else if (keyseg->flag & HA_BLOB_PART)
    {
      uint length;
      get_key_length(length,key);
#ifdef CHECK_KEYS
      if (length > keyseg->length || key+length > key_end)
	goto err;
#endif
      memcpy(record+keyseg->start+keyseg->bit_start,
	     (char*) &blob_ptr,sizeof(char*));
      memcpy(blob_ptr,key,length);
      blob_ptr+=length;

      /* The above changed info->lastkey2. Inform maria_rnext_same(). */
      info->update&= ~HA_STATE_RNEXT_SAME;

      _ma_store_blob_length(record+keyseg->start,
			    (uint) keyseg->bit_start,length);
      key+=length;
    }
    else if (keyseg->flag & HA_SWAP_KEY)
    {
      uchar *to=  record+keyseg->start+keyseg->length;
      uchar *end= key+keyseg->length;
#ifdef CHECK_KEYS
      if (end > key_end)
	goto err;
#endif
      do
      {
	 *--to= *key++;
      } while (key != end);
      continue;
    }
    else
    {
#ifdef CHECK_KEYS
      if (key+keyseg->length > key_end)
	goto err;
#endif
      memcpy(record+keyseg->start, key, (size_t) keyseg->length);
      key+= keyseg->length;
    }
  }
  DBUG_RETURN(0);

err:
  DBUG_RETURN(1);				/* Crashed row */
} /* _ma_put_key_in_record */


	/* Here when key reads are used */

int _ma_read_key_record(MARIA_HA *info, uchar *buf, MARIA_RECORD_POS filepos)
{
  fast_ma_writeinfo(info);
  if (filepos != HA_OFFSET_ERROR)
  {
    if (info->lastinx >= 0)
    {				/* Read only key */
      if (_ma_put_key_in_record(info,(uint) info->lastinx,buf))
      {
        maria_print_error(info->s, HA_ERR_CRASHED);
	my_errno=HA_ERR_CRASHED;
	return -1;
      }
      info->update|= HA_STATE_AKTIV; /* We should find a record */
      return 0;
    }
    my_errno=HA_ERR_WRONG_INDEX;
  }
  return(-1);				/* Wrong data to read */
}


/*
  Retrieve auto_increment info

  SYNOPSIS
    retrieve_auto_increment()
    info			Maria handler
    record			Row to update

  IMPLEMENTATION
    For signed columns we don't retrieve the auto increment value if it's
    less than zero.
*/

ulonglong ma_retrieve_auto_increment(MARIA_HA *info,const uchar *record)
{
  ulonglong value= 0;			/* Store unsigned values here */
  longlong s_value= 0;			/* Store signed values here */
  HA_KEYSEG *keyseg= info->s->keyinfo[info->s->base.auto_key-1].seg;
  const uchar *key= record + keyseg->start;

  switch (keyseg->type) {
  case HA_KEYTYPE_INT8:
    s_value= (longlong) *(char*)key;
    break;
  case HA_KEYTYPE_BINARY:
    value=(ulonglong)  *(uchar*) key;
    break;
  case HA_KEYTYPE_SHORT_INT:
    s_value= (longlong) sint2korr(key);
    break;
  case HA_KEYTYPE_USHORT_INT:
    value=(ulonglong) uint2korr(key);
    break;
  case HA_KEYTYPE_LONG_INT:
    s_value= (longlong) sint4korr(key);
    break;
  case HA_KEYTYPE_ULONG_INT:
    value=(ulonglong) uint4korr(key);
    break;
  case HA_KEYTYPE_INT24:
    s_value= (longlong) sint3korr(key);
    break;
  case HA_KEYTYPE_UINT24:
    value=(ulonglong) uint3korr(key);
    break;
  case HA_KEYTYPE_FLOAT:                        /* This shouldn't be used */
  {
    float f_1;
    float4get(f_1,key);
    /* Ignore negative values */
    value = (f_1 < (float) 0.0) ? 0 : (ulonglong) f_1;
    break;
  }
  case HA_KEYTYPE_DOUBLE:                       /* This shouldn't be used */
  {
    double f_1;
    float8get(f_1,key);
    /* Ignore negative values */
    value = (f_1 < 0.0) ? 0 : (ulonglong) f_1;
    break;
  }
  case HA_KEYTYPE_LONGLONG:
    s_value= sint8korr(key);
    break;
  case HA_KEYTYPE_ULONGLONG:
    value= uint8korr(key);
    break;
  default:
    DBUG_ASSERT(0);
    value=0;                                    /* Error */
    break;
  }

  /*
    The following code works becasue if s_value < 0 then value is 0
    and if s_value == 0 then value will contain either s_value or the
    correct value.
  */
  return (s_value > 0) ? (ulonglong) s_value : value;
}
