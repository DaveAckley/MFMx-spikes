#pragma once  /* -*- C++ -*- */
#include "T6Grid.h"
#include "PT_Ewp.h"
#include "S16C.h"
#include "UxC.h" // For U16C
#include "FastT2.h" // for between(..)
#include "MDist.h"
#include "DemoGlobal.h"
#include "EP_ACacheBlock.h"

namespace MFM {
#define LOGDL2D(sa,tag,d) do { d.log(GET_FILE_ID(__FILE__),__LINE__,sa,""#tag); } while (0)
  struct DL2D {
    using SitesArray = DL2D[DG::T6GRID_WIDTH][DG::T6GRID_HEIGHT];
    static constexpr U8C cNONE = U8C(U8_MAX,U8_MAX);

    U8C mNext, mPrev;
    bool isOccupied() const {
      if (mNext != cNONE && mPrev != cNONE) return true;
      MFM_API_ASSERT(mNext == cNONE && mPrev == cNONE,ILLEGAL_STATE);
      return false;
    }
    U8C getNextC() { return mNext; }
    U8C getPrevC() { return mPrev; }
    void setNextC(U8C c) { mNext = c; }
    void setPrevC(U8C c) { mPrev = c; }
    void setNone() { mNext = mPrev = cNONE; }
    void log() const {
      LOGPTAG(DL2D,this);
      LOGPTAG(DL2Dp,mPrev);
      LOGPTAG(DL2Dn,mNext);
    }
    void log(u16 fileid,u16 lineno,const SitesArray &sa,const char * tag) const {
      DL2D *base = (DL2D*) &sa;
      u32 dist = this - base; // DL2D distance
      U8C at(dist/DG::T6GRID_HEIGHT,dist%DG::T6GRID_HEIGHT);

      static constexpr u32 BUF_SIZ = 40;
      static char buf[BUF_SIZ];
      snprintf(buf,BUF_SIZ,"@%u,%u n:%u,%u p:%u,%u",
               at.x,at.y,
               mNext.x,mNext.y,
               mPrev.x,mPrev.y);
      markLogBlock(fileid,lineno,buf,tag);
    }

  };

  struct DLGridList {

    void logList() {
      LOGPTAG(DL2Dsol,mRoot);
      u32 count = 0;
      U8C last = DL2D::cNONE;
      if (mRoot != DL2D::cNONE) {
        U8C p = mRoot;
        do {
          DL2D & d = _get(p);
          LOGDL2D(mSites,DL2D=,d);
          ++count;
          if (last != DL2D::cNONE && d.getPrevC() != last) {
            LOGPTAG(DLGLlast,last);
            LOGPTAG(DLGLprev,d.getPrevC());
          }
          last = p;
          p = d.getNextC();
        } while (p != mRoot);
      }
      LOGPTAG(DLGLeol,count);
    }
    void demo() {
      LOGPTAG(DLGLdemo,sizeof(DLGridList));
      init();
      LOGPTAG(DLGLp12,pushFrontC({1,2}));
      LOGPTAG(DLGLp96,pushFrontC({9,6}));
      LOGPTAG(DLGLp37,pushFrontC({3,7}));
      logList();
      LOGPTAG(DLGLp12,pushFrontC({1,2}));
      logList();
      LOGPTAG(DLGLp37,pushFrontC({3,7}));
      logList();
      LOGPTAG(DLGLp00,pushFrontC({0,0}));
      logList();
      LOGPTAG(DLGLp00+,pushFrontC({0,0}));
      logList();
      LOGPTAG(DLGLp58,pushFrontC({5,8}));
      logList();
      U8C backc;
      while (popBackC(backc)) 
        LOGPTAG(DLGLpop,backc);
      LOGPTAG(DLGL~demo,this);
    }

    static bool isNoneC(U8C c) {
      return c == DL2D::cNONE;
    }
    static bool isValidC(U8C c) {
      return c.x < DG::T6GRID_WIDTH && c.y < DG::T6GRID_HEIGHT;
    }
    static void assertValidC(U8C c) {
      MFM_API_ASSERT(isValidC(c),ARRAY_INDEX_OUT_OF_BOUNDS);
    }

    u16 getLength() {
      AtomicScopeLock guard(mLock);
      return mLength;
    }

    void init() ;

    bool popBackC(U8C & c) {
      AtomicScopeLock guard(mLock);
      if (mRoot == DL2D::cNONE) return false;
      U8C backc =_get(mRoot).getPrevC();
      _remove(backc,false);
      c = backc;
      return true;
    }

    bool pushFrontC(U8C c) {
      AtomicScopeLock guard(mLock);
      DL2D & d = _get(c);
      //LOGDL2D(mSites,pFC,d);
      bool ret = false;
      if (d.isOccupied()) {
        if (oddsOf(14,15)) return true; // mostly don't move to front
        _remove(c,true);                // but occasionally do
        ret = true;             
      }
      // c is unlinked
      if (mRoot == DL2D::cNONE) { // list is empty
        d.setNextC(c);  // so point at self
        d.setPrevC(c);  // so point at self
        //LOGDL2D(mSites,nur,d);
      } else {
        DL2D & r = _get(mRoot);
        //LOGDL2D(mSites,olr,r);

        U8C oldnextc = r.getNextC();
        U8C oldprevc = r.getPrevC();

        d.setNextC(mRoot);      // our next is old root
        r.setPrevC(c);          // old root's prev is us
        if (oldnextc == mRoot)  // if old root's next was root
          r.setNextC(c);        // now it's us

        d.setPrevC(oldprevc);   // and our prev is old root prev
        if (oldprevc == mRoot)  // if old root's prev was root
          r.setPrevC(c);        // now it's us
        //LOGDL2D(mSites,fd,d);

        //LOGPTAG(opc,oldprevc);
        _get(oldprevc).setNextC(c); // and old tail's next is us
        //LOGDL2D(mSites,op,get(oldprevc));
      }
      mRoot = c;     // and we're the root either way
      ++mLength;
      //SNAP(10'000'000,LOGPTAG(mroo+,mLength));
      return ret;     // meaning moved to front (vs new insert)
    }

    bool removeForward(U8C c) {
      AtomicScopeLock guard(mLock);
      DL2D & d = _get(c);
      if (!d.isOccupied()) return false;
      _remove(c,true);
      return true;
    }

  private:
    static_assert((DG::T6GRID_WIDTH < U8_MAX),"T6GRID TOO WIDE");
    static_assert((DG::T6GRID_HEIGHT < U8_MAX),"T6GRID TOO HIGH");

    DL2D mSites[DG::T6GRID_WIDTH][DG::T6GRID_HEIGHT];

    AtomicLock mLock;
    U8C mRoot;
    u16 mLength;

    //// PRIVATE _METHODS ASSUME LOCK IS HELD
    DL2D & _get(U8C c) {
      assertValidC(c);
      return mSites[c.x][c.y];
    }
    DL2D & _getPrev(DL2D & n) { return _get(n.getPrevC()); }
    DL2D & _getNext(DL2D & n) { return _get(n.getNextC()); }

    void _remove(U8C c, bool fromFront) {
      DL2D & d = _get(c);
      MFM_API_ASSERT(d.isOccupied(),ILLEGAL_ARGUMENT);
      if (mRoot == c) {
        U8C newroot = (fromFront ? d.getNextC() : d.getPrevC());
        mRoot = (newroot == mRoot) ? DL2D::cNONE : newroot;
      }
      U8C prevc = d.getPrevC();
      U8C nextc = d.getNextC();
      _get(prevc).setNextC(nextc);
      _get(nextc).setPrevC(prevc);
      d.setNone();
      MFM_API_ASSERT(mLength > 0,ILLEGAL_STATE);
      --mLength;
      //SNAP(10'000'000,LOGPTAG(mroo-,mLength));
    }
  };

  struct L1GridManagerControl {
    bool mEventProcessingSuspendRequest;     //< set by outside
    bool mEventProcessingSuspendRequestSeen; //< set by GridManager
    bool mEventProcessingSuspendStatus;      //< set by EP_ACacheBlock
    u8 mSuperCellLeader;                     //< set by outside

    u8 getSuperCellLeader() const {
      memoryFence();
      return mSuperCellLeader;  // U8_MAX == no leader?
    }

    void setSuperCellLeader(u8 val) {
      mSuperCellLeader = val;
      memoryFence();
    }

    bool isEPSuspReq() const {
      memoryFence();
      return mEventProcessingSuspendRequest;
    }
    bool isEPSuspReqSeen() const {
      memoryFence();
      return mEventProcessingSuspendRequestSeen;
    }
    bool isEPSuspStatus() const {
      memoryFence();
      return mEventProcessingSuspendStatus;
    }
    void setEPSuspReq(bool b) {
      mEventProcessingSuspendRequest = b;
      memoryFence();
    }
    void setEPSuspReqSeen(bool b) {
      mEventProcessingSuspendRequestSeen = b;
      memoryFence();
    }
    void setEPSuspStatus(bool b) {
      mEventProcessingSuspendStatus = b;
      memoryFence();
    }

    void init() {
      memset_s(this,0,sizeof(*this));
      mSuperCellLeader = U8_MAX; // no SCL to start
    }
  };

  extern L1GridManagerControl theL1GridManagerControl;

  struct GridManager {
    static constexpr u8 MAX_NGBS = 8;
    
    void init(T6Grid & grid, ACacheBlockL1Control & acbl1, DLGridList & gridlist) ;

    U16C selectRandomSite(U16CRange bounds) ;

    S16C siteNumberToOffset(u32 sn) const {
      const MDist4 md;
      SPoint sp = md.getPoint(sn);
      return S16C(sp);
    }

    bool seekRandomNonEmptySite(U16C & found) ;

    bool matchesEW(const EventWindow & ew,U16C center) const ;

    void readEW(EventWindow & ew,U16C center) ;

    u32 writeEW(const EventWindow & ew,U16C center) ;

    void applyEWT(EwpPayload &ewt, u32 ngbidx) ;

    DLGridList * mDLGridListPtr;
    T6Grid * mT6GridPtr;
    ACacheBlockL1Control * mACBL1Ctrl;
    u32 mAutoseedWaitCount;
    u32 mEWsOffered;
    u32 mEWsEmptiesShipped;
    u32 mEWsReturned;
    u32 mEWsCommitted;
    u32 mEWsObsoleted;
    u32 mTotalAtomicChanges;
    u8 mEmptiesRun[MAX_NGBS];
    u8 mSuperCellLeaderCode;  //< if there is a SCL, it's us if it's this
  };

  extern DLGridList theDLGridList; // defined in hub/LiveB.cpp
}
