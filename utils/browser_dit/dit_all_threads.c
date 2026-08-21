/* dit_all_threads.c - set PSTATE.DIT on every thread of a process, via
 * DYLD_INSERT_LIBRARIES.
 *
 * PSTATE.DIT is per-thread and a freshly created thread starts with it clear,
 * so a plain constructor only covers the main thread. This interposes
 * pthread_create and sets DIT at the top of every thread's start routine.
 *
 * Build twice:
 *   -DDIT_ENABLE=1  -> the treatment arm (DIT actually set)
 *   -DDIT_ENABLE=0  -> the null control (identical interposition, trampoline
 *                      and malloc traffic, but no DIT write). A vs null
 *                      isolates the cost of the harness; null vs treatment
 *                      isolates the cost of DIT.
 *
 * DIT_ONLY_PROG=<substr>[,<substr>...] restricts DIT to processes whose name
 * matches, leaving the rest untouched. FLOP section 7 got its 4.5% Speedometer
 * figure by patching "the DIT bit in the rendering process" specifically, so
 * reproducing that methodology means renderer-only, not every process.
 *
 * DIT_ONLY_THREAD=<substr>[,<substr>...] restricts DIT further, to threads whose
 * name matches. This attributes the renderer's DIT cost between thread classes -
 * the point being to find out how much of it sits in code an AOT compiler pass
 * could actually reach. It is implemented by interposing pthread_setname_np,
 * because a thread's name is set by the thread itself, after it starts: at
 * pthread_create time the name does not exist yet. Consequences, both reported:
 *   - work a thread does BEFORE naming itself runs without DIT. Gecko names its
 *     threads at the top of the start routine, so this window is startup-only,
 *     but it makes every DIT_ONLY_THREAD number a slight LOWER bound.
 *   - threads that never call pthread_setname_np never get DIT at all.
 * In thread mode the main thread is also left clear at load and picked up when
 * it names itself (Gecko names it "GeckoMain").
 *
 * Diagnostics go to stderr, one line per process at load and one at exit:
 *   [dit] load pid=... prog=... enable=1 main_dit=1 applies=1
 *   [dit] exit pid=... prog=... threads_started=N threads_total=M
 * `applies` is 0 for a process excluded by DIT_ONLY_PROG - that is a deliberate
 * opt-out, not a coverage failure, and the report distinguishes the two.
 * threads_total comes from task_threads() and includes libdispatch worker
 * threads, which are NOT created via pthread_create and therefore do NOT get
 * DIT. The gap between the two numbers is the honest coverage figure and the
 * benchmark reports it.
 */

#include <dlfcn.h>
#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef DIT_ENABLE
#define DIT_ENABLE 1
#endif

static atomic_uint g_threads_started;
/* When DIT_ONLY_PROG is set (comma-separated substrings), only processes whose
 * name matches get DIT. FLOP section 7 is explicit that their 4.5% Speedometer
 * number came from patching "the DIT bit in the rendering process", not every
 * process, so matching that methodology means renderer-only. */
static int g_applies = 1;
/* Thread-class mode: non-NULL when DIT_ONLY_THREAD is set. In this mode DIT is
 * NOT set at thread start or at load; it is set only when a thread names itself
 * and the name matches. */
static const char *g_thread_filter;
static atomic_uint g_threads_matched;
/* Per-thread logging is useful during `verify` but is pointless noise inside a
 * timed run, so it is opt-in. The exit-time summary is not enough on its own:
 * the harness kills Firefox, and destructors do not run on SIGKILL. */
static int g_verbose;

static inline void dit_set(void) {
#if DIT_ENABLE
    /* In thread-class mode DIT is granted only by name, in dit_apply_for_name;
     * the unconditional grants at load and at thread start are suppressed. */
    if (g_applies && !g_thread_filter)
        __asm__ volatile("msr DIT, #1" ::: "memory");
#endif
}

static inline void dit_force_set(void) {
#if DIT_ENABLE
    __asm__ volatile("msr DIT, #1" ::: "memory");
#endif
}

static inline void dit_force_clear(void) {
#if DIT_ENABLE
    __asm__ volatile("msr DIT, #0" ::: "memory");
#endif
}

/* True if `filter` is unset/empty, or `name` contains one of its comma-separated
 * substrings. Shared by the process filter and the thread filter. */
static int name_matches(const char *filter, const char *name) {
    if (!filter || !*filter)
        return 1;
    if (!name)
        return 0;
    for (const char *p = filter; *p;) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        char needle[256];
        if (n && n < sizeof needle) {
            memcpy(needle, p, n);
            needle[n] = '\0';
            if (strstr(name, needle))
                return 1;
        }
        p = comma ? comma + 1 : p + n;
    }
    return 0;
}

/* True if DIT_ONLY_PROG is unset, or this process's name matches it. */
static int prog_selected(void) {
    return name_matches(getenv("DIT_ONLY_PROG"), getprogname());
}

/* PSTATE.DIT is reported in bit 24 of the DIT system register. */
static inline unsigned long dit_get(void) {
    unsigned long d;
    __asm__ volatile("mrs %0, DIT" : "=r"(d));
    return (d >> 24) & 1UL;
}

static unsigned thread_count(void) {
    thread_act_array_t list;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &list, &n) != KERN_SUCCESS)
        return 0;
    for (mach_msg_type_number_t i = 0; i < n; i++)
        mach_port_deallocate(mach_task_self(), list[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)list, n * sizeof(*list));
    return (unsigned)n;
}

struct start_args {
    void *(*fn)(void *);
    void *arg;
};

static void *trampoline(void *p) {
    struct start_args a = *(struct start_args *)p;
    free(p);
    dit_set();
    if (g_verbose)
        fprintf(stderr, "[dit] thread pid=%d prog=%s n=%u total=%u dit=%lu\n",
                (int)getpid(), getprogname(),
                atomic_load_explicit(&g_threads_started, memory_order_relaxed),
                thread_count(), dit_get());
    return a.fn(a.arg);
}

/* Thread-class mode. A thread names itself, from itself, after it has started -
 * so pthread_setname_np is the only hook that sees both the thread and its name.
 * Applied on every call so a rename is followed, in both directions. */
static void dit_apply_for_name(const char *name) {
    if (!g_applies || !g_thread_filter)
        return;
    int match = name_matches(g_thread_filter, name);
    if (match) {
        dit_force_set();
        atomic_fetch_add_explicit(&g_threads_matched, 1u, memory_order_relaxed);
    } else {
        dit_force_clear();
    }
    if (g_verbose)
        fprintf(stderr, "[dit] tname pid=%d prog=%s name=%s match=%d dit=%lu\n",
                (int)getpid(), getprogname(), name ? name : "(null)", match,
                dit_get());
}

static int dit_pthread_setname_np(const char *name) {
    int rc = pthread_setname_np(name);
    dit_apply_for_name(name);
    return rc;
}

__attribute__((used, section("__DATA,__interpose"))) static struct {
    const void *replacement;
    const void *replacee;
} interpose_pthread_setname_np = {(const void *)dit_pthread_setname_np,
                                  (const void *)pthread_setname_np};

/* Call pthread_create directly by name. dyld does not rebind references inside
 * the image that carries the __interpose section, so this reaches libSystem's
 * implementation rather than recursing.
 *
 * Do NOT "harden" this into dlsym(RTLD_NEXT, "pthread_create"): dyld applies
 * interposition to dlsym lookups as well, so that variant hands back this very
 * function and the first thread creation dies of stack overflow. Verified the
 * hard way. */
static int dit_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                              void *(*start_routine)(void *), void *arg) {
    struct start_args *a = malloc(sizeof *a);
    if (!a) /* never drop the thread just because we could not wrap it */
        return pthread_create(thread, attr, start_routine, arg);

    a->fn = start_routine;
    a->arg = arg;
    int rc = pthread_create(thread, attr, trampoline, a);
    if (rc != 0)
        free(a);
    else
        atomic_fetch_add_explicit(&g_threads_started, 1u, memory_order_relaxed);
    return rc;
}

__attribute__((used, section("__DATA,__interpose"))) static struct {
    const void *replacement;
    const void *replacee;
} interpose_pthread_create = {(const void *)dit_pthread_create,
                              (const void *)pthread_create};

__attribute__((constructor)) static void dit_load(void) {
    const char *tf = getenv("DIT_ONLY_THREAD");
    g_thread_filter = (tf && *tf) ? tf : NULL;
    g_applies = prog_selected();
    g_verbose = getenv("DIT_VERBOSE") != NULL;
    /* Must follow g_thread_filter: in thread-class mode the main thread starts
     * clear and is picked up when it names itself. */
    dit_set(); /* the main thread predates any interposed pthread_create */
    fprintf(stderr,
            "[dit] load pid=%d prog=%s enable=%d main_dit=%lu applies=%d tfilter=%s\n",
            (int)getpid(), getprogname(), DIT_ENABLE, dit_get(), g_applies,
            g_thread_filter ? g_thread_filter : "-");
}

__attribute__((destructor)) static void dit_unload(void) {
    fprintf(stderr,
            "[dit] exit pid=%d prog=%s threads_started=%u threads_total=%u "
            "threads_matched=%u\n",
            (int)getpid(), getprogname(),
            atomic_load_explicit(&g_threads_started, memory_order_relaxed),
            thread_count(),
            atomic_load_explicit(&g_threads_matched, memory_order_relaxed));
}
