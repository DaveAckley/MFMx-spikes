#include "ZHostDecompressor.h"
#include "HostUtils.h"
#include "BGRImage.h"
#include "QuietBox.h"
#include "OurTLBs.h"
#include "Blackhole.h"
#include "ImageCode.h"

namespace MFM {
  static RGBPix renderPhysicsHACK(P4Atom a) {
    RGBPix c;
    u16 t = a.getType();
    switch (t) {
    case P4Atom::EMPTY_TYPE:
      c.set(15u,10u,5u);        // assume blackish for empty
      break; 

    case 2u: // (DReg)
      c.set(250u,20u,30u);
      break;

    case 3u: // (Res)
      c.set(20u,250u,30u);
      break;

    case 4u: // MINFB and
    case 5u: // MAXFB
      {
        constexpr u32 slowBits = 1u;
        u32 val = a.mStg[1]; // get hidden counter
        u8 rd = (val>>0+slowBits)&0xf; rd = (rd-8)*(rd-8); // underflow
        u8 gd = (val>>4+slowBits)&0xf; gd = (gd-8)*(gd-8); // overflow
        u8 bd = (val>>8+slowBits)&0xf; bd = (bd-8)*(bd-8); // whatever
        c.set(50u+3u*rd,50u+3u*gd,50+3u*bd);
      }
      break;

    case P4Atom::INACCESSIBLE_TYPE:
      c.set(0x30,0x40,0x50);
      break;

    default:
      c.set((u8) (t*50), 20u, (u8) (255-(t*50)));
    }
    return c;
  }

  static s32 ZHByteSource(bool canread, void * ctxt) { // >=0 byte, -1 eof, -2 blocked
    MFM_API_ASSERT_NONNULL(ctxt);
    ZHostDecompressor & zhd = *(ZHostDecompressor*) ctxt;
    return zhd.sourceByte(canread);
  }

  static bool ZHByteSink(const u8 byte, void * ctxt) { // true: wrote, false: blocked, error, or eof
    MFM_API_ASSERT_NONNULL(ctxt);
    ZHostDecompressor & zhd = *(ZHostDecompressor*) ctxt;
    return zhd.sinkByte(byte);
  }

  ZHostDecompressor::ZHostDecompressor()
    : mTLBI(U32_MAX)
  {
  }

  bool ZHostDecompressor::sinkByte(const u8 byte) {
    //    SNAP(50,KTprintf(mBHTag,"ZHDHARO OUT 0x%02x\n",byte));
    if (mQuitDecompressorThread.load()) { // should we quit?
      KTprintf(mBHTag,"ZHD %p GUDBYEEO %u\n",this,mTLBI);
      throw "OUT";
    }
    //    EACH(10'000,KTprintf(mBHTag,"CLAMBONES GONNA RISE %u\n",__EACHNUM__));
    if (mHARIO.tryPutByte(byte)) {
      //EACH(1,KTprintf(mBHTag,"<xLZO:%u%u 0x%02x\n",countDecimalDigits(__EACHNUM__),__EACHNUM__,byte));

      // consumed a byte. 
      mUncompressedBytesOut++;
      //EACH(1,KTprintf(mBHTag,"HRUB:#%u 0x%02x\n",mUncompressedBytesOut,byte));
      /*EACH(10'000,KTprintf(mBHTag,"AT ZBSink %p %u/%u = %0.1f%%  %0.1fx\n",this,
                            mUncompressedBytesOut,
                            mCompressedBytesIn,
                            100.0*mCompressedBytesIn/mUncompressedBytesOut,
                            ((double)mUncompressedBytesOut)/mCompressedBytesIn
                            ));*/

      if (mHARIO.canGetObj()) {
        AtomReport ar;
        mHARIO.getObj(ar);        // produced an ar.
        mHARIO.discardBytes();    // get ready to go again
        u16 spin = (u16) (mAtomReportsReceived&0xffff);
        bool arvalid = ar.isValid(spin);
        bool atomvalid = ar.mAtom.isValid();
        ++mAtomReportsReceived;
        if (arvalid && atomvalid) {
          BGRImageHD & bgr = QuietBox::getT6GridImage() ;
          QuietBox& qb = QuietBox::get();

          Blackhole * bhp = qb.getBlackholeIfPresent(mChipNum);
          MFM_API_ASSERT_NONNULL(bhp);
          Blackhole & bh = *bhp;
          OurTLBs & tlbs = bh.getOurTLBs();
          OurTLBs::TLBInfo & info = tlbs.getTLBInfo(mTLBI);
          const T6Image & image = info.getDeployedImageOrDie();
          u32 ic = image.getImageCode();
          if (ic == ImageCode::IC_HUB) {

            CellBlock cb = image.copyCellBlockOrDie();
            U8C stride = cb.mCellStride;
          
            U16C c = DG::getChipOrigin(mChipNum); 
            U16C o = c + DG::getTLBIOrigin(mTLBI,stride); 
            const T6GridInfo & t6i = qb.getT6GridInfoFor(DG::Coord(o.x,o.y));
            U16C gridc(t6i.mT6GridOrigin.x + ar.mCoord.x,
                       t6i.mT6GridOrigin.y + ar.mCoord.y);
            RGBPix color = renderPhysicsHACK(ar.mAtom);
            bgr.setPixel(gridc,color);
          }
        }

        //        EACH(1,KTprintf(mBHTag,"#%llu (%u=%u=%u%s) HAR@(%u,%u) = 0x%04x'%04x'%08x'%08x %s%s\n",
        EACH(10'000,KTprintf(mBHTag,"#%llu (%u=%u=%u%s) HAR@(%u,%u) %s%s\n",
                        mAtomReportsReceived,
                        spin,ar.mSpin1,ar.mSpin2,arvalid?"":" XXX",
                        ar.mCoord.x,ar.mCoord.y,
                        //                        ar.mAtom.mParityAndType,ar.mAtom.mData0,
                        //                        ar.mAtom.mStg[0],ar.mAtom.mStg[1],
                        arvalid?"":"INVALID ",
                        atomvalid? "GOOD" : "INVALID"));
      }
      return true;
    }
    
    EACH(100'000,KTprintf(mBHTag,"ZBSink FALSE %u\n",__EACHNUM__));
    return false; // try again later
  }

  s32 ZHostDecompressor::sourceByte(bool canread) {
    SNAP(3,KTprintf(mBHTag,"ZHDHARO IN %u\n",canread));
    bool htp = false; // mTLBI >= 15 && mTLBI <= 20;

    if (mQuitDecompressorThread.load()) { // should we quit?
      KTprintf(mBHTag,"ZHD %p GUDBYEEI %u\n",this,mTLBI);
      throw "IN";
    }

    //EACH(100'000'000,HTprintf("ZHD %p sourceByte(%u) %u\n",this,canread,__EACHNUM__));
    // Check if have a current packet
    if (mCurrentCarACB.second) {
      ACacheBlock & acb = *mCurrentCarACB.second; // We do.
      ACacheBlockPayload & pay = acb.payload();
      if (mCurrentBytesRead < pay.getCurrentLength()) { // Anything left?
        if (canread) return 0;  // EOF test? Not EOF.
        u32 readfrom = mCurrentBytesRead;
        u8 byte = pay.getByteOrDie(mCurrentBytesRead++);
        ++mCompressedBytesIn;
        {
          u32 idx = mCompressedBytesIn-1;
          if ((idx % 990) > 980 || (idx % 990) <  20)
        //EACH(1,KTprintf(mBHTag,"HRCB:#%llu 0x%02x %u/%u\n",
          EACH(100'000,KTprintf(mBHTag,"HRCB:#%llu 0x%02x %u/%u\n",
                          idx, byte,
                          mCurrentBytesRead-1,
                          pay.getCurrentLength()));
        }
        return (s32) byte; // or read return next
      }
      // FALL THROUGH

      if (false) EACH(1,KTprintf(mBHTag,"ACB %p DONE\n",&acb));
      // Current packet all used up. Return it
      {
        u32 idx = pay.getCurrentLength()-1;
        if (false)
          KTprintf(mBHTag,"Last of old: #%u = 0x%02x\n",
                   idx,
                   pay.getByteOrDie(idx));
      }
      if (false)
        KTprintf(mBHTag,"ZBSOURCE T%u +%uB (%llu) RETURNING car%u %p\n",
                 mTLBI, pay.getCurrentLength(), mCompressedBytesIn, mCurrentCarACB.first, &acb);
      while (!mACBO.add(mCurrentCarACB)) EACH(100'000,HBPTAG(SPIN?,__EACHNUM__));
      if (false)
        KTprintf(mBHTag,"ZBSOURCE T%u RETURNED car%u %p\n",
                 mTLBI,
                 mCurrentCarACB.first,
                 mCurrentCarACB.second
                 );
      mCurrentCarACB.second = 0;
      // FALL THROUGH
    }
    if (htp) SNAP(3,KTprintf(mBHTag,"ZHDsB13\n"));

    // We do not have a current packet
    if (mACBI.remove(mCurrentCarACB)) { // Now we do
      //XXX seeing a null?      MFM_API_ASSERT_NONNULL(mCurrentCarACB.second);
      if (mCurrentCarACB.second) {
        mCurrentBytesRead = 0;    // set up to read it
        if (false)
          EACH(1,KTprintf(mBHTag,"%u ZHD ANUCAR car%u %p +%d 0x%04x %dB\n",
                          __EACHNUM__,
                          mCurrentCarACB.first,
                          mCurrentCarACB.second,
                          mCurrentCarACB.second->payload().getCurrentLength(),
                          mCurrentCarACB.second->payload().getCurrentFlags(),
                          mCurrentCarACB.second->payload().getBytesRemaining()
                          ));
        {
          ACacheBlock & nacb = *mCurrentCarACB.second; 
          ACacheBlockPayload & npay = nacb.payload();
          u32 len = npay.getCurrentLength();
          if (false && len > 0)
            KTprintf(mBHTag,"First of new: @%u/%u = 0x%02x\n",
                     (u32) 0,
                     len,
                     npay.getByteOrDie(0));
        }
      } else KTprintf(mBHTag,"ZHDnull y y y?\n");

      // FALL THROUGH
    }
    if (false && htp) EACH(2'000,KTprintf(mBHTag,"ZHDsB15 %u\n",__EACHNUM__));
    return -2;                  // blocked, try again
  }
  
  void ZHostDecompressor::init(const u32 tlbi, const u32 chipnum) {
    if (mDecompressorThreadPtr) // already initted?
      HOST_FATAL(ILLEGAL_STATE,"Thread already running"); // uwack
    mQuitDecompressorThread.store(false); // set up for thread

    mHARIO.init(false); // false: init for deserialization (bytes -> obj)
    mACBI.init();
    mACBO.init();
    mCurrentCarACB.second = 0; // NULL ptr means no current

    mTLBI = tlbi;
    mChipNum = chipnum;
    mBHTag = BHTag(TagType::T6TADR, (u8) mChipNum, (u32) mTLBI);

    mLZ.init(ZHByteSource, this, ZHByteSink, this);
    HTprintf("ZHD %p HERE HALLO %u HARIO\n",this,tlbi);
    KTprintf(mBHTag,"ZHD %p HERE HALLO %u HARIO\n",this,tlbi);

    mDecompressorThreadPtr = std::make_unique<std::thread>([this,tlbi]() {
      try {
        HTprintf("ZHDecompThread %p HERE HALLO\n",this);
        mLZ.decode();
      }
      catch (const char * str) {
        HTprintf("ZHD CATCH %p GUDBYEE %u (%s)\n",this,tlbi,str);
      }
    });    
  }
}
