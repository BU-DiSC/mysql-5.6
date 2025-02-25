/* Copyright (c) 2000, 2022, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/* Defines to make different thread packages compatible */

#ifndef THREAD_TYPE_INCLUDED
#define THREAD_TYPE_INCLUDED

/**
  @file include/mysql/thread_type.h
*/

/**
  Flags for the THD::system_thread variable

  @note In git dd2303ce072f185b02b50b58d841c70687f41bf9, THD::system_thread was
  changed from a bool to a "bitmap" (uint) and later to this enum. But it was
  never used as a bitmap. This appears to have been an oversight in the original
  commit mentioned above (Dec 2003). It was later extended with more system
  thread types, but we never assign more than one bit to THD::system_thread at
  at time (nor would it make sense to). We can probably move these to use plain
  incrementing numbers and wouldn't have to worry about running out of bits.
*/
enum enum_thread_type {
  NON_SYSTEM_THREAD = 0,
  SYSTEM_THREAD_SLAVE_IO = 1,
  SYSTEM_THREAD_SLAVE_SQL = 2,
  SYSTEM_THREAD_NDBCLUSTER_BINLOG = 4,
  SYSTEM_THREAD_EVENT_SCHEDULER = 8,
  SYSTEM_THREAD_EVENT_WORKER = 16,
  SYSTEM_THREAD_INFO_REPOSITORY = 32,
  SYSTEM_THREAD_SLAVE_WORKER = 64,
  SYSTEM_THREAD_COMPRESS_GTID_TABLE = 128,
  SYSTEM_THREAD_BACKGROUND = 256,
  SYSTEM_THREAD_DD_INITIALIZE = 512,
  SYSTEM_THREAD_DD_RESTART = 1024,
  SYSTEM_THREAD_SERVER_INITIALIZE = 2048,
  SYSTEM_THREAD_INIT_FILE = 4096,
  SYSTEM_THREAD_SERVER_UPGRADE = 8192,
  SYSTEM_THREAD_GROUP_REPLICATION_CONNECTION = 16384
};

#endif /* THREAD_TYPE_INCLUDED */
