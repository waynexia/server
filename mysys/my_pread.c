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
#include "my_base.h"
#include <m_string.h>
#include <errno.h>
#ifdef HAVE_PREAD
#include <unistd.h>
#endif

/*
  Read a chunk of bytes from a file from a given position

  SYNOPSIOS
    my_pread()
    Filedes	File decsriptor
    Buffer	Buffer to read data into
    Count	Number of bytes to read
    offset	Position to read from
    MyFlags	Flags

  NOTES
    This differs from the normal pread() call in that we don't care
    to set the position in the file back to the original position
    if the system doesn't support pread().

  RETURN
    (size_t) -1   Error
    #             Number of bytes read
*/

size_t my_pread(File Filedes, uchar *Buffer, size_t Count, my_off_t offset,
                myf MyFlags)
{
  size_t readbytes;
  int error= 0;
#ifndef HAVE_PREAD
  int save_errno;
#endif
#ifndef DBUG_OFF
  char llbuf[22];
  DBUG_ENTER("my_pread");
  DBUG_PRINT("my",("fd: %d  Seek: %s  Buffer: 0x%lx  Count: %lu  MyFlags: %d",
		   Filedes, ullstr(offset, llbuf), (long) Buffer,
                   (ulong)Count, MyFlags));
#endif
  for (;;)
  {
    errno= 0;    /* Linux, Windows don't reset this on EOF/success */
#ifndef HAVE_PREAD
    pthread_mutex_lock(&my_file_info[Filedes].mutex);
    readbytes= (uint) -1;
    error= (lseek(Filedes, offset, MY_SEEK_SET) == (my_off_t) -1 ||
	    (readbytes= read(Filedes, Buffer, Count)) != Count);
    save_errno= errno;
    pthread_mutex_unlock(&my_file_info[Filedes].mutex);
    if (error)
    {
      errno= save_errno;
#else
    if ((error= ((readbytes= pread(Filedes, Buffer, Count, offset)) != Count)))
    {
#endif
      my_errno= errno ? errno : -1;
      if (errno == 0 || (readbytes != (size_t) -1 &&
                         (MyFlags & (MY_NABP | MY_FNABP))))
        my_errno= HA_ERR_FILE_TOO_SHORT;
      DBUG_PRINT("warning",("Read only %d bytes off %u from %d, errno: %d",
                            (int) readbytes, (uint) Count,Filedes,my_errno));
#ifdef THREAD
      if ((readbytes == 0 || readbytes == (size_t) -1) && errno == EINTR)
      {
        DBUG_PRINT("debug", ("my_pread() was interrupted and returned %d",
                             (int) readbytes));
        continue;                              /* Interrupted */
      }
#endif
      if (MyFlags & (MY_WME | MY_FAE | MY_FNABP))
      {
	if (readbytes == (size_t) -1)
	  my_error(EE_READ, MYF(ME_BELL+ME_WAITTANG),
		   my_filename(Filedes),my_errno);
	else if (MyFlags & (MY_NABP | MY_FNABP))
	  my_error(EE_EOFERR, MYF(ME_BELL+ME_WAITTANG),
		   my_filename(Filedes),my_errno);
      }
      if (readbytes == (size_t) -1 || (MyFlags & (MY_FNABP | MY_NABP)))
	DBUG_RETURN(MY_FILE_ERROR);		/* Return with error */
    }
    if (MyFlags & (MY_NABP | MY_FNABP))
      DBUG_RETURN(0);				/* Read went ok; Return 0 */
    DBUG_RETURN(readbytes);			/* purecov: inspected */
  }
} /* my_pread */


/*
  Write a chunk of bytes to a file at a given position

  SYNOPSIOS
    my_pwrite()
    Filedes	File decsriptor
    Buffer	Buffer to write data from
    Count	Number of bytes to write
    offset	Position to write to
    MyFlags	Flags

  NOTES
    This differs from the normal pwrite() call in that we don't care
    to set the position in the file back to the original position
    if the system doesn't support pwrite()

  RETURN
    (size_t) -1   Error
    #             Number of bytes read
*/

size_t my_pwrite(int Filedes, const uchar *Buffer, size_t Count,
                 my_off_t offset, myf MyFlags)
{
  size_t writenbytes, written;
  uint errors;
#ifndef DBUG_OFF
  char llbuf[22];
  DBUG_ENTER("my_pwrite");
  DBUG_PRINT("my",("fd: %d  Seek: %s  Buffer: 0x%lx  Count: %lu  MyFlags: %d",
		   Filedes, ullstr(offset, llbuf), (long) Buffer,
                   (ulong)Count, MyFlags));
#endif
  errors= 0;
  written= 0;

  for (;;)
  {
#ifndef HAVE_PREAD
    int error;
    writenbytes= (size_t) -1;
    pthread_mutex_lock(&my_file_info[Filedes].mutex);
    error= (lseek(Filedes, offset, MY_SEEK_SET) != (my_off_t) -1 &&
            (writenbytes = write(Filedes, Buffer, Count)) == Count);
    pthread_mutex_unlock(&my_file_info[Filedes].mutex);
    if (error)
      break;
#else
    if ((writenbytes= pwrite(Filedes, Buffer, Count,offset)) == Count)
      break;
    my_errno= errno;
#endif
    if (writenbytes != (size_t) -1)
    {					/* Safegueard */
      written+=writenbytes;
      Buffer+=writenbytes;
      Count-=writenbytes;
      offset+=writenbytes;
    }
    DBUG_PRINT("error",("Write only %u bytes", (uint) writenbytes));
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
    if ((writenbytes && writenbytes != (size_t) -1) || my_errno == EINTR)
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
  DBUG_EXECUTE_IF("check", my_seek(Filedes, -1, SEEK_SET, MYF(0)););
  if (MyFlags & (MY_NABP | MY_FNABP))
    DBUG_RETURN(0);			/* Want only errors */
  DBUG_RETURN(writenbytes+written); /* purecov: inspected */
} /* my_pwrite */
