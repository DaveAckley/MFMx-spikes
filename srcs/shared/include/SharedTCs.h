#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

#if 0
  struct InterHubPayloadDEBUG {
    struct Data {
      static constexpr u32 DATA_COUNT = 238;
      u32 mCount;
      u32 mData[DATA_COUNT];
      u32 mSum;
      u32 sumIt() {
        u32 ret = 0;
        ret += mCount;
        for (u32 i = 0; i < sizeof(mData)/sizeof(mData[0]); ++i) {
          ret += mData[i];
        }
        return ret;
      }
      void setSum() { mSum = sumIt(); }
      void init() {
        mCount = 87;
        for (u32 i = 0; i < sizeof(mData)/sizeof(mData[0]); ++i) {
          mData[i] = i;
        }
        setSum();
      }
      static bool match(const Data &d1, const Data &d2) {
        bool ret = true;
        if (d1.mCount != d2.mCount)
          ret = report(offsetof(Data,mCount),1000,d1.mCount,d2.mCount);
        for (u32 i = 0; i < sizeof(mData)/sizeof(mData[0]); ++i) {
          if (d1.mData[i] != d2.mData[i])
            ret = report(offsetof(Data,mData)+4*i,i,d1.mData[i],d2.mData[i]);
        }
        if (d1.mSum != d2.mSum) ret = report(offsetof(Data,mSum),2000,d1.mSum,d2.mSum);
        return ret;
      }
      static bool report(u32 offs, u32 idx, u32 v1, u32 v2) {
        HBPTAG(D2DX,idx);
        HBPTAG(DOFX,offs);
        HBPTAG(DOFW,offs/4);
        HBPTAG(IHSZ,sizeof(InterHubPayload));
        HBXTAG(d1,v1);
        HBXTAG(d2,v2);
        return false;
      }
    };
    u32 mMAGIC;
    Data mData1;
    //    u32 mWASTOID[721];
    Data mData2;
    u32 mCIGAM;
    static constexpr u32 IHP_MAGIC = 0x49485f00;
    static constexpr u32 IHP_CIGAM = 0x49485fff;
    void init() {
      memset_s(this,'\0',sizeof(*this));
      mMAGIC = IHP_MAGIC;
      mCIGAM = IHP_CIGAM;
      mData1.init();
      mData2.init();
    }

    bool checksCheck() const {
      //      HBPTAG(chkAt,this);
      bool ret = Data::match(mData1,mData2);
      if (!ret) HBPTAG(chkFAIL,ret);
      return ret;
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        HBPTAG(ihpma,mMAGIC);
        HBPTAG(ihpci,mCIGAM);
        return false;
      }
      if (!checksCheck()) {
        HBXTAG(DRXX,mMAGIC);
        mData1.init();
        mData2.init();
      } else {
        mData1.mCount++;
        mData1.setSum();
        mData2.mCount++;
        mData2.setSum();
        HBPTAG(@,this);
        HBPTAG(DMA1,mData1.mCount);
        //        HBPTAG(DMA2,mData2.mCount);
      }
      return true;
    }

    bool isValid() const {
      return
        mMAGIC == IHP_MAGIC &&
        mCIGAM == IHP_CIGAM;
    }

  };
#endif

  struct InterHubPayload {
    static constexpr u32 IHP_MAGIC = 0x4948504d;
    static constexpr u32 IHP_CIGAM = 0x49485043;
    void init() {
      memset_s(this,'\0',sizeof(*this));
      mMAGIC = IHP_MAGIC;
      mCIGAM = IHP_CIGAM;
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        HBPTAG(ihpma,mMAGIC);
        HBPTAG(ihpci,mCIGAM);
        return false;
      }
      //      HBPTAG(IHAT,this);
      //      HBPTAG(ISIZ,sizeof(*this));
      //      HBXTAG(IEND,(u32)(((char*)this)+sizeof(*this)));
      ++mAtomInfoCount;
      //      HBPTAG(mAIC,mAtomInfoCount);
      if (inside) {
        ++mOrigin.x;
        //        HBPTAG(IHP-i,(u32) mOrigin.x);
      } else {
        ++mOrigin.y;
        //        HBPTAG(IHP-o,(u32) mOrigin.y);
      }
      //      HBPTAG(IHP-a,mOrigin);
      return true;
    }

    bool isValid() const {
      return
        mMAGIC == IHP_MAGIC &&
        mCIGAM == IHP_CIGAM;
    }
    u32 mMAGIC;
    S16C mOrigin;               //< origin relative coord, updates on transits
    u16 mAtomInfoCount;
    struct AtomInfo {
      S16C mOffset;             //< position relative to mOrigin
      P4Atom mAtom;
    };
    static constexpr u32 MAX_ATOMS = ((1u<<12) - sizeof(mOrigin)) / sizeof(AtomInfo);
    AtomInfo mAtomInfos[MAX_ATOMS];
    u32 mCIGAM;
  };
  
  class InterHubBlock : public TC<InterHubBlock,sizeof(InterHubPayload)> {
  public:
    const char * getName() const { return "InterHubBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    InterHubPayload & payload() { return *(InterHubPayload*) getDataStart(); }
    void init() {
      //HBNOTE(getName());
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      //HBNOTE(preCloseTC);
      //      payload().checksCheck();
      closeTC(sizeof(payload())); // and then close it, with a full load
      //HBNOTE(postCloseTC);
      //      payload().checksCheck();
      //HBNOTE(postSetDepTC);
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
      //      payload().checksCheck();
      if (false) {
        TCMarker h = getHeader();
        HBPTAG(-?-,h.isValid());
        HBXTAG(huh,h.getU32());
        HBXTAG(hmg,(u32)h.mTCMMagic);
        HBXTAG(hnc,(u32)h.mTCMNonce);
        HBXTAG(hsz,(u32)h.mTCMSize);
        HBXTAG(hst,(u32)h.mTCMState);
        HBXTAG(*hh,&getHeader());
        HBPTAG(tcm,h.mTCMSize);
        HBXTAG(*ff,&getFooter());
        HBXTAG(2ff,getFooter().getU32());
        HBXTAG(this,this);
        HBXTAG(*ak,&getAnkleOrDie());
        HBXTAG(2aa,getAnkleOrDie().getU32());
        HBPTAG(-f-,getFooter().isValid());
        HBPTAG(-a-,getAnkle().isValid());
        HBPTAG(-==,getFooter() == getHeader());
        HBPTAG(-s-,h.mTCMSize);
        HBPTAG(-O-,isComplete());
      }
    }
  };

  typedef TCStorage<InterHubBlock,2> InterHubStorage;

  enum EwpPayloadCode : u8 {
    EWPC_UNINITTED = 0u,
    EWPC_EMPTY,
    EWPC_SOURCE_ONLY,
    EWPC_SOURCE_AND_DEST
  };

  struct EwpPayloadState {
    EwpPayloadCode mPayloadCode;
  };
  struct EwpPayload {
    EwpPayloadState mPayloadState;
    s32 mHiddenXPos, mHiddenYPos; // host side use only
    EventWindow mOld, mNew;
    //TimeStamp mSTVLTime;          // host side use only
    void init() {
      mHiddenXPos = 0;
      mHiddenYPos = 0;
    }

    static constexpr u32 payloadSizeFromCode(EwpPayloadCode plc) {
      switch(plc) {
      case EwpPayloadCode::EWPC_UNINITTED:
      case EwpPayloadCode::EWPC_EMPTY:
        return offsetof(EwpPayload,mHiddenXPos);

      case EwpPayloadCode::EWPC_SOURCE_ONLY:
        return offsetof(EwpPayload,mNew);

      case EwpPayloadCode::EWPC_SOURCE_AND_DEST:
        return sizeof(EwpPayload);
      }
      return 0u;                // 'unreachable'
    }

    u32 currentPayloadSize() const {
      return payloadSizeFromCode(mPayloadState.mPayloadCode);
    }
  };

  class EwpBlock : public TC<EwpBlock,sizeof(EwpPayload)> {
  public:
    const char * getName() const { return "EwpBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    EwpPayload & payload() { return *(EwpPayload*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

}
