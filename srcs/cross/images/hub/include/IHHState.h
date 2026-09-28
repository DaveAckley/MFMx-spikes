#pragma once   /* -*- C++ -*- */

namespace MFM {

#define IHH_ALL_STATES()                               \
  XX(IHH_NONE, initial and null state 0)               \
  XX(IHH_PAUSE,events holding for external control)    \
  XX(IHH_RUN,running events under current parms)       \
  XX(IHH_RECV_SITES,leader accepting sites)            \
  XX(IHH_SEND_SITES,follower sending sites)            \
  XX(IHH_SEND_CACHE,leader sending cache)              \
  XX(IHH_RECV_CACHE,follower accepting cache)          \


  enum IHHState : u8 {
#define XX(N,C) N,
    IHH_ALL_STATES()
#undef XX      
      };
    
  static constexpr u32 IHH_STATE_COUNT =
#define XX(N,C) +1
    IHH_ALL_STATES()
#undef XX      
    ;

  static constexpr const char * IHH_STATE_NAMES[] = {
#define XX(N,C) ""#N,      
    IHH_ALL_STATES()
#undef XX
  };

  static constexpr const char * getIHHStateName(IHHState us) {
    if (us >= IHH_STATE_COUNT) return "??";
    return IHH_STATE_NAMES[us];
  }
}
