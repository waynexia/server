/* Copyright (C) 2000 MySQL AB

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

#include "mysys_priv.h"
#include "mysys_err.h"
#include <errno.h>
#ifdef HAVE_PREAD
#include <unistd.h>
#endif

	/* Read a chunk of bytes from a file  */

uint my_pread(File Filedes, byte *Buffer, uint Count, my_off_t offset,
	      myf MyFlags)
{
  uint readbytes;
  int error= 0;
  DBUG_ENTER("my_pread");
  DBUG_PRINT("my",("Fd: %d  Seek: %lu  Buffer: 0x%lx  Count: %u  MyFlags: %d",
		   Filedes, (ulong) offset, (long) Buffer, Count, MyFlags));

  for (;;)
  {
#ifndef __WIN__
    errno=0;					/* Linux doesn't reset this */
#endif
#ifndef HAVE_PREAD
    off_t old_offset;

    pthread_mutex_lock(&my_file_info[Filedes].mutex);
    /*
      As we cannot change the file pointer, we save the old position,
      before seeking to the given offset
    */

    error= (old_offset= (off_t)lseek(Filedes, 0L, MY_SEEK_CUR)) == -1L ||
           lseek(Filedes, offset, MY_SEEK_SET) == -1L;

    if (!error)                                 /* Seek was successful */
    {
      if ((readbytes = (uint) read(Filedes, Buffer, Count)) == -1L)
        my_errno= errno ? errno : -1;

      /*
        We should seek back, even if read failed. If this fails,
        we will return an error. If read failed as well, we will
        save the errno from read, not from lseek().
      */
      if ((error= (lseek(Filedes, old_offset, MY_SEEK_SET) == -1L)) &&
          readbytes != -1L)
        my_errno= errno;
    }

    pthread_mutex_unlock(&my_file_info[Filedes].mutex);
#else
    if ((error= ((readbytes =
                  (uint) pread(Filedes, Buffer, Count, offset)) != Count)))
      my_errno= errno ? errno : -1;
#endif
    if (error || readbytes != Count)
    {
      DBUG_PRINT("warning",("Read only %d bytes off %u from %d, errno: %d",
			    (int) readbytes, Count,Filedes,my_errno));
#ifdef THREAD
      if ((readbytes == 0 || (int) readbytes == -1) && errno == EINTR)
      {
        DBUG_PRINT("debug", ("my_pread() was interrupted and returned %d",
                             (int) readbytes));
        continue;                              /* Interrupted */
      }
#endif
      if (MyFlags & (MY_WME | MY_FAE | MY_FNABP))
      {
	if ((int) readbytes == -1)
	  my_error(EE_READ, MYF(ME_BELL+ME_WAITTANG),
		   my_filename(Filedes),my_errno);
	else if (MyFlags & (MY_NABP | MY_FNABP))
        {
	  my_error(EE_EOFERR, MYF(ME_BELL+ME_WAITTANG),
		   my_filename(Filedes),my_errno);
        }
      }
      if ((int) readbytes == -1 || (MyFlags & (MY_FNABP | MY_NABP)))
	DBUG_RETURN(MY_FILE_ERROR);		/* Return with error */
    }
    if (MyFlags & (MY_NABP | MY_FNABP))
      DBUG_RETURN(0);				/* Read went ok; Return 0 */
    DBUG_RETURN(readbytes);			/* purecov: inspected */
  }
} /* my_pread */


	/* Write a chunk of bytes to a file */

uint my_pwrite(int Filedes, const byte *Buffer, uint Count, my_off_t offset,
	       myf MyFlags)
{
  uint writenbytes,errors;
  ulong written;
  DBUG_ENTER("my_pwrite");
  DBUG_PRINT("my",("Fd: %d  Seek: %lu  Buffer: 0x%lx  Count: %d  MyFlags: %d",
		   Filedes, (ulong) offset, (long) Buffer, Count, MyFlags));
  errors=0; written=0L;

  for (;;)
  {
#ifndef HAVE_PREAD
    int error= 0;
    off_t old_offset;
    writenbytes= (uint) -1;
    pthread_mutex_lock(&my_file_info[Filedes].mutex);

    /*
      As we cannot change the file pointer, we save the old position,
      before seeking to the given offset
    */
    error= ((old_offset= (off_t)lseek(Filedes, 0L, MY_SEEK_CUR)) == -1L ||
            lseek(Filedes, offset, MY_SEEK_SET) == -1L);

    if (!error)                                 /* Seek was successful */
    {
      if ((writenbytes = (uint) write(Filedes, Buffer, Count)) == -1L)
        my_errno= errno;

      /*
        We should seek back, even if write failed. If this fails,
        we will return an error. If write failed as well, we will
        save the errno from write, not from lseek().
      */
      if ((error= (lseek(Filedes, old_offset, MY_SEEK_SET) == -1L)) &&
          writenbytes != -1L)
        my_errno= errno;
    }
    pthread_mutex_unlock(&my_file_info[Filedes].mutex);

    if (!error && writenbytes == Count)
      break;
#else
    if ((writenbytes = (uint) pwrite(Filedes, Buffer, Count,offset)) == Count)
      break;
    else
      my_errno= errno;
#endif
    if ((int) writenbytes != -1)
    {					/* Safegueard */
      written+=writenbytes;
      Buffer+=writenbytes;
      Count-=writenbytes;
      offset+=writenbytes;
    }
    DBUG_PRINT("error",("Write only %d bytes, error: %d",
                        writenbytes, my_errno));
#ifndef NO_BACKGROUND
#ifdef THREAD
    if (my_thread_var->abort)
      MyFlags&= ~ MY_WAIT_IF_FULL;		/* End if aborted by user */
#endif
    if ((my_errno == ENOSPC || my_errno == EDQUOT) &&
        (MyFlags & MY_WAIT_IF_FULL))
    {
      if (!(errors++ % MY_WAIT_GIVE_USER_A_MESSAGE))
	my_error(EE_DISK_FULL,MYF(ME_BELL | ME_NOREFRESH),
		 my_filename(Filedes),my_errno,MY_WAIT_FOR_USER_TO_FIX_PANIC);
      VOID(sleep(MY_WAIT_FOR_USER_TO_FIX_PANIC));
      continue;
    }
    if ((writenbytes > 0 && (uint) writenbytes != (uint) -1) ||
        my_errno == EINTR)
      continue;					/* Retry */
#endif
    if (MyFlags & (MY_NABP | MY_FNABP))
    {
      if (MyFlags & (MY_WME | MY_FAE | MY_FNABP))
      {
	my_error(EE_WRITE, MYF(ME_BELL | ME_WAITTANG),
		 my_filename(Filedes),my_errno);
      }
      DBUG_RETURN(MY_FILE_ERROR);		/* Error on read */
    }
    else
      break;					/* Return bytes written */
  }
  if (MyFlags & (MY_NABP | MY_FNABP))
    DBUG_RETURN(0);			/* Want only errors */
  DBUG_RETURN(writenbytes+written); /* purecov: inspected */
} /* my_pwrite */
