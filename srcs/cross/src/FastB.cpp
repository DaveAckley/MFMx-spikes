#include "FastLocal.h"
#include "FastB.h"
#include "LiveB.h"
#include "Printf.h"
//#include "T6ImageBlock.h"
#include "BlockCode.h"
#include "StandardLife.h"
//#include "HartTasksLib.h" // for PublicSequencer etc
#include "Debug.h" // for HBNOTE etc

namespace MFM {

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    //    HBPTAG(@,__FUNCTION__);
    return liveB(hb);          // go do your hart B thing you
  }
}
