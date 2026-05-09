#include "ImageBlock.h"
#include "BlockCode.h"
#ifdef BUILD_HOST
#include <stdio.h> // for snprintf
#else
#include "Printf.h" // for snprintf
#endif

namespace MFM {
  const char *ImageBlockAddr::to_repr() const {
    static constexpr u32 BUF_SIZ = 100;
    static char buf[BUF_SIZ];
    const ImageBlockAddr & iba = *this;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
    if (iba.goodMagic()) 
      snprintf(buf,BUF_SIZ,
               "<ImageBlockAddr:bc=%s(%u),al=%u,hc=%u/0x%x,ad=0x%x>",
               getNameFromBlockCode((BlockCode) iba.getBlockCode()),
               iba.getBlockCode(),
               iba.getStorageCount(),
               iba.getHostChunkOffsetOpt(),
               iba.getHostChunkOffsetOpt() == U8_MAX ? 0u :
               HOST_COMMS_MAP_CHUNK_SIZE*iba.getHostChunkOffsetOpt(),
               iba.getBlockAddr());
    else
      snprintf(buf,BUF_SIZ,
               "<ImageBlockAddr:invalid(0x%x),%u,%u,0x%x>",
               iba.mIBAMagic,
               iba.getBlockCode(),
               iba.getStorageCount(),
               iba.getBlockAddr());
#pragma GCC diagnostic pop
    return buf;
  }
}
