/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef RESUME_REQ_HPP
#define RESUME_REQ_HPP

#include "SignalData.hpp"

class ResumeReq {

  /**
   * Reciver(s)
   */
  friend class Ndbcntr;

  /**
   * Sender
   */
  friend class MgmtSrvr;

public:
  STATIC_CONST( SignalLength = 2 );
  
public:
  
  Uint32 senderRef;
  Uint32 senderData;
};

class ResumeRef {

  /**
   * Reciver(s)
   */
  friend class MgmtSrvr;
  
  /**
   * Sender
   */
  friend class Ndbcntr;

public:
  STATIC_CONST( SignalLength = 2 );
  
  enum ErrorCode {
    OK = 0,
    NodeShutdownInProgress = 1,
    SystemShutdownInProgress = 2,
    NodeShutdownWouldCauseSystemCrash = 3
  };
  
public:
  Uint32 senderData;
  Uint32 errorCode;
};
#endif
