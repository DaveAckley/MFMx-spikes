#include "EWControl.h"
#include "Fail.h"
#include <time.h>     /* For time() */
#include "MDist.h"

namespace MFM {
  struct StringBlock {
    StringBlock()
      : mBytes(0)
      , mLen(0)
      , mRowLen(0)
      , mFillPos(0)
      , mDims({0,0})
    {}

    ~StringBlock() { clear(); }

    void displayAtom(S32C dtcoord, S32C dsize, S32C sgcoord, S32C ssize, const P4Atom & a) {
      u16 t = a.getType();
      char xch = ' ', ych = ' ';
      u32 div;
      if (ssize.x < 3u) div = 5u;
      else if (ssize.x < 13u) div = 50u;
      else div = 500u;

      // NO AXES IN HERE
      //      if ((sgcoord.x-ssize.x/2u)/div != (sgcoord.x+ssize.x/2u)/div) xch = '.';
      //      if ((sgcoord.y-ssize.y/2u)/div != (sgcoord.y+ssize.y/2u)/div) ych = '.';

      char ch;
      if (xch != ' ' && ych != ' ') ch = '+';
      else if (xch != ' ') ch = xch;
      else ch = ych;

      put2D(dtcoord,t==0 ? ' ' : '0'+t);
      put2D(dtcoord+S32C({1,0}), ch);
    }

    void clear() {
      delete [] mBytes;
      mBytes = 0;
      mLen = 0;
      mDims = { 0,0 };
      mFillPos = 0u;
    }

    void append(std::string s) {
      const char * data = s.data();
      u32 len = s.size();
      while (len-->0) 
        if (mFillPos < mLen) mBytes[mFillPos++] = *data++;
    }

    bool put2D(S32C at, std::string s, bool erase = false) {
      const char * data = s.data();
      u32 len = s.size();
      while (len-->0) {
        if (put2D(at,*data++,erase)) at.x++;
        else return false;
      }
      return true;
    }

    bool put2D(S32C at, char c, bool erase = false) {
      if (at.x < 0 || at.x >= mDims.x ||
          at.y < 0 || at.y >= mDims.y)
        return false;
      char * p = &mBytes[at.y * mRowLen + at.x];
      if (c != ' ' || erase)
        if (*p != '\n') *p = c;
      return true;
    }

    void init(S32C dims) {
      if (dims != mDims) {
        clear();
        mDims = dims;
        mRowLen = mDims.x+1u;
        mLen = mRowLen*mDims.y;
        mBytes = new char [mLen+1]; // +1 for null
      }
      mFillPos = 0u;
      memset_s(mBytes,' ',mLen);
      for (u32 r = 0u; r < mDims.y; ++r) {
        mBytes[r*mRowLen+0] = '>'; // DEBUG
        mBytes[r*mRowLen+mDims.x-1] = '|'; // DEBUG
        mBytes[r*mRowLen+mDims.x] = '\n';
      }
      mBytes[mLen] = '\0';
    }

    std::string_view asSV() { return std::string_view(mBytes,mLen); }

    char * mBytes;
    u32 mLen;
    u32 mRowLen;
    u32 mFillPos;
    S32C mDims;
  };
  thread_local HostRandom myPRNG;
  thread_local StringBlock ewRenderBlock;
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
      OurScopeLock guard(mEWRunnerThreadMutex);
      mEWRunnerThreadPtr = std::make_unique<std::thread>(&EWControl::runEWThread,&theEWControl);
      mEWRunnerThreadAlive.store(true);
    }
  }
  
  void EWControl::runEWThread() {
    while (mEWRunnerThreadAlive.load()) {
      if (false && mEWsActive.load()) {
        // Do some? events
        // DEBUG: Try to allocate using STVL!
        S32C center(myPRNG.Between(GRID_XMIN,GRID_XMAX),
                    myPRNG.Between(GRID_YMIN,GRID_YMAX));
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
      OurScopeLock guard(mEWRunnerThreadMutex);
      mEWsActive.store(false);
      mEWRunnerThreadAlive.store(false);
    }
    mEWRunnerThreadPtr->join();
  }

  void EWControl::initGrid() {
    mEWsActive = false;
    mMin = {-5,-5};
    mMax = {+5,+5};
    memset_s(mGrid, 0u, sizeof(mGrid));

    S32C sc = randomCoordInBounds();
    setAtom(sc,P4Atom::makeStartAtom());
    //printActiveGrid();
  }

  S32C EWControl::randomCoordInBounds() {
    s32 x = myPRNG.Between(mMin.x,mMax.x); // note with center in bounds,
    s32 y = myPRNG.Between(mMin.y,mMax.y); // ew may reach beyond bounds
    return { x, y };
  }

  bool EWControl::pickEWCenter(S32C & occupied, EWLocker::Entry & token) {
    u32 area = (mMax.x - mMin.x)*(mMax.y - mMin.y);
    // try sampling for a 
    bool nothingYet = true;
    for (u32 i = 0u;
         (i < area/5u) ||
           (nothingYet && i < 2u*area); ++i) {
      S32C c = randomCoordInBounds();
      u16 t = getAtom(c).getType();
      if (t != P4Atom::EMPTY_TYPE &&
          t != P4Atom::INACCESSIBLE_TYPE) {
        // Found a possible. Is it locked?
        nothingYet = false;
        if (mEWLocker.tryLock(c,token)) {
          // we got the lock!
          ++mEWCentersBySampling;
          occupied = c;
          ++mEventCentersPicked;
          return true;
        } else ++mEventCentersLockedOut;
      }
    }
    // SCREW ENUMERATION JUST FAIL
    return false;
#if 0
    // fall back to enumeration
    u32 count = 0u;
    for (s32 x = mMin.x; x <= mMax.x; ++x) {
      for (s32 y = mMin.y; y <= mMax.y; ++y) {
        S32C c(x,y);
        u16 t = getAtom(c).getType();
        if (t != P4Atom::EMPTY_TYPE &&
            t != P4Atom::INACCESSIBLE_TYPE &&
            myPRNG.OneIn(++count))
          occupied = c;
      }
    }

    if (count > 0u) {
      if (mEWLocker.tryLock(occupied,token)) {
        ++mEWCentersByEnumeration;
        ++mEventCentersPicked;
        return true;
      }
      ++mEventCentersLockedOut;
    }
    return false;
#endif
  }

  std::string_view EWControl::statsLine() const {
    std::string & ret = statsLineBuffer;
    ret.reserve(128);
    ret = "";
    ret += "" + size4(mEventCentersLockedOut) + "lo";
    ret += " " + size4(mEWGoodUnlocks) + "gu";
    ret += "/" + size4(mEWFailedUnlocks) + "fu";

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

  typedef MDist<4> MDist4;

  void EWControl::fillEW(S32C center, EventWindow & ew) {
    const MDist4 & md = MDist4::get();
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.GetPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      ew.mAtoms[sn] = a;
    }
  }

  bool EWControl::tryLoadEWCar(EWCarStorage::EWCar & ec) {
    S32C ctr;
    EWLocker::Entry token;
    if (!pickEWCenter(ctr,token)) return false; // ah fudge
    EWBlock & eb = ec.getContent();
    eb.mHiddenXPos = ctr.x;
    eb.mHiddenYPos = ctr.y;
    eb.mSTVLTime = token.mWhenAllocated;
    fillEW(ctr,eb.mOld);
    eb.mNew.reset();
    return true;
  }

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
    //tsize.x--; // RESERVE LAST COLUMN HACK
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
        //S32C acoord = sgcoord + S32C(myPRNG.Create(ssize.x),myPRNG.Create(ssize.y));
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
        bool neg = sval < 0;
        u64 val = neg ? -sval : sval;
        std::string num = size4(val);
        if (neg) num = "-"+num;
        //num = "*"+num;
        
        sb.put2D(dtcoord,num);
      }
    }
    
    return sb.asSV();
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
        s32 ay = agy + myPRNG.Create(aside); // sample in y box
        u32 syo = ay*ASPECT_RATIO.y;

        s32 ax = agx + myPRNG.Create(aside); // sample in x box
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
    const MDist4 & md = MDist4::get();
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.GetPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      if (a != oldew.mAtoms[sn]) {
        ++mEWLateCommits;
        return -1;
      }
    }

    bool changed = false;
    for (u32 sn = 0u; sn < 41u; ++sn) {
      P4Atom a = newew.mAtoms[sn];
      if (oldew.mAtoms[sn] == a) continue;
      changed = true;

      SPoint c = md.GetPoint(sn);
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
