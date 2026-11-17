#if defined(BOARD_PMC_02_AA) && defined(PMC_LEGACY_BOOTLOADER)

.syntax unified
.cpu cortex-m4
.thumb

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    cpsid i
    movs r0, #0
    msr control, r0
    isb
    ldr sp, =_estack
    bl pmc_legacy_early_init

    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
1:
    cmp r0, r1
    bcs 2f
    ldr r3, [r2], #4
    str r3, [r0], #4
    b 1b
2:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
3:
    cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b
4:
    ldr r0, =_sccmram
    ldr r1, =_eccmram
    ldr r2, =_siccmram
5:
    cmp r0, r1
    bcs 6f
    ldr r3, [r2], #4
    str r3, [r0], #4
    b 5b
6:
    bl SystemInit
    dsb
    isb
    cpsie i
    bl __libc_init_array
    bl main
7:
    b 7b
.size Reset_Handler, .-Reset_Handler

#endif