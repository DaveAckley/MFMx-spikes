/**
   * @brief Flushes the data cache using FENCE and then reads a fresh 32 bit value from an address.
   *
   * @param address The memory address to read from.
   * @return The fresh value read from the address.
   */
  inline u32 readFresh32L1(u32* address) {
    u32 value;

    asm volatile (
  "fence\n\t"   // on blackhole, flushes the (non-coherent) L0 data cache
  "lw %0, 0(%1)\n\t" // so when we do the read we get a non-stale value

  : "=r" (value)  // output reg
  : "r" (address) // input reg
  : "memory" // really need this clobber? because cache flush is 'memory side effects'?
 );
    return value;
  }

  /**
   * @brief same as readFresh32L1 but for 16B
   *
   * @param address The memory address to read from.
   * @return The fresh value read from the address.
   */
  inline u16 readFresh16L1(u16* address) {
    u32 value;

    asm volatile (
  "fence\n\t"   // on blackhole, flushes the (non-coherent) L0 data cache
  "lhu %0, 0(%1)\n\t" // so when we do the read we get a non-stale value

  : "=r" (value)  // output reg
  : "r" (address) // input reg
  : "memory" // really need this clobber? because cache flush is 'memory side effects'?
 );
    return (u16) value;
  }

  /**
   * @brief same as readFresh32L1 but for 8B
   *
   * @param address The memory address to read from.
   * @return The fresh value read from the address.
   */
  inline u8 readFresh8L1(u8* address) {
    u32 value;

    asm volatile (
  "fence\n\t"   // on blackhole, flushes the (non-coherent) L0 data cache
  "lbu %0, 0(%1)\n\t" // so when we do the read we get a non-stale value

  : "=r" (value)  // output reg
  : "r" (address) // input reg
  : "memory" // really need this clobber? because cache flush is 'memory side effects'?
 );
    return (u8) value;
  }
