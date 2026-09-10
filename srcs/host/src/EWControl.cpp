#include "EWControl.h"
#include "Fail.h"
#include <time.h>     /* For time() */
#include "MDist.h"
#include "StringBlock.h"
#include "UxC.h" // for U16C
#include "QuietBox.h"
#include "DemoGlobal.h"
#include "Blackhole.h"

namespace MFM {

  thread_local StringBlock ewRenderBlock;
  thread_local BGRImageHD ewGraphicsRenderBlock;
  thread_local std::string statsLineBuffer;

  EWControl EWControl::theEWControl;

  EWControl & EWControl::getTheEWControl() {
    return theEWControl;
  }

  EWControl::EWControl()
    : mEWRunnerThreadMutex("EWRN")
  {
    mStartTime = std::chrono::steady_clock::now();
    initGrid();
    {
      AtomicScopeLock guard(mEWRunnerThreadMutex);
      mEWRunnerThreadPtr = std::make_unique<std::thread>(&EWControl::runEWThread,&theEWControl);
      mEWRunnerThreadAlive.store(true);
    }
  }
  
  void EWControl::runEWThread() {
    while (mEWRunnerThreadAlive.load()) {
      if (false // need this? EWPs trigger by empty car arrivals from the sticks, not here
          && isActive()) {
        // Do some? events
        // DEBUG: Try to allocate using STVL!
        S32C center(hostPRNG.Between(GRID_XMIN,GRID_XMAX),
                    hostPRNG.Between(GRID_YMIN,GRID_YMAX));
        EWLocker::Entry token;
        if (mEWLocker.tryLock(center,token)) 
          ++mEventCount;
        // just let 'em get reaped
      }
      {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
      }
    }
  }

  EWControl::~EWControl() {
    {
      AtomicScopeLock guard(mEWRunnerThreadMutex);
      setActive(false);
      mEWRunnerThreadAlive.store(false);
    }
    mEWRunnerThreadPtr->join();
  }

  void EWControl::initGrid() {
    setActive(false);
    mMin = {-5,-5};
    mMax = {+5,+5};
    memset_s(mGrid, 0u, sizeof(mGrid));

    S32C sc = randomCoordInBounds();
    setAtom(sc,P4Atom::makeStartAtom());
    //printActiveGrid();
  }

  S32C EWControl::randomCoordInBounds() {
    s32 x = hostPRNG.Between(mMin.x,mMax.x); // note with center in bounds,
    s32 y = hostPRNG.Between(mMin.y,mMax.y); // ew may reach beyond bounds
    return { x, y };
  }

  s32 EWControl::scanHubGrid(u32 cn) {
    s32 ret = 0;

    QuietBox & qb = QuietBox::get();
    Blackhole * bh = qb.getBlackholeIfPresent(cn);
    if (bh) ret += bh->scanHubGrid();

    return ret;
  }

  std::string EWControl::doSeed(u32 atomType) {
    U16C fullGridSize = DG::getGlobalGridSize();
    U32C loc(hostPRNG.Between(0,fullGridSize.x-1),
             hostPRNG.Between(0,fullGridSize.y-1));

    /* SEED IN HOST
    setAtom(loc,P4Atom::makeStartAtom());
    return "Seed@"+std::to_string(loc.x)+","+std::to_string(loc.y);
    */
    // SEED IN T6
    QuietBox & qb = QuietBox::get();
    DG::Coord dgc = loc;
    DG::Address addr = DG::mapCoordToAddress(dgc);

    Eprintf("TRYDOSEED (%d,%d) [%u]-> %u\n",
            loc.x,loc.y,atomType,addr.isValid());

    if (!addr.isValid())
      return "BadSeed@"+std::to_string(loc.x)+","+std::to_string(loc.y);
    bool worked = qb.storeP4Atom(dgc, P4Atom::makeAtom(atomType));
    Eprintf("DOdoSeed %u (%d,%d)\n",
            worked,
            dgc.x,dgc.y);
    if (!worked)
      return "BadStore@"+std::to_string(loc.x)+","+std::to_string(loc.y);
    return "Seed@"+std::to_string(loc.x)+","+std::to_string(loc.y);
  }

  std::string EWControl::doNuke(bool large) {
    S32C loc = randomCoordInBounds();
    s32 r = hostPRNG.Create(large? 500 : 50) + 5;
    s32 r2 = r*r;
    u32 nuked = 0u;
    for (s32 x = loc.x - r; x <= loc.x+r; ++x) {
      for (s32 y = loc.y - r; y <= loc.y+r; ++y) {
        S32C at(x,y);
        if (at.euclideanSquaredDistance(loc) < r2) {
          u16 t = getAtom(at).getType();
          if (t != P4Atom::EMPTY_TYPE &&
              t != P4Atom::INACCESSIBLE_TYPE) {
            setAtom(at,P4Atom::makeEmptyAtom());
            ++nuked;
          }
        }
      }
    }
    return size4(nuked)+"!"+std::to_string(r)+"@"+std::to_string(loc.x)+","+std::to_string(loc.y);
  }

  bool EWControl::pickEWCenter(S32C & occupied, EWLocker::Entry & token) {
    if (!isActive()) return false; // don't even try now
    u32 area = (mMax.x - mMin.x)*(mMax.y - mMin.y);
    // try sampling for a 
    bool nothingYet = true;
    for (u32 i = 0u;
         (i < area/2u) ||
           (nothingYet && i < 3u*area); ++i) {
      S32C c = randomCoordInBounds();
      u16 t = getAtom(c).getType();
      if (t != P4Atom::EMPTY_TYPE &&
          t != P4Atom::INACCESSIBLE_TYPE) {
        // Found a possible. Is it locked?
        nothingYet = false;
        if (mEWLocker.tryLock(c,token)) {
          // we got the lock!
          ++mEventCount;
          ++mEWCentersBySampling;
          occupied = c;
          ++mEventCentersPicked;
#if 0          
          if (t == P4Atom::START_TYPE) {
            Eprintf("EWSTART (%s) %s\n",c.to_repr().c_str(),token.to_repr().c_str());
          }
#endif
          return true;
        } else ++mEventCentersLockedOut;
      }
    }
#if 0
    // fall back to enumeration
    u32 count = 0u;
    for (s32 x = mMin.x; x <= mMax.x; ++x) {
      for (s32 y = mMin.y; y <= mMax.y; ++y) {
        S32C c(x,y);
        u16 t = getAtom(c).getType();
        if (t != P4Atom::EMPTY_TYPE &&
            t != P4Atom::INACCESSIBLE_TYPE &&
            mEWLocker.tryLock(c,token)) {
          if (hostPRNG.OneIn(++count)) occupied = c;
          mEWLocker.unlock(token);
        }
      }
    }

    if (count > 0u) {
      if (mEWLocker.tryLock(occupied,token)) {
        ++mEventCount;
        ++mEWCentersByEnumeration;
        ++mEventCentersPicked;
        return true;
      }
      ++mEventCentersLockedOut;
    }
#endif
    return false;
  }

  std::string_view EWControl::statsLine() const {
    std::string & ret = statsLineBuffer;
    ret.reserve(128);
    ret = "";
    ret += "" + size4(mEventCentersLockedOut) + "lo";
    ret += " " + size4(mEWGoodUnlocks) + "gu";
    ret += "/" + size4(mEWFailedUnlocks) + "fu";
    ret += "/" + size4(mEWLocker.getExpired()) + "ex";

    ret += " " + size4(mEventCentersPicked) + "ews";
    //    ret += "(" + size4(mEWCentersBySampling) + "samp";
    //    ret += " " + size4(mEWCentersByEnumeration) + "enum)";
    ret += " " +size4(mEventCenterCommitsAttempted) + "catm";
    ret += "(" + size4(mEWGoodCommits) + "g";
    ret += " " + size4(mEWLateCommits) + "l";
    ret += " " + size4(mEWUnchangedCommits) + "i)";
    ret += " " + pct4(mEWUnchangedCommits+mEWGoodCommits,mEventCentersPicked);
    ret += "ewar";

    TimeStamp now = std::chrono::steady_clock::now();
    typedef std::chrono::duration<double> dsecs;
    dsecs secs = std::chrono::duration_cast<dsecs>(now - mStartTime);
    double seconds = secs.count();
    if (seconds > 0) {
      u32 gups = mEWGoodUnlocks/seconds;
      ret += " " + size4(gups) + "gups";
    }

    return std::string_view(ret);
  }

  void EWControl::fillEW(S32C center, EventWindow & ew) {
    const MDist4 md;
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.getPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      ew.mAtoms[sn] = a;
    }
  }

#if 0
  bool EWControl::tryLoadEWCar(EWCarStorage::EWCar & ec) {
    S32C ctr;
    EWLocker::Entry token;
    if (!pickEWCenter(ctr,token)) return false; // ah fudge
    EWBlock & eb = ec.getContent();
    eb.mHiddenXPos = ctr.x;
    eb.mHiddenYPos = ctr.y;
    eb.mSTVLTime = token.mWhenAllocated;
    fillEW(ctr,eb.mOld);
    {
      P4Atom a = eb.mOld.getAtom(0u);
      if (a.getType() == P4Atom::START_TYPE)
        Eprintf("LOADEWSTART (%d,%d) ts=%f\n",
                eb.mHiddenXPos, eb.mHiddenYPos,
                secondsSinceStart(eb.mSTVLTime));
    }
    eb.mNew.reset();
    return true;
  }
#endif

  static u8 charForType(u32 type) {
    if (type == P4Atom::EMPTY_TYPE) return ' ';
    if (type == P4Atom::START_TYPE) return 's';
    if (type < 10) return (u8) ('0'+type);
    return '?';
  }

  void EWControl::printActiveGrid() {
    printf("<<<(%d,%d)..(%d,%d)\n",
           mMin.x, mMin.y, 
           mMax.x, mMax.y);
    for (s32 y = mMin.y; y <= mMax.y; ++y) {
      printf("== ");
      for (s32 x = mMin.x; x <= mMax.x; ++x) {
        S32C c(x,y);
        const P4Atom a = getAtom(c);
        u32 type = a.getType();
        printf("%c",charForType(type));
      }
      printf(" ==\n");
    }
    printf(">>>\n");
  }


  std::string_view EWControl::renderGridWindow(S32C gcenter, S32C tsize, s32 zoom) {
    const S32C tcenter = tsize/2; // gcenter anchor in textels
    const S32C dcenter = tcenter/ASPECT_RATIO; // gcenter anchor in display boxes

    if (zoom > 0) return "POSITIVE ZOOM NOT IMPLEMENTED";
    u32 szoom = 1-zoom;         // 'sample zoom'
    S32C ssize(szoom,szoom);    // size of each sample box in atoms

    S32C dsize = ASPECT_RATIO;   // size of each display box in textels (FIX FOR +ive ZOOM)
    S32C dcount = tsize / dsize; // count of display boxes in textual (FIX TO ROUND UP?)
    
    StringBlock & sb = ewRenderBlock;
    sb.init(tsize);

    if (false)
      sb.append(stringFormat("gcenter (%d,%d), tsize (%d,%d), dcount (%d,%d) zoom %d, szoom %d\n",
                             gcenter.x,gcenter.y,
                             tsize.x,tsize.y,
                             dcount.x,dcount.y,
                             zoom, szoom));

    BackgroundPadder bgp;
    //XXX FIX ME    bgp.init(asize, acorner);

    // iterate over display boxes in textual screen
    unsigned short count = 0u;
    for (s32 dby = 0; dby < dcount.y; ++dby) {
      for (s32 dbx = 0; dbx < dcount.x; ++dbx) {

        S32C dcoord(dbx,dby);       // current display box index [0..tsize)
        //if (dbx == dby) sb.put2D(dcoord,"HEWO");
        S32C dtcoord = dcoord*ASPECT_RATIO; // upper left tcoord of current db
        // dsize as above

        // FILL CURRENT DISPLAY BOX

        // map from dbox to sample box
        // (1) where is this dbox relative to dcenter?
        S32C doffset = dcoord - dcenter; // dist dcenter->dcoord in dboxes

        // (2) where is this dbox relative to gcenter
        S32C dgcoord = gcenter + doffset;

        // (3) where is the sample box relative to gcenter
        S32C sgcoord = dgcoord*ssize - ssize/2;
        S32C sgmax = sgcoord + ssize - 1;

        // (4) sample an atom inside sample box
        //S32C acoord = sgcoord + S32C(hostPRNG.Create(ssize.x),hostPRNG.Create(ssize.y));
        S32C acoord = sgcoord + ssize/2; // just take middle?

        if (false &&
            ((abs(dcoord.x) < 3u && abs(dcoord.y) < 3) ||
             (abs(dgcoord.x) < 2u && abs(dgcoord.y) < 2)))
          sb.append(stringFormat("dcoord (%d,%d), dtcoord (%d,%d), dsize (%d,%d), doffset (%d,%d), dgcoord (%d,%d), sgcoord (%d,%d) sgmax (%d,%d) @ (%d,%d)\n",
                                 dcoord.x,dcoord.y,
                                 dtcoord.x,dtcoord.y,
                                 dsize.x,dsize.y,
                                 doffset.x,doffset.y,
                                 dgcoord.x,dgcoord.y,
                                 sgcoord.x,sgcoord.y,
                                 sgmax.x,sgmax.y,
                                 acoord.x,acoord.y));
        //if (++count > 100) return std::string_view(ret);

        sb.displayAtom(dtcoord,dsize,sgcoord,ssize,getAtom(acoord));
      }
      //      ret += "\n";
    }
    // draw grid tics
    s32 yinc = dcount.y / 4;
    s32 xinc = dcount.x / 4;
    for (s32 dby = 0; dby < dcount.y; dby += yinc) {
      for (s32 dbx = 0; dbx < dcount.x; dbx += xinc) {
        S32C dcoord(dbx,dby);       
        S32C dtcoord = dcoord*ASPECT_RATIO; // upper left tcoord of current db
        // dsize as above

        if (dby != 0 || dbx != 0) sb.put2D(dtcoord+S32C(1,0),'*');

        if (dby != 0 && dbx != 0) continue;

        // map from dbox to sample box
        // (1) where is this dbox relative to dcenter?
        S32C doffset = dcoord - dcenter; // dist dcenter->dcoord in dboxes
        
        // (2) where is this dbox relative to gcenter
        S32C dgcoord = gcenter + doffset;

        // (3) where is the sample box relative to gcenter
        S32C sgcoord = dgcoord*ssize - ssize/2;
        S32C sgmax = sgcoord + ssize - 1;

        s64 sval = (dby == 0) ? sgcoord.x : sgcoord.y;
        std::string num;
        if (true) {
          num = "["+std::to_string(sval)+"]";
        } else {
          bool neg = sval < 0;
          u64 val = neg ? -sval : sval;
          num = size4(val);
          if (neg) num = "-"+num;
          //num = "*"+num;
        }
        sb.put2D(dtcoord,num,true);
      }
    }
    
    return sb.asSV();
  }

  std::string_view EWControl::renderGraphicsGridWindow(S32C pixelsize, s32 zoom) {
    BGRImageHD & bgr = renderGraphicsGridWindowToImage();
    return bgr.asSV();
  }

  BGRImageHD & EWControl::renderGraphicsGridWindowToImage() {
    //BGRImageHD & bgr = ewGraphicsRenderBlock;
    BGRImageHD & bgr = QuietBox::getT6GridImage();

#if 1
    U16C size = bgr.gridSize();
    U16C idx;
    RGBPix c,t;
    c.set(235u,245u,255u);
    t.set(255u,200u,200u);
    for (idx.y = 0u; idx.y < size.y; ++idx.y) {
      for (idx.x = 0u; idx.x < size.x; ++idx.x) {
        if (false && idx.x % 100 == 0 && idx.y % 100 == 0)
          bgr.setPixel(idx, c);
        if (idx.x >= 10 && idx.y >= 10 && (idx.x-10) % 138 == 0 && (idx.y-10) % 108 == 0)
          bgr.drawCross(idx, 10, c);
      }
    }
#endif
    return bgr;
  }  

#if 0
  std::string_view EWControl::renderGridWindowOLD(S32C scorner, S32C ssize, s32 zoom) {
    if (zoom > 0) return "POSITIVE ZOOM NOT IMPLEMENTED";
    u32 aside = 1-zoom;
    Eprintf("RNGD10 (%d,%d)c (%d,%d)s %dz %daside\n",
            scorner.x,scorner.y,
            ssize.x,ssize.y,
            zoom,aside);
    std::string & ret = ewRenderBuffer;
    ret = "";
    ret.reserve((ssize.x+1)*ASPECT_RATIO.x*ssize.y*ASPECT_RATIO.y);
    const S32C asize(ssize.x/ASPECT_RATIO.x,ssize.y/ASPECT_RATIO.y);
    const S32C acorner(scorner.x/ASPECT_RATIO.x,scorner.y/ASPECT_RATIO.y);
    Eprintf("RNGD11 (%d,%d)ac (%d,%d)as (%d,%d)AR\n",
            acorner.x,acorner.y,
            asize.x,asize.y,
            ASPECT_RATIO.x,ASPECT_RATIO.y);
    BackgroundPadder bgp;
    bgp.init(asize, acorner);
    for (s32 agy = 0; agy < asize.y * aside; agy = agy + aside) { // 'atom' x & y
      if (false && agy==0) {
        ret += " ISALIVE=";
        ret += (mEWRunnerThreadAlive.load() ? "TRUE" : "FALSE");
        ret += " ISACTIVE=";
        ret += (isActive() ? "YEP" : "NOP");
        ret += " EVENTS=";
        ret += std::to_string(mEventCount);
        continue;
      }
      for (s32 agx = 0; agx < asize.x * aside; agx = agx + aside) {
        bgp.at({agx,agy});

        // have to resample y on every x for uniformity..
        s32 ay = agy + hostPRNG.Create(aside); // sample in y box
        u32 syo = ay*ASPECT_RATIO.y;

        s32 ax = agx + hostPRNG.Create(aside); // sample in x box
        u32 sxo = ax*ASPECT_RATIO.x;

        if (false) Eprintf("RNGD12 (%d,%d)ag (%d,%d)axy (%d,%d)sxyo\n",
                agx,agy, ax,ay, sxo,syo);

        if (agy == asize.y*aside/2 && agx == asize.x*aside/2) {
          ret += bgp.padChr('*');// DEBUG FLAG CENTER OF SIZE
          continue;
        }
        s32 gridax = acorner.x + ax;
        s32 griday = acorner.y + ay;
        u16 t = getAtom({gridax,griday}).getType();

        if (false) Eprintf("RNGD13 (%d,%d)gaxy %ut <<%s>>\n",
                gridax,griday,t,
                bgp.padChr('?').c_str());

        switch (t) {
        case P4Atom::EMPTY_TYPE: ret += bgp.padChr(' '); break;
        case P4Atom::INACCESSIBLE_TYPE: ret += bgp.padChr(':'); break;
        default:
          if (t > 0 && t < 10) ret += bgp.padChr((char) ('0'+t));
          else if (t >= 10 && t < 26+10) ret += bgp.padChr((char) ('a'-10+t));
          else ret += bgp.padChr('?');
        }
      }
      ret += "\n";
    }
    return std::string_view(ret);
  }
  #endif

  s32 EWControl::commitEWIfPossible(BHTag tag, S32C center, TimeStamp when, EventWindow & oldew, EventWindow & newew) {
    EWLocker::Entry entry;
    entry.mPosition = center;
    entry.mWhenAllocated = when;

    ++mEventCenterCommitsAttempted;
    const MDist4 md;
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.getPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      if (a != oldew.mAtoms[sn]) {
        P4Atom b = oldew.mAtoms[sn];
        Eprintf("LATE %llu %u(%d,%d) old %04x-%04x-%08x-%08x cur %04x-%04x-%08x-%08x\n",
                mEWLateCommits,
                sn, at.x, at.y,
                b.mParityAndType,b.mData0, b.mStg[0], b.mStg[1],
                a.mParityAndType,a.mData0, a.mStg[0], a.mStg[1]);
        ++mEWLateCommits;

        if (mEWLocker.unlock(entry)) ++mEWGoodUnlocks;
        else ++mEWFailedUnlocks;

        return -1;
      }
    }

    bool changed = false;
    for (u32 sn = 0u; sn < 41u; ++sn) {
      P4Atom a = newew.mAtoms[sn];
      if (oldew.mAtoms[sn] == a) continue;
      changed = true;

      SPoint c = md.getPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      if (setAtom(at, a) && a.getType() != P4Atom::EMPTY_TYPE) {
        if (at.x < mMin.x) mMin.x = at.x;
        if (at.x > mMax.x) mMax.x = at.x;
        if (at.y < mMin.y) mMin.y = at.y;
        if (at.y > mMax.y) mMax.y = at.y;
      }
    }

    if (mEWLocker.unlock(entry)) ++mEWGoodUnlocks;
    else ++mEWFailedUnlocks;

    if (changed) {
      ++mEWGoodCommits;
      return 0;
    }
    ++mEWUnchangedCommits;
    return -2; // we already HAD that transition, sir, <sniff!>, please move along
  }

  void EWControl::BackgroundPadder::init(S32C as, S32C ac) {
    asize = as;
    acorner = ac;

    agdpt = {0,0};

    steps = {1,1};
    for (u32 i = 0u; steps.x < asize.x/4u; ++i) steps.x *= 2u;
    for (u32 i = 0u; steps.y < asize.y/4u; ++i) steps.y *= 2u;
    Eprintf("BGPD10 (%d,%d)asz (%d,%d)acn (%d,%d)agp (%d,%d)stp\n",
            asize.x,asize.y,
            acorner.x,acorner.y,
            agdpt.x,agdpt.y,
            steps.x,steps.y);
  }

  std::string EWControl::BackgroundPadder::padChr(const char ch) const {
    std::string ret{ ch };
    bool first = true;
    while (ret.length() < ASPECT_RATIO.x) {
      char ch = ' ';
      if (first) {
        bool onx = 0u==absMod((acorner.y + agdpt.y),steps.y);
        bool ony = 0u==absMod((acorner.x + agdpt.x),steps.x);
        if (onx && ony) ch = '+';
        else if (onx) ch = '-';
        else if (ony) ch = '|';
        Eprintf("BGPD11 (%d,%d)asz (%d,%d)acn (%d,%d)agp (%d,%d)stp (%d,%d)abm %dx%dy '%c'\n",
                asize.x,asize.y,
                acorner.x,acorner.y,
                agdpt.x,agdpt.y,
                steps.x,steps.y,
                absMod((acorner.x + agdpt.x),steps.x),
                absMod((acorner.y + agdpt.y),steps.y),
                onx,ony,ch);
        first = false;
      }
      ret += ch;
    }
    return ret;
  }

}
