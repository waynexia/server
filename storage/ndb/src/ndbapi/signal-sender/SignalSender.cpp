/* Copyright (c) 2003-2005 MySQL AB

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

#include "SignalSender.hpp"
#include "ConfigRetriever.hpp"
#include <NdbSleep.h>
#include <SignalLoggerManager.hpp>

SimpleSignal::SimpleSignal(bool dealloc){
  memset(this, 0, sizeof(* this));
  deallocSections = dealloc;
}

SimpleSignal::~SimpleSignal(){
  if(!deallocSections)
    return;
  if(ptr[0].p != 0) delete []ptr[0].p;
  if(ptr[1].p != 0) delete []ptr[1].p;
  if(ptr[2].p != 0) delete []ptr[2].p;
}

void 
SimpleSignal::set(class SignalSender& ss,
		  Uint8  trace, Uint16 recBlock, Uint16 gsn, Uint32 len){
  
  header.theTrace                = trace;
  header.theReceiversBlockNumber = recBlock;
  header.theVerId_signalNumber   = gsn;
  header.theLength               = len;
  header.theSendersBlockRef      = refToBlock(ss.getOwnRef());
}

void
SimpleSignal::print(FILE * out){
  fprintf(out, "---- Signal ----------------\n");
  SignalLoggerManager::printSignalHeader(out, header, 0, 0, false);
  SignalLoggerManager::printSignalData(out, header, theData);
  for(Uint32 i = 0; i<header.m_noOfSections; i++){
    Uint32 len = ptr[i].sz;
    fprintf(out, " --- Section %d size=%d ---\n", i, len);
    Uint32 * signalData = ptr[i].p;
    while(len >= 7){
      fprintf(out, 
              " H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x\n",
              signalData[0], signalData[1], signalData[2], signalData[3], 
              signalData[4], signalData[5], signalData[6]);
      len -= 7;
      signalData += 7;
    }
    if(len > 0){
      fprintf(out, " H\'%.8x", signalData[0]);
      for(Uint32 i = 1; i<len; i++)
        fprintf(out, " H\'%.8x", signalData[i]);
      fprintf(out, "\n");
    }
  }
}

SignalSender::SignalSender(const char * connectString){
  m_cond = NdbCondition_Create();
  theFacade = TransporterFacade::start_instance(connectString);
  m_blockNo = theFacade->open(this, execSignal, execNodeStatus);
  assert(m_blockNo > 0);
}

SignalSender::~SignalSender(){
  theFacade->close(m_blockNo);
  theFacade->stop_instance();
  NdbCondition_Destroy(m_cond);
}

Uint32
SignalSender::getOwnRef() const {
  return numberToRef(m_blockNo, theFacade->ownId());
}

bool
SignalSender::connectOne(Uint32 timeOutMillis){
  NDB_TICKS start = NdbTick_CurrentMillisecond();
  NDB_TICKS now = start;
  while(theFacade->theClusterMgr->getNoOfConnectedNodes() == 0 &&
	(timeOutMillis == 0 || (now - start) < timeOutMillis)){
    NdbSleep_MilliSleep(100);
  }
  return theFacade->theClusterMgr->getNoOfConnectedNodes() > 0;
}

bool
SignalSender::connectAll(Uint32 timeOutMillis){
  NDB_TICKS start = NdbTick_CurrentMillisecond();
  NDB_TICKS now = start;
  while(theFacade->theClusterMgr->getNoOfConnectedNodes() < 1 &&
	(timeOutMillis == 0 || (now - start) < timeOutMillis)){	
    NdbSleep_MilliSleep(100);
  }
  return theFacade->theClusterMgr->getNoOfConnectedNodes() >= 1;
}


Uint32
SignalSender::getAliveNode(){
  return theFacade->get_an_alive_node();
}

const ClusterMgr::Node & 
SignalSender::getNodeInfo(Uint16 nodeId) const {
  return theFacade->theClusterMgr->getNodeInfo(nodeId);
}

Uint32
SignalSender::getNoOfConnectedNodes() const {
  return theFacade->theClusterMgr->getNoOfConnectedNodes();
}

SendStatus
SignalSender::sendSignal(Uint16 nodeId, const SimpleSignal * s){
  return theFacade->theTransporterRegistry->prepareSend(&s->header,
							1, // JBB
							&s->theData[0],
							nodeId, 
							&s->ptr[0]);
}

template<class T>
SimpleSignal *
SignalSender::waitFor(Uint32 timeOutMillis, T & t){
  
  Guard g(theFacade->theMutexPtr);
  
  SimpleSignal * s = t.check(m_jobBuffer);
  if(s != 0){
    return s;
  }
  
  NDB_TICKS now = NdbTick_CurrentMillisecond();
  NDB_TICKS stop = now + timeOutMillis;
  Uint32 wait = (timeOutMillis == 0 ? 10 : timeOutMillis);
  do {
    NdbCondition_WaitTimeout(m_cond,
			     theFacade->theMutexPtr, 
			     wait);
    
    
    SimpleSignal * s = t.check(m_jobBuffer);
    if(s != 0){
      return s;
    }
    
    now = NdbTick_CurrentMillisecond();
    wait = (timeOutMillis == 0 ? 10 : stop - now);
  } while(stop > now || timeOutMillis == 0);
  
  return 0;
} 

class WaitForAny {
public:
  SimpleSignal * check(Vector<SimpleSignal*> & m_jobBuffer){
    if(m_jobBuffer.size() > 0){
      SimpleSignal * s = m_jobBuffer[0];
      m_jobBuffer.erase(0);
	return s;
    }
      return 0;
  }
};
  
SimpleSignal *
SignalSender::waitFor(Uint32 timeOutMillis){
  
  WaitForAny w;
  return waitFor(timeOutMillis, w);
}

class WaitForNode {
public:
  Uint32 m_nodeId;
  SimpleSignal * check(Vector<SimpleSignal*> & m_jobBuffer){
    Uint32 len = m_jobBuffer.size();
    for(Uint32 i = 0; i<len; i++){
      if(refToNode(m_jobBuffer[i]->header.theSendersBlockRef) == m_nodeId){
	SimpleSignal * s = m_jobBuffer[i];
	m_jobBuffer.erase(i);
	return s;
      }
    }
    return 0;
  }
};

SimpleSignal *
SignalSender::waitFor(Uint16 nodeId, Uint32 timeOutMillis){
  
  WaitForNode w;
  w.m_nodeId = nodeId;
  return waitFor(timeOutMillis, w);
}

#include <NdbApiSignal.hpp>

void
SignalSender::execSignal(void* signalSender, 
			 NdbApiSignal* signal, 
			 class LinearSectionPtr ptr[3]){
  SimpleSignal * s = new SimpleSignal(true);
  s->header = * signal;
  memcpy(&s->theData[0], signal->getDataPtr(), 4 * s->header.theLength);
  for(Uint32 i = 0; i<s->header.m_noOfSections; i++){
    s->ptr[i].p = new Uint32[ptr[i].sz];
    s->ptr[i].sz = ptr[i].sz;
    memcpy(s->ptr[i].p, ptr[i].p, 4 * ptr[i].sz);
  }
  SignalSender * ss = (SignalSender*)signalSender;
  ss->m_jobBuffer.push_back(s);
  NdbCondition_Signal(ss->m_cond);
}
  
void 
SignalSender::execNodeStatus(void* signalSender, 
			     Uint16 NodeId, 
			     bool alive, 
			     bool nfCompleted){
}
  
