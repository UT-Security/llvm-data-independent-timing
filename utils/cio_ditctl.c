/*
 * libditctl.dylib - arm selector for the CIO-parity run.
 *
 * CIO's eval drivers know nothing about PSTATE.DIT: their mitigation is
 * instruction substitution, so there is no mode to set. We need a blanket-DIT
 * arm anyway, and the drivers must stay byte-identical across arms or the
 * comparison is confounded. So the mode is set from OUTSIDE the program, by a
 * constructor in an injected dylib, and every arm injects this same dylib and
 * differs only in the environment:
 *
 *   ENABLE_DIT=1  -> msr dit, #1 before main, never cleared  (blanket)
 *   ENABLE_DIT=0  -> nothing                                  (every other arm)
 *
 * It also raises QoS to USER_INTERACTIVE for P-cluster residency, the same as
 * libqospin.dylib -- CIO used `taskset -c 0`, which has no macOS equivalent.
 *
 * The destructor reads PSTATE.DIT back and reports it. That is a validity gate,
 * not decoration: the blanket arm must exit with dit=1, and every selectively
 * placed arm must exit with dit=0. An arm that exits with dit=1 when it should
 * not has leaked the mode past an unbalanced exit and is blanket in disguise.
 */
#include <pthread/qos.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned long dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

__attribute__((constructor)) static void ditctl_start(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const char *e = getenv("ENABLE_DIT");
    if (e && e[0] == '1')
        __asm__ volatile("msr dit, #1\n\tisb" ::: "memory");
}

__attribute__((destructor)) static void ditctl_end(void) {
    fprintf(stderr, "DITCTL exit dit=%lu\n", dit_get());
}
