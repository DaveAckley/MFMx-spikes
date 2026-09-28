#include "FastT2.h"
#include "Printf.h"
#include "CrossUtils.h"
#include "AtomicLock.h"
#include "Debug.h"
#include "HartTasksLib.h" // for TEF stuff
#include "T6Phaser.h" 

namespace MFM {

  /// SERVICES FOR OTHER HARTS:
  u32 createByMail() {
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_NC); // NC got no mailboxes
    u32 ret =  *((volatile u32 *) (MAILBOX_BASE_T2+0u)); // BLOCKING READ
    *((volatile u32 *) (MAILBOX_BASE_T2+0u)) = 1u;       // WRITE REQUEST for next number
    return ret;
  }

  u32 create(u32 max) {
    if (__builtin_expect(max == 0u,0)) return 0u; 
    u32 bits = 32u - COUNT_LEADING_ZEROS(max-1u);
    u32 ret;
    do {
      ret = createBits(bits);
    } while (ret >= max);
    return ret;
  }

  struct FastT2 {
    RandMT mRandom;
    typedef RingBuffer<u32,4> RandomBuffer;
    RandomBuffer mRandomBuffer;
    void fillRandomBuffer() {
      while (!mRandomBuffer.isFull())
        mRandomBuffer.add(mRandom.randomMT());
    }
    u32 mBlocked;
    u32 getFromRandomBuffer() {
      u32 ret;
      while (!mRandomBuffer.remove(ret)) {
        if ((mBlocked++ & 0xffff) == 0) HBPTAG(RNDBLK,mBlocked);
      }
      return ret;
    }
  };
  FAST_LOCAL(FastT2,fT2,2);

  static volatile bool mT2Serving = false; 

  static bool isRandomServerReady() {
    return mT2Serving;
  }

  static void primePump() {
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_NC);
    *((volatile u32 *) (MAILBOX_BASE_T2)) = 1u; // write to T2 (from any but NC)

    LOGNOTE(PRIMD);
  }

  void preloadT2Mailbox() { // run once at startup on each hart except NC
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_NC); // NC got no mailboxes
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_T2); // T2 does, but doesn't use this code

    //HBNOTE("PRELD");

    while (!isRandomServerReady()) { }

    LOGNOTE(RDEE);
    
    primePump();

  }
  
  int initT2() {
    //    HBNOTE("INIT2");

    extern HostBlock theHostBlock;
    HostBlock & hb = theHostBlock;
    
    u32 seed = hb.mCommonArgs[0] * (hb.mNoC0.x+1) + (hb.mNoC0.y);
    LOGXTAG(SEED,seed);

    fT2.mRandom.seedMT_MFM(seed);

    /// DRAIN ALL INBOUND MAILBOXES, THEN SET mT2Serving
    u32 addr = MAILBOX_BASE;
    bool canread;

    for (u32 hartnum = HARTNUM_B; hartnum <= HARTNUM_T2; ++hartnum, (addr += MAILBOX_INCR)) {
      while ((canread = *((volatile u32 *) (addr+4u)))) { // TRYREAD
        u32 toss = *((volatile u32 *) (addr+0u));         // READ, discard
      }
    }

    primePump(); // Note T2 doesn't call preloadT2Mailbox()

    HBNOTE(CREAHUP);
    mT2Serving = true;

    return 0;
  }

  static int liveT2(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveT2(HostBlock & hb) {

    // SERVE BUFFERED RANDOM #s TO B,T0,T1,T2:
    // THEORY: If they send us a msg, eat it and send back a PRNG#

    // To prime the pumps so that this works properly, each
    // participating hart needs to call preloadT2Mailbox() precisely
    // once early in their boot flow, to send the initial messages.

    // After that's all set up, there will usually - hopefully - be a
    // RND# waiting for each participating hart as soon as they call
    // createByMail(). That will be sadly untrue if they are consuming
    // #s so fast we haven't refilled their buffer by their next call
    // on createByMail(), in which case they will block until we have.

    // It also means that shuffling the random state vector will occur
    // - at least in part - _between_ calls for #s, rather than while
    // a current request is pending.

    // Aaand, it also means nobody but hart T2 ever needs to see the
    // actual PRNG state, so it can be hidden in T2's local RAM, to be
    // accessed much faster and eat less L1 space.

    // (Aaaaaaand, oh yeah, the REASON we're doing all this is that
    // there's no sure way to tell, from a standing start, if a WRITE
    // to a mailbox will block or not. But we CAN test if a READ will
    // block or not, so the idea here is to rotate the initiative 180
    // degrees. At steady state, we do a WRITE to them only AFTER they
    // did a read from us, which freed up one of our outgoing slots,
    // so we can safely push once and not block.)

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    HBNOTE(PRNG LIVE);
    u32 spin = 0u;
    while (true) {
      T6Phaser::handle();
      fT2.fillRandomBuffer();
      if ((++spin & 0xff'ffff) == 0u) {
        hb.hartbeat(fAll.mHartNum);
        if (fT2.mBlocked > 0) DP.printf("RNDBLOCKED %d %d\n", spin, fT2.mBlocked);
        if ((spin & 0x7ff'ffff) == 0) {
          LOGXTAG(RNDOGHETTI,spin);
          HBXTAG(RNDOGETTY,spin);
        }
      }
      for (u32 hartnum = HARTNUM_B; hartnum <= HARTNUM_T2; ++hartnum) {
        u32 addr = MAILBOX_BASE + MAILBOX_INCR*(hartnum - HARTNUM_B);
        bool canread = *((volatile u32 *) (addr+4u)); // TRYREAD
        if (!canread) continue;
        u32 toss = *((volatile u32 *) (addr+0u)); // READ, discard
        *((volatile u32 *) (addr+0u)) = fT2.getFromRandomBuffer(); // WRITE
      }
    }
    return 0u; // NOT REACHED
  }

  // ENTERED AFTER HartTaskerPrivate.run() returns!
  int hartMainT2(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T2);
    HBPTAG(@,__FUNCTION__);
    
    hb.hartbeat(fAll.mHartNum);

    //    HBNOTE("T2LIV");
    
    return liveT2(hb);          // go do your hart t2 thing you
  }

  ////////
  extern HostBlock theHostBlock;
  
  TEFResult TaskEpochFunction_PRNG(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    switch (hei) {
    case HE_BORN0:
      if (hartnum!=HARTNUM_T2) return TEFR_NO_THANKS;
      HBNOTE(init PRNG);
      initT2();
      return TEFR_CONTINUE;

    case HE_BORN1:
      if (hartnum==HARTNUM_T2 || hartnum==HARTNUM_NC)
        return TEFR_NO_THANKS;
      preloadT2Mailbox();
      return TEFR_CONTINUE;

    case HE_GROW1:
      if (hartnum!=HARTNUM_T2) return TEFR_NO_THANKS;
      HBNOTE(H2DOGONE);
      return TEFR_HART_OUT;

    case HE_GROW2:
      if (hartnum==HARTNUM_T2 || hartnum==HARTNUM_NC)
        return TEFR_NO_THANKS;
      HBPTAG(R1K,create(1'000'000));
      return TEFR_CONTINUE;

    default:
      SNAP(3,HBPTAG(PRNG,getHartEpochName(hei)));
      break;
    }
    return TEFR_NO_THANKS;
  }
  

}
