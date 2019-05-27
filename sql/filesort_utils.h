/* Copyright (c) 2010, 2012 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef FILESORT_UTILS_INCLUDED
#define FILESORT_UTILS_INCLUDED

#include "my_base.h"
#include "sql_array.h"

class Sort_param;
/*
  Calculate cost of merge sort

    @param num_rows            Total number of rows.
    @param num_keys_per_buffer Number of keys per buffer.
    @param elem_size           Size of each element.

    Calculates cost of merge sort by simulating call to merge_many_buff().

  @retval
    Computed cost of merge sort in disk seeks.

  @note
    Declared here in order to be able to unit test it,
    since library dependencies have not been sorted out yet.

    See also comments get_merge_many_buffs_cost().
*/

double get_merge_many_buffs_cost_fast(ha_rows num_rows,
                                      ha_rows num_keys_per_buffer,
                                      uint    elem_size);


/**
  A wrapper class around the buffer used by filesort().
  The buffer is a contiguous chunk of memory,
  where the first part is <num_records> pointers to the actual data.

  We wrap the buffer in order to be able to do lazy initialization of the
  pointers: the buffer is often much larger than what we actually need.

  The buffer must be kept available for multiple executions of the
  same sort operation, so we have explicit allocate and free functions,
  rather than doing alloc/free in CTOR/DTOR.
*/
class Filesort_buffer
{
public:
  Filesort_buffer()
    : m_idx_array(), m_start_of_data(NULL), allocated_size(0)
  {}
  
  ~Filesort_buffer()
  {
    my_free(m_idx_array.array());
  }

  bool is_allocated()
  {
    return m_idx_array.array() != 0;
  }
  void reset()
  {
    m_idx_array.reset();
  }

  /** Sort me... */
  void sort_buffer(const Sort_param *param, uint count);

  /// Initializes a record pointer.
  uchar *get_record_buffer(uint idx)
  {
    m_idx_array[idx]= m_start_of_data + (idx * m_record_length);
    return m_idx_array[idx];
  }

  /// Initializes all the record pointers.
  void init_record_pointers()
  {
    for (uint ix= 0; ix < m_idx_array.size(); ++ix)
      (void) get_record_buffer(ix);
  }

  /// Returns total size: pointer array + record buffers.
  size_t sort_buffer_size() const
  {
    return allocated_size;
  }

  /// Allocates the buffer, but does *not* initialize pointers.
  uchar **alloc_sort_buffer(uint num_records, uint record_length);

  /// Frees the buffer.
  void free_sort_buffer();

  /// Getter, for calling routines which still use the uchar** interface.
  uchar **get_sort_keys() { return m_idx_array.array(); }

  /**
    We need an assignment operator, see filesort().
    This happens to have the same semantics as the one that would be
    generated by the compiler. We still implement it here, to show shallow
    assignment explicitly: we have two objects sharing the same array.
  */
  Filesort_buffer &operator=(const Filesort_buffer &rhs)
  {
    m_idx_array= rhs.m_idx_array;
    m_record_length= rhs.m_record_length;
    m_start_of_data= rhs.m_start_of_data;
    allocated_size=  rhs.allocated_size;
    return *this;
  }

private:
  typedef Bounds_checked_array<uchar*> Idx_array;

  Idx_array  m_idx_array;                       /* Pointers to key data */
  uint       m_record_length;
  uchar     *m_start_of_data;                   /* Start of key data */
  size_t    allocated_size;
};

#endif  // FILESORT_UTILS_INCLUDED
