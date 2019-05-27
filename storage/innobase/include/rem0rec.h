/*****************************************************************************

Copyright (c) 1994, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, 2018, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/********************************************************************//**
@file include/rem0rec.h
Record manager

Created 5/30/1994 Heikki Tuuri
*************************************************************************/

#ifndef rem0rec_h
#define rem0rec_h

#ifndef UNIV_INNOCHECKSUM
#include "data0data.h"
#include "rem0types.h"
#include "mtr0types.h"
#include "page0types.h"
#include "dict0dict.h"
#include "trx0types.h"
#endif /*! UNIV_INNOCHECKSUM */
#include <ostream>
#include <sstream>

/* Number of extra bytes in an old-style record,
in addition to the data and the offsets */
#define REC_N_OLD_EXTRA_BYTES	6
/* Number of extra bytes in a new-style record,
in addition to the data and the offsets */
#define REC_N_NEW_EXTRA_BYTES	5

#define REC_NEW_STATUS		3	/* This is single byte bit-field */
#define REC_NEW_STATUS_MASK	0x7UL
#define REC_NEW_STATUS_SHIFT	0

/* The following four constants are needed in page0zip.cc in order to
efficiently compress and decompress pages. */

/* The offset of heap_no in a compact record */
#define REC_NEW_HEAP_NO		4
/* The shift of heap_no in a compact record.
The status is stored in the low-order bits. */
#define	REC_HEAP_NO_SHIFT	3

/* Length of a B-tree node pointer, in bytes */
#define REC_NODE_PTR_SIZE	4

/** SQL null flag in a 1-byte offset of ROW_FORMAT=REDUNDANT records */
#define REC_1BYTE_SQL_NULL_MASK	0x80UL
/** SQL null flag in a 2-byte offset of ROW_FORMAT=REDUNDANT records */
#define REC_2BYTE_SQL_NULL_MASK	0x8000UL

/** In a 2-byte offset of ROW_FORMAT=REDUNDANT records, the second most
significant bit denotes that the tail of a field is stored off-page. */
#define REC_2BYTE_EXTERN_MASK	0x4000UL

#ifdef UNIV_DEBUG
/* Length of the rec_get_offsets() header */
# define REC_OFFS_HEADER_SIZE	4
#else /* UNIV_DEBUG */
/* Length of the rec_get_offsets() header */
# define REC_OFFS_HEADER_SIZE	2
#endif /* UNIV_DEBUG */

/* Number of elements that should be initially allocated for the
offsets[] array, first passed to rec_get_offsets() */
#define REC_OFFS_NORMAL_SIZE	OFFS_IN_REC_NORMAL_SIZE
#define REC_OFFS_SMALL_SIZE	10

/** Get the base address of offsets.  The extra_size is stored at
this position, and following positions hold the end offsets of
the fields. */
#define rec_offs_base(offsets) (offsets + REC_OFFS_HEADER_SIZE)

/** Compact flag ORed to the extra size returned by rec_get_offsets() */
const ulint REC_OFFS_COMPACT = ~(ulint(~0) >> 1);
/** SQL NULL flag in offsets returned by rec_get_offsets() */
const ulint REC_OFFS_SQL_NULL = REC_OFFS_COMPACT;
/** External flag in offsets returned by rec_get_offsets() */
const ulint REC_OFFS_EXTERNAL = REC_OFFS_COMPACT >> 1;
/** Default value flag in offsets returned by rec_get_offsets() */
const ulint REC_OFFS_DEFAULT = REC_OFFS_COMPACT >> 2;
/** Mask for offsets returned by rec_get_offsets() */
const ulint REC_OFFS_MASK = REC_OFFS_DEFAULT - 1;

#ifndef UNIV_INNOCHECKSUM
/******************************************************//**
The following function is used to get the pointer of the next chained record
on the same page.
@return pointer to the next chained record, or NULL if none */
UNIV_INLINE
const rec_t*
rec_get_next_ptr_const(
/*===================*/
	const rec_t*	rec,	/*!< in: physical record */
	ulint		comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to get the pointer of the next chained record
on the same page.
@return pointer to the next chained record, or NULL if none */
UNIV_INLINE
rec_t*
rec_get_next_ptr(
/*=============*/
	rec_t*	rec,	/*!< in: physical record */
	ulint	comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to get the offset of the
next chained record on the same page.
@return the page offset of the next chained record, or 0 if none */
UNIV_INLINE
ulint
rec_get_next_offs(
/*==============*/
	const rec_t*	rec,	/*!< in: physical record */
	ulint		comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the next record offset field
of an old-style record. */
UNIV_INLINE
void
rec_set_next_offs_old(
/*==================*/
	rec_t*	rec,	/*!< in: old-style physical record */
	ulint	next)	/*!< in: offset of the next record */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to set the next record offset field
of a new-style record. */
UNIV_INLINE
void
rec_set_next_offs_new(
/*==================*/
	rec_t*	rec,	/*!< in/out: new-style physical record */
	ulint	next)	/*!< in: offset of the next record */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to get the number of fields
in an old-style record.
@return number of data fields */
UNIV_INLINE
ulint
rec_get_n_fields_old(
/*=================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to get the number of fields
in a record.
@return number of data fields */
UNIV_INLINE
ulint
rec_get_n_fields(
/*=============*/
	const rec_t*		rec,	/*!< in: physical record */
	const dict_index_t*	index)	/*!< in: record descriptor */
	MY_ATTRIBUTE((warn_unused_result));

/** Confirms the n_fields of the entry is sane with comparing the other
record in the same page specified
@param[in]	index	index
@param[in]	rec	record of the same page
@param[in]	entry	index entry
@return	true if n_fields is sane */
UNIV_INLINE
bool
rec_n_fields_is_sane(
	dict_index_t*	index,
	const rec_t*	rec,
	const dtuple_t*	entry)
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
The following function is used to get the number of records owned by the
previous directory record.
@return number of owned records */
UNIV_INLINE
ulint
rec_get_n_owned_old(
/*================*/
	const rec_t*	rec)	/*!< in: old-style physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the number of owned records. */
UNIV_INLINE
void
rec_set_n_owned_old(
/*================*/
	rec_t*	rec,		/*!< in: old-style physical record */
	ulint	n_owned)	/*!< in: the number of owned */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to get the number of records owned by the
previous directory record.
@return number of owned records */
UNIV_INLINE
ulint
rec_get_n_owned_new(
/*================*/
	const rec_t*	rec)	/*!< in: new-style physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the number of owned records. */
UNIV_INLINE
void
rec_set_n_owned_new(
/*================*/
	rec_t*		rec,	/*!< in/out: new-style physical record */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page, or NULL */
	ulint		n_owned)/*!< in: the number of owned */
	MY_ATTRIBUTE((nonnull(1)));
/******************************************************//**
The following function is used to retrieve the info bits of
a record.
@return info bits */
UNIV_INLINE
ulint
rec_get_info_bits(
/*==============*/
	const rec_t*	rec,	/*!< in: physical record */
	ulint		comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the info bits of a record. */
UNIV_INLINE
void
rec_set_info_bits_old(
/*==================*/
	rec_t*	rec,	/*!< in: old-style physical record */
	ulint	bits)	/*!< in: info bits */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to set the info bits of a record. */
UNIV_INLINE
void
rec_set_info_bits_new(
/*==================*/
	rec_t*	rec,	/*!< in/out: new-style physical record */
	ulint	bits)	/*!< in: info bits */
	MY_ATTRIBUTE((nonnull));

/** Determine the status bits of a non-REDUNDANT record.
@param[in]	rec	ROW_FORMAT=COMPACT,DYNAMIC,COMPRESSED record
@return status bits */
inline
rec_comp_status_t
rec_get_status(const rec_t* rec)
{
	byte bits = rec[-REC_NEW_STATUS] & REC_NEW_STATUS_MASK;
	ut_ad(bits <= REC_STATUS_INSTANT);
	return static_cast<rec_comp_status_t>(bits);
}

/** Set the status bits of a non-REDUNDANT record.
@param[in,out]	rec	ROW_FORMAT=COMPACT,DYNAMIC,COMPRESSED record
@param[in]	bits	status bits */
inline
void
rec_set_status(rec_t* rec, byte bits)
{
	ut_ad(bits <= REC_STATUS_INSTANT);
	rec[-REC_NEW_STATUS] = (rec[-REC_NEW_STATUS] & ~REC_NEW_STATUS_MASK)
		| bits;
}

/** Get the length of added field count in a REC_STATUS_INSTANT record.
@param[in]	n_add_field	number of added fields, minus one
@return	storage size of the field count, in bytes */
inline unsigned rec_get_n_add_field_len(ulint n_add_field)
{
	ut_ad(n_add_field < REC_MAX_N_FIELDS);
	return n_add_field < 0x80 ? 1 : 2;
}

/** Set the added field count in a REC_STATUS_INSTANT record.
@param[in,out]	header	variable header of a REC_STATUS_INSTANT record
@param[in]	n_add	number of added fields, minus 1
@return	record header before the number of added fields */
inline void rec_set_n_add_field(byte*& header, ulint n_add)
{
	ut_ad(n_add < REC_MAX_N_FIELDS);

	if (n_add < 0x80) {
		*header-- = byte(n_add);
	} else {
		*header-- = byte(n_add) | 0x80;
		*header-- = byte(n_add >> 7);
	}
}

/******************************************************//**
The following function is used to retrieve the info and status
bits of a record.  (Only compact records have status bits.)
@return info bits */
UNIV_INLINE
ulint
rec_get_info_and_status_bits(
/*=========================*/
	const rec_t*	rec,	/*!< in: physical record */
	ulint		comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the info and status
bits of a record.  (Only compact records have status bits.) */
UNIV_INLINE
void
rec_set_info_and_status_bits(
/*=========================*/
	rec_t*	rec,	/*!< in/out: compact physical record */
	ulint	bits)	/*!< in: info bits */
	MY_ATTRIBUTE((nonnull));

/******************************************************//**
The following function tells if record is delete marked.
@return nonzero if delete marked */
UNIV_INLINE
ulint
rec_get_deleted_flag(
/*=================*/
	const rec_t*	rec,	/*!< in: physical record */
	ulint		comp)	/*!< in: nonzero=compact page format */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the deleted bit. */
UNIV_INLINE
void
rec_set_deleted_flag_old(
/*=====================*/
	rec_t*	rec,	/*!< in: old-style physical record */
	ulint	flag)	/*!< in: nonzero if delete marked */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to set the deleted bit. */
UNIV_INLINE
void
rec_set_deleted_flag_new(
/*=====================*/
	rec_t*		rec,	/*!< in/out: new-style physical record */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page, or NULL */
	ulint		flag)	/*!< in: nonzero if delete marked */
	MY_ATTRIBUTE((nonnull(1)));
/******************************************************//**
The following function tells if a new-style record is a node pointer.
@return TRUE if node pointer */
UNIV_INLINE
bool
rec_get_node_ptr_flag(
/*==================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to get the order number
of an old-style record in the heap of the index page.
@return heap order number */
UNIV_INLINE
ulint
rec_get_heap_no_old(
/*================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the heap number
field in an old-style record. */
UNIV_INLINE
void
rec_set_heap_no_old(
/*================*/
	rec_t*	rec,	/*!< in: physical record */
	ulint	heap_no)/*!< in: the heap number */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to get the order number
of a new-style record in the heap of the index page.
@return heap order number */
UNIV_INLINE
ulint
rec_get_heap_no_new(
/*================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));
/******************************************************//**
The following function is used to set the heap number
field in a new-style record. */
UNIV_INLINE
void
rec_set_heap_no_new(
/*================*/
	rec_t*	rec,	/*!< in/out: physical record */
	ulint	heap_no)/*!< in: the heap number */
	MY_ATTRIBUTE((nonnull));
/******************************************************//**
The following function is used to test whether the data offsets
in the record are stored in one-byte or two-byte format.
@return TRUE if 1-byte form */
UNIV_INLINE
ibool
rec_get_1byte_offs_flag(
/*====================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
The following function is used to set the 1-byte offsets flag. */
UNIV_INLINE
void
rec_set_1byte_offs_flag(
/*====================*/
	rec_t*	rec,	/*!< in: physical record */
	ibool	flag)	/*!< in: TRUE if 1byte form */
	MY_ATTRIBUTE((nonnull));

/******************************************************//**
Returns the offset of nth field end if the record is stored in the 1-byte
offsets form. If the field is SQL null, the flag is ORed in the returned
value.
@return offset of the start of the field, SQL null flag ORed */
UNIV_INLINE
ulint
rec_1_get_field_end_info(
/*=====================*/
	const rec_t*	rec,	/*!< in: record */
	ulint		n)	/*!< in: field index */
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
Returns the offset of nth field end if the record is stored in the 2-byte
offsets form. If the field is SQL null, the flag is ORed in the returned
value.
@return offset of the start of the field, SQL null flag and extern
storage flag ORed */
UNIV_INLINE
ulint
rec_2_get_field_end_info(
/*=====================*/
	const rec_t*	rec,	/*!< in: record */
	ulint		n)	/*!< in: field index */
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
Returns nonzero if the field is stored off-page.
@retval 0 if the field is stored in-page
@retval REC_2BYTE_EXTERN_MASK if the field is stored externally */
UNIV_INLINE
ulint
rec_2_is_field_extern(
/*==================*/
	const rec_t*	rec,	/*!< in: record */
	ulint		n)	/*!< in: field index */
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
Determine how many of the first n columns in a compact
physical record are stored externally.
@return number of externally stored columns */
ulint
rec_get_n_extern_new(
/*=================*/
	const rec_t*		rec,	/*!< in: compact physical record */
	const dict_index_t*	index,	/*!< in: record descriptor */
	ulint			n)	/*!< in: number of columns to scan */
	MY_ATTRIBUTE((nonnull, warn_unused_result));

/** Determine the offsets to each field in an index record.
@param[in]	rec		physical record
@param[in]	index		the index that the record belongs to
@param[in,out]	offsets		array comprising offsets[0] allocated elements,
				or an array from rec_get_offsets(), or NULL
@param[in]	leaf		whether this is a leaf-page record
@param[in]	n_fields	maximum number of offsets to compute
				(ULINT_UNDEFINED to compute all offsets)
@param[in,out]	heap		memory heap
@return the new offsets */
ulint*
rec_get_offsets_func(
	const rec_t*		rec,
	const dict_index_t*	index,
	ulint*			offsets,
	bool			leaf,
	ulint			n_fields,
#ifdef UNIV_DEBUG
	const char*		file,	/*!< in: file name where called */
	unsigned		line,	/*!< in: line number where called */
#endif /* UNIV_DEBUG */
	mem_heap_t**		heap)	/*!< in/out: memory heap */
#ifdef UNIV_DEBUG
	MY_ATTRIBUTE((nonnull(1,2,6,8),warn_unused_result));
#else /* UNIV_DEBUG */
	MY_ATTRIBUTE((nonnull(1,2,6),warn_unused_result));
#endif /* UNIV_DEBUG */

#ifdef UNIV_DEBUG
# define rec_get_offsets(rec, index, offsets, leaf, n, heap)		\
	rec_get_offsets_func(rec,index,offsets,leaf,n,__FILE__,__LINE__,heap)
#else /* UNIV_DEBUG */
# define rec_get_offsets(rec, index, offsets, leaf, n, heap)		\
	rec_get_offsets_func(rec, index, offsets, leaf, n, heap)
#endif /* UNIV_DEBUG */

/******************************************************//**
The following function determines the offsets to each field
in the record.  It can reuse a previously allocated array. */
void
rec_get_offsets_reverse(
/*====================*/
	const byte*		extra,	/*!< in: the extra bytes of a
					compact record in reverse order,
					excluding the fixed-size
					REC_N_NEW_EXTRA_BYTES */
	const dict_index_t*	index,	/*!< in: record descriptor */
	ulint			node_ptr,/*!< in: nonzero=node pointer,
					0=leaf node */
	ulint*			offsets)/*!< in/out: array consisting of
					offsets[0] allocated elements */
	MY_ATTRIBUTE((nonnull));
#ifdef UNIV_DEBUG
/** Validate offsets returned by rec_get_offsets().
@param[in]	rec	record, or NULL
@param[in]	index	the index that the record belongs in, or NULL
@param[in,out]	offsets	the offsets of the record
@return true */
bool
rec_offs_validate(
	const rec_t*		rec,
	const dict_index_t*	index,
	const ulint*		offsets)
	MY_ATTRIBUTE((nonnull(3), warn_unused_result));
/** Update debug data in offsets, in order to tame rec_offs_validate().
@param[in]	rec	record
@param[in]	index	the index that the record belongs in
@param[in]	leaf	whether the record resides in a leaf page
@param[in,out]	offsets	offsets from rec_get_offsets() to adjust */
void
rec_offs_make_valid(
	const rec_t*		rec,
	const dict_index_t*	index,
	bool			leaf,
	ulint*			offsets)
	MY_ATTRIBUTE((nonnull));
#else
# define rec_offs_make_valid(rec, index, leaf, offsets)
#endif /* UNIV_DEBUG */

/************************************************************//**
The following function is used to get the offset to the nth
data field in an old-style record.
@return offset to the field */
ulint
rec_get_nth_field_offs_old(
/*=======================*/
	const rec_t*	rec,	/*!< in: record */
	ulint		n,	/*!< in: index of the field */
	ulint*		len)	/*!< out: length of the field; UNIV_SQL_NULL
				if SQL null */
	MY_ATTRIBUTE((nonnull));
#define rec_get_nth_field_old(rec, n, len) \
((rec) + rec_get_nth_field_offs_old(rec, n, len))
/************************************************************//**
Gets the physical size of an old-style field.
Also an SQL null may have a field of size > 0,
if the data type is of a fixed size.
@return field size in bytes */
UNIV_INLINE
ulint
rec_get_nth_field_size(
/*===================*/
	const rec_t*	rec,	/*!< in: record */
	ulint		n)	/*!< in: index of the field */
	MY_ATTRIBUTE((warn_unused_result));
/************************************************************//**
The following function is used to get an offset to the nth
data field in a record.
@return offset from the origin of rec */
UNIV_INLINE
ulint
rec_get_nth_field_offs(
/*===================*/
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	ulint		n,	/*!< in: index of the field */
	ulint*		len)	/*!< out: length of the field; UNIV_SQL_NULL
				if SQL null */
	MY_ATTRIBUTE((nonnull));
#define rec_get_nth_field(rec, offsets, n, len) \
((rec) + rec_get_nth_field_offs(offsets, n, len))

/******************************************************//**
Determine if the offsets are for a record containing null BLOB pointers.
@return first field containing a null BLOB pointer, or NULL if none found */
UNIV_INLINE
const byte*
rec_offs_any_null_extern(
/*=====================*/
	const rec_t*	rec,		/*!< in: record */
	const ulint*	offsets)	/*!< in: rec_get_offsets(rec) */
	MY_ATTRIBUTE((warn_unused_result));

/** Mark the nth field as externally stored.
@param[in]	offsets		array returned by rec_get_offsets()
@param[in]	n		nth field */
void
rec_offs_make_nth_extern(
        ulint*		offsets,
        const ulint     n);

/** Determine the number of allocated elements for an array of offsets.
@param[in]	offsets		offsets after rec_offs_set_n_alloc()
@return number of elements */
inline
ulint
rec_offs_get_n_alloc(const ulint* offsets)
{
	ulint	n_alloc;
	ut_ad(offsets);
	n_alloc = offsets[0];
	ut_ad(n_alloc > REC_OFFS_HEADER_SIZE);
	UNIV_MEM_ASSERT_W(offsets, n_alloc * sizeof *offsets);
	return(n_alloc);
}

/** Determine the number of fields for which offsets have been initialized.
@param[in]	offsets	rec_get_offsets()
@return number of fields */
inline
ulint
rec_offs_n_fields(const ulint* offsets)
{
	ulint	n_fields;
	ut_ad(offsets);
	n_fields = offsets[1];
	ut_ad(n_fields > 0);
	ut_ad(n_fields <= REC_MAX_N_FIELDS);
	ut_ad(n_fields + REC_OFFS_HEADER_SIZE
	      <= rec_offs_get_n_alloc(offsets));
	return(n_fields);
}

/** Get a flag of a record field.
@param[in]	offsets	rec_get_offsets()
@param[in]	n	nth field
@param[in]	flag	flag to extract
@return	the flag of the record field */
inline
ulint
rec_offs_nth_flag(const ulint* offsets, ulint n, ulint flag)
{
	ut_ad(rec_offs_validate(NULL, NULL, offsets));
	ut_ad(n < rec_offs_n_fields(offsets));
	/* The DEFAULT, NULL, EXTERNAL flags are mutually exclusive. */
	ut_ad(ut_is_2pow(rec_offs_base(offsets)[1 + n]
			 & (REC_OFFS_DEFAULT
			    | REC_OFFS_SQL_NULL
			    | REC_OFFS_EXTERNAL)));
	return rec_offs_base(offsets)[1 + n] & flag;
}

/** Determine if a record field is missing
(should be replaced by dict_index_t::instant_field_value()).
@param[in]	offsets	rec_get_offsets()
@param[in]	n	nth field
@return	nonzero if default bit is set */
inline
ulint
rec_offs_nth_default(const ulint* offsets, ulint n)
{
	return rec_offs_nth_flag(offsets, n, REC_OFFS_DEFAULT);
}

/** Determine if a record field is SQL NULL
(should be replaced by dict_index_t::instant_field_value()).
@param[in]	offsets	rec_get_offsets()
@param[in]	n	nth field
@return	nonzero if SQL NULL set */
inline
ulint
rec_offs_nth_sql_null(const ulint* offsets, ulint n)
{
	return rec_offs_nth_flag(offsets, n, REC_OFFS_SQL_NULL);
}

/** Determine if a record field is stored off-page.
@param[in]	offsets	rec_get_offsets()
@param[in]	n	nth field
Returns nonzero if the extern bit is set in nth field of rec.
@return nonzero if externally stored */
inline
ulint
rec_offs_nth_extern(const ulint* offsets, ulint n)
{
	return rec_offs_nth_flag(offsets, n, REC_OFFS_EXTERNAL);
}

/** Get a global flag of a record.
@param[in]	offsets	rec_get_offsets()
@param[in]	flag	flag to extract
@return	the flag of the record field */
inline
ulint
rec_offs_any_flag(const ulint* offsets, ulint flag)
{
	ut_ad(rec_offs_validate(NULL, NULL, offsets));
	return *rec_offs_base(offsets) & flag;
}

/** Determine if the offsets are for a record containing off-page columns.
@param[in]	offsets	rec_get_offsets()
@return nonzero if any off-page columns exist */
inline bool rec_offs_any_extern(const ulint* offsets)
{
	return rec_offs_any_flag(offsets, REC_OFFS_EXTERNAL);
}

/** Determine if the offsets are for a record that is missing fields.
@param[in]	offsets	rec_get_offsets()
@return nonzero if any fields need to be replaced with
		dict_index_t::instant_field_value() */
inline
ulint
rec_offs_any_default(const ulint* offsets)
{
	return rec_offs_any_flag(offsets, REC_OFFS_DEFAULT);
}

/** Determine if the offsets are for other than ROW_FORMAT=REDUNDANT.
@param[in]	offsets	rec_get_offsets()
@return	nonzero	if ROW_FORMAT is COMPACT,DYNAMIC or COMPRESSED
@retval	0	if ROW_FORMAT=REDUNDANT */
inline
ulint
rec_offs_comp(const ulint* offsets)
{
	ut_ad(rec_offs_validate(NULL, NULL, offsets));
	return(*rec_offs_base(offsets) & REC_OFFS_COMPACT);
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ADD COLUMN or ALTER TABLE.
@param[in]	rec	leaf page record
@param[in]	comp	0 if ROW_FORMAT=REDUNDANT, else nonzero
@return	whether the record is the metadata pseudo-record */
inline bool rec_is_metadata(const rec_t* rec, ulint comp)
{
	bool is = !!(rec_get_info_bits(rec, comp) & REC_INFO_MIN_REC_FLAG);
	ut_ad(!is || !comp || rec_get_status(rec) == REC_STATUS_INSTANT);
	return is;
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ADD COLUMN or ALTER TABLE.
@param[in]	rec	leaf page record
@param[in]	index	index of the record
@return	whether the record is the metadata pseudo-record */
inline bool rec_is_metadata(const rec_t* rec, const dict_index_t& index)
{
	bool is = rec_is_metadata(rec, dict_table_is_comp(index.table));
	ut_ad(!is || index.is_instant());
	return is;
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ADD COLUMN (not other ALTER TABLE).
@param[in]	rec	leaf page record
@param[in]	comp	0 if ROW_FORMAT=REDUNDANT, else nonzero
@return	whether the record is the metadata pseudo-record */
inline bool rec_is_add_metadata(const rec_t* rec, ulint comp)
{
	bool is = rec_get_info_bits(rec, comp) == REC_INFO_MIN_REC_FLAG;
	ut_ad(!is || !comp || rec_get_status(rec) == REC_STATUS_INSTANT);
	return is;
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ADD COLUMN (not other ALTER TABLE).
@param[in]	rec	leaf page record
@param[in]	index	index of the record
@return	whether the record is the metadata pseudo-record */
inline bool rec_is_add_metadata(const rec_t* rec, const dict_index_t& index)
{
	bool is = rec_is_add_metadata(rec, dict_table_is_comp(index.table));
	ut_ad(!is || index.is_instant());
	return is;
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ALTER TABLE (not plain ADD COLUMN).
@param[in]	rec	leaf page record
@param[in]	comp	0 if ROW_FORMAT=REDUNDANT, else nonzero
@return	whether the record is the ALTER TABLE metadata pseudo-record */
inline bool rec_is_alter_metadata(const rec_t* rec, ulint comp)
{
	bool is = !(~rec_get_info_bits(rec, comp)
		    & (REC_INFO_MIN_REC_FLAG | REC_INFO_DELETED_FLAG));
	ut_ad(!is || rec_is_metadata(rec, comp));
	return is;
}

/** Determine if the record is the metadata pseudo-record
in the clustered index for instant ALTER TABLE (not plain ADD COLUMN).
@param[in]	rec	leaf page record
@param[in]	index	index of the record
@return	whether the record is the ALTER TABLE metadata pseudo-record */
inline bool rec_is_alter_metadata(const rec_t* rec, const dict_index_t& index)
{
	bool is = rec_is_alter_metadata(rec, dict_table_is_comp(index.table));
	ut_ad(!is || index.is_dummy || index.is_instant());
	return is;
}

/** Determine if a record is delete-marked (not a metadata pseudo-record).
@param[in]	rec	record
@param[in]	comp	nonzero if ROW_FORMAT!=REDUNDANT
@return	whether the record is a delete-marked user record */
inline bool rec_is_delete_marked(const rec_t* rec, ulint comp)
{
	return (rec_get_info_bits(rec, comp)
		& (REC_INFO_MIN_REC_FLAG | REC_INFO_DELETED_FLAG))
		== REC_INFO_DELETED_FLAG;
}

/** Get the nth field from an index.
@param[in]	rec	index record
@param[in]	index	index
@param[in]	offsets	rec_get_offsets(rec, index)
@param[in]	n	field number
@param[out]	len	length of the field in bytes, or UNIV_SQL_NULL
@return a read-only copy of the index field */
inline
const byte*
rec_get_nth_cfield(
	const rec_t*		rec,
	const dict_index_t*	index,
	const ulint*		offsets,
	ulint			n,
	ulint*			len)
{
	ut_ad(rec_offs_validate(rec, index, offsets));

	if (!rec_offs_nth_default(offsets, n)) {
		return rec_get_nth_field(rec, offsets, n, len);
	}
	return index->instant_field_value(n, len);
}

/******************************************************//**
Gets the physical size of a field.
@return length of field */
UNIV_INLINE
ulint
rec_offs_nth_size(
/*==============*/
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	ulint		n)	/*!< in: nth field */
	MY_ATTRIBUTE((warn_unused_result));

/******************************************************//**
Returns the number of extern bits set in a record.
@return number of externally stored fields */
UNIV_INLINE
ulint
rec_offs_n_extern(
/*==============*/
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
/***********************************************************//**
This is used to modify the value of an already existing field in a record.
The previous value must have exactly the same size as the new value. If len
is UNIV_SQL_NULL then the field is treated as an SQL null.
For records in ROW_FORMAT=COMPACT (new-style records), len must not be
UNIV_SQL_NULL unless the field already is SQL null. */
UNIV_INLINE
void
rec_set_nth_field(
/*==============*/
	rec_t*		rec,	/*!< in: record */
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	ulint		n,	/*!< in: index number of the field */
	const void*	data,	/*!< in: pointer to the data if not SQL null */
	ulint		len)	/*!< in: length of the data or UNIV_SQL_NULL.
				If not SQL null, must have the same
				length as the previous value.
				If SQL null, previous value must be
				SQL null. */
	MY_ATTRIBUTE((nonnull(1,2)));
/**********************************************************//**
The following function returns the data size of an old-style physical
record, that is the sum of field lengths. SQL null fields
are counted as length 0 fields. The value returned by the function
is the distance from record origin to record end in bytes.
@return size */
UNIV_INLINE
ulint
rec_get_data_size_old(
/*==================*/
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((warn_unused_result));
/**********************************************************//**
The following function sets the number of allocated elements
for an array of offsets. */
UNIV_INLINE
void
rec_offs_set_n_alloc(
/*=================*/
	ulint*	offsets,	/*!< out: array for rec_get_offsets(),
				must be allocated */
	ulint	n_alloc)	/*!< in: number of elements */
	MY_ATTRIBUTE((nonnull));
#define rec_offs_init(offsets) \
	rec_offs_set_n_alloc(offsets, (sizeof offsets) / sizeof *offsets)
/**********************************************************//**
The following function returns the data size of a physical
record, that is the sum of field lengths. SQL null fields
are counted as length 0 fields. The value returned by the function
is the distance from record origin to record end in bytes.
@return size */
UNIV_INLINE
ulint
rec_offs_data_size(
/*===============*/
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
/**********************************************************//**
Returns the total size of record minus data size of record.
The value returned by the function is the distance from record
start to record origin in bytes.
@return size */
UNIV_INLINE
ulint
rec_offs_extra_size(
/*================*/
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
/**********************************************************//**
Returns the total size of a physical record.
@return size */
UNIV_INLINE
ulint
rec_offs_size(
/*==========*/
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
#ifdef UNIV_DEBUG
/**********************************************************//**
Returns a pointer to the start of the record.
@return pointer to start */
UNIV_INLINE
byte*
rec_get_start(
/*==========*/
	const rec_t*	rec,	/*!< in: pointer to record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
/**********************************************************//**
Returns a pointer to the end of the record.
@return pointer to end */
UNIV_INLINE
byte*
rec_get_end(
/*========*/
	const rec_t*	rec,	/*!< in: pointer to record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((warn_unused_result));
#else /* UNIV_DEBUG */
# define rec_get_start(rec, offsets) ((rec) - rec_offs_extra_size(offsets))
# define rec_get_end(rec, offsets) ((rec) + rec_offs_data_size(offsets))
#endif /* UNIV_DEBUG */

/** Copy a physical record to a buffer.
@param[in]	buf	buffer
@param[in]	rec	physical record
@param[in]	offsets	array returned by rec_get_offsets()
@return pointer to the origin of the copy */
UNIV_INLINE
rec_t*
rec_copy(
	void*		buf,
	const rec_t*	rec,
	const ulint*	offsets);

/** Determine the size of a data tuple prefix in a temporary file.
@param[in]	index		clustered or secondary index
@param[in]	fields		data fields
@param[in]	n_fields	number of data fields
@param[out]	extra		record header size
@param[in]	status		REC_STATUS_ORDINARY or REC_STATUS_INSTANT
@return	total size, in bytes */
ulint
rec_get_converted_size_temp(
	const dict_index_t*	index,
	const dfield_t*		fields,
	ulint			n_fields,
	ulint*			extra,
	rec_comp_status_t	status = REC_STATUS_ORDINARY)
	MY_ATTRIBUTE((warn_unused_result, nonnull));

/** Determine the offset to each field in temporary file.
@param[in]	rec	temporary file record
@param[in]	index	index of that the record belongs to
@param[in,out]	offsets	offsets to the fields; in: rec_offs_n_fields(offsets)
@param[in]	n_core	number of core fields (index->n_core_fields)
@param[in]	def_val	default values for non-core fields
@param[in]	status	REC_STATUS_ORDINARY or REC_STATUS_INSTANT */
void
rec_init_offsets_temp(
	const rec_t*		rec,
	const dict_index_t*	index,
	ulint*			offsets,
	ulint			n_core,
	const dict_col_t::def_t*def_val,
	rec_comp_status_t	status = REC_STATUS_ORDINARY)
	MY_ATTRIBUTE((nonnull));
/** Determine the offset to each field in temporary file.
@param[in]	rec	temporary file record
@param[in]	index	index of that the record belongs to
@param[in,out]	offsets	offsets to the fields; in: rec_offs_n_fields(offsets)
*/
void
rec_init_offsets_temp(
	const rec_t*		rec,
	const dict_index_t*	index,
	ulint*			offsets)
	MY_ATTRIBUTE((nonnull));

/** Convert a data tuple prefix to the temporary file format.
@param[out]	rec		record in temporary file format
@param[in]	index		clustered or secondary index
@param[in]	fields		data fields
@param[in]	n_fields	number of data fields
@param[in]	status		REC_STATUS_ORDINARY or REC_STATUS_INSTANT */
void
rec_convert_dtuple_to_temp(
	rec_t*			rec,
	const dict_index_t*	index,
	const dfield_t*		fields,
	ulint			n_fields,
	rec_comp_status_t	status = REC_STATUS_ORDINARY)
	MY_ATTRIBUTE((nonnull));

/**************************************************************//**
Copies the first n fields of a physical record to a new physical record in
a buffer.
@return own: copied record */
rec_t*
rec_copy_prefix_to_buf(
/*===================*/
	const rec_t*		rec,		/*!< in: physical record */
	const dict_index_t*	index,		/*!< in: record descriptor */
	ulint			n_fields,	/*!< in: number of fields
						to copy */
	byte**			buf,		/*!< in/out: memory buffer
						for the copied prefix,
						or NULL */
	ulint*			buf_size)	/*!< in/out: buffer size */
	MY_ATTRIBUTE((nonnull));
/*********************************************************//**
Builds a physical record out of a data tuple and
stores it into the given buffer.
@return pointer to the origin of physical record */
rec_t*
rec_convert_dtuple_to_rec(
/*======================*/
	byte*			buf,	/*!< in: start address of the
					physical record */
	const dict_index_t*	index,	/*!< in: record descriptor */
	const dtuple_t*		dtuple,	/*!< in: data tuple */
	ulint			n_ext)	/*!< in: number of
					externally stored columns */
	MY_ATTRIBUTE((warn_unused_result));
/**********************************************************//**
Returns the extra size of an old-style physical record if we know its
data size and number of fields.
@return extra size */
UNIV_INLINE
ulint
rec_get_converted_extra_size(
/*=========================*/
	ulint	data_size,	/*!< in: data size */
	ulint	n_fields,	/*!< in: number of fields */
	ulint	n_ext)		/*!< in: number of externally stored columns */
	MY_ATTRIBUTE((const));
/**********************************************************//**
Determines the size of a data tuple prefix in ROW_FORMAT=COMPACT.
@return total size */
ulint
rec_get_converted_size_comp_prefix(
/*===============================*/
	const dict_index_t*	index,	/*!< in: record descriptor */
	const dfield_t*		fields,	/*!< in: array of data fields */
	ulint			n_fields,/*!< in: number of data fields */
	ulint*			extra)	/*!< out: extra size */
	MY_ATTRIBUTE((warn_unused_result, nonnull(1,2)));

/** Determine the size of a record in ROW_FORMAT=COMPACT.
@param[in]	index		record descriptor. dict_table_is_comp()
				is assumed to hold, even if it doesn't
@param[in]	tuple		logical record
@param[out]	extra		extra size
@return total size */
ulint
rec_get_converted_size_comp(
	const dict_index_t*	index,
	const dtuple_t*		tuple,
	ulint*			extra)
	MY_ATTRIBUTE((nonnull(1,2)));

/**********************************************************//**
The following function returns the size of a data tuple when converted to
a physical record.
@return size */
UNIV_INLINE
ulint
rec_get_converted_size(
/*===================*/
	dict_index_t*	index,	/*!< in: record descriptor */
	const dtuple_t*	dtuple,	/*!< in: data tuple */
	ulint		n_ext)	/*!< in: number of externally stored columns */
	MY_ATTRIBUTE((warn_unused_result, nonnull));
/** Copy the first n fields of a (copy of a) physical record to a data tuple.
The fields are copied into the memory heap.
@param[out]	tuple		data tuple
@param[in]	rec		index record, or a copy thereof
@param[in]	is_leaf		whether rec is a leaf page record
@param[in]	n_fields	number of fields to copy
@param[in,out]	heap		memory heap */
void
rec_copy_prefix_to_dtuple(
	dtuple_t*		tuple,
	const rec_t*		rec,
	const dict_index_t*	index,
	bool			is_leaf,
	ulint			n_fields,
	mem_heap_t*		heap)
	MY_ATTRIBUTE((nonnull));
/***************************************************************//**
Validates the consistency of a physical record.
@return TRUE if ok */
ibool
rec_validate(
/*=========*/
	const rec_t*	rec,	/*!< in: physical record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((nonnull));
/***************************************************************//**
Prints an old-style physical record. */
void
rec_print_old(
/*==========*/
	FILE*		file,	/*!< in: file where to print */
	const rec_t*	rec)	/*!< in: physical record */
	MY_ATTRIBUTE((nonnull));
/***************************************************************//**
Prints a spatial index record. */
void
rec_print_mbr_rec(
/*==========*/
	FILE*		file,	/*!< in: file where to print */
	const rec_t*	rec,	/*!< in: physical record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((nonnull));
/***************************************************************//**
Prints a physical record. */
void
rec_print_new(
/*==========*/
	FILE*		file,	/*!< in: file where to print */
	const rec_t*	rec,	/*!< in: physical record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
	MY_ATTRIBUTE((nonnull));
/***************************************************************//**
Prints a physical record. */
void
rec_print(
/*======*/
	FILE*			file,	/*!< in: file where to print */
	const rec_t*		rec,	/*!< in: physical record */
	const dict_index_t*	index)	/*!< in: record descriptor */
	MY_ATTRIBUTE((nonnull));

/** Pretty-print a record.
@param[in,out]	o	output stream
@param[in]	rec	physical record
@param[in]	info	rec_get_info_bits(rec)
@param[in]	offsets	rec_get_offsets(rec) */
void
rec_print(
	std::ostream&	o,
	const rec_t*	rec,
	ulint		info,
	const ulint*	offsets);

/** Wrapper for pretty-printing a record */
struct rec_index_print
{
	/** Constructor */
	rec_index_print(const rec_t* rec, const dict_index_t* index) :
		m_rec(rec), m_index(index)
	{}

	/** Record */
	const rec_t*		m_rec;
	/** Index */
	const dict_index_t*	m_index;
};

/** Display a record.
@param[in,out]	o	output stream
@param[in]	r	record to display
@return	the output stream */
std::ostream&
operator<<(std::ostream& o, const rec_index_print& r);

/** Wrapper for pretty-printing a record */
struct rec_offsets_print
{
	/** Constructor */
	rec_offsets_print(const rec_t* rec, const ulint* offsets) :
		m_rec(rec), m_offsets(offsets)
	{}

	/** Record */
	const rec_t*		m_rec;
	/** Offsets to each field */
	const ulint*		m_offsets;
};

/** Display a record.
@param[in,out]	o	output stream
@param[in]	r	record to display
@return	the output stream */
std::ostream&
operator<<(std::ostream& o, const rec_offsets_print& r);

# ifndef DBUG_OFF
/** Pretty-printer of records and tuples */
class rec_printer : public std::ostringstream {
public:
	/** Construct a pretty-printed record.
	@param rec	record with header
	@param offsets	rec_get_offsets(rec, ...) */
	rec_printer(const rec_t* rec, const ulint* offsets)
		:
		std::ostringstream ()
	{
		rec_print(*this, rec,
			  rec_get_info_bits(rec, rec_offs_comp(offsets)),
			  offsets);
	}

	/** Construct a pretty-printed record.
	@param rec record, possibly lacking header
	@param info rec_get_info_bits(rec)
	@param offsets rec_get_offsets(rec, ...) */
	rec_printer(const rec_t* rec, ulint info, const ulint* offsets)
		:
		std::ostringstream ()
	{
		rec_print(*this, rec, info, offsets);
	}

	/** Construct a pretty-printed tuple.
	@param tuple	data tuple */
	rec_printer(const dtuple_t* tuple)
		:
		std::ostringstream ()
	{
		dtuple_print(*this, tuple);
	}

	/** Construct a pretty-printed tuple.
	@param field	array of data tuple fields
	@param n	number of fields */
	rec_printer(const dfield_t* field, ulint n)
		:
		std::ostringstream ()
	{
		dfield_print(*this, field, n);
	}

	/** Destructor */
	virtual ~rec_printer() {}

private:
	/** Copy constructor */
	rec_printer(const rec_printer& other);
	/** Assignment operator */
	rec_printer& operator=(const rec_printer& other);
};
# endif /* !DBUG_OFF */

# ifdef UNIV_DEBUG
/** Read the DB_TRX_ID of a clustered index record.
@param[in]	rec	clustered index record
@param[in]	index	clustered index
@return the value of DB_TRX_ID */
trx_id_t
rec_get_trx_id(
	const rec_t*		rec,
	const dict_index_t*	index)
	MY_ATTRIBUTE((nonnull, warn_unused_result));
# endif /* UNIV_DEBUG */

/* Maximum lengths for the data in a physical record if the offsets
are given in one byte (resp. two byte) format. */
#define REC_1BYTE_OFFS_LIMIT	0x7FUL
#define REC_2BYTE_OFFS_LIMIT	0x7FFFUL

/* The data size of record must not be larger than this on
REDUNDANT row format because we reserve two upmost bits in a
two byte offset for special purposes */
#define REDUNDANT_REC_MAX_DATA_SIZE    (16383)

/* The data size of record must be smaller than this on
COMPRESSED row format because we reserve two upmost bits in a
two byte offset for special purposes */
#define COMPRESSED_REC_MAX_DATA_SIZE   (16384)

#ifdef WITH_WSREP
int wsrep_rec_get_foreign_key(
	byte 		*buf,     /* out: extracted key */
	ulint 		*buf_len, /* in/out: length of buf */
	const rec_t*	rec,	  /* in: physical record */
	dict_index_t*	index_for,  /* in: index for foreign table */
	dict_index_t*	index_ref,  /* in: index for referenced table */
	ibool		new_protocol); /* in: protocol > 1 */
#endif /* WITH_WSREP */

#include "rem0rec.ic"

#endif /* !UNIV_INNOCHECKSUM */
#endif /* rem0rec_h */
