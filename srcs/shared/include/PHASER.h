#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "Fail.h"
#include "TC.h"

namespace MFM {

  struct PhaserBolt {
    static constexpr u32 MAX_BOLT_SIZE = 40; //< for 64B packet size

    static constexpr u32 MAX_PHASER_COMMANDS = 8*sizeof(u32); //< mask limits

#define ALL_PHASER_COMMANDS() \
    XX(CARRY_ON,0)            \
    XX(HOLD_AT_BIRTH,0)       \
    XX(ALL_HARTS_PAUSE,0)     \
    XX(LOOP_BACK,0)           \
    XX(SUSPEND_EWPS,1)        \
    XX(SUPERCELL_LEADER,1)    \
    XX(SPIKE_PING,4)          \

#define ALL_PHASER_MASK_COMBOS()                                    \
    XX(ALL_EWP_ACTIVE,CmdMask::CARRY_ON|CmdMask::SUPERCELL_LEADER)  \

    enum Cmd : u8 {

#define XX(name,argc) CMD_##name,      
      ALL_PHASER_COMMANDS()
#undef XX

      CMD_COUNT
    };
    static_assert(CMD_COUNT <= MAX_PHASER_COMMANDS, "ALL_PHASER_COMMANDS too big");

    static constexpr const char * allPhaserCommandNames[CMD_COUNT+1] = {
#define XX(name,argc) #name,
      ALL_PHASER_COMMANDS()
#undef XX
      "?"
    };

    static constexpr const char * phaserCmdName(Cmd cmd) {
      if (cmd > CMD_COUNT) cmd = CMD_COUNT;
      return allPhaserCommandNames[cmd];
    }

    static constexpr const u32 allPhaserArgCounts[CMD_COUNT+1] = {
#define XX(name,argc) argc,
      ALL_PHASER_COMMANDS()
#undef XX
      0
    };

    static constexpr u32 phaserArgCount(Cmd cmd) {
      if (cmd > CMD_COUNT) cmd = CMD_COUNT;
      return allPhaserArgCounts[cmd];
    }

    enum class CmdMask : u32 {
      // First do single flag per cmd
#define XX(name,argc) name = 1u<<CMD_##name,      
      ALL_PHASER_COMMANDS()
#undef XX

      // Now do predefined combos
#define XX(name,val) name = val,            
      ALL_PHASER_MASK_COMBOS()
#undef XX
    };

    enum Done : u8 {
      NONE_DONE = 0x00,
      DONE_HB =   0x01,
      DONE_H0 =   0x02,
      DONE_H1 =   0x04,
      DONE_H2 =   0x08,
      DONE_HN =   0x10,
      ALL_DONE =  0x1f,
    };

    struct PhaserHeader {
      u8 mSeqNo;
      Cmd mCmd;
      Done mDone;
      u8 mSeqNo1;

      void init() { memset_s(this,0,sizeof(*this)); }

      void reinit(Cmd c) {
        ++mSeqNo;
        mCmd = c;
        mDone = NONE_DONE;
        mSeqNo1 = mSeqNo + 1;
      }

      bool isValid() const { return (u8) (mSeqNo + 1) == mSeqNo1; }

      bool isHandled() const { return mDone == ALL_DONE; }

      Cmd getCmd() const { return mCmd; }

      void setDone(Done d) { mDone = (Done) (mDone | d); }

      Done getDone() const { return mDone; }

      u8 getSeqNo() const { return mSeqNo; }
    };

    void init() { memset_s(this,0,sizeof(*this)); }
    void reinit(Cmd c) { mPhaserHeader.reinit(c); }
    bool isValid() const { return mPhaserHeader.isValid(); }
    bool isHandled() const { return mPhaserHeader.isHandled(); }
    Cmd getCmd() const { return mPhaserHeader.mCmd; }
    void setDone(Done d) { mPhaserHeader.setDone(d); }
    Done getDone() const { return mPhaserHeader.getDone(); }
    u8 getSeqNo() const { return mPhaserHeader.getSeqNo(); }

    PhaserHeader mPhaserHeader;
    static constexpr u32 BOLT_DATA_BYTES = MAX_BOLT_SIZE - sizeof mPhaserHeader;
    static_assert(BOLT_DATA_BYTES%4 == 0,"bad bolt size");
    static constexpr u32 BOLT_DATA_WORDS = BOLT_DATA_BYTES/4;

    bool getBoltDataWordIfAny(u32 idx, s32 & dest) const {
      if (idx >= BOLT_DATA_WORDS) return false;
      dest = mBoltWords[idx];
      return true;
    }
    bool setBoltDataWordIfAny(u32 idx, s32 source) {
      if (idx >= BOLT_DATA_WORDS) return false;
      mBoltWords[idx] = source;
      return true;
    }
    s32 mBoltWords[BOLT_DATA_WORDS];
  };

  class PhaserBlock : public TC<PhaserBlock,sizeof(PhaserBolt)> {
  public:
    const char * getName() const { return "PhaserBlock"; }
    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    PhaserBolt & payload() { return *(PhaserBolt*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

  static_assert(sizeof(PhaserBlock)==64,"bad size");
}



