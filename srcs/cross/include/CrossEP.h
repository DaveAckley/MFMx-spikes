#pragma once     /* -*- C++ -*- */
#include "EP.h"
#include "Printf.h"

namespace MFM {
  template <class SUB, class CAR_CONTENT>
  struct CrossEP : public EP<SUB, CAR_CONTENT> {
    Printer & to_repr(Printer &p) const ;
    Printer * mLogTo;

    Printer & logTo() {
      MFM_API_ASSERT_NONNULL(mLogTo);
      return *mLogTo;
    }
    void setPrinter(Printer * printptr) { mLogTo = printptr ? printptr : &DEVNULL; }
  };

  template <class SUB, class CAR_CONTENT>
  Printer & CrossEP<SUB,CAR_CONTENT>::to_repr(Printer &p) const {
    p.printf("<%s.%c:0x%p #%u @%u>\n",
             this->getName(),
             this->isIn()?'I':'O',
             this,
             this->getCarCount(),
             this->getCurrentCarIndex()
             );
    return p;
  }
  

}
