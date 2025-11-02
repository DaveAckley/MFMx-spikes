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

  void preloadT2Mailbox() { // run once at startup on each hart except NC
    MFM_API_ASSERT_NOT_ON_HART(HARTNUM_NC); // NC got no mailboxes
    *((volatile u32 *) (MAILBOX_BASE_T2+0u)) = 1u; // prime the random number pump
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

    while (true) {
      for (u32
             addr = MAILBOX_BASE,
             hartnum = HARTNUM_B;
           hartnum <= HARTNUM_T2;
           ++hartnum, addr += MAILBOX_INCR) {

        bool canread = *((volatile u32 *) (addr+4u)); // TRYREAD
        if (__builtin_expect(!canread,1)) continue;  
        *((volatile u32 *) (addr+0u));                // READ, discard
        *((volatile u32 *) (addr+0u)) = fT2.mRandom.randomMT(); // WRITE
      }
    }
  }

  int hartMainT2(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T2);
    preloadT2Mailbox();
    
    u32 seed = hb.mCommonArgs[0] * (hb.mXPos+1) + (hb.mYPos);
    fT2.mRandom.seedMT_MFM(seed);

    LOG.printf("%d:GO LIVE MAXSTAX %d\n",fAll.mHartNum,estimateStackUsage());

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT2(hb);          // go do your hart t2 thing you
  }

}
