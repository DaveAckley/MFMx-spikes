extern "C" {
  void _start() {
    volatile unsigned int* ram_address = (volatile unsigned int*)0xDEADBEEF;
    *ram_address = 0x12345678;
  }
}
