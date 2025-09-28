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
	
b_setup: # B must be first, as RISCV B has fixed reset address of zero
        li s0, 0
        j done_setup
#t0_setup = . - b_setup
t0_setup:       
        li s0, 1
        j done_setup
#t1_setup = . - b_setup
t1_setup:       
        li s0, 2
        j done_setup
#t2_setup = . - b_setup
t2_setup:       
        li s0, 3
        j done_setup
#nc_setup = . - b_setup
nc_setup:       
        li s0, 4
done_setup:
	.option push
	.option norelax
	# Initialize global pointer (same for all harts)
	lui gp,%hi(__global_pointer$)
	addi gp,gp,%lo(__global_pointer$)
	.option pop

	/*
        la s2, __host_block_start  # nee 'fn_arguments'
	sh2add t1, s0, s2          # t1: addr of mPerRiscArg[s0]
	addi t0,s0,1            # get s0 + 1
        sw t0,0(t1)             # stick it in finish loc
        j done                  # shortcut everything else
	*/

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
        la s2, __host_block_start  # nee 'fn_arguments'
	sh2add t1, s0, s2          # t1: addr of mPerRiscArg[s0]
	sw x0,0(t1)             # zero mPRA to show we're working
	li t3,100000000
        sll t3,t3,s0            # 100M to 1.6G
3:      addi t3,t3,-1
	bne t3,x0,3b            # waste some variable time
	addi t0,s0,1            # get s0 + 1
        sw t0,0(t1)             # stick it in finish loc
        fence
done:  
        j done
	.cfi_endproc
