#include "T6Image.h"
#include "HostBlock.h"
#include <fstream>
#include "Fail.h"

namespace MFM {
  void T6Image::init(std::string ikey, u8 imageCode, std::string path) {
    mIKey = ikey;
    mBinFilePath = path;
    mImageCode = imageCode;
    loadBinFile();
    MFM_API_ASSERT(mImageCode==getImageCode(),BAD_VALUE);
  }

  std::string T6Image::to_repr() const {
    char buf[1024];
    snprintf(buf,sizeof(buf),"<T6Image:%s.%u,len=%u,ibh=0x%08x>",
             mIKey.c_str(),
             getImageCode(),
             getBinFileSize(),
             getBinFileWords()[5]);
    return std::string(buf);
  }

  void T6Image::loadBinFile() {
    std::ifstream file(mBinFilePath, std::ios::binary | std::ios::ate); // Open in binary mode and at end

    if (!file.is_open()) 
      HOST_FATAL(NOT_FOUND,"Failed to open file: %s", mBinFilePath.c_str());

    mBinFileSize = file.tellg(); 
    mHostBlockAddr = mBinFileSize - sizeof(HostBlock);

    file.seekg(0, std::ios::beg); // Seek back to beginning

    mTheBinFile = std::make_unique<char[]>(mBinFileSize);

    if (!file.read(mTheBinFile.get(), mBinFileSize))
      HOST_FATAL(READ_FAILURE,"Failed to read file: %s", mBinFilePath.c_str());

    file.close();
  }
}
