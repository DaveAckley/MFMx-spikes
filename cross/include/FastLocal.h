#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"

#include "FastB.h"  
#include "FastT0.h" 
#include "FastT1.h" 
#include "FastT2.h" 
#include "FastNC.h" 

namespace MFM {
  struct FastAll {
    u8 mHartNum, mXPos, mYPos, mRsrv1;
  };

  extern FastAll fAll;
  extern FastB fB;
  extern FastT0 fT0;
  extern FastT1 fT1;
  extern FastT2 fT2;
  extern FastNC fNC;
}
