#include "T6Image.h"
#include "HostBlock.h"
#include <fstream>
#include "Fail.h"
#include "BytePhys.h"
#include <cstring>
#include <iterator>

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
             getBinFileWords()[WORD_OF_PTR_TO_IMAGE_BLOCK_HEADER]);
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

  /* Read a BCP file, verify it exactly as the EWP will (verifyBCP:
     header, checksum, tables, every instruction), check that it fits
     between the end of the image and L1_IMAGE_LIMIT, and keep it for
     CodeManager::deployRISCVCodeFromImage to write after the image. */
  std::string T6Image::attachPhysics(std::string path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "cannot open " + path;
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    const u32 size = (u32) bytes.size();
    char buf[256];
    if (size == 0u || size % 4u != 0u) {
      snprintf(buf, sizeof(buf), "%s: %u bytes, not a whole number of "
               "words", path.c_str(), size);
      return buf;
    }
    std::vector<u32> words(size / 4u);
    memcpy(words.data(), bytes.data(), size);
    const u8 * b = (const u8 *) words.data();
    const u32 bad = BytePhys::verifyBCP(b, size);
    if (bad != 0u) {
      snprintf(buf, sizeof(buf), "%s: verifyBCP refuses it (0x%x, "
               "BytePhys.h 'BCP')", path.c_str(), bad);
      return buf;
    }
    const u32 at = physicsAddr();
    if (at > L1_IMAGE_LIMIT || size > L1_IMAGE_LIMIT - at) {
      snprintf(buf, sizeof(buf), "%s: %u bytes do not fit after image %s "
               "(0x%x..0x%x, room %u)", path.c_str(), size, mIKey.c_str(),
               at, L1_IMAGE_LIMIT,
               at < L1_IMAGE_LIMIT ? L1_IMAGE_LIMIT - at : 0u);
      return buf;
    }
    mPhysicsWords = words;
    mPhysicsPath = path;
    return "";
  }

  void T6Image::attachNoPhysics() {
    mPhysicsWords.assign(4u, 0u);
    mPhysicsPath = "";
  }

  std::string T6Image::describePhysics() const {
    char buf[512];
    const u32 at = physicsAddr();
    if (!takesPhysics())
      snprintf(buf, sizeof(buf), "%s: takes no physics", mIKey.c_str());
    else if (mPhysicsPath.size() == 0u)
      snprintf(buf, sizeof(buf), "%s: no physics loaded (empty marker at "
               "0x%x; the image's compiled-in one runs)", mIKey.c_str(), at);
    else {
      const u32 size = (u32) mPhysicsWords.size() * 4u;
      const u8 * b = (const u8 *) mPhysicsWords.data();
      snprintf(buf, sizeof(buf), "%s: physics %s, %u bytes, checksum "
               "0x%08x, %u types from %u, at 0x%x (room %u)",
               mIKey.c_str(), mPhysicsPath.c_str(), size,
               BytePhys::bcpRd32(b, BytePhys::BCPH_CHECKSUM),
               BytePhys::bcpRd16(b, BytePhys::BCPH_TYPES),
               BytePhys::bcpRd16(b, BytePhys::BCPH_FIRST_TYPE), at,
               L1_IMAGE_LIMIT - at);
    }
    return buf;
  }
}
