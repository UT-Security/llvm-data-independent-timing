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
        // HOISTED: what the pass emits. One enable before the loop, the whole body covered.
        msr     DIT, #1
1:      ldr     x9, [sp, #0x18]
        ldrb    w9, [x9, x8]
        ldr     x10, [sp, #0x10]
        ldrb    w10, [x10, x8]
        ldrh    w11, [sp, #0xc]
        add     x8, x8, #0x1
        cmp     x8, #0x10
        eor     w9, w10, w9
        and     w9, w9, #0xff
        orr     w9, w11, w9
        strh    w9, [sp, #0xc]
        b.ne    1b
        // the post-loop d handling stays inside the same region, exactly as the
        // pass places it: one enable at 413160, one clear before ret at 4131cc.
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
