#include "T6Phaser.h"
#include "FastLocal.h" // for sleepCycles

namespace MFM {
  void T6Phaser::handle() {
    while (true) {
      PhaserBlock & pb = getPhaserBlock();
      if (pb.payload().getCmd() != PhaserBolt::CMD_ALL_HARTS_PAUSE) break;
      sleepCycles(100'000);
    }
  }
}
