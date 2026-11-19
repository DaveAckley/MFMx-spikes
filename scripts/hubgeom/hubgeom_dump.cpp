// Prints the hub geometry constants of DemoGlobal.h as NAME=value lines,
// in the same names HubGeometry.py prints.  Used by hubgeom_check.sh.
#include "DemoGlobal.h"
#include <cstdio>
#include <cstdlib>

extern "C" void DieHereNow(signed, const char *, unsigned, const char *) {
  printf("FAIL DieHereNow\n");
  _Exit(2);
}

int main() {
#define XX(N, V) printf(N "=%u\n", (unsigned) (V))
  XX("T6GRID_BASE_WIDTH", MFM::DG::T6GRID_BASE_WIDTH);
  XX("T6GRID_BASE_HEIGHT", MFM::DG::T6GRID_BASE_HEIGHT);
  XX("T6GRID_OVERLAP_WIDTH", MFM::DG::T6GRID_OVERLAP_WIDTH);
  XX("T6GRID_OVERLAP_HEIGHT", MFM::DG::T6GRID_OVERLAP_HEIGHT);
  XX("T6GRID_WIDTH", MFM::DG::T6GRID_WIDTH);
  XX("T6GRID_HEIGHT", MFM::DG::T6GRID_HEIGHT);
  XX("ACTIVE_HUB_WIDTH", MFM::DG::BLACKHOLE_ACTIVE_HUB_WIDTH);
  XX("ACTIVE_HUB_HEIGHT", MFM::DG::BLACKHOLE_ACTIVE_HUB_HEIGHT);
  return 0;
}
