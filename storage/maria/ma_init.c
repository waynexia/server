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

/* Initialize an maria-database */

#include "maria_def.h"
#include <ft_global.h>
#include "ma_blockrec.h"

my_bool maria_inited= FALSE;
pthread_mutex_t THR_LOCK_maria;

/*
  Initialize maria

  SYNOPSIS
    maria_init()

  TODO
    Open log files and do recovery if need

  RETURN
  0  ok
  #  error number
*/

int maria_init(void)
{
  if (!maria_inited)
  {
    maria_inited= TRUE;
    pthread_mutex_init(&THR_LOCK_maria,MY_MUTEX_INIT_SLOW);
    _ma_init_block_record_data();
    loghandler_init();
  }
  return 0;
}


void maria_end(void)
{
  if (maria_inited)
  {
    maria_inited= FALSE;
    ft_free_stopwords();
    translog_destroy();
    ma_control_file_end();
    pthread_mutex_destroy(&THR_LOCK_maria);
  }
}
