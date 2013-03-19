/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA */

#include <ndb_global.h>
#include "NdbEnv.h"

const char* NdbEnv_GetEnv(const char* name, char * buf, int buflen)
{
    char* p = NULL;
    p = getenv(name);
    
    if (p != NULL && buf != NULL){
        strncpy(buf, p, buflen);
        buf[buflen-1] = 0;
    }
    return p;
}

