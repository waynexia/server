/* Copyright (C) 2004-2006 MySQL AB

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

#ifndef INCLUDES_MYSQL_INSTANCE_MANAGER_MESSAGES_H
#define INCLUDES_MYSQL_INSTANCE_MANAGER_MESSAGES_H

const char *message(unsigned sql_errno);

const char *errno_to_sqlstate(unsigned sql_errno);

#endif // INCLUDES_MYSQL_INSTANCE_MANAGER_MESSAGES_H
