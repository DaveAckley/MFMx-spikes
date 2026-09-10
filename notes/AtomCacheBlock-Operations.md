# AtomCacheBlock Operations In The HUB Image

## CURRENT IMPLEMENTATION OVERVIEW

### Prelude

 - HUB hn sends `EwpBlock`s to neighboring EWP tiles
 - Returning `EwpBlock`s are written to HUB L1 by NoCs
 - A complete returning `EwpBlock` is detected by HUB hn
 - Its carindex is pushed into COMM2COMP crbi in EP_Ewp's `T6EPL1Data`
 - HUB hb removes that carindex in LiveB `processHubCars`
 - HUB hb passes that car's payload to HUB's `GridManager::applyEWT`
 - applyEWT on hb detects valid EWP and passes it to `GridManager::writeEW`
 - writeEW on hb pushes modified grid coords onto `*mDLGridListPtr`,
   which points at `theDLGridList` in L1, declared in HUB LiveB.
 - AtomCacheBlock operations (to follow) pops from theDLGridList

### AtomCacheBlock High-level Status & Intentions

 - `ACacheBlock`s contain compressed `AtomReport`s
 - HUB hn sends `ACacheBlock`s to the host
 - Host (is to extract the AtomReports then) returns the ACBs empty
 - HUB dedicates h1 entirely to compressing AtomReports. This is both
   because compression is expected to be relatively time-consuming and
   because my implementation of it blocks for I/O rather than being a
   state machine, so it can't really do anything else.
 - HUB also creates a 1ms tick h0 task called `manageACacheBlockT0` by
   declaring a pointer to it in load section `.rodata_fp_table_t0`
   - Exactly what that does and should do is up for grabs.
 - The manageACacheBlockT0 task (once initialized) is to call
   `ACacheBlockPrivateControl::step(..)` 
 - That method is to call `updateCars(..)` and `tryToSendFrame(..)`
 - `ACacheBlockPrivateControl::updateCars(..)` is to:
   1. Check we have an ACB car in progress, and
   1. Check if an empty inbound car is available, and
   1. Check if an output car slot is available, and
   1. Check if the current car is ready to close

   and if all that is true, then it is to:

   1. Close the current car, and
   1. Push its carindex onto the `theACacheBlockL1Data` COMP2COMM
      ringbuffer, and
   1. Pop an empty carindex off the `theACacheBlockL1Data` COMM2COMP
      ringbuffer, and 
   1. Set it up as the new 'current car'.
 - `tryToSendFrame(..)` is all messed up, but was perhaps intended to
    feed AtomReports to h1, and to trigger sending an ACB once per ms.
    
## GENERAL EP ENDPOINT/ELEVATORPLATFORM STRUCTURE

## SIMPLIFIED IMPLEMENTATION PLAN

### Persistent State 

#### Public L1 State
 - The EwpBlock cars and the EP_EwpBlock endpoint
 - The ACacheBlock cars and the EP_ACacheBlock endpoint
 - The hn <-> hb EWB RingBuffer for done-><-new
 - The hn <-> h1 ACB RingBuffer for empty-><-filled 
 - The Grid 
 - The hb <-> h1 DL2DGridList for push-><-pop
 - ?The h1 byte source and sink?

#### Private FastRAM State
 - H1 lz compression state
 - HB GridManager
   - Pointers to gridlist, ACB L1 state, T6Grid
   - EW counters
 - 

### Key Processes
 - HB for Ewp creation & consumption
 - H0 for clock & ticks
 - H1 for compression
 - [H2 for PRNG]
 - HN for packet processing
 
### Initializations

### Key Questions
 - need private data class for every key process? 
 - is it just scrap state for a task?
 - you'd think you might want fast ram state on
   multiple harts, depending on your task?
 - what about mstick methods? 
   - they all run on h0, right?
   - so isn't that confusing?
   - should we try to not have such things?
 - is the distinction we want between methods
   depending on persistent state vs depending only
   on the arguments.
   - but within persistent state there is
     hart-private vs L1-public
 - what does EP_ACB private currently do?
 - should we just rebuild a new cleaner
   alternative next to ACB and try to take lessons
   learned?
   - What would we call such a thing? Just plain
     `ACB` instead of `ACacheBlock`?
   - Should we use a private naming scheme that
     includes the specific hart the private space
     is to be allocated for? 

## Loose Thoughts

Every bit of persistent state ought to be
associated with precisely one hart at any given
time.

That hart is responsible for cleaning and updating
that state until that persistent state is no
longer needed or responsibility for it is
explicitly transferred to another hart.

### Reimplementation Plan
 - go to init12/
 - review PT_ACacheBlock
   - keep as-is, rename, or replace? [RENAME]
 - review
   
