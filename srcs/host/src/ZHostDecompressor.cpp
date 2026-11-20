#include "ZHostDecompressor.h"
#include "HostUtils.h"
#include "BGRImage.h"
#include "QuietBox.h"
#include "OurTLBs.h"
#include "Blackhole.h"
#include "ImageCode.h"

namespace MFM {
  u32 ZHostDecompressor::mMade = 0;

  // Thrown inside the decoder thread (from sourceByte) to unwind decode() when
  // the hub has restarted its stream: the thread resets and decodes again.
  struct RestartStream { };
  static std::atomic<bool> theRestartEnabled{true};
  void ZHostDecompressor::setRestartEnabled(bool on) { theRestartEnabled.store(on); }
  bool ZHostDecompressor::restartEnabled() { return theRestartEnabled.load(); }

  // Every initted decompressor, for snapshotHealth().
  static std::mutex theZHDRegistryLock;
  static std::vector<ZHostDecompressor*> theZHDRegistry;

  bool ZHostDecompressor::injectDrop(u32 chip, u32 tlbi) {
    std::lock_guard<std::mutex> lock(theZHDRegistryLock);
    for (ZHostDecompressor * z : theZHDRegistry) {
      if (z->mChipNum == chip && z->mTLBI == tlbi) {
        z->mInjectDrop.store(true);
        return true;
      }
    }
    return false;
  }
  
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
    ++mMade;
    //    HTprintf("ZHD %p #%u\n",this,mMade);
  }

  ZHostDecompressor::~ZHostDecompressor() {
    if (mRegistered) {
      std::lock_guard<std::mutex> lock(theZHDRegistryLock);
      for (size_t i = 0; i < theZHDRegistry.size(); ++i) {
        if (theZHDRegistry[i] == this) {
          theZHDRegistry.erase(theZHDRegistry.begin() + i);
          break;
        }
      }
      mRegistered = false;
    }
    // Destroying a joinable std::thread calls std::terminate, which used
    // to abort the process at exit (one thread per hub).
    stopThread();
  }

  void ZHostDecompressor::stopThread() {
    mQuitDecompressorThread.store(true);
    if (mDecompressorThreadPtr && mDecompressorThreadPtr->joinable()) {
      mDecompressorThreadPtr->join();
    }
    mDecompressorThreadPtr.reset();
  }

  bool ZHostDecompressor::sinkByte(const u8 byte) {
    QuietBox & qb = QuietBox::get();
    //    SNAP(50,KTprintf(mBHTag,"ZHDHARO OUT 0x%02x\n",byte));
    if (mQuitDecompressorThread.load()) { // should we quit?
      KTprintf(mBHTag,"ZHD %p GUDBYEEO %u\n",this,mTLBI);
      throw "OUT";
    }
    //    EACH(10'000,KTprintf(mBHTag,"CLAMBONES GONNA RISE %u\n",__EACHNUM__));
    if (mHARIO.tryPutByte(byte)) {
      //EACH(1,KTprintf(mBHTag,"<xLZO:%u%u 0x%02x\n",countDecimalDigits(__EACHNUM__),__EACHNUM__,byte));

      // consumed a byte. 
      mOutHist[mOutHistPos++ % OUT_HISTORY] = byte;
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
        if (mSpinSync && ar.mSpin1 == ar.mSpin2) {
          // First record of a restarted stream: the hub's spin counter kept
          // running, so take its value as the one we expect now.
          u64 base = mAtomReportsReceived & ~0xffffull;
          if (ar.mSpin1 < (mAtomReportsReceived & 0xffffull)) base += 0x10000ull;
          mAtomReportsReceived = base | ar.mSpin1;
          mSpinSync = false;
        }
        u16 spin = (u16) (mAtomReportsReceived&0xffff);
        bool arvalid = ar.isValid(spin);
        bool atomvalid = ar.mAtom.isValid();
        // The spin tracks the TRANSPORT: it advances for every report that
        // arrived intact, whether or not its atom is any good (the hub can
        // read an atom while it is changing).  It used to advance only for
        // good atoms, so one torn atom made every later report look bad.
        if (arvalid) {
          noteGoodReport();
          ++mAtomReportsReceived;
          if (!atomvalid) ++mBadAtoms;   // skipped, not drawn
        } else {
          noteBadReport(ar,spin);
        }
        if (arvalid && atomvalid) {
          BGRImageHD & bgr = QuietBox::getT6GridImage() ;
          QuietBox& qb = QuietBox::get();

          Blackhole * bhp = qb.getBlackholeIfPresent(mChipNum);
          MFM_API_ASSERT_NONNULL(bhp);
          Blackhole & bh = *bhp;
          OurTLBs & tlbs = bh.getOurTLBs();
          OurTLBs::TLBInfo & info = tlbs.getTLBInfo(mTLBI);
          // The chip may be retreating (image undeployed) while this
          // thread still has atom reports queued: drop them.
          const T6Image * imgp = info.getDeployedImageIfAny();
          if (!imgp) return true;
          const T6Image & image = *imgp;
          u32 ic = image.getImageCode();
          if (ic == ImageCode::IC_HUB) {

            CellBlock cb = image.copyCellBlockOrDie();
            U8C stride = cb.mCellStride;
          
            U16C c = DG::getChipOrigin(mChipNum); 
            U16C o = c + DG::getTLBIOrigin(mTLBI,stride); 
      
            const T6GridInfo & t6i = qb.getT6GridInfoFor(DG::Coord(o.x,o.y));
            U16C gridc(t6i.mT6GridOrigin.x + ar.mCoord.x,
                       t6i.mT6GridOrigin.y + ar.mCoord.y);

            if (qb.mDrawGrid) {
              U16C nocacheo = o + U16C(4,4); // hub origin (rings are cropped from the image), (4,4) to get into EW center range
              RGBPix fg(255,255,255), bg(0,0,0);
              /*
              std::string label = mBHTag.to_string() + "\n" +
                o.to_string() + "\n" + t6i.mT6GridOrigin.to_string();
              */
              std::string label = o.to_string() + "\n" + mBHTag.to_string();
              bgr.drawString(nocacheo,label,fg,bg,2,2);
            }

            EACH(1'000'000,{
                KTprintf(mBHTag,"RNDER10 #%u t%u go = (%u,%u), ar = (%u,%u)\n",
                         mChipNum, mTLBI,
                         t6i.mT6GridOrigin.x, t6i.mT6GridOrigin.y,
                         ar.mCoord.x,ar.mCoord.y);
                KTprintf(mBHTag,"RNDER11 c = (%u,%u), o = (%u,%u), gc = (%u,%u)\n",
                         c.x,c.y,
                         o.x,o.y,
                         gridc.x,gridc.y);
              });
            
            RGBPix color = renderPhysicsHACK(ar.mAtom);
            U16C pixc;
            if (DG::gridCoordToPixel(gridc,pixc)) // N/W rings cropped, DG.h
              bgr.setPixel(pixc,color);
            qb.setSimAtom(gridc,ar.mAtom);
          }
        }

        //        EACH(1,KTprintf(mBHTag,"#%llu (%u=%u=%u%s) HAR@(%u,%u) = 0x%04x'%04x'%08x'%08x %s%s\n",
        EACH(100'000,KTprintf(mBHTag,"#%llu (%u=%u=%u%s) HAR@(%u,%u) %s%s\n",
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
    //    SNAP(3,KTprintf(mBHTag,"%p ZHDHARO IN %u\n",this,canread));
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
      if (mCurrentBytesRead < pay.getDataLength()) { // Anything left? (not the trailer)
        if (canread) return 0;  // EOF test? Not EOF.
        if (mInjectDrop.load() && mCurrentBytesRead + 1u < pay.getDataLength()) {
          mInjectDrop.store(false); // TEST: lose one byte, as a real loss would
          ++mCurrentBytesRead;
          ++mCompressedBytesIn;
          HTprintf("ZHD TEST dropped one compressed byte chip%u t%u (packet #%u, offset %u)\n",
                   mChipNum,mTLBI,mPacketsIn,mCurrentBytesRead-1u);
        }
        u32 readfrom = mCurrentBytesRead;
        u8 byte = pay.getByteOrDie(mCurrentBytesRead++);
        ++mCompressedBytesIn;
        {
          u32 idx = mCompressedBytesIn-1;
          if ((idx % 986) > 976 || (idx % 986) <  20)
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
          notePacket(mCurrentCarACB.first, npay);
          if (isRestartSOF(npay)) // the hub answered our request: new stream
            throw RestartStream();
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
  
  void ZHostDecompressor::notePacket(u32 car, const ACacheBlockPayload & pay) {
    PacketRec & r = mPktHist[mPacketsIn % PKT_HISTORY];
    ++mPacketsIn;
    u32 len = pay.getCurrentLength();
    r.mSeq = mPacketsIn;
    r.mCar = car;
    r.mFlagsAndLen = pay.mFlagsAndLen;
    r.mStartOffset = mCompressedBytesIn;
    for (u32 i = 0u; i < 4u; ++i) {
      r.mHead[i] = i < len ? pay.mCData[i] : 0u;
      r.mTail[i] = len >= 4u ? pay.mCData[len-4u+i] : 0u;
    }
    if (len > ACacheBlockPayload::ACBP_BUFFER_SIZE) // cannot be
      KTprintf(mBHTag,"ZHD BADPKT #%u car%u len %u flags 0x%04x\n",
               mPacketsIn,car,len,pay.getCurrentFlags());
    {                           // the hub's integrity trailer
      u16 seq = 0u;
      bool sumok = pay.checkTrailer(seq);
      bool seqok = !mHaveSeq || seq == mNextSeq;
      if (!sumok) {
        ++mPktSumBad;
        if (mPktSumBad <= 20u)
          KTprintf(mBHTag,"ZHD PKTSUM chip%u t%u pkt#%u car%u BAD CHECKSUM (seq %u) len %u, %u so far\n",
                   mChipNum,mTLBI,mPacketsIn,car,seq,len,mPktSumBad);
      } else if (!seqok) {
        ++mPktSeqBad;
        if (mPktSeqBad <= 20u)
          KTprintf(mBHTag,"ZHD PKTSEQ chip%u t%u pkt#%u car%u expected seq %u got %u (%d), %u so far\n",
                   mChipNum,mTLBI,mPacketsIn,car,mNextSeq,seq,
                   (int) (s16) (u16) (seq - mNextSeq),mPktSeqBad);
      }
      if (sumok) { mHaveSeq = true; mNextSeq = (u16) (seq + 1u); }
    }
  }

  void ZHostDecompressor::noteGoodReport() {
    if (mConsecInvalid == 0u) return; // the common case: nothing to do
    std::lock_guard<std::mutex> lock(mHealthLock);
    // Only reachable if a stream comes back by itself (a recovery).
    KTprintf(mBHTag,"ZHD RESYNC chip%u t%u after %u bad reports, now #%llu\n",
             mChipNum,mTLBI,mConsecInvalid,(unsigned long long) mAtomReportsReceived);
    mConsecInvalid = 0u;
    mDesynced.store(false);
    mRestartRequested.store(false);   // it came back by itself
  }

  void ZHostDecompressor::noteBadReport(const AtomReport & ar, u16 expected) {
    std::lock_guard<std::mutex> lock(mHealthLock);
    ++mInvalidTotal;
    ++mConsecInvalid;
    if (mConsecInvalid == 1u) {
      // First bad report of an episode: capture everything now, while
      // the packet and output history still end at the break.
      mDesyncAtReport = mAtomReportsReceived;
      mDesyncAtCBytes = mCompressedBytesIn;
      mDesyncAtPacket = mPacketsIn;
      // Both spins equal and merely not the expected one: whole reports
      // were skipped (or repeated).  Otherwise the bytes are misaligned.
      mDesyncKind = (ar.mSpin1 == ar.mSpin2) ? 1u : 2u;
      logDesyncDetail("FIRST-BAD",ar,expected);
    } else if (mConsecInvalid == DESYNC_THRESHOLD) {
      ++mDesyncs;
      mDesynced.store(true);
      HTprintf("ZHD DESYNC chip%u t%u (%s) at report #%llu, %llu compressed bytes, packet #%u\n",
               mChipNum,mTLBI,mDesyncKind == 1u ? "reports skipped" : "garbage",
               (unsigned long long) mDesyncAtReport,
               (unsigned long long) mDesyncAtCBytes,mDesyncAtPacket);
      logDesyncDetail("DESYNCED",ar,expected);
      requestRestart();
    }
  }

  void ZHostDecompressor::requestRestart() {
    if (!restartEnabled()) return;
    mHostEpoch.store(mHostEpoch.load() ^ 1u);
    mRestartRequested.store(true);
    HTprintf("ZHD RESTART requested chip%u t%u (epoch %u)\n",
             mChipNum,mTLBI,mHostEpoch.load());
    KTprintf(mBHTag,"ZHD RESTART requested (epoch %u)\n",mHostEpoch.load());
  }

  void ZHostDecompressor::stampFeedback(ACacheBlockPayload & pay) const {
    ACBHostFeedback fb;
    fb.mExpectedSpin = (u16) (mAtomReportsReceived & 0xffffull);
    fb.mPacketSeq = (u16) (mPacketsIn & 0xffffu);
    fb.mConsecInvalid = (u16) (mConsecInvalid > 0xffffu ? 0xffffu : mConsecInvalid);
    pay.setFeedback(fb);        // racy reads of our own counters: fine
    u16 flags = 0u;
    if (mRestartRequested.load())
      flags = ACacheBlockPayload::ACBP_FLAG_RESTART_REQ |
        ((mHostEpoch.load() & 1u) ? ACacheBlockPayload::ACBP_FLAG_EPOCH : 0u);
    pay.mFlagsAndLen = flags;   // the length stays 0: still an empty car
  }

  bool ZHostDecompressor::isRestartSOF(const ACacheBlockPayload & pay) const {
    if (!mRestartRequested.load()) return false;
    u32 fl = pay.getCurrentFlags();
    if (!(fl & ACacheBlockPayload::ACBP_FLAG_IS_SOF)) return false;
    bool epoch = (fl & ACacheBlockPayload::ACBP_FLAG_EPOCH) != 0u;
    return epoch == ((mHostEpoch.load() & 1u) != 0u);   // answers THIS request
  }

  // In the decoder thread, after RestartStream unwound decode(): start over.
  // The current car (the SOF one) is still current, unread.
  void ZHostDecompressor::restartDecoder() {
    mLZ.reset();                // ring and trees as the hub's encoder has now
    mHARIO.init(false);         // drop any half record
    {
      std::lock_guard<std::mutex> lock(mHealthLock);
      mConsecInvalid = 0u;
      mDesynced.store(false);
      mSpinSync = true;
    }
    mRestartRequested.store(false);
    u32 n = mRestarts.fetch_add(1u) + 1u;
    HTprintf("ZHD RESTARTED chip%u t%u (#%u) at %llu good reports, %llu compressed bytes\n",
             mChipNum,mTLBI,n,(unsigned long long) mAtomReportsReceived,
             (unsigned long long) mCompressedBytesIn);
    KTprintf(mBHTag,"ZHD RESTARTED #%u at %llu good reports, %llu compressed bytes\n",
             n,(unsigned long long) mAtomReportsReceived,
             (unsigned long long) mCompressedBytesIn);
  }

  void ZHostDecompressor::logDesyncDetail(const char * what,
                                          const AtomReport & ar, u16 expected) {
    constexpr u32 OBJ = AtomReportIO::OBJ_SIZE;
    KTprintf(mBHTag,"ZHD %s chip%u t%u #%llu: expected spin 0x%04x got 0x%04x/0x%04x "
             "coord (%u,%u) kind %u, %llu compressed B in (%u packets), "
             "%llu decoded B out (%llu mod %u), packets: %u bad checksum %u bad seq\n",
             what,mChipNum,mTLBI,(unsigned long long) mAtomReportsReceived,
             expected,ar.mSpin1,ar.mSpin2,ar.mCoord.x,ar.mCoord.y,mDesyncKind,
             (unsigned long long) mCompressedBytesIn,mPacketsIn,
             (unsigned long long) mUncompressedBytesOut,
             (unsigned long long) (mUncompressedBytesOut % OBJ),OBJ,
             mPktSumBad,mPktSeqBad);
    if (what[0] != 'F') return;   // the history only for the first bad one
    {
      std::string hex;
      char b[4];
      u32 n = mUncompressedBytesOut < OUT_HISTORY ? (u32) mUncompressedBytesOut
                                                  : OUT_HISTORY;
      for (u32 i = 0u; i < n; ++i) {
        snprintf(b,sizeof(b),"%02x ",mOutHist[(mOutHistPos - n + i) % OUT_HISTORY]);
        hex += b;
      }
      KTprintf(mBHTag,"ZHD last %u decoded bytes (oldest first): %s\n",n,hex.c_str());
    }
    u32 n = mPacketsIn < PKT_HISTORY ? mPacketsIn : PKT_HISTORY;
    for (u32 i = 0u; i < n; ++i) {
      const PacketRec & r = mPktHist[(mPacketsIn - n + i) % PKT_HISTORY];
      KTprintf(mBHTag,"ZHD pkt#%u car%u flags 0x%04x len %u start %llu head %02x%02x%02x%02x tail %02x%02x%02x%02x\n",
               r.mSeq,r.mCar,r.mFlagsAndLen & ACacheBlockPayload::ACBP_FLAGS_MASK,
               r.mFlagsAndLen & ACacheBlockPayload::ACBP_LEN_MASK,
               (unsigned long long) r.mStartOffset,
               r.mHead[0],r.mHead[1],r.mHead[2],r.mHead[3],
               r.mTail[0],r.mTail[1],r.mTail[2],r.mTail[3]);
    }
  }

  ZHDHealth ZHostDecompressor::health() const {
    std::lock_guard<std::mutex> lock(mHealthLock);
    ZHDHealth h;
    h.mChip = mChipNum;
    h.mTLBI = mTLBI;
    h.mReports = mAtomReportsReceived;       // racy read of a counter: fine
    h.mInvalid = mInvalidTotal;
    h.mBadAtoms = mBadAtoms;       // racy read, like mReports
    h.mConsecInvalid = mConsecInvalid;
    h.mDesyncs = mDesyncs;
    h.mDesynced = mDesynced.load();
    h.mDesyncAtReport = mDesyncAtReport;
    h.mDesyncAtCBytes = mDesyncAtCBytes;
    h.mDesyncAtPacket = mDesyncAtPacket;
    h.mDesyncKind = mDesyncKind;
    h.mRestarts = mRestarts.load();
    h.mRestartPending = mRestartRequested.load();
    return h;
  }

  std::vector<ZHDHealth> ZHostDecompressor::snapshotHealth() {
    std::lock_guard<std::mutex> lock(theZHDRegistryLock);
    std::vector<ZHDHealth> out;
    out.reserve(theZHDRegistry.size());
    for (const ZHostDecompressor * z : theZHDRegistry) out.push_back(z->health());
    return out;
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

    if (!mRegistered) {
      std::lock_guard<std::mutex> lock(theZHDRegistryLock);
      theZHDRegistry.push_back(this);
      mRegistered = true;
    }

    mLZ.init(ZHByteSource, this, ZHByteSink, this);
    //    HTprintf("ZHD %s HERE HALLO %u HARIO MADE %u\n",mBHTag.to_string().c_str(),tlbi,mMade);
    KTprintf(mBHTag,"ZHD %p HERE HALLO %u HARIO MADE %u\n",this,tlbi,mMade);

    mDecompressorThreadPtr = std::make_unique<std::thread>([this,tlbi]() {
      for (;;) {
        try {
          //HTprintf("ZHDecompThread %p HERE HALLO\n",this);
          mLZ.decode();
          break;
        }
        catch (const RestartStream &) {
          restartDecoder();     // the hub restarted its stream: decode afresh
        }
        catch (const char * str) {
          HTprintf("ZHD CATCH %p GUDBYEE %u (%s)\n",this,tlbi,str);
          break;
        }
      }
    });    
  }
}
