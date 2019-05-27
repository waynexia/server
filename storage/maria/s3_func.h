#ifndef S3_FUNC_INCLUDED
#define S3_FUNC_INCLUDED
/* Copyright (C) 2019 MariaDB Corporation Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA */

/*
  Interface function used by S3 storage engine and aria_copy_for_s3
*/

#ifdef WITH_S3_STORAGE_ENGINE
C_MODE_START
#include <libmarias3/marias3.h>

/* Store information about a s3 connection */

typedef struct s3_info
{
  LEX_CSTRING access_key, secret_key, region, bucket;

  /* The following will be filled in by maria_open() */
  LEX_CSTRING database, table;

  /* Sent to open to verify version */
  LEX_CUSTRING tabledef_version;
} S3_INFO;


/* flag + length is stored in this header */
#define COMPRESS_HEADER 4

/* Max length of an AWS PATH */
#define AWS_PATH_LENGTH ((NAME_LEN)*3+3+10+6+11)

void s3_init_library(void);
void s3_deinit_library(void);
int aria_copy_to_s3(ms3_st *s3_client, const char *aws_bucket,
                    const char *path,
                    const char *database, const char *table_name,
                    ulong block_size, my_bool compression,
                    my_bool force, my_bool display);
int aria_copy_from_s3(ms3_st *s3_client, const char *aws_bucket,
                      const char *path,const char *database,
                      my_bool compression, my_bool force, my_bool display);
int aria_delete_from_s3(ms3_st *s3_client, const char *aws_bucket,
                        const char *database, const char *table,
                        my_bool display);
int aria_rename_s3(ms3_st *s3_client, const char *aws_bucket,
                   const char *from_database, const char *from_table,
                   const char *to_database, const char *to_table);
ms3_st *s3_open_connection(S3_INFO *s3);
my_bool s3_put_object(ms3_st *s3_client, const char *aws_bucket,
                      const char *name, uchar *data, size_t length,
                      my_bool compression);
my_bool s3_get_object(ms3_st *s3_client, const char *aws_bucket,
                      const char *name, S3_BLOCK *block, my_bool compression,
                      my_bool print_error);
my_bool s3_delete_object(ms3_st *s3_client, const char *aws_bucket,
                      const char *name, my_bool print_error);
my_bool s3_rename_object(ms3_st *s3_client, const char *aws_bucket,
                         const char *from_name, const char *to_name,
                         my_bool print_error);
void s3_free(S3_BLOCK *data);
my_bool s3_copy_from_file(ms3_st *s3_client, const char *aws_bucket,
                          char *aws_path, File file, my_off_t start,
                          my_off_t file_end, uchar *block, size_t block_size,
                          my_bool compression, my_bool display);
my_bool s3_copy_to_file(ms3_st *s3_client, const char *aws_bucket,
                        char *aws_path, File file, my_off_t start,
                        my_off_t file_end, my_bool compression,
                        my_bool display);
int s3_delete_directory(ms3_st *s3_client, const char *aws_bucket,
                        const char *path);
int s3_rename_directory(ms3_st *s3_client, const char *aws_bucket,
                        const char *from_name, const char *to_name,
                        my_bool print_error);

S3_INFO *s3_info_copy(S3_INFO *old);
my_bool set_database_and_table_from_path(S3_INFO *s3, const char *path);
my_bool s3_get_frm(ms3_st *s3_client, S3_INFO *S3_info, S3_BLOCK *block);
my_bool s3_frm_exists(ms3_st *s3_client, S3_INFO *s3_info);
int s3_check_frm_version(ms3_st *s3_client, S3_INFO *s3_info);
my_bool read_index_header(ms3_st *client, S3_INFO *s3, S3_BLOCK *block);
int32 s3_unique_file_number(void);
my_bool s3_block_read(struct st_pagecache *pagecache,
                      PAGECACHE_IO_HOOK_ARGS *args,
                      struct st_pagecache_file *file,
                      S3_BLOCK *block);
C_MODE_END
#else

C_MODE_START
/* Dummy structures and interfaces to be used when compiling without S3 */
struct s3_info;
typedef struct s3_info S3_INFO;
struct ms3_st;
C_MODE_END
#endif /* WITH_S3_STORAGE_ENGINE */
#endif /* HA_S3_FUNC_INCLUDED */
