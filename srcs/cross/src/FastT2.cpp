#include "FastT2.h"
#include "Printf.h"

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
  };
  FAST_LOCAL(FastT2,fT2,t2);
  bool mT2Serving = false;
  static bool randomServerReady() { return mT2Serving; }

  static void primePump() {
    *((volatile u32 *) (MAILBOX_BASE_T2+0u)) = 1u; // prime the random number pump
  }

  void preloadT2Mailbox() { // run once at startup on each hart except NC
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_NC); // NC got no mailboxes
    while (!randomServerReady()) { }
    primePump();
  }
  
  static void initT2() {
    /// DRAIN ALL INBOUND MAILBOXES, THEN SET mT2Serving
    u32 addr = MAILBOX_BASE;
    bool canread;
    for (u32 hartnum = HARTNUM_B; hartnum <= HARTNUM_T2; ++hartnum, (addr += MAILBOX_INCR)) {
      while ((canread = *((volatile u32 *) (addr+4u)))) { // TRYREAD
        u32 toss = *((volatile u32 *) (addr+0u));         // READ, discard
      }
    }
    mT2Serving = true;
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

    initT2();
    
    u8 spin = 0u;
    while (true) {
      if (spin++ == 0u) hb.hartbeat(fAll.mHartNum);
      for (u32 hartnum = HARTNUM_B; hartnum <= HARTNUM_T2; ++hartnum) {
        u32 addr = MAILBOX_BASE + MAILBOX_INCR*(hartnum - HARTNUM_B);
        bool canread = *((volatile u32 *) (addr+4u)); // TRYREAD
        if (!canread) continue;
        u32 toss = *((volatile u32 *) (addr+0u)); // READ, discard
        *((volatile u32 *) (addr+0u)) = fT2.mRandom.randomMT(); // WRITE
      }
    }
    return 0u; // NOT REACHED
  }

  int hartMainT2(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T2);
    primePump(); // Note T2 doesn't call preloadT2Mailbox()
    
    DP.printf("T2 HI1\n");
    u32 seed = hb.mCommonArgs[0] * (hb.mPos.x+1) + (hb.mPos.y);
    fT2.mRandom.seedMT_MFM(seed);
    hb.hartbeat(fAll.mHartNum);
    DP.printf("T2 HI2 %d\n",hb.mPerHartWatchdog[fAll.mHartNum]);

    LOG.printf("%s:GO LIVE MAXSTAX %d\n",hartName(fAll.mHartNum),estimateStackUsage());

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    return liveT2(hb);          // go do your hart t2 thing you
  }

}
