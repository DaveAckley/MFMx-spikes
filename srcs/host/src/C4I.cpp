#include "C4I.h"

#include <fcntl.h>
#include <thread>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <inttypes.h>

#include "OurTLBs.h"
#include "CodeManager.h"
#include "Constants.h"

//#include "P2PElevator.h"
#include "TransportBlock.h"
#include "HostBlock.h"

#include "t6-exports.h"

namespace MFM {

  C4I::C4I()
  { }
}
