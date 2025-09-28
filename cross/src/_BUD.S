        .section .start,"ax",@progbits
        .global _start
        .type   _start, @function

_start:
        .cfi_startproc
        .cfi_undefined ra /* "Inform the debugger that there is nowhere to backtrace past _start." */
	
	.global t0_setup
	.global t1_setup
	.global t2_setup
	.global nc_setup

        # B must be first, at addr 0, as RISCV B has fixed reset address of zero
b_setup: j b_init               # B entrypoint is 0
t0_setup: j t0_init             # T0 entrypoint must be addr 4
t1_setup: j t1_init             # T1 entrypoint must be addr 8
t2_setup: j t2_init             # T2 entrypoint must be addr 12
nc_setup: j nc_init             # NC entrypoint must be addr 16
	
	# Not setting up gp until pretty late, so:
	.option push
	.option norelax
b_init:
	li s0,0                 # we are hart0
	la sp, __stack_top8K    # we've got 8k fastram 	
	# zero bss for everybody
        la t1, __bss_start
        la t2, __bss_end
1:      beq t1,t2,2f
        sw x0,(t1)
        addi t1, t1, 4
        j 1b
2:      ## OTHER FIVE-BABIES-LEVEL SETUP?
        # ..not yet..
	
        ## READY TO ROCK
        la t1, 0xFFB121B0   # RISCV_DEBUG_REG_SOFT_RESET_0
	sw x0,(t1)          # release all
        j exit_setup

t0_init:
        li s0,1                 # we are hart1
	la sp, __stack_top4K    # we've got 4k fastram 	
	j exit_setup

t1_init:
        li s0,2                 # we are hart2
	la sp, __stack_top4K    # we've got 4k fastram 	
	j exit_setup
	
t2_init:
        li s0,3                 # we are hart3
	la sp, __stack_top4K    # we've got 4k fastram 	
	j exit_setup
	
nc_init:
        li s0,4                 # we are hart4
	la sp, __stack_top8K    # we've got 8k fastram 	
	# FALL THROUGH: j exit_setup
        
exit_setup:
	# Initialize global pointer (same for all harts)
	lui gp,%hi(__global_pointer$)
	addi gp,gp,%lo(__global_pointer$)
	.option pop

	# Zero fastram (same for all harts given each sp)
        la t1, __fast_start
3:      beq t1,sp,runmain
        sw x0,(t1)
        addi t1, t1, 4
        j 3b
runmain:        
	mv a0, s0                  # first arg is hart#
        la a1, __host_block_start  # second arg is struct HOSTBLOCK *
	call hartmain

	/* if/when they return, mark us as done */
	la s2, __host_block_start  # get host block
	sh2add t1, s0, s2          # t1: addr of mPerRiscArg[s0]
	addi t0,s0,1            # get s0 + 1
        sw t0,0(t1)             # stick it in finish loc
        fence
done:  
        j done
	.cfi_endproc
