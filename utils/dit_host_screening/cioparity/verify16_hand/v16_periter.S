// Hand-placed crypto_verify_16 from the pass binary's own code, exposed as
// __wrap_crypto_verify_16 so -Wl,--wrap swaps it in ahead of the library's.
// optblocker_u16 is a static in verify.c, so a local zero halfword stands in.
        .data
        .p2align 1
optblk: .hword 0
        .text
        .globl  __wrap_crypto_verify_16
        .p2align 2
        .type   __wrap_crypto_verify_16,@function
__wrap_crypto_verify_16:
        sub     sp, sp, #0x20
        mov     x8, xzr
        str     x0, [sp, #0x18]
        str     x1, [sp, #0x10]
        strh    wzr, [sp, #0xc]
        // PER ITERATION: the clean instructions (pointer reloads, i++, the bound) run
        // DIT-off; only the eight the analysis marks tainted are covered. Two switches
        // per iteration, 32 per call.
1:      ldr     x9, [sp, #0x18]
        ldr     x10, [sp, #0x10]
        msr     DIT, #1
        ldrb    w9, [x9, x8]
        ldrb    w10, [x10, x8]
        ldrh    w11, [sp, #0xc]
        eor     w9, w10, w9
        and     w9, w9, #0xff
        orr     w9, w11, w9
        strh    w9, [sp, #0xc]
        msr     DIT, #0
        add     x8, x8, #0x1
        cmp     x8, #0x10
        b.ne    1b
        // the post-loop d handling: covered as one region in both variants
        msr     DIT, #1
        ldrh    w8, [sp, #0xc]
        strh    w8, [sp, #0xc]
        ldrh    w8, [sp, #0xc]
        sub     w8, w8, #0x1
        strh    w8, [sp, #0xc]
        adrp    x8, optblk
        add     x8, x8, :lo12:optblk
        ldrh    w9, [sp, #0xc]
        ldrh    w8, [x8]
        lsr     w8, w8, #2
        eor     w8, w8, w9, lsr #15
        strh    w8, [sp, #0xc]
        ldrh    w8, [sp, #0xc]
        sub     w0, w8, #0x1
        add     sp, sp, #0x20
        msr     DIT, #0
        ret
        .size   __wrap_crypto_verify_16, .-__wrap_crypto_verify_16
