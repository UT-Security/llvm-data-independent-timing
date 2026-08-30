// Exit-form variants for the PSTATE.DIT callee-saved ABI.
// Each is a leaf callee shaped like a real instrumented function:
//   entry read (where the ABI needs one), entry enable, small body, exit form.
// x19 is callee-saved, so the two ABI arms pay the spill a real RA would.
        .text
        .arch armv8.4-a

.macro BODY
        add     x0, x0, #1
        add     x0, x0, #1
        add     x0, x0, #1
        add     x0, x0, #1
.endm

        .p2align 4
        .globl  _exit_noop          // call + return only: subtracts caller overhead
_exit_noop:
        BODY
        ret

        .p2align 4
        .globl  _exit_imm           // TODAY: unconditional enable / unconditional clear
_exit_imm:
        msr     DIT, #1
        BODY
        msr     DIT, #0
        ret

        .p2align 4
        .globl  _exit_reg           // ABI, branchless restore
_exit_reg:
        str     x19, [sp, #-16]!
        mrs     x19, DIT
        msr     DIT, #1
        BODY
        msr     DIT, x19
        ldr     x19, [sp], #16
        ret

        .p2align 4
        .globl  _exit_grd           // ABI, guarded clear
_exit_grd:
        str     x19, [sp, #-16]!
        mrs     x19, DIT
        msr     DIT, #1
        BODY
        tbnz    x19, #24, 1f
        msr     DIT, #0
1:
        ldr     x19, [sp], #16
        ret

// ---- Experiment B: predictor isolation --------------------------------------
// The caller must establish a real entry DIT state for experiment A, and the
// only way to do that from a pattern is `msr DIT, xN`, which serializes. That
// drains the pipeline before the guard resolves and so UNDERSTATES a mispredict.
// These variants take the entry value in x1 instead, with no serializing write
// in front of the branch, to price the mispredict itself.
        .p2align 4
        .globl  _b_noop
_b_noop:
        BODY
        ret

        .p2align 4
        .globl  _b_reg              // branchless restore, value already in x1
_b_reg:
        BODY
        msr     DIT, x1
        ret

        .p2align 4
        .globl  _b_grd              // guarded clear, guard value already in x1
_b_grd:
        BODY
        tbnz    x1, #24, 2f
        msr     DIT, #0
2:
        ret

// ---- Branch-cost control ----------------------------------------------------
// Same tbnz on the same pattern, guarding a trivial add instead of `msr DIT,#0`.
// Its cost above _b_noop IS the branch + mispredict cost for that pattern. If
// this reads ~0 on the "random" pattern, the predictor LEARNED the pattern and
// no mispredict was measured -- which would invalidate the cheap-mispredict
// claim rather than support it.
        .p2align 4
        .globl  _b_brctl
_b_brctl:
        BODY
        tbnz    x1, #24, 3f
        add     x0, x0, #1
3:
        ret
