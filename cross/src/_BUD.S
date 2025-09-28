        .section .start,"ax",@progbits
        .global _start
        .type   _start, @function

_start:
        .cfi_startproc
        .cfi_undefined ra /* "Inform the debugger that there is nowhere to backtrace past _start." */
	
b_setup: # B must be first, as RISCV B has fixed reset address of zero
        li s0, 0
        j done_setup
t0_setup:                
        li s0, 1
        j done_setup
t1_setup:
        li s0, 2
        j done_setup
t2_setup:
        li s0, 3
        j done_setup
nc_setup:
        li s0, 4
done_setup:
	.option push
	.option norelax
	# Initialize global pointer (same for all harts)
	lui gp,%hi(__global_pointer$)
	addi gp,gp,%lo(__global_pointer$)
	.option pop

	# Initialize stack pointer (B&N@8k, T0-2@4k)
	la sp, __stack_top4K    # assume 4k fastram
        andi s2, s0, 3          # get bottom two bits of hart#
        bnez s2, zerobss        # 4k is right for them 
	la sp, __stack_top8K    # you've got 8k fastram
zerobss:      
        la t1, __bss_start
        la t2, __bss_end
2:      beq t1,t2,runmain
        sw x0,(t1)
        addi t1, t1, 4
        j 2b
runmain:        
        la s2, fn_arguments
        lw t2, 0(s2) # function pointer
        lw t1, 4(s2) # loop count
        sh2add s0, s0, s2
        lw t0, 8(s0) # memory pointer
        li s1, 1 # constant one (for incrementing)
        csrrsi x0, 0x7c0, 8 # disable L0 cache
        csrrs t3, 0xc00, x0 # sample the cycle counter
	#rdcycle t3
        jr t2
atomic_loop:
#ORIG        amoadd.w x0, 0(t0), s1
        amoadd.w x0, s1, 0(t0)
#        amoadd.w t1,t2,t3
#        subi t1, t1, 1
        addi t1, t1, -1
        bne t1, x0, atomic_loop
        fence 
        csrrs t4, 0xc00, x0 # sample the cycle counter
	#rdcycle t4
        j fini
non_atomic_loop:
        lw t2, 0(t0)
        add t2, t2, s1
        sw t2, 0(t0)
#        sub t1, t1, s1
        addi t1, t1, -1
        bne t1, x0, non_atomic_loop
        fence
        csrrs t4, 0xc00, x0 # sample the cycle counter
	#rdcycle t4
fini:  
        csrrci x0, 0x7c0, 8 # enable L0 again cache
        sub t4, t4, t3 # compute elapsed cycles
        sw t4, 8(s0)
        fence
done:  
        j done
	.cfi_endproc
fn_arguments:
