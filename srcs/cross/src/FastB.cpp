#include "FastLocal.h"
#include "FastB.h"
#include "LiveB.h"
#include "Printf.h"
//#include "T6ImageBlock.h"
#include "BlockCode.h"
#include "DefaultLives.h"
//#include "T6CellO.h"

namespace MFM {

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    return liveB(hb);          // go do your hart B thing you
  }
}
