        .section .start,"ax",@progbits
        .global _start
        .type   _start, @function

_start:
        .cfi_startproc
        .cfi_undefined ra
	
        csrrsi x0, 0x7c0, 8 # disable L0 cache
        #csrrs t3, x0, 0xc00 # sample the cycle counter
        RDCYCLE t3 # sample the cycle counter
        fence
done:  
        j done
	.cfi_endproc
