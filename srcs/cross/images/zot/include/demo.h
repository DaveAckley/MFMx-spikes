#pragma once  /* -*- C++ -*- */

#include "utils.h" // for hartChar
#include "FastLocal.h" // for fAll

namespace MFM {
template<char HART>
struct LdSectionName {
  static constexpr char hartChar = HART;
  static constexpr const char value[] = {
    '.','f','a','s','t','r','a','m','_',HART,'\0'
  };
};

struct FastStuff {
  int mFastInt;
  bool mFastBool;
};

struct L1Stuff {
  char mL1Char;
  const char * mL1String;
};

template <class L1PUB, class FASTPRIV>
struct PtrPair {
  L1PUB * mL1Pub;
  FASTPRIV * mFastPriv;
};

template <class L1PUB, class FASTPRIV, typename SECTNAME>
PtrPair<L1PUB,FASTPRIV> getPtrPair() {
  static L1PUB mTheL1PubInstance; // default into L1
  static FASTPRIV __attribute__((section(SECTNAME::value))) mTheFastPrivInstance;
  PtrPair<L1PUB,FASTPRIV> ret;
  ret.mL1Pub = &mTheL1PubInstance;
  ret.mFastPriv =
    (hartChar(fAll.mHartNum) == SECTNAME::hartChar) ? // if on correct hart
    &mTheFastPrivInstance :                           // expose fastpriv obj
    0;                                                // else don't
  return ret;
}
}
