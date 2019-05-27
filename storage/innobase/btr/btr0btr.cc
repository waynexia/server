/*****************************************************************************

Copyright (c) 1994, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2012, Facebook Inc.
Copyright (c) 2014, 2019, MariaDB Corporation.

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

/**************************************************//**
@file btr/btr0btr.cc
The B-tree

Created 6/2/1994 Heikki Tuuri
*******************************************************/

#include "btr0btr.h"

#include "page0page.h"
#include "page0zip.h"
#include "gis0rtree.h"

#include "btr0cur.h"
#include "btr0sea.h"
#include "btr0pcur.h"
#include "btr0defragment.h"
#include "rem0cmp.h"
#include "lock0lock.h"
#include "ibuf0ibuf.h"
#include "trx0trx.h"
#include "srv0mon.h"
#include "gis0geo.h"
#include "dict0boot.h"
#include "row0sel.h" /* row_search_max_autoinc() */

Atomic_counter<uint32_t> btr_validate_index_running;

/**************************************************************//**
Checks if the page in the cursor can be merged with given page.
If necessary, re-organize the merge_page.
@return	true if possible to merge. */
static
bool
btr_can_merge_with_page(
/*====================*/
	btr_cur_t*	cursor,		/*!< in: cursor on the page to merge */
	ulint		page_no,	/*!< in: a sibling page */
	buf_block_t**	merge_block,	/*!< out: the merge block */
	mtr_t*		mtr);		/*!< in: mini-transaction */

/** Report that an index page is corrupted.
@param[in]	buffer block
@param[in]	index tree */
void btr_corruption_report(const buf_block_t* block, const dict_index_t* index)
{
	ib::fatal()
		<< "Flag mismatch in page " << block->page.id
		<< " index " << index->name
		<< " of table " << index->table->name;
}

/*
Latching strategy of the InnoDB B-tree
--------------------------------------

Node pointer page latches acquisition is protected by index->lock latch.

Before MariaDB 10.2.2, all node pointer pages were protected by index->lock
either in S (shared) or X (exclusive) mode and block->lock was not acquired on
node pointer pages.

After MariaDB 10.2.2, block->lock S-latch or X-latch is used to protect
node pointer pages and obtaiment of node pointer page latches is protected by
index->lock.

(0) Definition: B-tree level.

(0.1) The leaf pages of the B-tree are at level 0.

(0.2) The parent of a page at level L has level L+1. (The level of the
root page is equal to the tree height.)

(0.3) The B-tree lock (index->lock) is the parent of the root page and
has a level = tree height + 1.

Index->lock has 3 possible locking modes:

(1) S-latch:

(1.1) All latches for pages must be obtained in descending order of tree level.

(1.2) Before obtaining the first node pointer page latch at a given B-tree
level, parent latch must be held (at level +1 ).

(1.3) If a node pointer page is already latched at the same level
we can only obtain latch to its right sibling page latch at the same level.

(1.4) Release of the node pointer page latches must be done in
child-to-parent order. (Prevents deadlocks when obtained index->lock
in SX mode).

(1.4.1) Level L node pointer page latch can be released only when
no latches at children level i.e. level < L are hold.

(1.4.2) All latches from node pointer pages must be released so
that no latches are obtained between.

(1.5) [implied by (1.1), (1.2)] Root page latch must be first node pointer
latch obtained.

(2) SX-latch:

In this case rules (1.2) and (1.3) from S-latch case are relaxed and
merged into (2.2) and rule (1.4) is removed. Thus, latch acquisition
can be skipped at some tree levels and latches can be obtained in
a less restricted order.

(2.1) [identical to (1.1)]: All latches for pages must be obtained in descending
order of tree level.

(2.2) When a node pointer latch at level L is obtained,
the left sibling page latch in the same level or some ancestor
page latch (at level > L) must be hold.

(2.3) [implied by (2.1), (2.2)] The first node pointer page latch obtained can
be any node pointer page.

(3) X-latch:

Node pointer latches can be obtained in any order.

NOTE: New rules after MariaDB 10.2.2 does not affect the latching rules of leaf pages:

index->lock S-latch is needed in read for the node pointer traversal. When the leaf
level is reached, index-lock can be released (and with the MariaDB 10.2.2 changes, all
node pointer latches). Left to right index travelsal in leaf page level can be safely done
by obtaining right sibling leaf page latch and then releasing the old page latch.

Single leaf page modifications (BTR_MODIFY_LEAF) are protected by index->lock
S-latch.

B-tree operations involving page splits or merges (BTR_MODIFY_TREE) and page
allocations are protected by index->lock X-latch.

Node pointers
-------------
Leaf pages of a B-tree contain the index records stored in the
tree. On levels n > 0 we store 'node pointers' to pages on level
n - 1. For each page there is exactly one node pointer stored:
thus the our tree is an ordinary B-tree, not a B-link tree.

A node pointer contains a prefix P of an index record. The prefix
is long enough so that it determines an index record uniquely.
The file page number of the child page is added as the last
field. To the child page we can store node pointers or index records
which are >= P in the alphabetical order, but < P1 if there is
a next node pointer on the level, and P1 is its prefix.

If a node pointer with a prefix P points to a non-leaf child,
then the leftmost record in the child must have the same
prefix P. If it points to a leaf node, the child is not required
to contain any record with a prefix equal to P. The leaf case
is decided this way to allow arbitrary deletions in a leaf node
without touching upper levels of the tree.

We have predefined a special minimum record which we
define as the smallest record in any alphabetical order.
A minimum record is denoted by setting a bit in the record
header. A minimum record acts as the prefix of a node pointer
which points to a leftmost node on any level of the tree.

File page allocation
--------------------
In the root node of a B-tree there are two file segment headers.
The leaf pages of a tree are allocated from one file segment, to
make them consecutive on disk if possible. From the other file segment
we allocate pages for the non-leaf levels of the tree.
*/

#ifdef UNIV_BTR_DEBUG
/**************************************************************//**
Checks a file segment header within a B-tree root page.
@return TRUE if valid */
static
ibool
btr_root_fseg_validate(
/*===================*/
	const fseg_header_t*	seg_header,	/*!< in: segment header */
	ulint			space)		/*!< in: tablespace identifier */
{
	ulint	offset = mach_read_from_2(seg_header + FSEG_HDR_OFFSET);

	ut_a(mach_read_from_4(seg_header + FSEG_HDR_SPACE) == space);
	ut_a(offset >= FIL_PAGE_DATA);
	ut_a(offset <= srv_page_size - FIL_PAGE_DATA_END);
	return(TRUE);
}
#endif /* UNIV_BTR_DEBUG */

/**************************************************************//**
Gets the root node of a tree and x- or s-latches it.
@return root page, x- or s-latched */
buf_block_t*
btr_root_block_get(
/*===============*/
	const dict_index_t*	index,	/*!< in: index tree */
	ulint			mode,	/*!< in: either RW_S_LATCH
					or RW_X_LATCH */
	mtr_t*			mtr)	/*!< in: mtr */
{
	if (!index->table || !index->table->space) {
		return NULL;
	}

	buf_block_t*	block = btr_block_get(
		page_id_t(index->table->space_id, index->page),
		index->table->space->zip_size(), mode,
		index, mtr);

	if (!block) {
		index->table->file_unreadable = true;

		ib_push_warning(
			static_cast<THD*>(NULL), DB_DECRYPTION_FAILED,
			"Table %s in file %s is encrypted but encryption service or"
			" used key_id is not available. "
			" Can't continue reading table.",
			index->table->name.m_name,
			UT_LIST_GET_FIRST(index->table->space->chain)->name);

		return NULL;
	}

	btr_assert_not_corrupted(block, index);

#ifdef UNIV_BTR_DEBUG
	if (!dict_index_is_ibuf(index)) {
		const page_t*	root = buf_block_get_frame(block);

		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_LEAF
					    + root, index->table->space_id));
		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
					    + root, index->table->space_id));
	}
#endif /* UNIV_BTR_DEBUG */

	return(block);
}

/**************************************************************//**
Gets the root node of a tree and sx-latches it for segment access.
@return root page, sx-latched */
page_t*
btr_root_get(
/*=========*/
	const dict_index_t*	index,	/*!< in: index tree */
	mtr_t*			mtr)	/*!< in: mtr */
{
	/* Intended to be used for segment list access.
	SX lock doesn't block reading user data by other threads.
	And block the segment list access by others.*/
	buf_block_t* root = btr_root_block_get(index, RW_SX_LATCH,
					       mtr);
	return(root ? buf_block_get_frame(root) : NULL);
}

/**************************************************************//**
Gets the height of the B-tree (the level of the root, when the leaf
level is assumed to be 0). The caller must hold an S or X latch on
the index.
@return tree height (level of the root) */
ulint
btr_height_get(
/*===========*/
	dict_index_t*	index,	/*!< in: index tree */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	ulint		height=0;
	buf_block_t*	root_block;

	ut_ad(srv_read_only_mode
	      || mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
					   MTR_MEMO_S_LOCK
					   | MTR_MEMO_X_LOCK
					   | MTR_MEMO_SX_LOCK));

	/* S latches the page */
	root_block = btr_root_block_get(index, RW_S_LATCH, mtr);

	if (root_block) {
		height = btr_page_get_level(buf_block_get_frame(root_block));

		/* Release the S latch on the root page. */
		mtr->memo_release(root_block, MTR_MEMO_PAGE_S_FIX);

		ut_d(sync_check_unlock(&root_block->lock));
	}

	return(height);
}

/**************************************************************//**
Checks a file segment header within a B-tree root page and updates
the segment header space id.
@return TRUE if valid */
static
bool
btr_root_fseg_adjust_on_import(
/*===========================*/
	fseg_header_t*	seg_header,	/*!< in/out: segment header */
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page,
					or NULL */
	ulint		space,		/*!< in: tablespace identifier */
	mtr_t*		mtr)		/*!< in/out: mini-transaction */
{
	ulint	offset = mach_read_from_2(seg_header + FSEG_HDR_OFFSET);

	if (offset < FIL_PAGE_DATA
	    || offset > srv_page_size - FIL_PAGE_DATA_END) {

		return(FALSE);

	} else if (page_zip) {
		mach_write_to_4(seg_header + FSEG_HDR_SPACE, space);
		page_zip_write_header(page_zip, seg_header + FSEG_HDR_SPACE,
				      4, mtr);
	} else {
		mlog_write_ulint(seg_header + FSEG_HDR_SPACE,
				 space, MLOG_4BYTES, mtr);
	}

	return(TRUE);
}

/**************************************************************//**
Checks and adjusts the root node of a tree during IMPORT TABLESPACE.
@return error code, or DB_SUCCESS */
dberr_t
btr_root_adjust_on_import(
/*======================*/
	const dict_index_t*	index)	/*!< in: index tree */
{
	dberr_t			err;
	mtr_t			mtr;
	page_t*			page;
	buf_block_t*		block;
	page_zip_des_t*		page_zip;
	dict_table_t*		table = index->table;
	const page_id_t		page_id(table->space_id, index->page);
	const ulint		zip_size = table->space->zip_size();

	DBUG_EXECUTE_IF("ib_import_trigger_corruption_3",
			return(DB_CORRUPTION););

	mtr_start(&mtr);

	mtr_set_log_mode(&mtr, MTR_LOG_NO_REDO);

	block = btr_block_get(page_id, zip_size, RW_X_LATCH, index, &mtr);

	page = buf_block_get_frame(block);
	page_zip = buf_block_get_page_zip(block);

	if (!fil_page_index_page_check(page) || page_has_siblings(page)) {
		err = DB_CORRUPTION;

	} else if (dict_index_is_clust(index)) {
		bool	page_is_compact_format;

		page_is_compact_format = page_is_comp(page) > 0;

		/* Check if the page format and table format agree. */
		if (page_is_compact_format != dict_table_is_comp(table)) {
			err = DB_CORRUPTION;
		} else {
			/* Check that the table flags and the tablespace
			flags match. */
			ulint tf = dict_tf_to_fsp_flags(table->flags);
			ulint sf = table->space->flags;
			sf &= ~FSP_FLAGS_MEM_MASK;
			tf &= ~FSP_FLAGS_MEM_MASK;
			if (fil_space_t::is_flags_equal(tf, sf)
			    || fil_space_t::is_flags_equal(sf, tf)) {
				mutex_enter(&fil_system.mutex);
				table->space->flags = (table->space->flags
						       & ~FSP_FLAGS_MEM_MASK)
					| (tf & FSP_FLAGS_MEM_MASK);
				mutex_exit(&fil_system.mutex);
				err = DB_SUCCESS;
			} else {
				err = DB_CORRUPTION;
			}
		}
	} else {
		err = DB_SUCCESS;
	}

	/* Check and adjust the file segment headers, if all OK so far. */
	if (err == DB_SUCCESS
	    && (!btr_root_fseg_adjust_on_import(
			FIL_PAGE_DATA + PAGE_BTR_SEG_LEAF
			+ page, page_zip, table->space_id, &mtr)
		|| !btr_root_fseg_adjust_on_import(
			FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
			+ page, page_zip, table->space_id, &mtr))) {

		err = DB_CORRUPTION;
	}

	mtr_commit(&mtr);

	return(err);
}

/**************************************************************//**
Creates a new index page (not the root, and also not
used in page reorganization).  @see btr_page_empty(). */
void
btr_page_create(
/*============*/
	buf_block_t*	block,	/*!< in/out: page to be created */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page, or NULL */
	dict_index_t*	index,	/*!< in: index */
	ulint		level,	/*!< in: the B-tree level of the page */
	mtr_t*		mtr)	/*!< in: mtr */
{
	page_t*		page = buf_block_get_frame(block);

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	if (page_zip) {
		page_create_zip(block, index, level, 0, mtr);
	} else {
		page_create(block, mtr, dict_table_is_comp(index->table),
			    dict_index_is_spatial(index));
		/* Set the level of the new index page */
		btr_page_set_level(page, NULL, level, mtr);
	}

	/* For Spatial Index, initialize the Split Sequence Number */
	if (dict_index_is_spatial(index)) {
		page_set_ssn_id(block, page_zip, 0, mtr);
	}

	btr_page_set_index_id(page, page_zip, index->id, mtr);
}

/**************************************************************//**
Allocates a new file page to be used in an ibuf tree. Takes the page from
the free list of the tree, which must contain pages!
@return new allocated block, x-latched */
static
buf_block_t*
btr_page_alloc_for_ibuf(
/*====================*/
	dict_index_t*	index,	/*!< in: index tree */
	mtr_t*		mtr)	/*!< in: mtr */
{
	fil_addr_t	node_addr;
	page_t*		root;
	page_t*		new_page;
	buf_block_t*	new_block;

	root = btr_root_get(index, mtr);

	node_addr = flst_get_first(root + PAGE_HEADER
				   + PAGE_BTR_IBUF_FREE_LIST, mtr);
	ut_a(node_addr.page != FIL_NULL);

	new_block = buf_page_get(
		page_id_t(index->table->space_id, node_addr.page),
		index->table->space->zip_size(),
		RW_X_LATCH, mtr);

	new_page = buf_block_get_frame(new_block);
	buf_block_dbg_add_level(new_block, SYNC_IBUF_TREE_NODE_NEW);

	flst_remove(root + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST,
		    new_page + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST_NODE,
		    mtr);
	ut_ad(flst_validate(root + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST,
			    mtr));

	return(new_block);
}

/**************************************************************//**
Allocates a new file page to be used in an index tree. NOTE: we assume
that the caller has made the reservation for free extents!
@retval NULL if no page could be allocated
@retval block, rw_lock_x_lock_count(&block->lock) == 1 if allocation succeeded
(init_mtr == mtr, or the page was not previously freed in mtr)
@retval block (not allocated or initialized) otherwise */
static MY_ATTRIBUTE((nonnull, warn_unused_result))
buf_block_t*
btr_page_alloc_low(
/*===============*/
	dict_index_t*	index,		/*!< in: index */
	ulint		hint_page_no,	/*!< in: hint of a good page */
	byte		file_direction,	/*!< in: direction where a possible
					page split is made */
	ulint		level,		/*!< in: level where the page is placed
					in the tree */
	mtr_t*		mtr,		/*!< in/out: mini-transaction
					for the allocation */
	mtr_t*		init_mtr)	/*!< in/out: mtr or another
					mini-transaction in which the
					page should be initialized.
					If init_mtr!=mtr, but the page
					is already X-latched in mtr, do
					not initialize the page. */
{
	fseg_header_t*	seg_header;
	page_t*		root;

	root = btr_root_get(index, mtr);

	if (level == 0) {
		seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_LEAF;
	} else {
		seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_TOP;
	}

	/* Parameter TRUE below states that the caller has made the
	reservation for free extents, and thus we know that a page can
	be allocated: */

	buf_block_t* block = fseg_alloc_free_page_general(
		seg_header, hint_page_no, file_direction,
		TRUE, mtr, init_mtr);

#ifdef UNIV_DEBUG_SCRUBBING
	if (block != NULL) {
		fprintf(stderr,
			"alloc %lu:%lu to index: %lu root: %lu\n",
			buf_block_get_page_no(block),
			buf_block_get_space(block),
			index->id,
			dict_index_get_page(index));
	} else {
		fprintf(stderr,
			"failed alloc index: %lu root: %lu\n",
			index->id,
			dict_index_get_page(index));
	}
#endif /* UNIV_DEBUG_SCRUBBING */

	return block;
}

/**************************************************************//**
Allocates a new file page to be used in an index tree. NOTE: we assume
that the caller has made the reservation for free extents!
@retval NULL if no page could be allocated
@retval block, rw_lock_x_lock_count(&block->lock) == 1 if allocation succeeded
(init_mtr == mtr, or the page was not previously freed in mtr)
@retval block (not allocated or initialized) otherwise */
buf_block_t*
btr_page_alloc(
/*===========*/
	dict_index_t*	index,		/*!< in: index */
	ulint		hint_page_no,	/*!< in: hint of a good page */
	byte		file_direction,	/*!< in: direction where a possible
					page split is made */
	ulint		level,		/*!< in: level where the page is placed
					in the tree */
	mtr_t*		mtr,		/*!< in/out: mini-transaction
					for the allocation */
	mtr_t*		init_mtr)	/*!< in/out: mini-transaction
					for x-latching and initializing
					the page */
{
	buf_block_t*	new_block;

	if (dict_index_is_ibuf(index)) {

		return(btr_page_alloc_for_ibuf(index, mtr));
	}

	new_block = btr_page_alloc_low(
		index, hint_page_no, file_direction, level, mtr, init_mtr);

	if (new_block) {
		buf_block_dbg_add_level(new_block, SYNC_TREE_NODE_NEW);
	}

	return(new_block);
}

/**************************************************************//**
Gets the number of pages in a B-tree.
@return number of pages, or ULINT_UNDEFINED if the index is unavailable */
ulint
btr_get_size(
/*=========*/
	dict_index_t*	index,	/*!< in: index */
	ulint		flag,	/*!< in: BTR_N_LEAF_PAGES or BTR_TOTAL_SIZE */
	mtr_t*		mtr)	/*!< in/out: mini-transaction where index
				is s-latched */
{
	fseg_header_t*	seg_header;
	page_t*		root;
	ulint		n=0;
	ulint		dummy;

	ut_ad(srv_read_only_mode
	      || mtr_memo_contains(mtr, dict_index_get_lock(index),
				   MTR_MEMO_S_LOCK));

	if (index->page == FIL_NULL
	    || dict_index_is_online_ddl(index)
	    || !index->is_committed()) {
		return(ULINT_UNDEFINED);
	}

	root = btr_root_get(index, mtr);

	if (root) {
		if (flag == BTR_N_LEAF_PAGES) {
			seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_LEAF;

			fseg_n_reserved_pages(seg_header, &n, mtr);

		} else if (flag == BTR_TOTAL_SIZE) {
			seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_TOP;

			n = fseg_n_reserved_pages(seg_header, &dummy, mtr);

			seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_LEAF;

			n += fseg_n_reserved_pages(seg_header, &dummy, mtr);
		} else {
			ut_error;
		}
	} else {
		n = ULINT_UNDEFINED;
	}

	return(n);
}

/**************************************************************//**
Gets the number of reserved and used pages in a B-tree.
@return	number of pages reserved, or ULINT_UNDEFINED if the index
is unavailable */
UNIV_INTERN
ulint
btr_get_size_and_reserved(
/*======================*/
	dict_index_t*	index,	/*!< in: index */
	ulint		flag,	/*!< in: BTR_N_LEAF_PAGES or BTR_TOTAL_SIZE */
	ulint*		used,	/*!< out: number of pages used (<= reserved) */
	mtr_t*		mtr)	/*!< in/out: mini-transaction where index
				is s-latched */
{
	fseg_header_t*	seg_header;
	page_t*		root;
	ulint		n=ULINT_UNDEFINED;
	ulint		dummy;

	ut_ad(mtr_memo_contains(mtr, dict_index_get_lock(index),
				MTR_MEMO_S_LOCK));

	ut_a(flag == BTR_N_LEAF_PAGES || flag == BTR_TOTAL_SIZE);

	if (index->page == FIL_NULL
	    || dict_index_is_online_ddl(index)
	    || !index->is_committed()) {
		return(ULINT_UNDEFINED);
	}

	root = btr_root_get(index, mtr);
	*used = 0;

	if (root) {

		seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_LEAF;

		n = fseg_n_reserved_pages(seg_header, used, mtr);

		if (flag == BTR_TOTAL_SIZE) {
			seg_header = root + PAGE_HEADER + PAGE_BTR_SEG_TOP;

			n += fseg_n_reserved_pages(seg_header, &dummy, mtr);
			*used += dummy;

		}
	}

	return(n);
}

/**************************************************************//**
Frees a page used in an ibuf tree. Puts the page to the free list of the
ibuf tree. */
static
void
btr_page_free_for_ibuf(
/*===================*/
	dict_index_t*	index,	/*!< in: index tree */
	buf_block_t*	block,	/*!< in: block to be freed, x-latched */
	mtr_t*		mtr)	/*!< in: mtr */
{
	page_t*		root;

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	root = btr_root_get(index, mtr);

	flst_add_first(root + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST,
		       buf_block_get_frame(block)
		       + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST_NODE, mtr);

	ut_ad(flst_validate(root + PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST,
			    mtr));
}

/** Free an index page.
@param[in,out]	index	index tree
@param[in,out]	block	block to be freed
@param[in,out]	mtr	mini-transaction
@param[in]	blob	whether this is freeing a BLOB page */
void btr_page_free(dict_index_t* index, buf_block_t* block, mtr_t* mtr,
		   bool blob)
{
	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
#ifdef BTR_CUR_HASH_ADAPT
	ut_ad(!block->index || !blob);
	ut_ad(!block->index || page_is_leaf(block->frame));
#endif
	ut_ad(index->table->space_id == block->page.id.space());
	/* The root page is freed by btr_free_root(). */
	ut_ad(block->page.id.page_no() != index->page);
	ut_ad(mtr->is_named_space(index->table->space));

	/* The page gets invalid for optimistic searches: increment the frame
	modify clock */

	buf_block_modify_clock_inc(block);

	if (dict_index_is_ibuf(index)) {
		btr_page_free_for_ibuf(index, block, mtr);
		return;
	}

	/* TODO: Discard any operations for block from mtr->log.
	The page will be freed, so previous changes to it by this
	mini-transaction should not matter. */
	page_t* root = btr_root_get(index, mtr);
	fseg_header_t* seg_header = &root[blob || page_is_leaf(block->frame)
					  ? PAGE_HEADER + PAGE_BTR_SEG_LEAF
					  : PAGE_HEADER + PAGE_BTR_SEG_TOP];
	fseg_free_page(seg_header,
		       index->table->space, block->page.id.page_no(),
		       block->index != NULL, !block->page.flush_observer, mtr);

	/* The page was marked free in the allocation bitmap, but it
	should remain exclusively latched until mtr_t::commit() or until it
	is explicitly freed from the mini-transaction. */
	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	/* MDEV-15528 FIXME: Zero out the page after the redo log for
	this mini-transaction has been durably written.
	This must be done unconditionally if
	srv_immediate_scrub_data_uncompressed is set. */
}

/**************************************************************//**
Sets the child node file address in a node pointer. */
UNIV_INLINE
void
btr_node_ptr_set_child_page_no(
/*===========================*/
	rec_t*		rec,	/*!< in: node pointer record */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose uncompressed
				part will be updated, or NULL */
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	ulint		page_no,/*!< in: child node address */
	mtr_t*		mtr)	/*!< in: mtr */
{
	byte*	field;
	ulint	len;

	ut_ad(rec_offs_validate(rec, NULL, offsets));
	ut_ad(!page_rec_is_leaf(rec));
	ut_ad(!rec_offs_comp(offsets) || rec_get_node_ptr_flag(rec));

	/* The child address is in the last field */
	field = rec_get_nth_field(rec, offsets,
				  rec_offs_n_fields(offsets) - 1, &len);

	ut_ad(len == REC_NODE_PTR_SIZE);

	if (page_zip) {
		page_zip_write_node_ptr(page_zip, rec,
					rec_offs_data_size(offsets),
					page_no, mtr);
	} else {
		mlog_write_ulint(field, page_no, MLOG_4BYTES, mtr);
	}
}

/************************************************************//**
Returns the child page of a node pointer and sx-latches it.
@return child page, sx-latched */
static
buf_block_t*
btr_node_ptr_get_child(
/*===================*/
	const rec_t*	node_ptr,/*!< in: node pointer */
	dict_index_t*	index,	/*!< in: index */
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	mtr_t*		mtr)	/*!< in: mtr */
{
	ut_ad(rec_offs_validate(node_ptr, index, offsets));
	ut_ad(index->table->space_id
	      == page_get_space_id(page_align(node_ptr)));

	return btr_block_get(
		page_id_t(index->table->space_id,
			  btr_node_ptr_get_child_page_no(node_ptr, offsets)),
		index->table->space->zip_size(),
		RW_SX_LATCH, index, mtr);
}

/************************************************************//**
Returns the upper level node pointer to a page. It is assumed that mtr holds
an sx-latch on the tree.
@return rec_get_offsets() of the node pointer record */
static
ulint*
btr_page_get_father_node_ptr_func(
/*==============================*/
	ulint*		offsets,/*!< in: work area for the return value */
	mem_heap_t*	heap,	/*!< in: memory heap to use */
	btr_cur_t*	cursor,	/*!< in: cursor pointing to user record,
				out: cursor on node pointer record,
				its page x-latched */
	ulint		latch_mode,/*!< in: BTR_CONT_MODIFY_TREE
				or BTR_CONT_SEARCH_TREE */
	const char*	file,	/*!< in: file name */
	unsigned	line,	/*!< in: line where called */
	mtr_t*		mtr)	/*!< in: mtr */
{
	dtuple_t*	tuple;
	rec_t*		user_rec;
	rec_t*		node_ptr;
	ulint		level;
	ulint		page_no;
	dict_index_t*	index;

	ut_ad(latch_mode == BTR_CONT_MODIFY_TREE
	      || latch_mode == BTR_CONT_SEARCH_TREE);

	page_no = btr_cur_get_block(cursor)->page.id.page_no();
	index = btr_cur_get_index(cursor);
	ut_ad(!dict_index_is_spatial(index));

	ut_ad(srv_read_only_mode
	      || mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
					   MTR_MEMO_X_LOCK
					   | MTR_MEMO_SX_LOCK));

	ut_ad(dict_index_get_page(index) != page_no);

	level = btr_page_get_level(btr_cur_get_page(cursor));

	user_rec = btr_cur_get_rec(cursor);
	ut_a(page_rec_is_user_rec(user_rec));

	tuple = dict_index_build_node_ptr(index, user_rec, 0, heap, level);
	dberr_t err = DB_SUCCESS;

	err = btr_cur_search_to_nth_level(
		index, level + 1, tuple,
		PAGE_CUR_LE, latch_mode, cursor, 0,
		file, line, mtr);

	if (err != DB_SUCCESS) {
		ib::warn() << " Error code: " << err
			<< " btr_page_get_father_node_ptr_func "
			<< " level: " << level + 1
			<< " called from file: "
			<< file << " line: " << line
			<< " table: " << index->table->name
			<< " index: " << index->name();
	}

	node_ptr = btr_cur_get_rec(cursor);

	offsets = rec_get_offsets(node_ptr, index, offsets, false,
				  ULINT_UNDEFINED, &heap);

	if (btr_node_ptr_get_child_page_no(node_ptr, offsets) != page_no) {
		rec_t*	print_rec;

		ib::error()
			<< "Corruption of an index tree: table "
			<< index->table->name
			<< " index " << index->name
			<< ", father ptr page no "
			<< btr_node_ptr_get_child_page_no(node_ptr, offsets)
			<< ", child page no " << page_no;

		print_rec = page_rec_get_next(
			page_get_infimum_rec(page_align(user_rec)));
		offsets = rec_get_offsets(print_rec, index, offsets,
					  page_rec_is_leaf(user_rec),
					  ULINT_UNDEFINED, &heap);
		page_rec_print(print_rec, offsets);
		offsets = rec_get_offsets(node_ptr, index, offsets, false,
					  ULINT_UNDEFINED, &heap);
		page_rec_print(node_ptr, offsets);

		ib::fatal()
			<< "You should dump + drop + reimport the table to"
			<< " fix the corruption. If the crash happens at"
			<< " database startup. " << FORCE_RECOVERY_MSG
			<< " Then dump + drop + reimport.";
	}

	return(offsets);
}

#define btr_page_get_father_node_ptr(of,heap,cur,mtr)			\
	btr_page_get_father_node_ptr_func(				\
		of,heap,cur,BTR_CONT_MODIFY_TREE,__FILE__,__LINE__,mtr)

#define btr_page_get_father_node_ptr_for_validate(of,heap,cur,mtr)	\
	btr_page_get_father_node_ptr_func(				\
		of,heap,cur,BTR_CONT_SEARCH_TREE,__FILE__,__LINE__,mtr)

/************************************************************//**
Returns the upper level node pointer to a page. It is assumed that mtr holds
an x-latch on the tree.
@return rec_get_offsets() of the node pointer record */
static
ulint*
btr_page_get_father_block(
/*======================*/
	ulint*		offsets,/*!< in: work area for the return value */
	mem_heap_t*	heap,	/*!< in: memory heap to use */
	dict_index_t*	index,	/*!< in: b-tree index */
	buf_block_t*	block,	/*!< in: child page in the index */
	mtr_t*		mtr,	/*!< in: mtr */
	btr_cur_t*	cursor)	/*!< out: cursor on node pointer record,
				its page x-latched */
{
	rec_t*	rec
		= page_rec_get_next(page_get_infimum_rec(buf_block_get_frame(
								 block)));
	btr_cur_position(index, rec, block, cursor);
	return(btr_page_get_father_node_ptr(offsets, heap, cursor, mtr));
}

/** Seek to the parent page of a B-tree page.
@param[in,out]	index	b-tree
@param[in]	block	child page
@param[in,out]	mtr	mini-transaction
@param[out]	cursor	cursor pointing to the x-latched parent page */
void btr_page_get_father(dict_index_t* index, buf_block_t* block, mtr_t* mtr,
			 btr_cur_t* cursor)
{
	mem_heap_t*	heap;
	rec_t*		rec
		= page_rec_get_next(page_get_infimum_rec(buf_block_get_frame(
								 block)));
	btr_cur_position(index, rec, block, cursor);

	heap = mem_heap_create(100);
	btr_page_get_father_node_ptr(NULL, heap, cursor, mtr);
	mem_heap_free(heap);
}

/** PAGE_INDEX_ID value for freed index B-trees */
static const index_id_t	BTR_FREED_INDEX_ID = 0;

/** Free a B-tree root page. btr_free_but_not_root() must already
have been called.
In a persistent tablespace, the caller must invoke fsp_init_file_page()
before mtr.commit().
@param[in,out]	block		index root page
@param[in,out]	mtr		mini-transaction
@param[in]	invalidate	whether to invalidate PAGE_INDEX_ID */
static void btr_free_root(buf_block_t* block, mtr_t* mtr, bool invalidate)
{
	fseg_header_t*	header;

	ut_ad(mtr_memo_contains_flagged(mtr, block, MTR_MEMO_PAGE_X_FIX
					| MTR_MEMO_PAGE_SX_FIX));
	ut_ad(mtr->is_named_space(block->page.id.space()));

	btr_search_drop_page_hash_index(block);

	header = buf_block_get_frame(block) + PAGE_HEADER + PAGE_BTR_SEG_TOP;
#ifdef UNIV_BTR_DEBUG
	ut_a(btr_root_fseg_validate(header, block->page.id.space()));
#endif /* UNIV_BTR_DEBUG */
	if (invalidate) {
		btr_page_set_index_id(
			buf_block_get_frame(block),
			buf_block_get_page_zip(block),
			BTR_FREED_INDEX_ID, mtr);
	}

	while (!fseg_free_step(header, true, mtr)) {
		/* Free the entire segment in small steps. */
	}
}

/** Prepare to free a B-tree.
@param[in]	page_id		page id
@param[in]	zip_size	ROW_FORMAT=COMPRESSED page size, or 0
@param[in]	index_id	PAGE_INDEX_ID contents
@param[in,out]	mtr		mini-transaction
@return root block, to invoke btr_free_but_not_root() and btr_free_root()
@retval NULL if the page is no longer a matching B-tree page */
static MY_ATTRIBUTE((warn_unused_result))
buf_block_t*
btr_free_root_check(
	const page_id_t		page_id,
	ulint			zip_size,
	index_id_t		index_id,
	mtr_t*			mtr)
{
	ut_ad(page_id.space() != SRV_TMP_SPACE_ID);
	ut_ad(index_id != BTR_FREED_INDEX_ID);

	buf_block_t*	block = buf_page_get(
		page_id, zip_size, RW_X_LATCH, mtr);

	if (block) {
		buf_block_dbg_add_level(block, SYNC_TREE_NODE);

		if (fil_page_index_page_check(block->frame)
		    && index_id == btr_page_get_index_id(block->frame)) {
			/* This should be a root page.
			It should not be possible to reassign the same
			index_id for some other index in the tablespace. */
			ut_ad(!page_has_siblings(block->frame));
		} else {
			block = NULL;
		}
	}

	return(block);
}

/** Create the root node for a new index tree.
@param[in]	type			type of the index
@param[in]	index_id		index id
@param[in,out]	space			tablespace where created
@param[in]	index			index
@param[in,out]	mtr			mini-transaction
@return	page number of the created root
@retval	FIL_NULL	if did not succeed */
ulint
btr_create(
	ulint			type,
	fil_space_t*		space,
	index_id_t		index_id,
	dict_index_t*		index,
	mtr_t*			mtr)
{
	buf_block_t*		block;
	page_t*			page;
	page_zip_des_t*		page_zip;

	ut_ad(mtr->is_named_space(space));
	ut_ad(index_id != BTR_FREED_INDEX_ID);

	/* Create the two new segments (one, in the case of an ibuf tree) for
	the index tree; the segment headers are put on the allocated root page
	(for an ibuf tree, not in the root, but on a separate ibuf header
	page) */

	if (UNIV_UNLIKELY(type & DICT_IBUF)) {
		/* Allocate first the ibuf header page */
		buf_block_t*	ibuf_hdr_block = fseg_create(
			space, 0,
			IBUF_HEADER + IBUF_TREE_SEG_HEADER, mtr);

		if (ibuf_hdr_block == NULL) {
			return(FIL_NULL);
		}

		buf_block_dbg_add_level(
			ibuf_hdr_block, SYNC_IBUF_TREE_NODE_NEW);

		ut_ad(ibuf_hdr_block->page.id.page_no()
		      == IBUF_HEADER_PAGE_NO);
		/* Allocate then the next page to the segment: it will be the
		tree root page */

		block = fseg_alloc_free_page(
			buf_block_get_frame(ibuf_hdr_block)
			+ IBUF_HEADER + IBUF_TREE_SEG_HEADER,
			IBUF_TREE_ROOT_PAGE_NO,
			FSP_UP, mtr);

		if (block == NULL) {
			return(FIL_NULL);
		}

		ut_ad(block->page.id.page_no() == IBUF_TREE_ROOT_PAGE_NO);

		buf_block_dbg_add_level(block, SYNC_IBUF_TREE_NODE_NEW);

		flst_init(block, PAGE_HEADER + PAGE_BTR_IBUF_FREE_LIST, mtr);
	} else {
		block = fseg_create(space, 0,
				    PAGE_HEADER + PAGE_BTR_SEG_TOP, mtr);

		if (block == NULL) {
			return(FIL_NULL);
		}

		buf_block_dbg_add_level(block, SYNC_TREE_NODE_NEW);

		if (!fseg_create(space, block->page.id.page_no(),
				 PAGE_HEADER + PAGE_BTR_SEG_LEAF, mtr)) {
			/* Not enough space for new segment, free root
			segment before return. */
			btr_free_root(block, mtr,
				      !index->table->is_temporary());
			return(FIL_NULL);
		}

		/* The fseg create acquires a second latch on the page,
		therefore we must declare it: */
		buf_block_dbg_add_level(block, SYNC_TREE_NODE_NEW);
	}

	/* Create a new index page on the allocated segment page */
	page_zip = buf_block_get_page_zip(block);

	if (page_zip) {
		page = page_create_zip(block, index, 0, 0, mtr);
	} else {
		page = page_create(block, mtr,
				   dict_table_is_comp(index->table),
				   dict_index_is_spatial(index));
		/* Set the level of the new index page */
		btr_page_set_level(page, NULL, 0, mtr);
	}

	/* Set the index id of the page */
	btr_page_set_index_id(page, page_zip, index_id, mtr);

	/* Set the next node and previous node fields */
	btr_page_set_next(page, page_zip, FIL_NULL, mtr);
	btr_page_set_prev(page, page_zip, FIL_NULL, mtr);

	/* We reset the free bits for the page in a separate
	mini-transaction to allow creation of several trees in the
	same mtr, otherwise the latch on a bitmap page would prevent
	it because of the latching order.

	Note: Insert Buffering is disabled for temporary tables given that
	most temporary tables are smaller in size and short-lived. */
	if (!(type & DICT_CLUSTERED) && !index->table->is_temporary()) {
		ibuf_reset_free_bits(block);
	}

	/* In the following assertion we test that two records of maximum
	allowed size fit on the root page: this fact is needed to ensure
	correctness of split algorithms */

	ut_ad(page_get_max_insert_size(page, 2) > 2 * BTR_PAGE_MAX_REC_SIZE);

	return(block->page.id.page_no());
}

/** Free a B-tree except the root page. The root page MUST be freed after
this by calling btr_free_root.
@param[in,out]	block		root page
@param[in]	log_mode	mtr logging mode */
static
void
btr_free_but_not_root(
	buf_block_t*	block,
	mtr_log_t	log_mode)
{
	ibool	finished;
	mtr_t	mtr;

	ut_ad(fil_page_index_page_check(block->frame));
	ut_ad(!page_has_siblings(block->frame));
leaf_loop:
	mtr_start(&mtr);
	mtr_set_log_mode(&mtr, log_mode);
	mtr.set_named_space_id(block->page.id.space());

	page_t*	root = block->frame;

	if (!root) {
		mtr_commit(&mtr);
		return;
	}

#ifdef UNIV_BTR_DEBUG
	ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_LEAF
				    + root, block->page.id.space()));
	ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
				    + root, block->page.id.space()));
#endif /* UNIV_BTR_DEBUG */

	/* NOTE: page hash indexes are dropped when a page is freed inside
	fsp0fsp. */

	finished = fseg_free_step(root + PAGE_HEADER + PAGE_BTR_SEG_LEAF,
				  true, &mtr);
	mtr_commit(&mtr);

	if (!finished) {

		goto leaf_loop;
	}
top_loop:
	mtr_start(&mtr);
	mtr_set_log_mode(&mtr, log_mode);
	mtr.set_named_space_id(block->page.id.space());

	root = block->frame;

#ifdef UNIV_BTR_DEBUG
	ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
				    + root, block->page.id.space()));
#endif /* UNIV_BTR_DEBUG */

	finished = fseg_free_step_not_header(
		root + PAGE_HEADER + PAGE_BTR_SEG_TOP, true, &mtr);
	mtr_commit(&mtr);

	if (!finished) {
		goto top_loop;
	}
}

/** Free a persistent index tree if it exists.
@param[in]	page_id		root page id
@param[in]	zip_size	ROW_FORMAT=COMPRESSED page size, or 0
@param[in]	index_id	PAGE_INDEX_ID contents
@param[in,out]	mtr		mini-transaction */
void
btr_free_if_exists(
	const page_id_t		page_id,
	ulint			zip_size,
	index_id_t		index_id,
	mtr_t*			mtr)
{
	buf_block_t* root = btr_free_root_check(
		page_id, zip_size, index_id, mtr);

	if (root == NULL) {
		return;
	}

	btr_free_but_not_root(root, mtr->get_log_mode());
	mtr->set_named_space_id(page_id.space());
	btr_free_root(root, mtr, true);
}

/** Free an index tree in a temporary tablespace.
@param[in]	page_id		root page id */
void btr_free(const page_id_t page_id)
{
	mtr_t		mtr;
	mtr.start();
	mtr.set_log_mode(MTR_LOG_NO_REDO);

	buf_block_t*	block = buf_page_get(page_id, 0, RW_X_LATCH, &mtr);

	if (block) {
		btr_free_but_not_root(block, MTR_LOG_NO_REDO);
		btr_free_root(block, &mtr, false);
	}
	mtr.commit();
}

/** Read the last used AUTO_INCREMENT value from PAGE_ROOT_AUTO_INC.
@param[in,out]	index	clustered index
@return	the last used AUTO_INCREMENT value
@retval	0 on error or if no AUTO_INCREMENT value was used yet */
ib_uint64_t
btr_read_autoinc(dict_index_t* index)
{
	ut_ad(index->is_primary());
	ut_ad(index->table->persistent_autoinc);
	ut_ad(!index->table->is_temporary());
	mtr_t		mtr;
	mtr.start();
	ib_uint64_t	autoinc;
	if (buf_block_t* block = buf_page_get(
		    page_id_t(index->table->space_id, index->page),
		    index->table->space->zip_size(),
		    RW_S_LATCH, &mtr)) {
		autoinc = page_get_autoinc(block->frame);
	} else {
		autoinc = 0;
	}
	mtr.commit();
	return autoinc;
}

/** Read the last used AUTO_INCREMENT value from PAGE_ROOT_AUTO_INC,
or fall back to MAX(auto_increment_column).
@param[in]	table	table containing an AUTO_INCREMENT column
@param[in]	col_no	index of the AUTO_INCREMENT column
@return	the AUTO_INCREMENT value
@retval	0 on error or if no AUTO_INCREMENT value was used yet */
ib_uint64_t
btr_read_autoinc_with_fallback(const dict_table_t* table, unsigned col_no)
{
	ut_ad(table->persistent_autoinc);
	ut_ad(!table->is_temporary());

	dict_index_t*	index = dict_table_get_first_index(table);

	if (index == NULL) {
		return 0;
	}

	mtr_t		mtr;
	mtr.start();
	buf_block_t*	block = buf_page_get(
		page_id_t(index->table->space_id, index->page),
		index->table->space->zip_size(),
		RW_S_LATCH, &mtr);

	ib_uint64_t	autoinc	= block ? page_get_autoinc(block->frame) : 0;
	const bool	retry	= block && autoinc == 0
		&& !page_is_empty(block->frame);
	mtr.commit();

	if (retry) {
		/* This should be an old data file where
		PAGE_ROOT_AUTO_INC was initialized to 0.
		Fall back to reading MAX(autoinc_col).
		There should be an index on it. */
		const dict_col_t*	autoinc_col
			= dict_table_get_nth_col(table, col_no);
		while (index && index->fields[0].col != autoinc_col) {
			index = dict_table_get_next_index(index);
		}

		if (index) {
			autoinc = row_search_max_autoinc(index);
		}
	}

	return autoinc;
}

/** Write the next available AUTO_INCREMENT value to PAGE_ROOT_AUTO_INC.
@param[in,out]	index	clustered index
@param[in]	autoinc	the AUTO_INCREMENT value
@param[in]	reset	whether to reset the AUTO_INCREMENT
			to a possibly smaller value than currently
			exists in the page */
void
btr_write_autoinc(dict_index_t* index, ib_uint64_t autoinc, bool reset)
{
	ut_ad(index->is_primary());
	ut_ad(index->table->persistent_autoinc);
	ut_ad(!index->table->is_temporary());

	mtr_t		mtr;
	mtr.start();
	fil_space_t* space = index->table->space;
	mtr.set_named_space(space);
	page_set_autoinc(buf_page_get(page_id_t(space->id, index->page),
				      space->zip_size(),
				      RW_SX_LATCH, &mtr),
			 index, autoinc, &mtr, reset);
	mtr.commit();
}

/*************************************************************//**
Reorganizes an index page.

IMPORTANT: On success, the caller will have to update IBUF_BITMAP_FREE
if this is a compressed leaf page in a secondary index. This has to
be done either within the same mini-transaction, or by invoking
ibuf_reset_free_bits() before mtr_commit(). On uncompressed pages,
IBUF_BITMAP_FREE is unaffected by reorganization.

@retval true if the operation was successful
@retval false if it is a compressed page, and recompression failed */
bool
btr_page_reorganize_low(
/*====================*/
	bool		recovery,/*!< in: true if called in recovery:
				locks should not be updated, i.e.,
				there cannot exist locks on the
				page, and a hash index should not be
				dropped: it cannot exist */
	ulint		z_level,/*!< in: compression level to be used
				if dealing with compressed page */
	page_cur_t*	cursor,	/*!< in/out: page cursor */
	dict_index_t*	index,	/*!< in: the index tree of the page */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	buf_block_t*	block		= page_cur_get_block(cursor);
	buf_pool_t*	buf_pool	= buf_pool_from_bpage(&block->page);
	page_t*		page		= buf_block_get_frame(block);
	page_zip_des_t*	page_zip	= buf_block_get_page_zip(block);
	buf_block_t*	temp_block;
	page_t*		temp_page;
	ulint		data_size1;
	ulint		data_size2;
	ulint		max_ins_size1;
	ulint		max_ins_size2;
	bool		success		= false;
	ulint		pos;
	bool		log_compressed;
	bool		is_spatial;

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	btr_assert_not_corrupted(block, index);
	ut_ad(fil_page_index_page_check(block->frame));
	ut_ad(index->is_dummy
	      || block->page.id.space() == index->table->space->id);
	ut_ad(index->is_dummy
	      || block->page.id.page_no() != index->page
	      || !page_has_siblings(page));
#ifdef UNIV_ZIP_DEBUG
	ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */
	data_size1 = page_get_data_size(page);
	max_ins_size1 = page_get_max_insert_size_after_reorganize(page, 1);
	/* Turn logging off */
	mtr_log_t	log_mode = mtr_set_log_mode(mtr, MTR_LOG_NONE);

	temp_block = buf_block_alloc(buf_pool);
	temp_page = temp_block->frame;

	MONITOR_INC(MONITOR_INDEX_REORG_ATTEMPTS);

	/* This function can be called by log redo with a "dummy" index.
	So we would trust more on the original page's type */
	is_spatial = (fil_page_get_type(page) == FIL_PAGE_RTREE
		      || dict_index_is_spatial(index));

	/* Copy the old page to temporary space */
	buf_frame_copy(temp_page, page);

	if (!recovery) {
		btr_search_drop_page_hash_index(block);
	}

	/* Save the cursor position. */
	pos = page_rec_get_n_recs_before(page_cur_get_rec(cursor));

	/* Recreate the page: note that global data on page (possible
	segment headers, next page-field, etc.) is preserved intact */

	page_create(block, mtr, dict_table_is_comp(index->table), is_spatial);

	/* Copy the records from the temporary space to the recreated page;
	do not copy the lock bits yet */

	page_copy_rec_list_end_no_locks(block, temp_block,
					page_get_infimum_rec(temp_page),
					index, mtr);

	/* Copy the PAGE_MAX_TRX_ID or PAGE_ROOT_AUTO_INC. */
	memcpy(page + (PAGE_HEADER + PAGE_MAX_TRX_ID),
	       temp_page + (PAGE_HEADER + PAGE_MAX_TRX_ID), 8);
	/* PAGE_MAX_TRX_ID is unused in clustered index pages
	(other than the root where it is repurposed as PAGE_ROOT_AUTO_INC),
	non-leaf pages, and in temporary tables. It was always
	zero-initialized in page_create() in all InnoDB versions.
	PAGE_MAX_TRX_ID must be nonzero on dict_index_is_sec_or_ibuf()
	leaf pages.

	During redo log apply, dict_index_is_sec_or_ibuf() always
	holds, even for clustered indexes. */
	ut_ad(recovery || index->table->is_temporary()
	      || !page_is_leaf(temp_page)
	      || !dict_index_is_sec_or_ibuf(index)
	      || page_get_max_trx_id(page) != 0);
	/* PAGE_MAX_TRX_ID must be zero on non-leaf pages other than
	clustered index root pages. */
	ut_ad(recovery
	      || page_get_max_trx_id(page) == 0
	      || (dict_index_is_sec_or_ibuf(index)
		  ? page_is_leaf(temp_page)
		  : block->page.id.page_no() == index->page));

	/* If innodb_log_compressed_pages is ON, page reorganize should log the
	compressed page image.*/
	log_compressed = page_zip && page_zip_log_pages;

	if (log_compressed) {
		mtr_set_log_mode(mtr, log_mode);
	}

	if (page_zip
	    && !page_zip_compress(page_zip, page, index, z_level, mtr)) {

		/* Restore the old page and exit. */
#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
		/* Check that the bytes that we skip are identical. */
		ut_a(!memcmp(page, temp_page, PAGE_HEADER));
		ut_a(!memcmp(PAGE_HEADER + PAGE_N_RECS + page,
			     PAGE_HEADER + PAGE_N_RECS + temp_page,
			     PAGE_DATA - (PAGE_HEADER + PAGE_N_RECS)));
		ut_a(!memcmp(srv_page_size - FIL_PAGE_DATA_END + page,
			     srv_page_size - FIL_PAGE_DATA_END + temp_page,
			     FIL_PAGE_DATA_END));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

		memcpy(PAGE_HEADER + page, PAGE_HEADER + temp_page,
		       PAGE_N_RECS - PAGE_N_DIR_SLOTS);
		memcpy(PAGE_DATA + page, PAGE_DATA + temp_page,
		       srv_page_size - PAGE_DATA - FIL_PAGE_DATA_END);

#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
		ut_a(!memcmp(page, temp_page, srv_page_size));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

		goto func_exit;
	}

	data_size2 = page_get_data_size(page);
	max_ins_size2 = page_get_max_insert_size_after_reorganize(page, 1);

	if (data_size1 != data_size2 || max_ins_size1 != max_ins_size2) {
		ib::error()
			<< "Page old data size " << data_size1
			<< " new data size " << data_size2
			<< ", page old max ins size " << max_ins_size1
			<< " new max ins size " << max_ins_size2;

		ib::error() << BUG_REPORT_MSG;
		ut_ad(0);
	} else {
		success = true;
	}

	/* Restore the cursor position. */
	if (pos > 0) {
		cursor->rec = page_rec_get_nth(page, pos);
	} else {
		ut_ad(cursor->rec == page_get_infimum_rec(page));
	}

#ifdef UNIV_ZIP_DEBUG
	ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */

	if (!recovery) {
		if (block->page.id.page_no() == index->page
		    && fil_page_get_type(temp_page) == FIL_PAGE_TYPE_INSTANT) {
			/* Preserve the PAGE_INSTANT information. */
			ut_ad(!page_zip);
			ut_ad(index->is_instant());
			memcpy(FIL_PAGE_TYPE + page,
			       FIL_PAGE_TYPE + temp_page, 2);
			memcpy(PAGE_HEADER + PAGE_INSTANT + page,
			       PAGE_HEADER + PAGE_INSTANT + temp_page, 2);
			if (!index->table->instant) {
			} else if (page_is_comp(page)) {
				memcpy(PAGE_NEW_INFIMUM + page,
				       PAGE_NEW_INFIMUM + temp_page, 8);
				memcpy(PAGE_NEW_SUPREMUM + page,
				       PAGE_NEW_SUPREMUM + temp_page, 8);
			} else {
				memcpy(PAGE_OLD_INFIMUM + page,
				       PAGE_OLD_INFIMUM + temp_page, 8);
				memcpy(PAGE_OLD_SUPREMUM + page,
				       PAGE_OLD_SUPREMUM + temp_page, 8);
			}
		}

		if (!dict_table_is_locking_disabled(index->table)) {
			/* Update the record lock bitmaps */
			lock_move_reorganize_page(block, temp_block);
		}
	}

func_exit:
	buf_block_free(temp_block);

	/* Restore logging mode */
	mtr_set_log_mode(mtr, log_mode);

	if (success) {
		mlog_id_t	type;
		byte*		log_ptr;

		/* Write the log record */
		if (page_zip) {
			ut_ad(page_is_comp(page));
			type = MLOG_ZIP_PAGE_REORGANIZE;
		} else if (page_is_comp(page)) {
			type = MLOG_COMP_PAGE_REORGANIZE;
		} else {
			type = MLOG_PAGE_REORGANIZE;
		}

		log_ptr = log_compressed
			? NULL
			: mlog_open_and_write_index(
				mtr, page, index, type,
				page_zip ? 1 : 0);

		/* For compressed pages write the compression level. */
		if (log_ptr && page_zip) {
			mach_write_to_1(log_ptr, z_level);
			mlog_close(mtr, log_ptr + 1);
		}

		MONITOR_INC(MONITOR_INDEX_REORG_SUCCESSFUL);
	}

	if (UNIV_UNLIKELY(fil_page_get_type(page) == FIL_PAGE_TYPE_INSTANT)) {
		/* Log the PAGE_INSTANT information. */
		ut_ad(!page_zip);
		ut_ad(index->is_instant());
		ut_ad(!recovery);
		mlog_write_ulint(FIL_PAGE_TYPE + page, FIL_PAGE_TYPE_INSTANT,
				 MLOG_2BYTES, mtr);
		mlog_write_ulint(PAGE_HEADER + PAGE_INSTANT + page,
				 mach_read_from_2(PAGE_HEADER + PAGE_INSTANT
						  + page),
				 MLOG_2BYTES, mtr);
		if (!index->table->instant) {
		} else if (page_is_comp(page)) {
			mlog_log_string(PAGE_NEW_INFIMUM + page, 8, mtr);
			mlog_log_string(PAGE_NEW_SUPREMUM + page, 8, mtr);
		} else {
			mlog_log_string(PAGE_OLD_INFIMUM + page, 8, mtr);
			mlog_log_string(PAGE_OLD_SUPREMUM + page, 8, mtr);
		}
	}

	return(success);
}

/*************************************************************//**
Reorganizes an index page.

IMPORTANT: On success, the caller will have to update IBUF_BITMAP_FREE
if this is a compressed leaf page in a secondary index. This has to
be done either within the same mini-transaction, or by invoking
ibuf_reset_free_bits() before mtr_commit(). On uncompressed pages,
IBUF_BITMAP_FREE is unaffected by reorganization.

@retval true if the operation was successful
@retval false if it is a compressed page, and recompression failed */
bool
btr_page_reorganize_block(
/*======================*/
	bool		recovery,/*!< in: true if called in recovery:
				locks should not be updated, i.e.,
				there cannot exist locks on the
				page, and a hash index should not be
				dropped: it cannot exist */
	ulint		z_level,/*!< in: compression level to be used
				if dealing with compressed page */
	buf_block_t*	block,	/*!< in/out: B-tree page */
	dict_index_t*	index,	/*!< in: the index tree of the page */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	page_cur_t	cur;
	page_cur_set_before_first(block, &cur);

	return(btr_page_reorganize_low(recovery, z_level, &cur, index, mtr));
}

/*************************************************************//**
Reorganizes an index page.

IMPORTANT: On success, the caller will have to update IBUF_BITMAP_FREE
if this is a compressed leaf page in a secondary index. This has to
be done either within the same mini-transaction, or by invoking
ibuf_reset_free_bits() before mtr_commit(). On uncompressed pages,
IBUF_BITMAP_FREE is unaffected by reorganization.

@retval true if the operation was successful
@retval false if it is a compressed page, and recompression failed */
bool
btr_page_reorganize(
/*================*/
	page_cur_t*	cursor,	/*!< in/out: page cursor */
	dict_index_t*	index,	/*!< in: the index tree of the page */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	return(btr_page_reorganize_low(false, page_zip_level,
				       cursor, index, mtr));
}

/***********************************************************//**
Parses a redo log record of reorganizing a page.
@return end of log record or NULL */
byte*
btr_parse_page_reorganize(
/*======================*/
	byte*		ptr,	/*!< in: buffer */
	byte*		end_ptr,/*!< in: buffer end */
	dict_index_t*	index,	/*!< in: record descriptor */
	bool		compressed,/*!< in: true if compressed page */
	buf_block_t*	block,	/*!< in: page to be reorganized, or NULL */
	mtr_t*		mtr)	/*!< in: mtr or NULL */
{
	ulint	level = page_zip_level;

	ut_ad(ptr != NULL);
	ut_ad(end_ptr != NULL);
	ut_ad(index != NULL);

	/* If dealing with a compressed page the record has the
	compression level used during original compression written in
	one byte. Otherwise record is empty. */
	if (compressed) {
		if (ptr == end_ptr) {
			return(NULL);
		}

		level = mach_read_from_1(ptr);

		ut_a(level <= 9);
		++ptr;
	} else {
		level = page_zip_level;
	}

	if (block != NULL) {
		btr_page_reorganize_block(true, level, block, index, mtr);
	}

	return(ptr);
}

/** Empty an index page (possibly the root page). @see btr_page_create().
@param[in,out]	block		page to be emptied
@param[in,out]	page_zip	compressed page frame, or NULL
@param[in]	index		index of the page
@param[in]	level		B-tree level of the page (0=leaf)
@param[in,out]	mtr		mini-transaction */
void
btr_page_empty(
	buf_block_t*	block,
	page_zip_des_t*	page_zip,
	dict_index_t*	index,
	ulint		level,
	mtr_t*		mtr)
{
	page_t*	page = buf_block_get_frame(block);

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	ut_ad(page_zip == buf_block_get_page_zip(block));
	ut_ad(!index->is_dummy);
	ut_ad(index->table->space->id == block->page.id.space());
#ifdef UNIV_ZIP_DEBUG
	ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */

	btr_search_drop_page_hash_index(block);

	/* Recreate the page: note that global data on page (possible
	segment headers, next page-field, etc.) is preserved intact */

	/* Preserve PAGE_ROOT_AUTO_INC when creating a clustered index
	root page. */
	const ib_uint64_t	autoinc
		= dict_index_is_clust(index)
		&& index->page == block->page.id.page_no()
		? page_get_autoinc(page)
		: 0;

	if (page_zip) {
		page_create_zip(block, index, level, autoinc, mtr);
	} else {
		page_create(block, mtr, dict_table_is_comp(index->table),
			    dict_index_is_spatial(index));
		btr_page_set_level(page, NULL, level, mtr);
		if (autoinc) {
			mlog_write_ull(PAGE_HEADER + PAGE_MAX_TRX_ID + page,
				       autoinc, mtr);
		}
	}
}

/** Write instant ALTER TABLE metadata to a root page.
@param[in,out]	root	clustered index root page
@param[in]	index	clustered index with instant ALTER TABLE
@param[in,out]	mtr	mini-transaction */
void btr_set_instant(buf_block_t* root, const dict_index_t& index, mtr_t* mtr)
{
	ut_ad(index.n_core_fields > 0);
	ut_ad(index.n_core_fields < REC_MAX_N_FIELDS);
	ut_ad(index.is_instant());
	ut_ad(fil_page_get_type(root->frame) == FIL_PAGE_TYPE_INSTANT
	      || fil_page_get_type(root->frame) == FIL_PAGE_INDEX);
	ut_ad(!page_has_siblings(root->frame));
	ut_ad(root->page.id.page_no() == index.page);

	rec_t* infimum = page_get_infimum_rec(root->frame);
	rec_t* supremum = page_get_supremum_rec(root->frame);
	byte* page_type = root->frame + FIL_PAGE_TYPE;
	uint16_t i = page_header_get_field(root->frame, PAGE_INSTANT);

	switch (mach_read_from_2(page_type)) {
	case FIL_PAGE_TYPE_INSTANT:
		ut_ad(page_get_instant(root->frame) == index.n_core_fields);
		if (memcmp(infimum, "infimum", 8)
		    || memcmp(supremum, "supremum", 8)) {
			ut_ad(index.table->instant);
			ut_ad(!memcmp(infimum, field_ref_zero, 8));
			ut_ad(!memcmp(supremum, field_ref_zero, 7));
			/* The n_core_null_bytes only matters for
			ROW_FORMAT=COMPACT and ROW_FORMAT=DYNAMIC tables. */
			ut_ad(supremum[7] == index.n_core_null_bytes
			      || !index.table->not_redundant());
			return;
		}
		break;
	default:
		ut_ad(!"wrong page type");
		/* fall through */
	case FIL_PAGE_INDEX:
		ut_ad(!page_is_comp(root->frame)
		      || !page_get_instant(root->frame));
		ut_ad(!memcmp(infimum, "infimum", 8));
		ut_ad(!memcmp(supremum, "supremum", 8));
		mlog_write_ulint(page_type, FIL_PAGE_TYPE_INSTANT,
				 MLOG_2BYTES, mtr);
		ut_ad(i <= PAGE_NO_DIRECTION);
		i |= index.n_core_fields << 3;
		mlog_write_ulint(PAGE_HEADER + PAGE_INSTANT + root->frame, i,
				 MLOG_2BYTES, mtr);
		break;
	}

	if (index.table->instant) {
		mlog_memset(root, infimum - root->frame, 8, 0, mtr);
		mlog_memset(root, supremum - root->frame, 7, 0, mtr);
		mlog_write_ulint(&supremum[7], index.n_core_null_bytes,
				 MLOG_1BYTE, mtr);
	}
}

/*************************************************************//**
Makes tree one level higher by splitting the root, and inserts
the tuple. It is assumed that mtr contains an x-latch on the tree.
NOTE that the operation of this function must always succeed,
we cannot reverse it: therefore enough free disk space must be
guaranteed to be available before this function is called.
@return inserted record */
rec_t*
btr_root_raise_and_insert(
/*======================*/
	ulint		flags,	/*!< in: undo logging and locking flags */
	btr_cur_t*	cursor,	/*!< in: cursor at which to insert: must be
				on the root page; when the function returns,
				the cursor is positioned on the predecessor
				of the inserted record */
	ulint**		offsets,/*!< out: offsets on inserted record */
	mem_heap_t**	heap,	/*!< in/out: pointer to memory heap, or NULL */
	const dtuple_t*	tuple,	/*!< in: tuple to insert */
	ulint		n_ext,	/*!< in: number of externally stored columns */
	mtr_t*		mtr)	/*!< in: mtr */
{
	dict_index_t*	index;
	page_t*		root;
	page_t*		new_page;
	ulint		new_page_no;
	rec_t*		rec;
	dtuple_t*	node_ptr;
	ulint		level;
	rec_t*		node_ptr_rec;
	page_cur_t*	page_cursor;
	page_zip_des_t*	root_page_zip;
	page_zip_des_t*	new_page_zip;
	buf_block_t*	root_block;
	buf_block_t*	new_block;

	root = btr_cur_get_page(cursor);
	root_block = btr_cur_get_block(cursor);
	root_page_zip = buf_block_get_page_zip(root_block);
	ut_ad(!page_is_empty(root));
	index = btr_cur_get_index(cursor);
	ut_ad(index->n_core_null_bytes <= UT_BITS_IN_BYTES(index->n_nullable));
#ifdef UNIV_ZIP_DEBUG
	ut_a(!root_page_zip || page_zip_validate(root_page_zip, root, index));
#endif /* UNIV_ZIP_DEBUG */
#ifdef UNIV_BTR_DEBUG
	if (!dict_index_is_ibuf(index)) {
		ulint	space = index->table->space_id;

		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_LEAF
					    + root, space));
		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
					    + root, space));
	}

	ut_a(dict_index_get_page(index) == page_get_page_no(root));
#endif /* UNIV_BTR_DEBUG */
	ut_ad(mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
					MTR_MEMO_X_LOCK
					| MTR_MEMO_SX_LOCK));
	ut_ad(mtr_memo_contains(mtr, root_block, MTR_MEMO_PAGE_X_FIX));

	/* Allocate a new page to the tree. Root splitting is done by first
	moving the root records to the new page, emptying the root, putting
	a node pointer to the new page, and then splitting the new page. */

	level = btr_page_get_level(root);

	new_block = btr_page_alloc(index, 0, FSP_NO_DIR, level, mtr, mtr);

	if (new_block == NULL && os_has_said_disk_full) {
		return(NULL);
	}

	new_page = buf_block_get_frame(new_block);
	new_page_zip = buf_block_get_page_zip(new_block);
	ut_a(!new_page_zip == !root_page_zip);
	ut_a(!new_page_zip
	     || page_zip_get_size(new_page_zip)
	     == page_zip_get_size(root_page_zip));

	btr_page_create(new_block, new_page_zip, index, level, mtr);

	/* Set the next node and previous node fields of new page */
	btr_page_set_next(new_page, new_page_zip, FIL_NULL, mtr);
	btr_page_set_prev(new_page, new_page_zip, FIL_NULL, mtr);

	/* Copy the records from root to the new page one by one. */

	if (0
#ifdef UNIV_ZIP_COPY
	    || new_page_zip
#endif /* UNIV_ZIP_COPY */
	    || !page_copy_rec_list_end(new_block, root_block,
				       page_get_infimum_rec(root),
				       index, mtr)) {
		ut_a(new_page_zip);

		/* Copy the page byte for byte. */
		page_zip_copy_recs(new_page_zip, new_page,
				   root_page_zip, root, index, mtr);

		/* Update the lock table and possible hash index. */
		lock_move_rec_list_end(new_block, root_block,
				       page_get_infimum_rec(root));

		/* Move any existing predicate locks */
		if (dict_index_is_spatial(index)) {
			lock_prdt_rec_move(new_block, root_block);
		} else {
			btr_search_move_or_delete_hash_entries(
				new_block, root_block);
		}
	}

	if (dict_index_is_sec_or_ibuf(index)) {
		/* In secondary indexes and the change buffer,
		PAGE_MAX_TRX_ID can be reset on the root page, because
		the field only matters on leaf pages, and the root no
		longer is a leaf page. (Older versions of InnoDB did
		set PAGE_MAX_TRX_ID on all secondary index pages.) */
		if (root_page_zip) {
			byte* p = PAGE_HEADER + PAGE_MAX_TRX_ID + root;
			memset(p, 0, 8);
			page_zip_write_header(root_page_zip, p, 8, mtr);
		} else {
			mlog_write_ull(PAGE_HEADER + PAGE_MAX_TRX_ID
				       + root, 0, mtr);
		}
	} else {
		/* PAGE_ROOT_AUTO_INC is only present in the clustered index
		root page; on other clustered index pages, we want to reserve
		the field PAGE_MAX_TRX_ID for future use. */
		if (new_page_zip) {
			byte* p = PAGE_HEADER + PAGE_MAX_TRX_ID + new_page;
			memset(p, 0, 8);
			page_zip_write_header(new_page_zip, p, 8, mtr);
		} else {
			mlog_write_ull(PAGE_HEADER + PAGE_MAX_TRX_ID
				       + new_page, 0, mtr);
		}
	}

	/* If this is a pessimistic insert which is actually done to
	perform a pessimistic update then we have stored the lock
	information of the record to be inserted on the infimum of the
	root page: we cannot discard the lock structs on the root page */

	if (!dict_table_is_locking_disabled(index->table)) {
		lock_update_root_raise(new_block, root_block);
	}

	/* Create a memory heap where the node pointer is stored */
	if (!*heap) {
		*heap = mem_heap_create(1000);
	}

	rec = page_rec_get_next(page_get_infimum_rec(new_page));
	new_page_no = new_block->page.id.page_no();

	/* Build the node pointer (= node key and page address) for the
	child */
	if (dict_index_is_spatial(index)) {
		rtr_mbr_t		new_mbr;

		rtr_page_cal_mbr(index, new_block, &new_mbr, *heap);
		node_ptr = rtr_index_build_node_ptr(
			index, &new_mbr, rec, new_page_no, *heap);
	} else {
		node_ptr = dict_index_build_node_ptr(
			index, rec, new_page_no, *heap, level);
	}
	/* The node pointer must be marked as the predefined minimum record,
	as there is no lower alphabetical limit to records in the leftmost
	node of a level: */
	dtuple_set_info_bits(node_ptr,
			     dtuple_get_info_bits(node_ptr)
			     | REC_INFO_MIN_REC_FLAG);

	/* Rebuild the root page to get free space */
	btr_page_empty(root_block, root_page_zip, index, level + 1, mtr);
	/* btr_page_empty() is supposed to zero-initialize the field. */
	ut_ad(!page_get_instant(root_block->frame));

	if (index->is_instant()) {
		ut_ad(!root_page_zip);
		btr_set_instant(root_block, *index, mtr);
	}

	/* Set the next node and previous node fields, although
	they should already have been set.  The previous node field
	must be FIL_NULL if root_page_zip != NULL, because the
	REC_INFO_MIN_REC_FLAG (of the first user record) will be
	set if and only if !page_has_prev(). */
	btr_page_set_next(root, root_page_zip, FIL_NULL, mtr);
	btr_page_set_prev(root, root_page_zip, FIL_NULL, mtr);

	page_cursor = btr_cur_get_page_cur(cursor);

	/* Insert node pointer to the root */

	page_cur_set_before_first(root_block, page_cursor);

	node_ptr_rec = page_cur_tuple_insert(page_cursor, node_ptr,
					     index, offsets, heap, 0, mtr);

	/* The root page should only contain the node pointer
	to new_page at this point.  Thus, the data should fit. */
	ut_a(node_ptr_rec);

	/* We play safe and reset the free bits for the new page */

	if (!dict_index_is_clust(index)
	    && !index->table->is_temporary()) {
		ibuf_reset_free_bits(new_block);
	}

	if (tuple != NULL) {
		/* Reposition the cursor to the child node */
		page_cur_search(new_block, index, tuple, page_cursor);
	} else {
		/* Set cursor to first record on child node */
		page_cur_set_before_first(new_block, page_cursor);
	}

	/* Split the child and insert tuple */
	if (dict_index_is_spatial(index)) {
		/* Split rtree page and insert tuple */
		return(rtr_page_split_and_insert(flags, cursor, offsets, heap,
						 tuple, n_ext, mtr));
	} else {
		return(btr_page_split_and_insert(flags, cursor, offsets, heap,
						 tuple, n_ext, mtr));
	}
}

/*************************************************************//**
Decides if the page should be split at the convergence point of inserts
converging to the left.
@return TRUE if split recommended */
ibool
btr_page_get_split_rec_to_left(
/*===========================*/
	btr_cur_t*	cursor,	/*!< in: cursor at which to insert */
	rec_t**		split_rec) /*!< out: if split recommended,
				the first record on upper half page,
				or NULL if tuple to be inserted should
				be first */
{
	page_t*	page;
	rec_t*	insert_point;
	rec_t*	infimum;

	page = btr_cur_get_page(cursor);
	insert_point = btr_cur_get_rec(cursor);

	if (page_header_get_ptr(page, PAGE_LAST_INSERT)
	    == page_rec_get_next(insert_point)) {

		infimum = page_get_infimum_rec(page);

		/* If the convergence is in the middle of a page, include also
		the record immediately before the new insert to the upper
		page. Otherwise, we could repeatedly move from page to page
		lots of records smaller than the convergence point. */

		if (infimum != insert_point
		    && page_rec_get_next(infimum) != insert_point) {

			*split_rec = insert_point;
		} else {
			*split_rec = page_rec_get_next(insert_point);
		}

		return(TRUE);
	}

	return(FALSE);
}

/*************************************************************//**
Decides if the page should be split at the convergence point of inserts
converging to the right.
@return TRUE if split recommended */
ibool
btr_page_get_split_rec_to_right(
/*============================*/
	btr_cur_t*	cursor,	/*!< in: cursor at which to insert */
	rec_t**		split_rec) /*!< out: if split recommended,
				the first record on upper half page,
				or NULL if tuple to be inserted should
				be first */
{
	page_t*	page;
	rec_t*	insert_point;

	page = btr_cur_get_page(cursor);
	insert_point = btr_cur_get_rec(cursor);

	/* We use eager heuristics: if the new insert would be right after
	the previous insert on the same page, we assume that there is a
	pattern of sequential inserts here. */

	if (page_header_get_ptr(page, PAGE_LAST_INSERT) == insert_point) {

		rec_t*	next_rec;

		next_rec = page_rec_get_next(insert_point);

		if (page_rec_is_supremum(next_rec)) {
split_at_new:
			/* Split at the new record to insert */
			*split_rec = NULL;
		} else {
			rec_t*	next_next_rec = page_rec_get_next(next_rec);
			if (page_rec_is_supremum(next_next_rec)) {

				goto split_at_new;
			}

			/* If there are >= 2 user records up from the insert
			point, split all but 1 off. We want to keep one because
			then sequential inserts can use the adaptive hash
			index, as they can do the necessary checks of the right
			search position just by looking at the records on this
			page. */

			*split_rec = next_next_rec;
		}

		return(TRUE);
	}

	return(FALSE);
}

/*************************************************************//**
Calculates a split record such that the tuple will certainly fit on
its half-page when the split is performed. We assume in this function
only that the cursor page has at least one user record.
@return split record, or NULL if tuple will be the first record on
the lower or upper half-page (determined by btr_page_tuple_smaller()) */
static
rec_t*
btr_page_get_split_rec(
/*===================*/
	btr_cur_t*	cursor,	/*!< in: cursor at which insert should be made */
	const dtuple_t*	tuple,	/*!< in: tuple to insert */
	ulint		n_ext)	/*!< in: number of externally stored columns */
{
	page_t*		page;
	page_zip_des_t*	page_zip;
	ulint		insert_size;
	ulint		free_space;
	ulint		total_data;
	ulint		total_n_recs;
	ulint		total_space;
	ulint		incl_data;
	rec_t*		ins_rec;
	rec_t*		rec;
	rec_t*		next_rec;
	ulint		n;
	mem_heap_t*	heap;
	ulint*		offsets;

	page = btr_cur_get_page(cursor);

	insert_size = rec_get_converted_size(cursor->index, tuple, n_ext);
	free_space  = page_get_free_space_of_empty(page_is_comp(page));

	page_zip = btr_cur_get_page_zip(cursor);
	if (page_zip) {
		/* Estimate the free space of an empty compressed page. */
		ulint	free_space_zip = page_zip_empty_size(
			cursor->index->n_fields,
			page_zip_get_size(page_zip));

		if (free_space > (ulint) free_space_zip) {
			free_space = (ulint) free_space_zip;
		}
	}

	/* free_space is now the free space of a created new page */

	total_data   = page_get_data_size(page) + insert_size;
	total_n_recs = ulint(page_get_n_recs(page)) + 1;
	ut_ad(total_n_recs >= 2);
	total_space  = total_data + page_dir_calc_reserved_space(total_n_recs);

	n = 0;
	incl_data = 0;
	ins_rec = btr_cur_get_rec(cursor);
	rec = page_get_infimum_rec(page);

	heap = NULL;
	offsets = NULL;

	/* We start to include records to the left half, and when the
	space reserved by them exceeds half of total_space, then if
	the included records fit on the left page, they will be put there
	if something was left over also for the right page,
	otherwise the last included record will be the first on the right
	half page */

	do {
		/* Decide the next record to include */
		if (rec == ins_rec) {
			rec = NULL;	/* NULL denotes that tuple is
					now included */
		} else if (rec == NULL) {
			rec = page_rec_get_next(ins_rec);
		} else {
			rec = page_rec_get_next(rec);
		}

		if (rec == NULL) {
			/* Include tuple */
			incl_data += insert_size;
		} else {
			offsets = rec_get_offsets(rec, cursor->index, offsets,
						  page_is_leaf(page),
						  ULINT_UNDEFINED, &heap);
			incl_data += rec_offs_size(offsets);
		}

		n++;
	} while (incl_data + page_dir_calc_reserved_space(n)
		 < total_space / 2);

	if (incl_data + page_dir_calc_reserved_space(n) <= free_space) {
		/* The next record will be the first on
		the right half page if it is not the
		supremum record of page */

		if (rec == ins_rec) {
			rec = NULL;

			goto func_exit;
		} else if (rec == NULL) {
			next_rec = page_rec_get_next(ins_rec);
		} else {
			next_rec = page_rec_get_next(rec);
		}
		ut_ad(next_rec);
		if (!page_rec_is_supremum(next_rec)) {
			rec = next_rec;
		}
	}

func_exit:
	if (heap) {
		mem_heap_free(heap);
	}
	return(rec);
}

/*************************************************************//**
Returns TRUE if the insert fits on the appropriate half-page with the
chosen split_rec.
@return true if fits */
static MY_ATTRIBUTE((nonnull(1,3,4,6), warn_unused_result))
bool
btr_page_insert_fits(
/*=================*/
	btr_cur_t*	cursor,	/*!< in: cursor at which insert
				should be made */
	const rec_t*	split_rec,/*!< in: suggestion for first record
				on upper half-page, or NULL if
				tuple to be inserted should be first */
	ulint**		offsets,/*!< in: rec_get_offsets(
				split_rec, cursor->index); out: garbage */
	const dtuple_t*	tuple,	/*!< in: tuple to insert */
	ulint		n_ext,	/*!< in: number of externally stored columns */
	mem_heap_t**	heap)	/*!< in: temporary memory heap */
{
	page_t*		page;
	ulint		insert_size;
	ulint		free_space;
	ulint		total_data;
	ulint		total_n_recs;
	const rec_t*	rec;
	const rec_t*	end_rec;

	page = btr_cur_get_page(cursor);

	ut_ad(!split_rec
	      || !page_is_comp(page) == !rec_offs_comp(*offsets));
	ut_ad(!split_rec
	      || rec_offs_validate(split_rec, cursor->index, *offsets));

	insert_size = rec_get_converted_size(cursor->index, tuple, n_ext);
	free_space  = page_get_free_space_of_empty(page_is_comp(page));

	/* free_space is now the free space of a created new page */

	total_data   = page_get_data_size(page) + insert_size;
	total_n_recs = ulint(page_get_n_recs(page)) + 1;

	/* We determine which records (from rec to end_rec, not including
	end_rec) will end up on the other half page from tuple when it is
	inserted. */

	if (split_rec == NULL) {
		rec = page_rec_get_next(page_get_infimum_rec(page));
		end_rec = page_rec_get_next(btr_cur_get_rec(cursor));

	} else if (cmp_dtuple_rec(tuple, split_rec, *offsets) >= 0) {

		rec = page_rec_get_next(page_get_infimum_rec(page));
		end_rec = split_rec;
	} else {
		rec = split_rec;
		end_rec = page_get_supremum_rec(page);
	}

	if (total_data + page_dir_calc_reserved_space(total_n_recs)
	    <= free_space) {

		/* Ok, there will be enough available space on the
		half page where the tuple is inserted */

		return(true);
	}

	while (rec != end_rec) {
		/* In this loop we calculate the amount of reserved
		space after rec is removed from page. */

		*offsets = rec_get_offsets(rec, cursor->index, *offsets,
					   page_is_leaf(page),
					   ULINT_UNDEFINED, heap);

		total_data -= rec_offs_size(*offsets);
		total_n_recs--;

		if (total_data + page_dir_calc_reserved_space(total_n_recs)
		    <= free_space) {

			/* Ok, there will be enough available space on the
			half page where the tuple is inserted */

			return(true);
		}

		rec = page_rec_get_next_const(rec);
	}

	return(false);
}

/*******************************************************//**
Inserts a data tuple to a tree on a non-leaf level. It is assumed
that mtr holds an x-latch on the tree. */
void
btr_insert_on_non_leaf_level_func(
/*==============================*/
	ulint		flags,	/*!< in: undo logging and locking flags */
	dict_index_t*	index,	/*!< in: index */
	ulint		level,	/*!< in: level, must be > 0 */
	dtuple_t*	tuple,	/*!< in: the record to be inserted */
	const char*	file,	/*!< in: file name */
	unsigned	line,	/*!< in: line where called */
	mtr_t*		mtr)	/*!< in: mtr */
{
	big_rec_t*	dummy_big_rec;
	btr_cur_t	cursor;
	dberr_t		err;
	rec_t*		rec;
	mem_heap_t*	heap = NULL;
	ulint           offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*          offsets         = offsets_;
	rec_offs_init(offsets_);
	rtr_info_t	rtr_info;

	ut_ad(level > 0);

	if (!dict_index_is_spatial(index)) {
		dberr_t err = btr_cur_search_to_nth_level(
			index, level, tuple, PAGE_CUR_LE,
			BTR_CONT_MODIFY_TREE,
			&cursor, 0, file, line, mtr);

		if (err != DB_SUCCESS) {
			ib::warn() << " Error code: " << err
				   << " btr_page_get_father_node_ptr_func "
				   << " level: " << level
				   << " called from file: "
				   << file << " line: " << line
				   << " table: " << index->table->name
				   << " index: " << index->name;
		}
	} else {
		/* For spatial index, initialize structures to track
		its parents etc. */
		rtr_init_rtr_info(&rtr_info, false, &cursor, index, false);

		rtr_info_update_btr(&cursor, &rtr_info);

		btr_cur_search_to_nth_level(index, level, tuple,
					    PAGE_CUR_RTREE_INSERT,
					    BTR_CONT_MODIFY_TREE,
					    &cursor, 0, file, line, mtr);
	}

	ut_ad(cursor.flag == BTR_CUR_BINARY);

	err = btr_cur_optimistic_insert(
		flags
		| BTR_NO_LOCKING_FLAG
		| BTR_KEEP_SYS_FLAG
		| BTR_NO_UNDO_LOG_FLAG,
		&cursor, &offsets, &heap,
		tuple, &rec, &dummy_big_rec, 0, NULL, mtr);

	if (err == DB_FAIL) {
		err = btr_cur_pessimistic_insert(flags
						 | BTR_NO_LOCKING_FLAG
						 | BTR_KEEP_SYS_FLAG
						 | BTR_NO_UNDO_LOG_FLAG,
						 &cursor, &offsets, &heap,
						 tuple, &rec,
						 &dummy_big_rec, 0, NULL, mtr);
		ut_a(err == DB_SUCCESS);
	}

	if (heap != NULL) {
		mem_heap_free(heap);
	}

	if (dict_index_is_spatial(index)) {
		ut_ad(cursor.rtr_info);

		rtr_clean_rtr_info(&rtr_info, true);
	}
}

/**************************************************************//**
Attaches the halves of an index page on the appropriate level in an
index tree. */
static MY_ATTRIBUTE((nonnull))
void
btr_attach_half_pages(
/*==================*/
	ulint		flags,		/*!< in: undo logging and
					locking flags */
	dict_index_t*	index,		/*!< in: the index tree */
	buf_block_t*	block,		/*!< in/out: page to be split */
	const rec_t*	split_rec,	/*!< in: first record on upper
					half page */
	buf_block_t*	new_block,	/*!< in/out: the new half page */
	ulint		direction,	/*!< in: FSP_UP or FSP_DOWN */
	mtr_t*		mtr)		/*!< in: mtr */
{
	ulint		prev_page_no;
	ulint		next_page_no;
	ulint		level;
	page_t*		page		= buf_block_get_frame(block);
	page_t*		lower_page;
	page_t*		upper_page;
	ulint		lower_page_no;
	ulint		upper_page_no;
	page_zip_des_t*	lower_page_zip;
	page_zip_des_t*	upper_page_zip;
	dtuple_t*	node_ptr_upper;
	mem_heap_t*	heap;
	buf_block_t*	prev_block = NULL;
	buf_block_t*	next_block = NULL;

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	ut_ad(mtr_memo_contains(mtr, new_block, MTR_MEMO_PAGE_X_FIX));

	/* Create a memory heap where the data tuple is stored */
	heap = mem_heap_create(1024);

	/* Based on split direction, decide upper and lower pages */
	if (direction == FSP_DOWN) {

		btr_cur_t	cursor;
		ulint*		offsets;

		lower_page = buf_block_get_frame(new_block);
		lower_page_no = new_block->page.id.page_no();
		lower_page_zip = buf_block_get_page_zip(new_block);
		upper_page = buf_block_get_frame(block);
		upper_page_no = block->page.id.page_no();
		upper_page_zip = buf_block_get_page_zip(block);

		/* Look up the index for the node pointer to page */
		offsets = btr_page_get_father_block(NULL, heap, index,
						    block, mtr, &cursor);

		/* Replace the address of the old child node (= page) with the
		address of the new lower half */

		btr_node_ptr_set_child_page_no(
			btr_cur_get_rec(&cursor),
			btr_cur_get_page_zip(&cursor),
			offsets, lower_page_no, mtr);
		mem_heap_empty(heap);
	} else {
		lower_page = buf_block_get_frame(block);
		lower_page_no = block->page.id.page_no();
		lower_page_zip = buf_block_get_page_zip(block);
		upper_page = buf_block_get_frame(new_block);
		upper_page_no = new_block->page.id.page_no();
		upper_page_zip = buf_block_get_page_zip(new_block);
	}

	/* Get the previous and next pages of page */
	prev_page_no = btr_page_get_prev(page, mtr);
	next_page_no = btr_page_get_next(page, mtr);

	const ulint	space = block->page.id.space();

	/* for consistency, both blocks should be locked, before change */
	if (prev_page_no != FIL_NULL && direction == FSP_DOWN) {
		prev_block = btr_block_get(
			page_id_t(space, prev_page_no), block->zip_size(),
			RW_X_LATCH, index, mtr);
	}
	if (next_page_no != FIL_NULL && direction != FSP_DOWN) {
		next_block = btr_block_get(
			page_id_t(space, next_page_no), block->zip_size(),
			RW_X_LATCH, index, mtr);
	}

	/* Get the level of the split pages */
	level = btr_page_get_level(buf_block_get_frame(block));
	ut_ad(level == btr_page_get_level(buf_block_get_frame(new_block)));

	/* Build the node pointer (= node key and page address) for the upper
	half */

	node_ptr_upper = dict_index_build_node_ptr(index, split_rec,
						   upper_page_no, heap, level);

	/* Insert it next to the pointer to the lower half. Note that this
	may generate recursion leading to a split on the higher level. */

	btr_insert_on_non_leaf_level(flags, index, level + 1,
				     node_ptr_upper, mtr);

	/* Free the memory heap */
	mem_heap_free(heap);

	/* Update page links of the level */

	if (prev_block) {
#ifdef UNIV_BTR_DEBUG
		ut_a(page_is_comp(prev_block->frame) == page_is_comp(page));
		ut_a(btr_page_get_next(prev_block->frame, mtr)
		     == block->page.id.page_no());
#endif /* UNIV_BTR_DEBUG */

		btr_page_set_next(buf_block_get_frame(prev_block),
				  buf_block_get_page_zip(prev_block),
				  lower_page_no, mtr);
	}

	if (next_block) {
#ifdef UNIV_BTR_DEBUG
		ut_a(page_is_comp(next_block->frame) == page_is_comp(page));
		ut_a(btr_page_get_prev(next_block->frame, mtr)
		     == page_get_page_no(page));
#endif /* UNIV_BTR_DEBUG */

		btr_page_set_prev(buf_block_get_frame(next_block),
				  buf_block_get_page_zip(next_block),
				  upper_page_no, mtr);
	}

	if (direction == FSP_DOWN) {
		/* lower_page is new */
		btr_page_set_prev(lower_page, lower_page_zip,
				  prev_page_no, mtr);
	} else {
		ut_ad(btr_page_get_prev(lower_page, mtr) == prev_page_no);
	}

	btr_page_set_next(lower_page, lower_page_zip, upper_page_no, mtr);
	btr_page_set_prev(upper_page, upper_page_zip, lower_page_no, mtr);

	if (direction != FSP_DOWN) {
		/* upper_page is new */
		btr_page_set_next(upper_page, upper_page_zip,
				  next_page_no, mtr);
	} else {
		ut_ad(btr_page_get_next(upper_page, mtr) == next_page_no);
	}
}

/*************************************************************//**
Determine if a tuple is smaller than any record on the page.
@return TRUE if smaller */
static MY_ATTRIBUTE((nonnull, warn_unused_result))
bool
btr_page_tuple_smaller(
/*===================*/
	btr_cur_t*	cursor,	/*!< in: b-tree cursor */
	const dtuple_t*	tuple,	/*!< in: tuple to consider */
	ulint**		offsets,/*!< in/out: temporary storage */
	ulint		n_uniq,	/*!< in: number of unique fields
				in the index page records */
	mem_heap_t**	heap)	/*!< in/out: heap for offsets */
{
	buf_block_t*	block;
	const rec_t*	first_rec;
	page_cur_t	pcur;

	/* Read the first user record in the page. */
	block = btr_cur_get_block(cursor);
	page_cur_set_before_first(block, &pcur);
	page_cur_move_to_next(&pcur);
	first_rec = page_cur_get_rec(&pcur);

	*offsets = rec_get_offsets(
		first_rec, cursor->index, *offsets, page_is_leaf(block->frame),
		n_uniq, heap);

	return(cmp_dtuple_rec(tuple, first_rec, *offsets) < 0);
}

/** Insert the tuple into the right sibling page, if the cursor is at the end
of a page.
@param[in]	flags	undo logging and locking flags
@param[in,out]	cursor	cursor at which to insert; when the function succeeds,
			the cursor is positioned before the insert point.
@param[out]	offsets	offsets on inserted record
@param[in,out]	heap	memory heap for allocating offsets
@param[in]	tuple	tuple to insert
@param[in]	n_ext	number of externally stored columns
@param[in,out]	mtr	mini-transaction
@return	inserted record (first record on the right sibling page);
	the cursor will be positioned on the page infimum
@retval	NULL if the operation was not performed */
static
rec_t*
btr_insert_into_right_sibling(
	ulint		flags,
	btr_cur_t*	cursor,
	ulint**		offsets,
	mem_heap_t*	heap,
	const dtuple_t*	tuple,
	ulint		n_ext,
	mtr_t*		mtr)
{
	buf_block_t*	block = btr_cur_get_block(cursor);
	page_t*		page = buf_block_get_frame(block);
	ulint		next_page_no = btr_page_get_next(page, mtr);

	ut_ad(mtr_memo_contains_flagged(
		      mtr, dict_index_get_lock(cursor->index),
		      MTR_MEMO_X_LOCK | MTR_MEMO_SX_LOCK));
	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	ut_ad(heap);

	if (next_page_no == FIL_NULL || !page_rec_is_supremum(
			page_rec_get_next(btr_cur_get_rec(cursor)))) {

		return(NULL);
	}

	page_cur_t	next_page_cursor;
	buf_block_t*	next_block;
	page_t*		next_page;
	btr_cur_t	next_father_cursor;
	rec_t*		rec = NULL;
	ulint		max_size;

	const ulint	space = block->page.id.space();

	next_block = btr_block_get(
		page_id_t(space, next_page_no), block->zip_size(),
		RW_X_LATCH, cursor->index, mtr);
	next_page = buf_block_get_frame(next_block);

	bool	is_leaf = page_is_leaf(next_page);

	btr_page_get_father(
		cursor->index, next_block, mtr, &next_father_cursor);

	page_cur_search(
		next_block, cursor->index, tuple, PAGE_CUR_LE,
		&next_page_cursor);

	max_size = page_get_max_insert_size_after_reorganize(next_page, 1);

	/* Extends gap lock for the next page */
	if (!dict_table_is_locking_disabled(cursor->index->table)) {
		lock_update_split_left(next_block, block);
	}

	rec = page_cur_tuple_insert(
		&next_page_cursor, tuple, cursor->index, offsets, &heap,
		n_ext, mtr);

	if (rec == NULL) {
		if (is_leaf
		    && next_block->page.zip.ssize
		    && !dict_index_is_clust(cursor->index)
		    && !cursor->index->table->is_temporary()) {
			/* Reset the IBUF_BITMAP_FREE bits, because
			page_cur_tuple_insert() will have attempted page
			reorganize before failing. */
			ibuf_reset_free_bits(next_block);
		}
		return(NULL);
	}

	ibool	compressed;
	dberr_t	err;
	ulint	level = btr_page_get_level(next_page);

	/* adjust cursor position */
	*btr_cur_get_page_cur(cursor) = next_page_cursor;

	ut_ad(btr_cur_get_rec(cursor) == page_get_infimum_rec(next_page));
	ut_ad(page_rec_get_next(page_get_infimum_rec(next_page)) == rec);

	/* We have to change the parent node pointer */

	compressed = btr_cur_pessimistic_delete(
		&err, TRUE, &next_father_cursor,
		BTR_CREATE_FLAG, false, mtr);

	ut_a(err == DB_SUCCESS);

	if (!compressed) {
		btr_cur_compress_if_useful(&next_father_cursor, FALSE, mtr);
	}

	dtuple_t*	node_ptr = dict_index_build_node_ptr(
		cursor->index, rec, next_block->page.id.page_no(),
		heap, level);

	btr_insert_on_non_leaf_level(
		flags, cursor->index, level + 1, node_ptr, mtr);

	ut_ad(rec_offs_validate(rec, cursor->index, *offsets));

	if (is_leaf
	    && !dict_index_is_clust(cursor->index)
	    && !cursor->index->table->is_temporary()) {
		/* Update the free bits of the B-tree page in the
		insert buffer bitmap. */

		if (next_block->page.zip.ssize) {
			ibuf_update_free_bits_zip(next_block, mtr);
		} else {
			ibuf_update_free_bits_if_full(
				next_block, max_size,
				rec_offs_size(*offsets) + PAGE_DIR_SLOT_SIZE);
		}
	}

	return(rec);
}

/*************************************************************//**
Splits an index page to halves and inserts the tuple. It is assumed
that mtr holds an x-latch to the index tree. NOTE: the tree x-latch is
released within this function! NOTE that the operation of this
function must always succeed, we cannot reverse it: therefore enough
free disk space (2 pages) must be guaranteed to be available before
this function is called.
NOTE: jonaso added support for calling function with tuple == NULL
which cause it to only split a page.

@return inserted record or NULL if run out of space */
rec_t*
btr_page_split_and_insert(
/*======================*/
	ulint		flags,	/*!< in: undo logging and locking flags */
	btr_cur_t*	cursor,	/*!< in: cursor at which to insert; when the
				function returns, the cursor is positioned
				on the predecessor of the inserted record */
	ulint**		offsets,/*!< out: offsets on inserted record */
	mem_heap_t**	heap,	/*!< in/out: pointer to memory heap, or NULL */
	const dtuple_t*	tuple,	/*!< in: tuple to insert */
	ulint		n_ext,	/*!< in: number of externally stored columns */
	mtr_t*		mtr)	/*!< in: mtr */
{
	buf_block_t*	block;
	page_t*		page;
	page_zip_des_t*	page_zip;
	ulint		page_no;
	byte		direction;
	ulint		hint_page_no;
	buf_block_t*	new_block;
	page_t*		new_page;
	page_zip_des_t*	new_page_zip;
	rec_t*		split_rec;
	buf_block_t*	left_block;
	buf_block_t*	right_block;
	buf_block_t*	insert_block;
	page_cur_t*	page_cursor;
	rec_t*		first_rec;
	byte*		buf = 0; /* remove warning */
	rec_t*		move_limit;
	ibool		insert_will_fit;
	ibool		insert_left;
	ulint		n_iterations = 0;
	rec_t*		rec;
	ulint		n_uniq;
	dict_index_t*	index;

	index = btr_cur_get_index(cursor);

	if (dict_index_is_spatial(index)) {
		/* Split rtree page and update parent */
		return(rtr_page_split_and_insert(flags, cursor, offsets, heap,
						 tuple, n_ext, mtr));
	}

	if (!*heap) {
		*heap = mem_heap_create(1024);
	}
	n_uniq = dict_index_get_n_unique_in_tree(cursor->index);
func_start:
	mem_heap_empty(*heap);
	*offsets = NULL;

	ut_ad(mtr_memo_contains_flagged(mtr,
					dict_index_get_lock(cursor->index),
					MTR_MEMO_X_LOCK | MTR_MEMO_SX_LOCK));
	ut_ad(!dict_index_is_online_ddl(cursor->index)
	      || (flags & BTR_CREATE_FLAG)
	      || dict_index_is_clust(cursor->index));
	ut_ad(rw_lock_own_flagged(dict_index_get_lock(cursor->index),
				  RW_LOCK_FLAG_X | RW_LOCK_FLAG_SX));

	block = btr_cur_get_block(cursor);
	page = buf_block_get_frame(block);
	page_zip = buf_block_get_page_zip(block);

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	ut_ad(!page_is_empty(page));

	/* try to insert to the next page if possible before split */
	rec = btr_insert_into_right_sibling(
		flags, cursor, offsets, *heap, tuple, n_ext, mtr);

	if (rec != NULL) {
		return(rec);
	}

	page_no = block->page.id.page_no();

	/* 1. Decide the split record; split_rec == NULL means that the
	tuple to be inserted should be the first record on the upper
	half-page */
	insert_left = FALSE;

	if (tuple != NULL && n_iterations > 0) {
		direction = FSP_UP;
		hint_page_no = page_no + 1;
		split_rec = btr_page_get_split_rec(cursor, tuple, n_ext);

		if (split_rec == NULL) {
			insert_left = btr_page_tuple_smaller(
				cursor, tuple, offsets, n_uniq, heap);
		}
	} else if (btr_page_get_split_rec_to_right(cursor, &split_rec)) {
		direction = FSP_UP;
		hint_page_no = page_no + 1;

	} else if (btr_page_get_split_rec_to_left(cursor, &split_rec)) {
		direction = FSP_DOWN;
		hint_page_no = page_no - 1;
		ut_ad(split_rec);
	} else {
		direction = FSP_UP;
		hint_page_no = page_no + 1;

		/* If there is only one record in the index page, we
		can't split the node in the middle by default. We need
		to determine whether the new record will be inserted
		to the left or right. */

		if (page_get_n_recs(page) > 1) {
			split_rec = page_get_middle_rec(page);
		} else if (btr_page_tuple_smaller(cursor, tuple,
						  offsets, n_uniq, heap)) {
			split_rec = page_rec_get_next(
				page_get_infimum_rec(page));
		} else {
			split_rec = NULL;
		}
	}

	DBUG_EXECUTE_IF("disk_is_full",
			os_has_said_disk_full = true;
			return(NULL););

	/* 2. Allocate a new page to the index */
	new_block = btr_page_alloc(cursor->index, hint_page_no, direction,
				   btr_page_get_level(page), mtr, mtr);

	if (new_block == NULL && os_has_said_disk_full) {
		return(NULL);
	}

	new_page = buf_block_get_frame(new_block);
	new_page_zip = buf_block_get_page_zip(new_block);
	btr_page_create(new_block, new_page_zip, cursor->index,
			btr_page_get_level(page), mtr);
	/* Only record the leaf level page splits. */
	if (page_is_leaf(page)) {
		cursor->index->stat_defrag_n_page_split ++;
		cursor->index->stat_defrag_modified_counter ++;
		btr_defragment_save_defrag_stats_if_needed(cursor->index);
	}

	/* 3. Calculate the first record on the upper half-page, and the
	first record (move_limit) on original page which ends up on the
	upper half */

	if (split_rec) {
		first_rec = move_limit = split_rec;

		*offsets = rec_get_offsets(split_rec, cursor->index, *offsets,
					   page_is_leaf(page), n_uniq, heap);

		if (tuple != NULL) {
			insert_left = cmp_dtuple_rec(
				tuple, split_rec, *offsets) < 0;
		} else {
			insert_left = 1;
		}

		if (!insert_left && new_page_zip && n_iterations > 0) {
			/* If a compressed page has already been split,
			avoid further splits by inserting the record
			to an empty page. */
			split_rec = NULL;
			goto insert_empty;
		}
	} else if (insert_left) {
		ut_a(n_iterations > 0);
		first_rec = page_rec_get_next(page_get_infimum_rec(page));
		move_limit = page_rec_get_next(btr_cur_get_rec(cursor));
	} else {
insert_empty:
		ut_ad(!split_rec);
		ut_ad(!insert_left);
		buf = UT_NEW_ARRAY_NOKEY(
			byte,
			rec_get_converted_size(cursor->index, tuple, n_ext));

		first_rec = rec_convert_dtuple_to_rec(buf, cursor->index,
						      tuple, n_ext);
		move_limit = page_rec_get_next(btr_cur_get_rec(cursor));
	}

	/* 4. Do first the modifications in the tree structure */

	btr_attach_half_pages(flags, cursor->index, block,
			      first_rec, new_block, direction, mtr);

	/* If the split is made on the leaf level and the insert will fit
	on the appropriate half-page, we may release the tree x-latch.
	We can then move the records after releasing the tree latch,
	thus reducing the tree latch contention. */
	if (tuple == NULL) {
		insert_will_fit = 1;
	}
	else if (split_rec) {
		insert_will_fit = !new_page_zip
			&& btr_page_insert_fits(cursor, split_rec,
						offsets, tuple, n_ext, heap);
	} else {
		if (!insert_left) {
			UT_DELETE_ARRAY(buf);
			buf = NULL;
		}

		insert_will_fit = !new_page_zip
			&& btr_page_insert_fits(cursor, NULL,
						offsets, tuple, n_ext, heap);
	}

	if (!srv_read_only_mode
	    && insert_will_fit
	    && page_is_leaf(page)
	    && !dict_index_is_online_ddl(cursor->index)) {

		mtr->memo_release(
			dict_index_get_lock(cursor->index),
			MTR_MEMO_X_LOCK | MTR_MEMO_SX_LOCK);

		/* NOTE: We cannot release root block latch here, because it
		has segment header and already modified in most of cases.*/
	}

	/* 5. Move then the records to the new page */
	if (direction == FSP_DOWN) {
		/*		fputs("Split left\n", stderr); */

		if (0
#ifdef UNIV_ZIP_COPY
		    || page_zip
#endif /* UNIV_ZIP_COPY */
		    || !page_move_rec_list_start(new_block, block, move_limit,
						 cursor->index, mtr)) {
			/* For some reason, compressing new_page failed,
			even though it should contain fewer records than
			the original page.  Copy the page byte for byte
			and then delete the records from both pages
			as appropriate.  Deleting will always succeed. */
			ut_a(new_page_zip);

			page_zip_copy_recs(new_page_zip, new_page,
					   page_zip, page, cursor->index, mtr);
			page_delete_rec_list_end(move_limit - page + new_page,
						 new_block, cursor->index,
						 ULINT_UNDEFINED,
						 ULINT_UNDEFINED, mtr);

			/* Update the lock table and possible hash index. */
			lock_move_rec_list_start(
				new_block, block, move_limit,
				new_page + PAGE_NEW_INFIMUM);

			btr_search_move_or_delete_hash_entries(
				new_block, block);

			/* Delete the records from the source page. */

			page_delete_rec_list_start(move_limit, block,
						   cursor->index, mtr);
		}

		left_block = new_block;
		right_block = block;

		if (!dict_table_is_locking_disabled(cursor->index->table)) {
			lock_update_split_left(right_block, left_block);
		}
	} else {
		/*		fputs("Split right\n", stderr); */

		if (0
#ifdef UNIV_ZIP_COPY
		    || page_zip
#endif /* UNIV_ZIP_COPY */
		    || !page_move_rec_list_end(new_block, block, move_limit,
					       cursor->index, mtr)) {
			/* For some reason, compressing new_page failed,
			even though it should contain fewer records than
			the original page.  Copy the page byte for byte
			and then delete the records from both pages
			as appropriate.  Deleting will always succeed. */
			ut_a(new_page_zip);

			page_zip_copy_recs(new_page_zip, new_page,
					   page_zip, page, cursor->index, mtr);
			page_delete_rec_list_start(move_limit - page
						   + new_page, new_block,
						   cursor->index, mtr);

			/* Update the lock table and possible hash index. */
			lock_move_rec_list_end(new_block, block, move_limit);

			ut_ad(!dict_index_is_spatial(index));

			btr_search_move_or_delete_hash_entries(
				new_block, block);

			/* Delete the records from the source page. */

			page_delete_rec_list_end(move_limit, block,
						 cursor->index,
						 ULINT_UNDEFINED,
						 ULINT_UNDEFINED, mtr);
		}

		left_block = block;
		right_block = new_block;

		if (!dict_table_is_locking_disabled(cursor->index->table)) {
			lock_update_split_right(right_block, left_block);
		}
	}

#ifdef UNIV_ZIP_DEBUG
	if (page_zip) {
		ut_a(page_zip_validate(page_zip, page, cursor->index));
		ut_a(page_zip_validate(new_page_zip, new_page, cursor->index));
	}
#endif /* UNIV_ZIP_DEBUG */

	/* At this point, split_rec, move_limit and first_rec may point
	to garbage on the old page. */

	/* 6. The split and the tree modification is now completed. Decide the
	page where the tuple should be inserted */

	if (tuple == NULL) {
		rec = NULL;
		goto func_exit;
	}

	if (insert_left) {
		insert_block = left_block;
	} else {
		insert_block = right_block;
	}

	/* 7. Reposition the cursor for insert and try insertion */
	page_cursor = btr_cur_get_page_cur(cursor);

	page_cur_search(insert_block, cursor->index, tuple, page_cursor);

	rec = page_cur_tuple_insert(page_cursor, tuple, cursor->index,
				    offsets, heap, n_ext, mtr);

#ifdef UNIV_ZIP_DEBUG
	{
		page_t*		insert_page
			= buf_block_get_frame(insert_block);

		page_zip_des_t*	insert_page_zip
			= buf_block_get_page_zip(insert_block);

		ut_a(!insert_page_zip
		     || page_zip_validate(insert_page_zip, insert_page,
					  cursor->index));
	}
#endif /* UNIV_ZIP_DEBUG */

	if (rec != NULL) {

		goto func_exit;
	}

	/* 8. If insert did not fit, try page reorganization.
	For compressed pages, page_cur_tuple_insert() will have
	attempted this already. */

	if (page_cur_get_page_zip(page_cursor)
	    || !btr_page_reorganize(page_cursor, cursor->index, mtr)) {

		goto insert_failed;
	}

	rec = page_cur_tuple_insert(page_cursor, tuple, cursor->index,
				    offsets, heap, n_ext, mtr);

	if (rec == NULL) {
		/* The insert did not fit on the page: loop back to the
		start of the function for a new split */
insert_failed:
		/* We play safe and reset the free bits for new_page */
		if (!dict_index_is_clust(cursor->index)
		    && !cursor->index->table->is_temporary()) {
			ibuf_reset_free_bits(new_block);
			ibuf_reset_free_bits(block);
		}

		n_iterations++;
		ut_ad(n_iterations < 2
		      || buf_block_get_page_zip(insert_block));
		ut_ad(!insert_will_fit);

		goto func_start;
	}

func_exit:
	/* Insert fit on the page: update the free bits for the
	left and right pages in the same mtr */

	if (!dict_index_is_clust(cursor->index)
	    && !cursor->index->table->is_temporary()
	    && page_is_leaf(page)) {

		ibuf_update_free_bits_for_two_pages_low(
			left_block, right_block, mtr);
	}

	MONITOR_INC(MONITOR_INDEX_SPLIT);

	ut_ad(page_validate(buf_block_get_frame(left_block), cursor->index));
	ut_ad(page_validate(buf_block_get_frame(right_block), cursor->index));

	if (tuple == NULL) {
		ut_ad(rec == NULL);
	}
	ut_ad(!rec || rec_offs_validate(rec, cursor->index, *offsets));
	return(rec);
}

/** Remove a page from the level list of pages.
@param[in]	space		space where removed
@param[in]	zip_size	ROW_FORMAT=COMPRESSED page size, or 0
@param[in,out]	page		page to remove
@param[in]	index		index tree
@param[in,out]	mtr		mini-transaction */
void
btr_level_list_remove_func(
	ulint			space,
	ulint			zip_size,
	page_t*			page,
	dict_index_t*		index,
	mtr_t*			mtr)
{
	ut_ad(page != NULL);
	ut_ad(mtr != NULL);
	ut_ad(mtr_memo_contains_page(mtr, page, MTR_MEMO_PAGE_X_FIX));
	ut_ad(space == page_get_space_id(page));
	/* Get the previous and next page numbers of page */

	const ulint	prev_page_no = btr_page_get_prev(page, mtr);
	const ulint	next_page_no = btr_page_get_next(page, mtr);

	/* Update page links of the level */

	if (prev_page_no != FIL_NULL) {
		buf_block_t*	prev_block
			= btr_block_get(page_id_t(space, prev_page_no),
					zip_size, RW_X_LATCH, index, mtr);

		page_t*		prev_page
			= buf_block_get_frame(prev_block);
#ifdef UNIV_BTR_DEBUG
		ut_a(page_is_comp(prev_page) == page_is_comp(page));
		ut_a(btr_page_get_next(prev_page, mtr)
		     == page_get_page_no(page));
#endif /* UNIV_BTR_DEBUG */

		btr_page_set_next(prev_page,
				  buf_block_get_page_zip(prev_block),
				  next_page_no, mtr);
	}

	if (next_page_no != FIL_NULL) {
		buf_block_t*	next_block
			= btr_block_get(
				page_id_t(space, next_page_no), zip_size,
				RW_X_LATCH, index, mtr);

		page_t*		next_page
			= buf_block_get_frame(next_block);
#ifdef UNIV_BTR_DEBUG
		ut_a(page_is_comp(next_page) == page_is_comp(page));
		ut_a(btr_page_get_prev(next_page, mtr)
		     == page_get_page_no(page));
#endif /* UNIV_BTR_DEBUG */

		btr_page_set_prev(next_page,
				  buf_block_get_page_zip(next_block),
				  prev_page_no, mtr);
	}
}

/****************************************************************//**
Writes the redo log record for setting an index record as the predefined
minimum record. */
UNIV_INLINE
void
btr_set_min_rec_mark_log(
/*=====================*/
	rec_t*		rec,	/*!< in: record */
	mlog_id_t	type,	/*!< in: MLOG_COMP_REC_MIN_MARK or
				MLOG_REC_MIN_MARK */
	mtr_t*		mtr)	/*!< in: mtr */
{
	mlog_write_initial_log_record(rec, type, mtr);

	/* Write rec offset as a 2-byte ulint */
	mlog_catenate_ulint(mtr, page_offset(rec), MLOG_2BYTES);
}

/****************************************************************//**
Parses the redo log record for setting an index record as the predefined
minimum record.
@return end of log record or NULL */
byte*
btr_parse_set_min_rec_mark(
/*=======================*/
	byte*	ptr,	/*!< in: buffer */
	byte*	end_ptr,/*!< in: buffer end */
	ulint	comp,	/*!< in: nonzero=compact page format */
	page_t*	page,	/*!< in: page or NULL */
	mtr_t*	mtr)	/*!< in: mtr or NULL */
{
	rec_t*	rec;

	if (end_ptr < ptr + 2) {

		return(NULL);
	}

	if (page) {
		ut_a(!page_is_comp(page) == !comp);

		rec = page + mach_read_from_2(ptr);

		btr_set_min_rec_mark(rec, mtr);
	}

	return(ptr + 2);
}

/****************************************************************//**
Sets a record as the predefined minimum record. */
void
btr_set_min_rec_mark(
/*=================*/
	rec_t*	rec,	/*!< in: record */
	mtr_t*	mtr)	/*!< in: mtr */
{
	ulint	info_bits;

	if (page_rec_is_comp(rec)) {
		info_bits = rec_get_info_bits(rec, TRUE);

		rec_set_info_bits_new(rec, info_bits | REC_INFO_MIN_REC_FLAG);

		btr_set_min_rec_mark_log(rec, MLOG_COMP_REC_MIN_MARK, mtr);
	} else {
		info_bits = rec_get_info_bits(rec, FALSE);

		rec_set_info_bits_old(rec, info_bits | REC_INFO_MIN_REC_FLAG);

		btr_set_min_rec_mark_log(rec, MLOG_REC_MIN_MARK, mtr);
	}
}

/*************************************************************//**
If page is the only on its level, this function moves its records to the
father page, thus reducing the tree height.
@return father block */
UNIV_INTERN
buf_block_t*
btr_lift_page_up(
/*=============*/
	dict_index_t*	index,	/*!< in: index tree */
	buf_block_t*	block,	/*!< in: page which is the only on its level;
				must not be empty: use
				btr_discard_only_page_on_level if the last
				record from the page should be removed */
	mtr_t*		mtr)	/*!< in: mtr */
{
	buf_block_t*	father_block;
	page_t*		father_page;
	ulint		page_level;
	page_zip_des_t*	father_page_zip;
	page_t*		page		= buf_block_get_frame(block);
	ulint		root_page_no;
	buf_block_t*	blocks[BTR_MAX_LEVELS];
	ulint		n_blocks;	/*!< last used index in blocks[] */
	ulint		i;
	bool		lift_father_up;
	buf_block_t*	block_orig	= block;

	ut_ad(!page_has_siblings(page));
	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	page_level = btr_page_get_level(page);
	root_page_no = dict_index_get_page(index);

	{
		btr_cur_t	cursor;
		ulint*		offsets	= NULL;
		mem_heap_t*	heap	= mem_heap_create(
			sizeof(*offsets)
			* (REC_OFFS_HEADER_SIZE + 1 + 1
			   + unsigned(index->n_fields)));
		buf_block_t*	b;

		if (dict_index_is_spatial(index)) {
			offsets = rtr_page_get_father_block(
				NULL, heap, index, block, mtr,
				NULL, &cursor);
		} else {
			offsets = btr_page_get_father_block(offsets, heap,
							    index, block,
							    mtr, &cursor);
		}
		father_block = btr_cur_get_block(&cursor);
		father_page_zip = buf_block_get_page_zip(father_block);
		father_page = buf_block_get_frame(father_block);

		n_blocks = 0;

		/* Store all ancestor pages so we can reset their
		levels later on.  We have to do all the searches on
		the tree now because later on, after we've replaced
		the first level, the tree is in an inconsistent state
		and can not be searched. */
		for (b = father_block;
		     b->page.id.page_no() != root_page_no; ) {
			ut_a(n_blocks < BTR_MAX_LEVELS);

			if (dict_index_is_spatial(index)) {
				offsets = rtr_page_get_father_block(
					NULL, heap, index, b, mtr,
					NULL, &cursor);
			} else {
				offsets = btr_page_get_father_block(offsets,
								    heap,
								    index, b,
								    mtr,
								    &cursor);
			}

			blocks[n_blocks++] = b = btr_cur_get_block(&cursor);
		}

		lift_father_up = (n_blocks && page_level == 0);
		if (lift_father_up) {
			/* The father page also should be the only on its level (not
			root). We should lift up the father page at first.
			Because the leaf page should be lifted up only for root page.
			The freeing page is based on page_level (==0 or !=0)
			to choose segment. If the page_level is changed ==0 from !=0,
			later freeing of the page doesn't find the page allocation
			to be freed.*/

			block = father_block;
			page = buf_block_get_frame(block);
			page_level = btr_page_get_level(page);

			ut_ad(!page_has_siblings(page));
			ut_ad(mtr_memo_contains(
				      mtr, block, MTR_MEMO_PAGE_X_FIX));

			father_block = blocks[0];
			father_page_zip = buf_block_get_page_zip(father_block);
			father_page = buf_block_get_frame(father_block);
		}

		mem_heap_free(heap);
	}

	btr_search_drop_page_hash_index(block);

	/* Make the father empty */
	btr_page_empty(father_block, father_page_zip, index, page_level, mtr);
	/* btr_page_empty() is supposed to zero-initialize the field. */
	ut_ad(!page_get_instant(father_block->frame));

	if (page_level == 0 && index->is_instant()) {
		ut_ad(!father_page_zip);
		btr_set_instant(father_block, *index, mtr);
	}

	page_level++;

	/* Copy the records to the father page one by one. */
	if (0
#ifdef UNIV_ZIP_COPY
	    || father_page_zip
#endif /* UNIV_ZIP_COPY */
	    || !page_copy_rec_list_end(father_block, block,
				       page_get_infimum_rec(page),
				       index, mtr)) {
		const page_zip_des_t*	page_zip
			= buf_block_get_page_zip(block);
		ut_a(father_page_zip);
		ut_a(page_zip);

		/* Copy the page byte for byte. */
		page_zip_copy_recs(father_page_zip, father_page,
				   page_zip, page, index, mtr);

		/* Update the lock table and possible hash index. */

		lock_move_rec_list_end(father_block, block,
				       page_get_infimum_rec(page));

		/* Also update the predicate locks */
		if (dict_index_is_spatial(index)) {
			lock_prdt_rec_move(father_block, block);
		} else {
			btr_search_move_or_delete_hash_entries(
				father_block, block);
		}
	}

	if (!dict_table_is_locking_disabled(index->table)) {
		/* Free predicate page locks on the block */
		if (dict_index_is_spatial(index)) {
			lock_mutex_enter();
			lock_prdt_page_free_from_discard(
				block, lock_sys.prdt_page_hash);
			lock_mutex_exit();
		}
		lock_update_copy_and_discard(father_block, block);
	}

	/* Go upward to root page, decrementing levels by one. */
	for (i = lift_father_up ? 1 : 0; i < n_blocks; i++, page_level++) {
		page_t*		page	= buf_block_get_frame(blocks[i]);
		page_zip_des_t*	page_zip= buf_block_get_page_zip(blocks[i]);

		ut_ad(btr_page_get_level(page) == page_level + 1);

		btr_page_set_level(page, page_zip, page_level, mtr);
#ifdef UNIV_ZIP_DEBUG
		ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */
	}

	if (dict_index_is_spatial(index)) {
		rtr_check_discard_page(index, NULL, block);
	}

	/* Free the file page */
	btr_page_free(index, block, mtr);

	/* We play it safe and reset the free bits for the father */
	if (!dict_index_is_clust(index)
	    && !index->table->is_temporary()) {
		ibuf_reset_free_bits(father_block);
	}
	ut_ad(page_validate(father_page, index));
	ut_ad(btr_check_node_ptr(index, father_block, mtr));

	return(lift_father_up ? block_orig : father_block);
}

/*************************************************************//**
Tries to merge the page first to the left immediate brother if such a
brother exists, and the node pointers to the current page and to the brother
reside on the same page. If the left brother does not satisfy these
conditions, looks at the right brother. If the page is the only one on that
level lifts the records of the page to the father page, thus reducing the
tree height. It is assumed that mtr holds an x-latch on the tree and on the
page. If cursor is on the leaf level, mtr must also hold x-latches to the
brothers, if they exist.
@return TRUE on success */
ibool
btr_compress(
/*=========*/
	btr_cur_t*	cursor,	/*!< in/out: cursor on the page to merge
				or lift; the page must not be empty:
				when deleting records, use btr_discard_page()
				if the page would become empty */
	ibool		adjust,	/*!< in: TRUE if should adjust the
				cursor position even if compression occurs */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	dict_index_t*	index;
	ulint		left_page_no;
	ulint		right_page_no;
	buf_block_t*	merge_block;
	page_t*		merge_page = NULL;
	page_zip_des_t*	merge_page_zip;
	ibool		is_left;
	buf_block_t*	block;
	page_t*		page;
	btr_cur_t	father_cursor;
	mem_heap_t*	heap;
	ulint*		offsets;
	ulint		nth_rec = 0; /* remove bogus warning */
	bool		mbr_changed = false;
#ifdef UNIV_DEBUG
	bool		leftmost_child;
#endif
	DBUG_ENTER("btr_compress");

	block = btr_cur_get_block(cursor);
	page = btr_cur_get_page(cursor);
	index = btr_cur_get_index(cursor);

	btr_assert_not_corrupted(block, index);

#ifdef UNIV_DEBUG
	if (dict_index_is_spatial(index)) {
		ut_ad(mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
						MTR_MEMO_X_LOCK));
	} else {
		ut_ad(mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
						MTR_MEMO_X_LOCK
						| MTR_MEMO_SX_LOCK));
	}
#endif /* UNIV_DEBUG */

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	const ulint zip_size = index->table->space->zip_size();

	MONITOR_INC(MONITOR_INDEX_MERGE_ATTEMPTS);

	left_page_no = btr_page_get_prev(page, mtr);
	right_page_no = btr_page_get_next(page, mtr);

#ifdef UNIV_DEBUG
	if (!page_is_leaf(page) && left_page_no == FIL_NULL) {
		ut_a(REC_INFO_MIN_REC_FLAG & rec_get_info_bits(
			page_rec_get_next(page_get_infimum_rec(page)),
			page_is_comp(page)));
	}
#endif /* UNIV_DEBUG */

	heap = mem_heap_create(100);

	if (dict_index_is_spatial(index)) {
		offsets = rtr_page_get_father_block(
			NULL, heap, index, block, mtr, cursor, &father_cursor);
		ut_ad(cursor->page_cur.block->page.id.page_no()
		      == block->page.id.page_no());
		rec_t*  my_rec = father_cursor.page_cur.rec;

		ulint page_no = btr_node_ptr_get_child_page_no(my_rec, offsets);

		if (page_no != block->page.id.page_no()) {
			ib::info() << "father positioned on page "
				<< page_no << "instead of "
				<< block->page.id.page_no();
			offsets = btr_page_get_father_block(
				NULL, heap, index, block, mtr, &father_cursor);
		}
	} else {
		offsets = btr_page_get_father_block(
			NULL, heap, index, block, mtr, &father_cursor);
	}

	if (adjust) {
		nth_rec = page_rec_get_n_recs_before(btr_cur_get_rec(cursor));
		ut_ad(nth_rec > 0);
	}

	if (left_page_no == FIL_NULL && right_page_no == FIL_NULL) {
		/* The page is the only one on the level, lift the records
		to the father */

		merge_block = btr_lift_page_up(index, block, mtr);
		goto func_exit;
	}

	ut_d(leftmost_child =
		left_page_no != FIL_NULL
		&& (page_rec_get_next(
			page_get_infimum_rec(
				btr_cur_get_page(&father_cursor)))
		    == btr_cur_get_rec(&father_cursor)));

	/* Decide the page to which we try to merge and which will inherit
	the locks */

	is_left = btr_can_merge_with_page(cursor, left_page_no,
					  &merge_block, mtr);

	DBUG_EXECUTE_IF("ib_always_merge_right", is_left = FALSE;);
retry:
	if (!is_left
	   && !btr_can_merge_with_page(cursor, right_page_no, &merge_block,
				       mtr)) {
		if (!merge_block) {
			merge_page = NULL;
		}
		goto err_exit;
	}

	merge_page = buf_block_get_frame(merge_block);

#ifdef UNIV_BTR_DEBUG
	if (is_left) {
		ut_a(btr_page_get_next(merge_page, mtr)
		     == block->page.id.page_no());
	} else {
		ut_a(btr_page_get_prev(merge_page, mtr)
		     == block->page.id.page_no());
	}
#endif /* UNIV_BTR_DEBUG */

#ifdef UNIV_GIS_DEBUG
	if (dict_index_is_spatial(index)) {
		if (is_left) {
			fprintf(stderr, "GIS_DIAG: merge left  %ld to %ld \n",
				(long) block->page.id.page_no(), left_page_no);
		} else {
			fprintf(stderr, "GIS_DIAG: merge right %ld to %ld\n",
				(long) block->page.id.page_no(), right_page_no);
		}
	}
#endif /* UNIV_GIS_DEBUG */

	ut_ad(page_validate(merge_page, index));

	merge_page_zip = buf_block_get_page_zip(merge_block);
#ifdef UNIV_ZIP_DEBUG
	if (merge_page_zip) {
		const page_zip_des_t*	page_zip
			= buf_block_get_page_zip(block);
		ut_a(page_zip);
		ut_a(page_zip_validate(merge_page_zip, merge_page, index));
		ut_a(page_zip_validate(page_zip, page, index));
	}
#endif /* UNIV_ZIP_DEBUG */

	/* Move records to the merge page */
	if (is_left) {
		btr_cur_t	cursor2;
		rtr_mbr_t	new_mbr;
		ulint*		offsets2 = NULL;

		/* For rtree, we need to update father's mbr. */
		if (dict_index_is_spatial(index)) {
			/* We only support merge pages with the same parent
			page */
			if (!rtr_check_same_block(
				index, &cursor2,
				btr_cur_get_block(&father_cursor),
				merge_block, heap)) {
				is_left = false;
				goto retry;
			}

			/* Set rtr_info for cursor2, since it is
			necessary in recursive page merge. */
			cursor2.rtr_info = cursor->rtr_info;
			cursor2.tree_height = cursor->tree_height;

			offsets2 = rec_get_offsets(
				btr_cur_get_rec(&cursor2), index, NULL,
				page_is_leaf(cursor2.page_cur.block->frame),
				ULINT_UNDEFINED, &heap);

			/* Check if parent entry needs to be updated */
			mbr_changed = rtr_merge_mbr_changed(
				&cursor2, &father_cursor,
				offsets2, offsets, &new_mbr);
		}

		rec_t*	orig_pred = page_copy_rec_list_start(
			merge_block, block, page_get_supremum_rec(page),
			index, mtr);

		if (!orig_pred) {
			goto err_exit;
		}

		btr_search_drop_page_hash_index(block);

		/* Remove the page from the level list */
		btr_level_list_remove(index->table->space_id,
				      zip_size, page, index, mtr);

		if (dict_index_is_spatial(index)) {
			rec_t*  my_rec = father_cursor.page_cur.rec;

			ulint page_no = btr_node_ptr_get_child_page_no(
						my_rec, offsets);

			if (page_no != block->page.id.page_no()) {

				ib::fatal() << "father positioned on "
					<< page_no << " instead of "
					<< block->page.id.page_no();

				ut_ad(0);
			}

			if (mbr_changed) {
#ifdef UNIV_DEBUG
				bool	success = rtr_update_mbr_field(
					&cursor2, offsets2, &father_cursor,
					merge_page, &new_mbr, NULL, mtr);

				ut_ad(success);
#else
				rtr_update_mbr_field(
					&cursor2, offsets2, &father_cursor,
					merge_page, &new_mbr, NULL, mtr);
#endif
			} else {
				rtr_node_ptr_delete(&father_cursor, mtr);
			}

			/* No GAP lock needs to be worrying about */
			lock_mutex_enter();
			lock_prdt_page_free_from_discard(
				block, lock_sys.prdt_page_hash);
			lock_rec_free_all_from_discard_page(block);
			lock_mutex_exit();
		} else {
			btr_cur_node_ptr_delete(&father_cursor, mtr);
			if (!dict_table_is_locking_disabled(index->table)) {
				lock_update_merge_left(
					merge_block, orig_pred, block);
			}
		}

		if (adjust) {
			nth_rec += page_rec_get_n_recs_before(orig_pred);
		}
	} else {
		rec_t*		orig_succ;
		ibool		compressed;
		dberr_t		err;
		btr_cur_t	cursor2;
					/* father cursor pointing to node ptr
					of the right sibling */
#ifdef UNIV_BTR_DEBUG
		byte		fil_page_prev[4];
#endif /* UNIV_BTR_DEBUG */

		if (dict_index_is_spatial(index)) {
			cursor2.rtr_info = NULL;

			/* For spatial index, we disallow merge of blocks
			with different parents, since the merge would need
			to update entry (for MBR and Primary key) in the
			parent of block being merged */
			if (!rtr_check_same_block(
				index, &cursor2,
				btr_cur_get_block(&father_cursor),
				merge_block, heap)) {
				goto err_exit;
			}

			/* Set rtr_info for cursor2, since it is
			necessary in recursive page merge. */
			cursor2.rtr_info = cursor->rtr_info;
			cursor2.tree_height = cursor->tree_height;
		} else {
			btr_page_get_father(index, merge_block, mtr, &cursor2);
		}

		if (merge_page_zip && left_page_no == FIL_NULL) {

			/* The function page_zip_compress(), which will be
			invoked by page_copy_rec_list_end() below,
			requires that FIL_PAGE_PREV be FIL_NULL.
			Clear the field, but prepare to restore it. */
#ifdef UNIV_BTR_DEBUG
			memcpy(fil_page_prev, merge_page + FIL_PAGE_PREV, 4);
#endif /* UNIV_BTR_DEBUG */
			compile_time_assert(FIL_NULL == 0xffffffffU);
			memset(merge_page + FIL_PAGE_PREV, 0xff, 4);
		}

		orig_succ = page_copy_rec_list_end(merge_block, block,
						   page_get_infimum_rec(page),
						   cursor->index, mtr);

		if (!orig_succ) {
			ut_a(merge_page_zip);
#ifdef UNIV_BTR_DEBUG
			if (left_page_no == FIL_NULL) {
				/* FIL_PAGE_PREV was restored from
				merge_page_zip. */
				ut_a(!memcmp(fil_page_prev,
					     merge_page + FIL_PAGE_PREV, 4));
			}
#endif /* UNIV_BTR_DEBUG */
			goto err_exit;
		}

		btr_search_drop_page_hash_index(block);

#ifdef UNIV_BTR_DEBUG
		if (merge_page_zip && left_page_no == FIL_NULL) {

			/* Restore FIL_PAGE_PREV in order to avoid an assertion
			failure in btr_level_list_remove(), which will set
			the field again to FIL_NULL.  Even though this makes
			merge_page and merge_page_zip inconsistent for a
			split second, it is harmless, because the pages
			are X-latched. */
			memcpy(merge_page + FIL_PAGE_PREV, fil_page_prev, 4);
		}
#endif /* UNIV_BTR_DEBUG */

		/* Remove the page from the level list */
		btr_level_list_remove(index->table->space_id,
				      zip_size, page, index, mtr);

		ut_ad(btr_node_ptr_get_child_page_no(
			btr_cur_get_rec(&father_cursor), offsets)
			== block->page.id.page_no());

		/* Replace the address of the old child node (= page) with the
		address of the merge page to the right */
		btr_node_ptr_set_child_page_no(
			btr_cur_get_rec(&father_cursor),
			btr_cur_get_page_zip(&father_cursor),
			offsets, right_page_no, mtr);

#ifdef UNIV_DEBUG
		if (!page_is_leaf(page) && left_page_no == FIL_NULL) {
			ut_ad(REC_INFO_MIN_REC_FLAG & rec_get_info_bits(
				page_rec_get_next(page_get_infimum_rec(
					buf_block_get_frame(merge_block))),
				page_is_comp(page)));
		}
#endif /* UNIV_DEBUG */

		/* For rtree, we need to update father's mbr. */
		if (dict_index_is_spatial(index)) {
			ulint*	offsets2;
			ulint	rec_info;

			offsets2 = rec_get_offsets(
				btr_cur_get_rec(&cursor2), index, NULL,
				page_is_leaf(cursor2.page_cur.block->frame),
				ULINT_UNDEFINED, &heap);

			ut_ad(btr_node_ptr_get_child_page_no(
				btr_cur_get_rec(&cursor2), offsets2)
				== right_page_no);

			rec_info = rec_get_info_bits(
				btr_cur_get_rec(&father_cursor),
				rec_offs_comp(offsets));
			if (rec_info & REC_INFO_MIN_REC_FLAG) {
				/* When the father node ptr is minimal rec,
				we will keep it and delete the node ptr of
				merge page. */
				rtr_merge_and_update_mbr(&father_cursor,
							 &cursor2,
							 offsets, offsets2,
							 merge_page, mtr);
			} else {
				/* Otherwise, we will keep the node ptr of
				merge page and delete the father node ptr.
				This is for keeping the rec order in upper
				level. */
				rtr_merge_and_update_mbr(&cursor2,
							 &father_cursor,
							 offsets2, offsets,
							 merge_page, mtr);
			}
			lock_mutex_enter();
			lock_prdt_page_free_from_discard(
				block, lock_sys.prdt_page_hash);
			lock_rec_free_all_from_discard_page(block);
			lock_mutex_exit();
		} else {

			compressed = btr_cur_pessimistic_delete(&err, TRUE,
								&cursor2,
								BTR_CREATE_FLAG,
								false, mtr);
			ut_a(err == DB_SUCCESS);

			if (!compressed) {
				btr_cur_compress_if_useful(&cursor2,
							   FALSE,
							   mtr);
			}

			if (!dict_table_is_locking_disabled(index->table)) {
				lock_update_merge_right(
					merge_block, orig_succ, block);
			}
		}
	}

	if (!dict_index_is_clust(index)
	    && !index->table->is_temporary()
	    && page_is_leaf(merge_page)) {
		/* Update the free bits of the B-tree page in the
		insert buffer bitmap.  This has to be done in a
		separate mini-transaction that is committed before the
		main mini-transaction.  We cannot update the insert
		buffer bitmap in this mini-transaction, because
		btr_compress() can be invoked recursively without
		committing the mini-transaction in between.  Since
		insert buffer bitmap pages have a lower rank than
		B-tree pages, we must not access other pages in the
		same mini-transaction after accessing an insert buffer
		bitmap page. */

		/* The free bits in the insert buffer bitmap must
		never exceed the free space on a page.  It is safe to
		decrement or reset the bits in the bitmap in a
		mini-transaction that is committed before the
		mini-transaction that affects the free space. */

		/* It is unsafe to increment the bits in a separately
		committed mini-transaction, because in crash recovery,
		the free bits could momentarily be set too high. */

		if (zip_size) {
			/* Because the free bits may be incremented
			and we cannot update the insert buffer bitmap
			in the same mini-transaction, the only safe
			thing we can do here is the pessimistic
			approach: reset the free bits. */
			ibuf_reset_free_bits(merge_block);
		} else {
			/* On uncompressed pages, the free bits will
			never increase here.  Thus, it is safe to
			write the bits accurately in a separate
			mini-transaction. */
			ibuf_update_free_bits_if_full(merge_block,
						      srv_page_size,
						      ULINT_UNDEFINED);
		}
	}

	ut_ad(page_validate(merge_page, index));
#ifdef UNIV_ZIP_DEBUG
	ut_a(!merge_page_zip || page_zip_validate(merge_page_zip, merge_page,
						  index));
#endif /* UNIV_ZIP_DEBUG */

	if (dict_index_is_spatial(index)) {
#ifdef UNIV_GIS_DEBUG
		fprintf(stderr, "GIS_DIAG: compressed away  %ld\n",
			(long) block->page.id.page_no());
		fprintf(stderr, "GIS_DIAG: merged to %ld\n",
			(long) merge_block->page.id.page_no());
#endif

		rtr_check_discard_page(index, NULL, block);
	}

	/* Free the file page */
	btr_page_free(index, block, mtr);

	/* btr_check_node_ptr() needs parent block latched.
	If the merge_block's parent block is not same,
	we cannot use btr_check_node_ptr() */
	ut_ad(leftmost_child
	      || btr_check_node_ptr(index, merge_block, mtr));
func_exit:
	mem_heap_free(heap);

	if (adjust) {
		ut_ad(nth_rec > 0);
		btr_cur_position(
			index,
			page_rec_get_nth(merge_block->frame, nth_rec),
			merge_block, cursor);
	}

	MONITOR_INC(MONITOR_INDEX_MERGE_SUCCESSFUL);

	DBUG_RETURN(TRUE);

err_exit:
	/* We play it safe and reset the free bits. */
	if (zip_size
	    && merge_page
	    && page_is_leaf(merge_page)
	    && !dict_index_is_clust(index)) {

		ibuf_reset_free_bits(merge_block);
	}

	mem_heap_free(heap);
	DBUG_RETURN(FALSE);
}

/*************************************************************//**
Discards a page that is the only page on its level.  This will empty
the whole B-tree, leaving just an empty root page.  This function
should almost never be reached, because btr_compress(), which is invoked in
delete operations, calls btr_lift_page_up() to flatten the B-tree. */
ATTRIBUTE_COLD
static
void
btr_discard_only_page_on_level(
/*===========================*/
	dict_index_t*	index,	/*!< in: index tree */
	buf_block_t*	block,	/*!< in: page which is the only on its level */
	mtr_t*		mtr)	/*!< in: mtr */
{
	ulint		page_level = 0;
	trx_id_t	max_trx_id;

	ut_ad(!index->is_dummy);

	/* Save the PAGE_MAX_TRX_ID from the leaf page. */
	max_trx_id = page_get_max_trx_id(buf_block_get_frame(block));

	while (block->page.id.page_no() != dict_index_get_page(index)) {
		btr_cur_t	cursor;
		buf_block_t*	father;
		const page_t*	page	= buf_block_get_frame(block);

		ut_a(page_get_n_recs(page) == 1);
		ut_a(page_level == btr_page_get_level(page));
		ut_a(!page_has_siblings(page));
		ut_ad(fil_page_index_page_check(page));
		ut_ad(block->page.id.space() == index->table->space->id);
		ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
		btr_search_drop_page_hash_index(block);

		if (dict_index_is_spatial(index)) {
			/* Check any concurrent search having this page */
			rtr_check_discard_page(index, NULL, block);
			rtr_page_get_father(index, block, mtr, NULL, &cursor);
		} else {
			btr_page_get_father(index, block, mtr, &cursor);
		}
		father = btr_cur_get_block(&cursor);

		if (!dict_table_is_locking_disabled(index->table)) {
			lock_update_discard(
				father, PAGE_HEAP_NO_SUPREMUM, block);
		}

		/* Free the file page */
		btr_page_free(index, block, mtr);

		block = father;
		page_level++;
	}

	/* block is the root page, which must be empty, except
	for the node pointer to the (now discarded) block(s). */
	ut_ad(!page_has_siblings(block->frame));

#ifdef UNIV_BTR_DEBUG
	if (!dict_index_is_ibuf(index)) {
		const page_t*	root	= buf_block_get_frame(block);
		const ulint	space	= index->table->space_id;
		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_LEAF
					    + root, space));
		ut_a(btr_root_fseg_validate(FIL_PAGE_DATA + PAGE_BTR_SEG_TOP
					    + root, space));
	}
#endif /* UNIV_BTR_DEBUG */

	mem_heap_t* heap = NULL;
	const rec_t* rec = NULL;
	ulint* offsets = NULL;
	if (index->table->instant) {
		const rec_t* r = page_rec_get_next(page_get_infimum_rec(
							   block->frame));
		ut_ad(rec_is_metadata(r, *index) == index->is_instant());
		if (rec_is_alter_metadata(r, *index)) {
			heap = mem_heap_create(srv_page_size);
			offsets = rec_get_offsets(r, index, NULL, true,
						  ULINT_UNDEFINED, &heap);
			rec = rec_copy(mem_heap_alloc(heap,
						      rec_offs_size(offsets)),
				       r, offsets);
			rec_offs_make_valid(rec, index, true, offsets);
		}
	}

	btr_page_empty(block, buf_block_get_page_zip(block), index, 0, mtr);
	ut_ad(page_is_leaf(buf_block_get_frame(block)));
	/* btr_page_empty() is supposed to zero-initialize the field. */
	ut_ad(!page_get_instant(block->frame));

	if (index->is_primary()) {
		if (rec) {
			DBUG_ASSERT(index->table->instant);
			DBUG_ASSERT(rec_is_alter_metadata(rec, *index));
			btr_set_instant(block, *index, mtr);
			rec = page_cur_insert_rec_low(
				page_get_infimum_rec(block->frame),
				index, rec, offsets, mtr);
			ut_ad(rec);
			mem_heap_free(heap);
		} else if (index->is_instant()) {
			index->clear_instant_add();
		}
	} else if (!index->table->is_temporary()) {
		/* We play it safe and reset the free bits for the root */
		ibuf_reset_free_bits(block);

		ut_a(max_trx_id);
		page_set_max_trx_id(block,
				    buf_block_get_page_zip(block),
				    max_trx_id, mtr);
	}
}

/*************************************************************//**
Discards a page from a B-tree. This is used to remove the last record from
a B-tree page: the whole page must be removed at the same time. This cannot
be used for the root page, which is allowed to be empty. */
void
btr_discard_page(
/*=============*/
	btr_cur_t*	cursor,	/*!< in: cursor on the page to discard: not on
				the root page */
	mtr_t*		mtr)	/*!< in: mtr */
{
	dict_index_t*	index;
	ulint		left_page_no;
	ulint		right_page_no;
	buf_block_t*	merge_block;
	page_t*		merge_page;
	buf_block_t*	block;
	page_t*		page;
	rec_t*		node_ptr;
	btr_cur_t	parent_cursor;

	block = btr_cur_get_block(cursor);
	index = btr_cur_get_index(cursor);

	ut_ad(dict_index_get_page(index) != block->page.id.page_no());

	ut_ad(mtr_memo_contains_flagged(mtr, dict_index_get_lock(index),
					MTR_MEMO_X_LOCK | MTR_MEMO_SX_LOCK));

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	MONITOR_INC(MONITOR_INDEX_DISCARD);

	if (dict_index_is_spatial(index)) {
		rtr_page_get_father(index, block, mtr, cursor, &parent_cursor);
	} else {
		btr_page_get_father(index, block, mtr, &parent_cursor);
	}

	/* Decide the page which will inherit the locks */

	left_page_no = btr_page_get_prev(buf_block_get_frame(block), mtr);
	right_page_no = btr_page_get_next(buf_block_get_frame(block), mtr);

	const ulint zip_size = index->table->space->zip_size();
	ut_d(bool parent_is_different = false);
	if (left_page_no != FIL_NULL) {
		merge_block = btr_block_get(
			page_id_t(index->table->space_id, left_page_no),
			zip_size, RW_X_LATCH, index, mtr);

		merge_page = buf_block_get_frame(merge_block);
#ifdef UNIV_BTR_DEBUG
		ut_a(btr_page_get_next(merge_page, mtr)
		     == block->page.id.page_no());
#endif /* UNIV_BTR_DEBUG */
		ut_d(parent_is_different =
			(page_rec_get_next(
				page_get_infimum_rec(
					btr_cur_get_page(
						&parent_cursor)))
			 == btr_cur_get_rec(&parent_cursor)));
	} else if (right_page_no != FIL_NULL) {
		merge_block = btr_block_get(
			page_id_t(index->table->space_id, right_page_no),
			zip_size, RW_X_LATCH, index, mtr);

		merge_page = buf_block_get_frame(merge_block);
#ifdef UNIV_BTR_DEBUG
		ut_a(btr_page_get_prev(merge_page, mtr)
		     == block->page.id.page_no());
#endif /* UNIV_BTR_DEBUG */
		ut_d(parent_is_different = page_rec_is_supremum(
			page_rec_get_next(btr_cur_get_rec(&parent_cursor))));
	} else {
		btr_discard_only_page_on_level(index, block, mtr);

		return;
	}

	page = buf_block_get_frame(block);
	ut_a(page_is_comp(merge_page) == page_is_comp(page));
	btr_search_drop_page_hash_index(block);

	if (left_page_no == FIL_NULL && !page_is_leaf(page)) {

		/* We have to mark the leftmost node pointer on the right
		side page as the predefined minimum record */
		node_ptr = page_rec_get_next(page_get_infimum_rec(merge_page));

		ut_ad(page_rec_is_user_rec(node_ptr));

		/* This will make page_zip_validate() fail on merge_page
		until btr_level_list_remove() completes.  This is harmless,
		because everything will take place within a single
		mini-transaction and because writing to the redo log
		is an atomic operation (performed by mtr_commit()). */
		btr_set_min_rec_mark(node_ptr, mtr);
	}

	if (dict_index_is_spatial(index)) {
		rtr_node_ptr_delete(&parent_cursor, mtr);
	} else {
		btr_cur_node_ptr_delete(&parent_cursor, mtr);
	}

	/* Remove the page from the level list */
	btr_level_list_remove(index->table->space_id, zip_size,
			      page, index, mtr);

#ifdef UNIV_ZIP_DEBUG
	{
		page_zip_des_t*	merge_page_zip
			= buf_block_get_page_zip(merge_block);
		ut_a(!merge_page_zip
		     || page_zip_validate(merge_page_zip, merge_page, index));
	}
#endif /* UNIV_ZIP_DEBUG */

	if (!dict_table_is_locking_disabled(index->table)) {
		if (left_page_no != FIL_NULL) {
			lock_update_discard(merge_block, PAGE_HEAP_NO_SUPREMUM,
					    block);
		} else {
			lock_update_discard(merge_block,
					    lock_get_min_heap_no(merge_block),
					    block);
		}
	}

	if (dict_index_is_spatial(index)) {
		rtr_check_discard_page(index, cursor, block);
	}

	/* Free the file page */
	btr_page_free(index, block, mtr);

	/* btr_check_node_ptr() needs parent block latched.
	If the merge_block's parent block is not same,
	we cannot use btr_check_node_ptr() */
	ut_ad(parent_is_different
	      || btr_check_node_ptr(index, merge_block, mtr));

	if (btr_cur_get_block(&parent_cursor)->page.id.page_no() == index->page
	    && !page_has_siblings(btr_cur_get_page(&parent_cursor))
	    && page_get_n_recs(btr_cur_get_page(&parent_cursor)) == 1) {
		btr_lift_page_up(index, merge_block, mtr);
	}
}

#ifdef UNIV_BTR_PRINT
/*************************************************************//**
Prints size info of a B-tree. */
void
btr_print_size(
/*===========*/
	dict_index_t*	index)	/*!< in: index tree */
{
	page_t*		root;
	fseg_header_t*	seg;
	mtr_t		mtr;

	if (dict_index_is_ibuf(index)) {
		fputs("Sorry, cannot print info of an ibuf tree:"
		      " use ibuf functions\n", stderr);

		return;
	}

	mtr_start(&mtr);

	root = btr_root_get(index, &mtr);

	seg = root + PAGE_HEADER + PAGE_BTR_SEG_TOP;

	fputs("INFO OF THE NON-LEAF PAGE SEGMENT\n", stderr);
	fseg_print(seg, &mtr);

	if (!dict_index_is_ibuf(index)) {

		seg = root + PAGE_HEADER + PAGE_BTR_SEG_LEAF;

		fputs("INFO OF THE LEAF PAGE SEGMENT\n", stderr);
		fseg_print(seg, &mtr);
	}

	mtr_commit(&mtr);
}

/************************************************************//**
Prints recursively index tree pages. */
static
void
btr_print_recursive(
/*================*/
	dict_index_t*	index,	/*!< in: index tree */
	buf_block_t*	block,	/*!< in: index page */
	ulint		width,	/*!< in: print this many entries from start
				and end */
	mem_heap_t**	heap,	/*!< in/out: heap for rec_get_offsets() */
	ulint**		offsets,/*!< in/out: buffer for rec_get_offsets() */
	mtr_t*		mtr)	/*!< in: mtr */
{
	const page_t*	page	= buf_block_get_frame(block);
	page_cur_t	cursor;
	ulint		n_recs;
	ulint		i	= 0;
	mtr_t		mtr2;

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_SX_FIX));

	ib::info() << "NODE ON LEVEL " << btr_page_get_level(page)
		<< " page " << block->page.id;

	page_print(block, index, width, width);

	n_recs = page_get_n_recs(page);

	page_cur_set_before_first(block, &cursor);
	page_cur_move_to_next(&cursor);

	while (!page_cur_is_after_last(&cursor)) {

		if (page_is_leaf(page)) {

			/* If this is the leaf level, do nothing */

		} else if ((i <= width) || (i >= n_recs - width)) {

			const rec_t*	node_ptr;

			mtr_start(&mtr2);

			node_ptr = page_cur_get_rec(&cursor);

			*offsets = rec_get_offsets(
				node_ptr, index, *offsets, false,
				ULINT_UNDEFINED, heap);
			btr_print_recursive(index,
					    btr_node_ptr_get_child(node_ptr,
								   index,
								   *offsets,
								   &mtr2),
					    width, heap, offsets, &mtr2);
			mtr_commit(&mtr2);
		}

		page_cur_move_to_next(&cursor);
		i++;
	}
}

/**************************************************************//**
Prints directories and other info of all nodes in the tree. */
void
btr_print_index(
/*============*/
	dict_index_t*	index,	/*!< in: index */
	ulint		width)	/*!< in: print this many entries from start
				and end */
{
	mtr_t		mtr;
	buf_block_t*	root;
	mem_heap_t*	heap	= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets	= offsets_;
	rec_offs_init(offsets_);

	fputs("--------------------------\n"
	      "INDEX TREE PRINT\n", stderr);

	mtr_start(&mtr);

	root = btr_root_block_get(index, RW_SX_LATCH, &mtr);

	btr_print_recursive(index, root, width, &heap, &offsets, &mtr);
	if (heap) {
		mem_heap_free(heap);
	}

	mtr_commit(&mtr);

	ut_ad(btr_validate_index(index, 0));
}
#endif /* UNIV_BTR_PRINT */

#ifdef UNIV_DEBUG
/************************************************************//**
Checks that the node pointer to a page is appropriate.
@return TRUE */
ibool
btr_check_node_ptr(
/*===============*/
	dict_index_t*	index,	/*!< in: index tree */
	buf_block_t*	block,	/*!< in: index page */
	mtr_t*		mtr)	/*!< in: mtr */
{
	mem_heap_t*	heap;
	dtuple_t*	tuple;
	ulint*		offsets;
	btr_cur_t	cursor;
	page_t*		page = buf_block_get_frame(block);

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));

	if (dict_index_get_page(index) == block->page.id.page_no()) {

		return(TRUE);
	}

	heap = mem_heap_create(256);

	if (dict_index_is_spatial(index)) {
		offsets = rtr_page_get_father_block(NULL, heap, index, block, mtr,
						    NULL, &cursor);
	} else {
		offsets = btr_page_get_father_block(NULL, heap, index, block, mtr,
						    &cursor);
	}

	if (page_is_leaf(page)) {

		goto func_exit;
	}

	tuple = dict_index_build_node_ptr(
		index, page_rec_get_next(page_get_infimum_rec(page)), 0, heap,
		btr_page_get_level(page));

	/* For spatial index, the MBR in the parent rec could be different
	with that of first rec of child, their relationship should be
	"WITHIN" relationship */
	if (dict_index_is_spatial(index)) {
		ut_a(!cmp_dtuple_rec_with_gis(
			tuple, btr_cur_get_rec(&cursor),
			offsets, PAGE_CUR_WITHIN));
	} else {
		ut_a(!cmp_dtuple_rec(tuple, btr_cur_get_rec(&cursor), offsets));
	}
func_exit:
	mem_heap_free(heap);

	return(TRUE);
}
#endif /* UNIV_DEBUG */

/************************************************************//**
Display identification information for a record. */
static
void
btr_index_rec_validate_report(
/*==========================*/
	const page_t*		page,	/*!< in: index page */
	const rec_t*		rec,	/*!< in: index record */
	const dict_index_t*	index)	/*!< in: index */
{
	ib::info() << "Record in index " << index->name
		<< " of table " << index->table->name
		<< ", page " << page_id_t(page_get_space_id(page),
					  page_get_page_no(page))
		<< ", at offset " << page_offset(rec);
}

/************************************************************//**
Checks the size and number of fields in a record based on the definition of
the index.
@return TRUE if ok */
ibool
btr_index_rec_validate(
/*===================*/
	const rec_t*		rec,		/*!< in: index record */
	const dict_index_t*	index,		/*!< in: index */
	ibool			dump_on_error)	/*!< in: TRUE if the function
						should print hex dump of record
						and page on error */
{
	ulint		len;
	const page_t*	page;
	mem_heap_t*	heap	= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets	= offsets_;
	rec_offs_init(offsets_);

	page = page_align(rec);

	if (dict_index_is_ibuf(index)) {
		/* The insert buffer index tree can contain records from any
		other index: we cannot check the number of fields or
		their length */

		return(TRUE);
	}

#ifdef VIRTUAL_INDEX_DEBUG
	if (dict_index_has_virtual(index)) {
		fprintf(stderr, "index name is %s\n", index->name());
	}
#endif
	if ((ibool)!!page_is_comp(page) != dict_table_is_comp(index->table)) {
		btr_index_rec_validate_report(page, rec, index);

		ib::error() << "Compact flag=" << !!page_is_comp(page)
			<< ", should be " << dict_table_is_comp(index->table);

		return(FALSE);
	}

	const bool is_alter_metadata = page_is_leaf(page)
		&& !page_has_prev(page)
		&& index->is_primary() && index->table->instant
		&& rec == page_rec_get_next_const(page_get_infimum_rec(page));

	if (is_alter_metadata
	    && !rec_is_alter_metadata(rec, page_is_comp(page))) {
		btr_index_rec_validate_report(page, rec, index);

		ib::error() << "First record is not ALTER TABLE metadata";
		return FALSE;
	}

	if (!page_is_comp(page)) {
		const ulint n_rec_fields = rec_get_n_fields_old(rec);
		if (n_rec_fields == DICT_FLD__SYS_INDEXES__MERGE_THRESHOLD
		    && index->id == DICT_INDEXES_ID) {
			/* A record for older SYS_INDEXES table
			(missing merge_threshold column) is acceptable. */
		} else if (is_alter_metadata) {
			if (n_rec_fields != ulint(index->n_fields) + 1) {
				goto n_field_mismatch;
			}
		} else if (n_rec_fields < index->n_core_fields
			   || n_rec_fields > index->n_fields) {
n_field_mismatch:
			btr_index_rec_validate_report(page, rec, index);

			ib::error() << "Has " << rec_get_n_fields_old(rec)
				    << " fields, should have "
				    << index->n_core_fields << ".."
				    << index->n_fields;

			if (dump_on_error) {
				fputs("InnoDB: corrupt record ", stderr);
				rec_print_old(stderr, rec);
				putc('\n', stderr);
			}
			return(FALSE);
		}
	}

	offsets = rec_get_offsets(rec, index, offsets, page_is_leaf(page),
				  ULINT_UNDEFINED, &heap);
	const dict_field_t* field = index->fields;
	ut_ad(rec_offs_n_fields(offsets)
	      == ulint(index->n_fields) + is_alter_metadata);

	for (unsigned i = 0; i < rec_offs_n_fields(offsets); i++) {
		rec_get_nth_field_offs(offsets, i, &len);

		ulint fixed_size;

		if (is_alter_metadata && i == index->first_user_field()) {
			fixed_size = FIELD_REF_SIZE;
			if (len != FIELD_REF_SIZE
			    || !rec_offs_nth_extern(offsets, i)) {
				goto len_mismatch;
			}

			continue;
		} else {
			fixed_size = dict_col_get_fixed_size(
				field->col, page_is_comp(page));
		}

		/* Note that if fixed_size != 0, it equals the
		length of a fixed-size column in the clustered index.
		We should adjust it here.
		A prefix index of the column is of fixed, but different
		length.  When fixed_size == 0, prefix_len is the maximum
		length of the prefix index column. */

		if (len_is_stored(len)
		    && (field->prefix_len
			? len > field->prefix_len
			: (fixed_size && len != fixed_size))) {
len_mismatch:
			btr_index_rec_validate_report(page, rec, index);
			ib::error	error;

			error << "Field " << i << " len is " << len
				<< ", should be " << fixed_size;

			if (dump_on_error) {
				error << "; ";
				rec_print(error.m_oss, rec,
					  rec_get_info_bits(
						  rec, rec_offs_comp(offsets)),
					  offsets);
			}
			if (heap) {
				mem_heap_free(heap);
			}
			return(FALSE);
		}

		field++;
	}

#ifdef VIRTUAL_INDEX_DEBUG
	if (dict_index_has_virtual(index)) {
		rec_print_new(stderr, rec, offsets);
	}
#endif

	if (heap) {
		mem_heap_free(heap);
	}
	return(TRUE);
}

/************************************************************//**
Checks the size and number of fields in records based on the definition of
the index.
@return TRUE if ok */
static
ibool
btr_index_page_validate(
/*====================*/
	buf_block_t*	block,	/*!< in: index page */
	dict_index_t*	index)	/*!< in: index */
{
	page_cur_t	cur;
	ibool		ret	= TRUE;
#ifndef DBUG_OFF
	ulint		nth	= 1;
#endif /* !DBUG_OFF */

	page_cur_set_before_first(block, &cur);

	/* Directory slot 0 should only contain the infimum record. */
	DBUG_EXECUTE_IF("check_table_rec_next",
			ut_a(page_rec_get_nth_const(
				     page_cur_get_page(&cur), 0)
			     == cur.rec);
			ut_a(page_dir_slot_get_n_owned(
				     page_dir_get_nth_slot(
					     page_cur_get_page(&cur), 0))
			     == 1););

	page_cur_move_to_next(&cur);

	for (;;) {
		if (page_cur_is_after_last(&cur)) {

			break;
		}

		if (!btr_index_rec_validate(cur.rec, index, TRUE)) {

			return(FALSE);
		}

		/* Verify that page_rec_get_nth_const() is correctly
		retrieving each record. */
		DBUG_EXECUTE_IF("check_table_rec_next",
				ut_a(cur.rec == page_rec_get_nth_const(
					     page_cur_get_page(&cur),
					     page_rec_get_n_recs_before(
						     cur.rec)));
				ut_a(nth++ == page_rec_get_n_recs_before(
					     cur.rec)););

		page_cur_move_to_next(&cur);
	}

	return(ret);
}

/************************************************************//**
Report an error on one page of an index tree. */
static
void
btr_validate_report1(
/*=================*/
	dict_index_t*		index,	/*!< in: index */
	ulint			level,	/*!< in: B-tree level */
	const buf_block_t*	block)	/*!< in: index page */
{
	ib::error	error;
	error << "In page " << block->page.id.page_no()
		<< " of index " << index->name
		<< " of table " << index->table->name;

	if (level > 0) {
		error << ", index tree level " << level;
	}
}

/************************************************************//**
Report an error on two pages of an index tree. */
static
void
btr_validate_report2(
/*=================*/
	const dict_index_t*	index,	/*!< in: index */
	ulint			level,	/*!< in: B-tree level */
	const buf_block_t*	block1,	/*!< in: first index page */
	const buf_block_t*	block2)	/*!< in: second index page */
{
	ib::error	error;
	error << "In pages " << block1->page.id
		<< " and " << block2->page.id << " of index " << index->name
		<< " of table " << index->table->name;

	if (level > 0) {
		error << ", index tree level " << level;
	}
}

/************************************************************//**
Validates index tree level.
@return TRUE if ok */
static
bool
btr_validate_level(
/*===============*/
	dict_index_t*	index,	/*!< in: index tree */
	const trx_t*	trx,	/*!< in: transaction or NULL */
	ulint		level,	/*!< in: level number */
	bool		lockout)/*!< in: true if X-latch index is intended */
{
	buf_block_t*	block;
	page_t*		page;
	buf_block_t*	right_block = 0; /* remove warning */
	page_t*		right_page = 0; /* remove warning */
	page_t*		father_page;
	btr_cur_t	node_cur;
	btr_cur_t	right_node_cur;
	rec_t*		rec;
	ulint		right_page_no;
	ulint		left_page_no;
	page_cur_t	cursor;
	dtuple_t*	node_ptr_tuple;
	bool		ret	= true;
	mtr_t		mtr;
	mem_heap_t*	heap	= mem_heap_create(256);
	ulint*		offsets	= NULL;
	ulint*		offsets2= NULL;
#ifdef UNIV_ZIP_DEBUG
	page_zip_des_t*	page_zip;
#endif /* UNIV_ZIP_DEBUG */
	ulint		savepoint = 0;
	ulint		savepoint2 = 0;
	ulint		parent_page_no = FIL_NULL;
	ulint		parent_right_page_no = FIL_NULL;
	bool		rightmost_child = false;

	mtr_start(&mtr);

	if (!srv_read_only_mode) {
		if (lockout) {
			mtr_x_lock(dict_index_get_lock(index), &mtr);
		} else {
			mtr_sx_lock(dict_index_get_lock(index), &mtr);
		}
	}

	block = btr_root_block_get(index, RW_SX_LATCH, &mtr);
	page = buf_block_get_frame(block);

	fil_space_t*		space	= index->table->space;
	const ulint		zip_size = space->zip_size();

	while (level != btr_page_get_level(page)) {
		const rec_t*	node_ptr;

		if (fseg_page_is_free(space, block->page.id.page_no())) {

			btr_validate_report1(index, level, block);

			ib::warn() << "Page is free";

			ret = false;
		}

		ut_a(index->table->space_id == block->page.id.space());
		ut_a(block->page.id.space() == page_get_space_id(page));
#ifdef UNIV_ZIP_DEBUG
		page_zip = buf_block_get_page_zip(block);
		ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */
		ut_a(!page_is_leaf(page));

		page_cur_set_before_first(block, &cursor);
		page_cur_move_to_next(&cursor);

		node_ptr = page_cur_get_rec(&cursor);
		offsets = rec_get_offsets(node_ptr, index, offsets, false,
					  ULINT_UNDEFINED, &heap);

		savepoint2 = mtr_set_savepoint(&mtr);
		block = btr_node_ptr_get_child(node_ptr, index, offsets, &mtr);
		page = buf_block_get_frame(block);

		/* For R-Tree, since record order might not be the same as
		linked index page in the lower level, we need to travers
		backwards to get the first page rec in this level.
		This is only used for index validation. Spatial index
		does not use such scan for any of its DML or query
		operations  */
		if (dict_index_is_spatial(index)) {
			left_page_no = btr_page_get_prev(page, &mtr);

			while (left_page_no != FIL_NULL) {
				/* To obey latch order of tree blocks,
				we should release the right_block once to
				obtain lock of the uncle block. */
				mtr_release_block_at_savepoint(
					&mtr, savepoint2, block);

				savepoint2 = mtr_set_savepoint(&mtr);
				block = btr_block_get(
					page_id_t(index->table->space_id,
						  left_page_no),
					zip_size,
					RW_SX_LATCH, index, &mtr);
				page = buf_block_get_frame(block);
				left_page_no = btr_page_get_prev(page, &mtr);
			}
		}
	}

	/* Now we are on the desired level. Loop through the pages on that
	level. */

loop:
	mem_heap_empty(heap);
	offsets = offsets2 = NULL;
	if (!srv_read_only_mode) {
		if (lockout) {
			mtr_x_lock(dict_index_get_lock(index), &mtr);
		} else {
			mtr_sx_lock(dict_index_get_lock(index), &mtr);
		}
	}

#ifdef UNIV_ZIP_DEBUG
	page_zip = buf_block_get_page_zip(block);
	ut_a(!page_zip || page_zip_validate(page_zip, page, index));
#endif /* UNIV_ZIP_DEBUG */

	ut_a(block->page.id.space() == index->table->space_id);

	if (fseg_page_is_free(space, block->page.id.page_no())) {

		btr_validate_report1(index, level, block);

		ib::warn() << "Page is marked as free";
		ret = false;

	} else if (btr_page_get_index_id(page) != index->id) {

		ib::error() << "Page index id " << btr_page_get_index_id(page)
			<< " != data dictionary index id " << index->id;

		ret = false;

	} else if (!page_validate(page, index)) {

		btr_validate_report1(index, level, block);
		ret = false;

	} else if (level == 0 && !btr_index_page_validate(block, index)) {

		/* We are on level 0. Check that the records have the right
		number of fields, and field lengths are right. */

		ret = false;
	}

	ut_a(btr_page_get_level(page) == level);

	right_page_no = btr_page_get_next(page, &mtr);
	left_page_no = btr_page_get_prev(page, &mtr);

	ut_a(!page_is_empty(page)
	     || (level == 0
		 && page_get_page_no(page) == dict_index_get_page(index)));

	if (right_page_no != FIL_NULL) {
		const rec_t*	right_rec;
		savepoint = mtr_set_savepoint(&mtr);

		right_block = btr_block_get(
			page_id_t(index->table->space_id, right_page_no),
			zip_size,
			RW_SX_LATCH, index, &mtr);

		right_page = buf_block_get_frame(right_block);

		if (btr_page_get_prev(right_page, &mtr)
		    != page_get_page_no(page)) {

			btr_validate_report2(index, level, block, right_block);
			fputs("InnoDB: broken FIL_PAGE_NEXT"
			      " or FIL_PAGE_PREV links\n", stderr);

			ret = false;
		}

		if (page_is_comp(right_page) != page_is_comp(page)) {
			btr_validate_report2(index, level, block, right_block);
			fputs("InnoDB: 'compact' flag mismatch\n", stderr);

			ret = false;

			goto node_ptr_fails;
		}

		rec = page_rec_get_prev(page_get_supremum_rec(page));
		right_rec = page_rec_get_next(page_get_infimum_rec(
						      right_page));
		offsets = rec_get_offsets(rec, index, offsets,
					  page_is_leaf(page),
					  ULINT_UNDEFINED, &heap);
		offsets2 = rec_get_offsets(right_rec, index, offsets2,
					   page_is_leaf(right_page),
					   ULINT_UNDEFINED, &heap);

		/* For spatial index, we cannot guarantee the key ordering
		across pages, so skip the record compare verification for
		now. Will enhanced in special R-Tree index validation scheme */
		if (!dict_index_is_spatial(index)
		    && cmp_rec_rec(rec, right_rec,
				   offsets, offsets2, index) >= 0) {

			btr_validate_report2(index, level, block, right_block);

			fputs("InnoDB: records in wrong order"
			      " on adjacent pages\n", stderr);

			fputs("InnoDB: record ", stderr);
			rec = page_rec_get_prev(page_get_supremum_rec(page));
			rec_print(stderr, rec, index);
			putc('\n', stderr);
			fputs("InnoDB: record ", stderr);
			rec = page_rec_get_next(
				page_get_infimum_rec(right_page));
			rec_print(stderr, rec, index);
			putc('\n', stderr);

			ret = false;
		}
	}

	if (level > 0 && left_page_no == FIL_NULL) {
		ut_a(REC_INFO_MIN_REC_FLAG & rec_get_info_bits(
			     page_rec_get_next(page_get_infimum_rec(page)),
			     page_is_comp(page)));
	}

	/* Similarly skip the father node check for spatial index for now,
	for a couple of reasons:
	1) As mentioned, there is no ordering relationship between records
	in parent level and linked pages in the child level.
	2) Search parent from root is very costly for R-tree.
	We will add special validation mechanism for R-tree later (WL #7520) */
	if (!dict_index_is_spatial(index)
	    && block->page.id.page_no() != dict_index_get_page(index)) {

		/* Check father node pointers */
		rec_t*	node_ptr;

		btr_cur_position(
			index, page_rec_get_next(page_get_infimum_rec(page)),
			block, &node_cur);
		offsets = btr_page_get_father_node_ptr_for_validate(
			offsets, heap, &node_cur, &mtr);

		father_page = btr_cur_get_page(&node_cur);
		node_ptr = btr_cur_get_rec(&node_cur);

		parent_page_no = page_get_page_no(father_page);
		parent_right_page_no = btr_page_get_next(father_page, &mtr);
		rightmost_child = page_rec_is_supremum(
					page_rec_get_next(node_ptr));

		btr_cur_position(
			index,
			page_rec_get_prev(page_get_supremum_rec(page)),
			block, &node_cur);

		offsets = btr_page_get_father_node_ptr_for_validate(
				offsets, heap, &node_cur, &mtr);

		if (node_ptr != btr_cur_get_rec(&node_cur)
		    || btr_node_ptr_get_child_page_no(node_ptr, offsets)
				     != block->page.id.page_no()) {

			btr_validate_report1(index, level, block);

			fputs("InnoDB: node pointer to the page is wrong\n",
			      stderr);

			fputs("InnoDB: node ptr ", stderr);
			rec_print(stderr, node_ptr, index);

			rec = btr_cur_get_rec(&node_cur);
			fprintf(stderr, "\n"
				"InnoDB: node ptr child page n:o "
				ULINTPF "\n",
				btr_node_ptr_get_child_page_no(rec, offsets));

			fputs("InnoDB: record on page ", stderr);
			rec_print_new(stderr, rec, offsets);
			putc('\n', stderr);
			ret = false;

			goto node_ptr_fails;
		}

		if (!page_is_leaf(page)) {
			node_ptr_tuple = dict_index_build_node_ptr(
				index,
				page_rec_get_next(page_get_infimum_rec(page)),
				0, heap, btr_page_get_level(page));

			if (cmp_dtuple_rec(node_ptr_tuple, node_ptr,
					   offsets)) {
				const rec_t* first_rec = page_rec_get_next(
					page_get_infimum_rec(page));

				btr_validate_report1(index, level, block);

				ib::error() << "Node ptrs differ on levels > 0";

				fputs("InnoDB: node ptr ",stderr);
				rec_print_new(stderr, node_ptr, offsets);
				fputs("InnoDB: first rec ", stderr);
				rec_print(stderr, first_rec, index);
				putc('\n', stderr);
				ret = false;

				goto node_ptr_fails;
			}
		}

		if (left_page_no == FIL_NULL) {
			ut_a(node_ptr == page_rec_get_next(
				     page_get_infimum_rec(father_page)));
			ut_a(!page_has_prev(father_page));
		}

		if (right_page_no == FIL_NULL) {
			ut_a(node_ptr == page_rec_get_prev(
				     page_get_supremum_rec(father_page)));
			ut_a(!page_has_next(father_page));
		} else {
			const rec_t*	right_node_ptr;

			right_node_ptr = page_rec_get_next(node_ptr);

			if (!lockout && rightmost_child) {

				/* To obey latch order of tree blocks,
				we should release the right_block once to
				obtain lock of the uncle block. */
				mtr_release_block_at_savepoint(
					&mtr, savepoint, right_block);

				btr_block_get(
					page_id_t(index->table->space_id,
						  parent_right_page_no),
					zip_size,
					RW_SX_LATCH, index, &mtr);

				right_block = btr_block_get(
					page_id_t(index->table->space_id,
						  right_page_no),
					zip_size,
					RW_SX_LATCH, index, &mtr);
			}

			btr_cur_position(
				index, page_rec_get_next(
					page_get_infimum_rec(
						buf_block_get_frame(
							right_block))),
				right_block, &right_node_cur);

			offsets = btr_page_get_father_node_ptr_for_validate(
					offsets, heap, &right_node_cur, &mtr);

			if (right_node_ptr
			    != page_get_supremum_rec(father_page)) {

				if (btr_cur_get_rec(&right_node_cur)
				    != right_node_ptr) {
					ret = false;
					fputs("InnoDB: node pointer to"
					      " the right page is wrong\n",
					      stderr);

					btr_validate_report1(index, level,
							     block);
				}
			} else {
				page_t*	right_father_page
					= btr_cur_get_page(&right_node_cur);

				if (btr_cur_get_rec(&right_node_cur)
				    != page_rec_get_next(
					    page_get_infimum_rec(
						    right_father_page))) {
					ret = false;
					fputs("InnoDB: node pointer 2 to"
					      " the right page is wrong\n",
					      stderr);

					btr_validate_report1(index, level,
							     block);
				}

				if (page_get_page_no(right_father_page)
				    != btr_page_get_next(father_page, &mtr)) {

					ret = false;
					fputs("InnoDB: node pointer 3 to"
					      " the right page is wrong\n",
					      stderr);

					btr_validate_report1(index, level,
							     block);
				}
			}
		}
	}

node_ptr_fails:
	/* Commit the mini-transaction to release the latch on 'page'.
	Re-acquire the latch on right_page, which will become 'page'
	on the next loop.  The page has already been checked. */
	mtr_commit(&mtr);

	if (trx_is_interrupted(trx)) {
		/* On interrupt, return the current status. */
	} else if (right_page_no != FIL_NULL) {

		mtr_start(&mtr);

		if (!lockout) {
			if (rightmost_child) {
				if (parent_right_page_no != FIL_NULL) {
					btr_block_get(
						page_id_t(
							index->table->space_id,
							parent_right_page_no),
						zip_size,
						RW_SX_LATCH, index, &mtr);
				}
			} else if (parent_page_no != FIL_NULL) {
				btr_block_get(
					page_id_t(index->table->space_id,
						  parent_page_no),
					zip_size,
					RW_SX_LATCH, index, &mtr);
			}
		}

		block = btr_block_get(
			page_id_t(index->table->space_id, right_page_no),
			zip_size,
			RW_SX_LATCH, index, &mtr);

		page = buf_block_get_frame(block);

		goto loop;
	}

	mem_heap_free(heap);

	return(ret);
}

/**************************************************************//**
Checks the consistency of an index tree.
@return	DB_SUCCESS if ok, error code if not */
dberr_t
btr_validate_index(
/*===============*/
	dict_index_t*	index,	/*!< in: index */
	const trx_t*	trx)	/*!< in: transaction or NULL */
{
	dberr_t err = DB_SUCCESS;
	bool lockout = dict_index_is_spatial(index);

	/* Full Text index are implemented by auxiliary tables,
	not the B-tree */
	if (dict_index_is_online_ddl(index) || (index->type & DICT_FTS)) {
		return(err);
	}

	mtr_t		mtr;

	mtr_start(&mtr);

	if (!srv_read_only_mode) {
		if (lockout) {
			mtr_x_lock(dict_index_get_lock(index), &mtr);
		} else {
			mtr_sx_lock(dict_index_get_lock(index), &mtr);
		}
	}

	page_t*	root = btr_root_get(index, &mtr);

	if (!root) {
		mtr_commit(&mtr);
		return DB_CORRUPTION;
	}

	ulint	n = btr_page_get_level(root);

	btr_validate_index_running++;
	for (ulint i = 0; i <= n; ++i) {

		if (!btr_validate_level(index, trx, n - i, lockout)) {
			err = DB_CORRUPTION;
			break;
		}
	}

	mtr_commit(&mtr);
	/* In theory we need release barrier here, so that
	btr_validate_index_running decrement is guaranteed to
	happen after latches are released.

	Original code issued SEQ_CST on update and non-atomic
	access on load. Which means it had broken synchronisation
	as well. */
	btr_validate_index_running--;

	return(err);
}

/**************************************************************//**
Checks if the page in the cursor can be merged with given page.
If necessary, re-organize the merge_page.
@return	true if possible to merge. */
static
bool
btr_can_merge_with_page(
/*====================*/
	btr_cur_t*	cursor,		/*!< in: cursor on the page to merge */
	ulint		page_no,	/*!< in: a sibling page */
	buf_block_t**	merge_block,	/*!< out: the merge block */
	mtr_t*		mtr)		/*!< in: mini-transaction */
{
	dict_index_t*	index;
	page_t*		page;
	ulint		n_recs;
	ulint		data_size;
	ulint		max_ins_size_reorg;
	ulint		max_ins_size;
	buf_block_t*	mblock;
	page_t*		mpage;
	DBUG_ENTER("btr_can_merge_with_page");

	if (page_no == FIL_NULL) {
		*merge_block = NULL;
		DBUG_RETURN(false);
	}

	index = btr_cur_get_index(cursor);
	page = btr_cur_get_page(cursor);

	const page_id_t		page_id(index->table->space_id, page_no);
	const ulint zip_size = index->table->space->zip_size();

	mblock = btr_block_get(page_id, zip_size, RW_X_LATCH, index, mtr);
	mpage = buf_block_get_frame(mblock);

	n_recs = page_get_n_recs(page);
	data_size = page_get_data_size(page);

	max_ins_size_reorg = page_get_max_insert_size_after_reorganize(
		mpage, n_recs);

	if (data_size > max_ins_size_reorg) {
		goto error;
	}

	/* If compression padding tells us that merging will result in
	too packed up page i.e.: which is likely to cause compression
	failure then don't merge the pages. */
	if (zip_size && page_is_leaf(mpage)
	    && (page_get_data_size(mpage) + data_size
		>= dict_index_zip_pad_optimal_page_size(index))) {

		goto error;
	}

	max_ins_size = page_get_max_insert_size(mpage, n_recs);

	if (data_size > max_ins_size) {

		/* We have to reorganize mpage */

		if (!btr_page_reorganize_block(
			    false, page_zip_level, mblock, index, mtr)) {

			goto error;
		}

		max_ins_size = page_get_max_insert_size(mpage, n_recs);

		ut_ad(page_validate(mpage, index));
		ut_ad(max_ins_size == max_ins_size_reorg);

		if (data_size > max_ins_size) {

			/* Add fault tolerance, though this should
			never happen */

			goto error;
		}
	}

	*merge_block = mblock;
	DBUG_RETURN(true);

error:
	*merge_block = NULL;
	DBUG_RETURN(false);
}
