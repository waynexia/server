/*
Licensed Materials - Property of IBM
DB2 Storage Engine Enablement
Copyright IBM Corporation 2007,2008
All rights reserved

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met: 
 (a) Redistributions of source code must retain this list of conditions, the
     copyright notice in section {d} below, and the disclaimer following this
     list of conditions. 
 (b) Redistributions in binary form must reproduce this list of conditions, the
     copyright notice in section (d) below, and the disclaimer following this
     list of conditions, in the documentation and/or other materials provided
     with the distribution. 
 (c) The name of IBM may not be used to endorse or promote products derived from
     this software without specific prior written permission. 
 (d) The text of the required copyright notice is: 
       Licensed Materials - Property of IBM
       DB2 Storage Engine Enablement 
       Copyright IBM Corporation 2007,2008 
       All rights reserved

THIS SOFTWARE IS PROVIDED BY IBM CORPORATION "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL IBM CORPORATION BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/


#ifndef DB2I_GLOBAL_H
#define DB2I_GLOBAL_H

#define MYSQL_SERVER 1

#include "my_global.h"
#include "my_sys.h"

const uint MAX_DB2_KEY_PARTS=120;
const int MAX_DB2_V5R4_LIBNAME_LENGTH = 10;
const int MAX_DB2_V6R1_LIBNAME_LENGTH = 30;
const int MAX_DB2_SCHEMANAME_LENGTH=258; 
const int MAX_DB2_FILENAME_LENGTH=258; 
const int MAX_DB2_COLNAME_LENGTH=128;
const int MAX_DB2_SAVEPOINTNAME_LENGTH=128;
const int MAX_DB2_QUALIFIEDNAME_LENGTH=MAX_DB2_V6R1_LIBNAME_LENGTH + 1 + MAX_DB2_FILENAME_LENGTH;
const uint32 MAX_CHAR_LENGTH = 32765;
const uint32 MAX_VARCHAR_LENGTH = 32739;
const uint32 MAX_DEC_PRECISION = 63;
const uint32 MAX_BLOB_LENGTH = 2147483646;
const uint32 MAX_BINARY_LENGTH = MAX_CHAR_LENGTH;
const uint32 MAX_VARBINARY_LENGTH = MAX_VARCHAR_LENGTH;
const uint32 MAX_FULL_ALLOCATE_BLOB_LENGTH = 65536;
const uint32 MAX_FOREIGN_LEN = 64000;
const char* DB2I_TEMP_TABLE_SCHEMA = "QTEMP";
const char DB2I_ADDL_INDEX_NAME_DELIMITER[5] = {'_','_','_','_','_'};
const char DB2I_DEFAULT_INDEX_NAME_DELIMITER[3] = {'_','_','_'};
const int DB2I_INDEX_NAME_LENGTH_TO_PRESERVE = 110;

enum enum_DB2I_INDEX_TYPE
{
  typeNone = 0,
  typeDefault = 'D',
  typeHex = 'H',
  typeAscii = 'A'
};

void* roundToQuadWordBdy(void* ptr)
{
  return (void*)(((uint64)(ptr)+0xf) & ~0xf);
}

typedef uint64_t ILEMemHandle;

struct OSVersion
{
  uint8 v;
  uint8 r;
};
extern OSVersion osVersion;


/**
  Allocate 16-byte aligned space using the MySQL heap allocator
  
  @details  Many of the spaces used by the QMY_* APIS are required to be
  aligned on 16 byte boundaries. The standard system malloc will do this 
  alignment by default. However, in order to use the heap debug and tracking
  features of the mysql allocator, we chose to implement an aligning wrapper
  around my_malloc. Essentially, we overallocate the storage space, find the 
  first aligned address in the space, store a pointer to the true malloc 
  allocation in the bytes immediately preceding the aligned address, and return 
  the aligned address to the caller.
  
  @parm size  The size of heap storage needed
  
  @return  A 16-byte aligned pointer to the storage requested.
*/
void* malloc_aligned(size_t size)
{
  char* p;
  char* base;
  base = (char*)my_malloc(size + sizeof(void*) + 15, MYF(MY_WME));
  if (likely(base))
  {
    p = (char*)roundToQuadWordBdy(base + sizeof(void*));
    char** p2 = (char**)(p - sizeof(void*));
    *p2 = base;
  }
  else
    p = NULL;
  
  return p;
}

/**
  Free a 16-byte aligned space alloced by malloc_aligned
  
  @details  We know that a pointer to the true malloced storage immediately
  precedes the aligned address, so we pull that out and call my_free().
  
  @parm p  A 16-byte aligned pointer generated by malloc_aligned
*/
void free_aligned(void* p)
{
  if (likely(p))
  {
    my_free(*(char**)((char*)p-sizeof(void*)));
  }  
}

#endif
