	.equ STACK_CANARY,0x11a2b30d
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
	
        la a0, __host_block_start  # arg 1 is struct HOSTBLOCK *
	call t6setup            # Do other t6 level setup on hartb only
	
        ## READY TO ROCK
        la t1, 0xFFB121B0   # RISCV_DEBUG_REG_SOFT_RESET_0
	sw x0,(t1)          # release all
        j exit_setup
        ## END OF HARTB-ONLY CODE

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
        
exit_setup:  ## ALL HARTS COME THROUGH HERE
	# Finally initialize global pointer (same for all harts)
	lui gp,%hi(__global_pointer$)
	addi gp,gp,%lo(__global_pointer$)
	.option pop

	## fastram zeroes automagically? - "When the RISCV comes out
	## of reset, the local data RAM will spend up to 2048 clock
	## cycles resetting its contents to zero."

	la t1, __fast_start     # get local ptr
	la t2, __host_block_start # get hb ptr
	sb s0,(t1)              # set fast[0] 0..7 to hartnum
        lbu t3,24(t2)           # get hb.mXPos
        sb t3,1(t1)             # stash in fALL.mXPos
        lbu t3,25(t2)           # get hb.mYPos
        sb t3,2(t1)             # stash in fall.mYPox

 	# Put stack canary in top 1KB of fastram (same for all harts given each sp)
        li t2, STACK_CANARY     # canary value
	addi t1,sp,-1024        # down 1K
 3: 	sw t2,(t1)              # you get a canary
        addi t1, t1, 4          # and YOU
        bne t1,sp,3b            # AND
        ## FALL THROUGH	
	
runmain:        
        la a0, __host_block_start  # arg is struct HOSTBLOCK *
	call t6main

	/* if/when they return, mark us as done */
	la s2, __host_block_start+4  # get host block, skipping HBMAGIC
	sh2add t1, s0, s2            # t1: addr of mPerRiscArg[s0] 
	addi t0,s0,1            # get s0 + 1
        sw t0,0(t1)             # stick it in finish loc

        #### DEBUG: BLOW THE MAGIC IF NOCs 0 ON HARTB
	j 2f                    # SKIP ALL THIS WE FIGGERED IT OUT
        
	bne s0, x0, 2f          # skip all this unless B
        la s2, __host_block_start  # get host block 
	li s3,250
	lbu t0,24(s2)           # get xpos
        beq t0,x0,1f            # blow magic if it's zero
	sb s3,24(s2)            # change it to illegal but known
        lbu t0,25(s2)           # get ypos
        bne t2,x0,2f            # fall though if zero
1:      lw t0,(s2)              # get magic
        addi t0,t0,16           # BLOW THE MAGIC
        sw t0,(s2)
2:      fence
done:  
        j done
	.cfi_endproc
	
	.global t6hang
        .type   t6hang, @function

	.cfi_startproc
t6hang:
        la t0, __fast_start     # addr of stashed hartnum
	lbu t0,0(t0)             # get hartnum
	la t2, __host_block_start+4  # get host block, skipping HBMAGIC
	sh2add t1, t0, t2       # t1: addr of mPerRiscArg[hartnum] 
        sw a0,0(t1)             # store arg
	li t1,1                 # get 1
	sll t1,t1,t0            # get 1<<hartnum
	lbu t0,23(t2)           # get mFails from hostblock+27
	or t0,t0,t1             # include 1<<hartnum in mFails
        sb t0,23(t2)            # (we explicitly ignore races here)
1:      j 1b                    # and hang 
        .cfi_endproc
	
	.global estimateStackUsage
        .type   estimateStackUsage, @function

	.cfi_startproc
estimateStackUsage:
        la t0, __fast_start     # addr of stashed hartnum
	lbu t0,0(t0)            # get hartnum
	li t1,4                 # get 4
        la t2,__stack_top8K     # assume 8k stack
	beq t0,x0,1f            # hart b (0) has that
        beq t0,t1,1f            # hart nc (4) has that
        la t2,__stack_top4K     # t2 now: top of stack       
1:      addi t0,t2,-1024        # t0 now: bottom of search (top 1k of stack)
	mv a0,t0                # start at stack bottom
	li t1, STACK_CANARY     # t1 now: what we're looking for
2:      addi a0,a0,4            # increment to next
        lw t3,(a0)              # t3 now: next word
        bne t3,t1,3f            # done at first non canary
        blt a0,t2,2b            # or loop if more
	# FALL THROUGH IF NOT FOUND
3:      sub a0,t2,a0            # convert to min bytes used
	ret
        
	.cfi_endproc
        
