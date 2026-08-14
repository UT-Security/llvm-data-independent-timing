/* ecore_exec.c - put this process into background QoS, then exec the target.
 *
 * Exists because `taskpolicy -b <browser>` does NOT work here: /usr/sbin/taskpolicy
 * is an Apple platform binary, so dyld strips DYLD_INSERT_LIBRARIES before
 * exec'ing it and the browser downstream never gets the DIT dylib. Measured:
 * procs=0, run timed out. Same root cause as the shipped-Safari blocker.
 *
 * This binary is ours and ad-hoc signed, so the insert survives through the exec
 * into the browser and its children. PRIO_DARWIN_BG is inherited across fork/exec,
 * so Chromium's helpers land on E-cores too.
 *
 * Why E-cores matter here: on this M5 the LVP is worth 4.01x on a P-core but only
 * ~1.16-1.21x on an E-core. If the browser's always-on DIT cost really is the LVP,
 * it should mostly vanish in this arm. That makes it a causal control, not just
 * another data point.
 */

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ecore_exec <program> [args...]\n");
        return 2;
    }
    if (setpriority(PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG) != 0)
        fprintf(stderr, "[ecore] setpriority(PRIO_DARWIN_BG) failed; "
                        "run is NOT on E-cores\n");
    execv(argv[1], &argv[1]);
    perror("[ecore] execv");
    return 127;
}
