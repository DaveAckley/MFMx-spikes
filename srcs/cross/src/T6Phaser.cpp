#include "T6Phaser.h"
#include "FastLocal.h" // for sleepCycles
#include "HostBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;

  namespace T6Phaser {
    void handle() {
      PhaserBlock & pb = getPhaserBlock();
      {
        while (pb.payload().getCmd() == PhaserBolt::CMD_ALL_HARTS_PAUSE) {
          EACH(100'000,HBPTAG(PHASEPAUSE,__EACHNUM__));
          sleepCycles(100'000);
        }
      }
      {
        HostBlock & hb = theHostBlock;
        while (pb.payload().getCmd() == PhaserBolt::CMD_HOLD_AT_BIRTH &&
               hb.mPerHartStatus[fAll.mHartNum] == FAILCode::LIVING) {
          EACH(100'000,HBPTAG(PHASEHOLD,__EACHNUM__));
          sleepCycles(100'000);
        }
      }
    }
  }
}

