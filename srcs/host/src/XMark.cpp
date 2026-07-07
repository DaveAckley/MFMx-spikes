#include "XMark.h"
namespace MFM {
  bool XMark::parseFromIStream(std::istream& is, U8C noc0, u8 chipnum, u32 ticksbase) {
    char ch;
    u8 fxmk[8];
    if (!(is.get(ch))) return false;
    fxmk[0] = (u8) ch;
    if (fxmk[0] < 8u) return false; // gotta have all the fixed-size stuff

    for (u32 i = 1; i < sizeof(fxmk); ++i) {
      if (!(is.get(ch))) return false;
      fxmk[i] = (u8) ch;
    }

    /** Wire format:
       0: <p256len>
       1: <cmd:3b,rsrv:2b,hartnum:3b>
       2: <tickslo>
       3: <tickshi>
       4: <fidlo>
       5: <fidhi>
       6: <linlo>
       7: <linhi>

       8+ <msg>*
     */
    u8 p256len = fxmk[0];
    u8 cmd = fxmk[1]>>5;
    u8 hartnum = fxmk[1]&0x7;
    u16 ticks = (fxmk[3]<<8) | fxmk[2];
    u16 fid = (fxmk[5]<<8) | fxmk[4];
    u16 lin = (fxmk[7]<<8) | fxmk[6];
    std::string msg = "";
    for (u32 i = 8; i < p256len; ++i) {
      if (!(is.get(ch))) return false;
      if (ch == '}') ch = '_'; // "OoB"
      msg.push_back(ch);
    }

    mTicksRelative = ticks;
    mTickStamp = ((u64)ticksbase) + ticks;
    mFidLin = U16C(fid,lin);
    mNoC = noc0;
    mChipNum = chipnum;
    mHartNum = hartnum;
    mCmd = cmd;
    mMsg = msg;
    return mValid = true;
  }

  void XMark::formatToOStream(std::ostream& os) {
    MFM_API_ASSERT(isValid(),ILLEGAL_STATE);
    os
      << "{" << (u32) mFidLin.x << ":" << (u32) mFidLin.y
      << " " << mTickStamp
      << " " << (u32) mChipNum
      << " " << (u32) mNoC.x << "," << (u32) mNoC.y
      << " " << "h" << hartChar(mHartNum)
      << " " << mMsg
      << "}" << std::endl;
  }
}
