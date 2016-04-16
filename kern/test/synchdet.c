/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization test code. Note that all these tests run with interrupts
 * disabled to make them deterministic.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <spl.h>
#include <current.h>

static inline int startup(int *spl, const char *test, unsigned num_thread);
static inline int cleanup(int spl, unsigned num_thread);
static inline void check_failure(void);
static void success(void) __attribute__((noreturn));
static void fail(const char *msg) __attribute__((noreturn));
static inline void wait_for_value(volatile int *p, int x);

static struct semaphore *donesem;
static volatile int test_success;


/******************************************************************************/
/* Tests: locks
 *
 * Includes the following tests:
 *   - Check lock can be reacquired after release
 *   - Check do_i_hold
 *     + false at beginning
 *     + true after acquire
 *     + false after acquire in other thread
 *     + false after release
 *   - Check only one thread can acquire lock at once
 *   - Check that with n competing threads all eventually get the lock
 */

/**********************************************************/
/* Test 1: Check lock can be reacquired after release */

static int reacquire_thread(void *data1, unsigned long data2)
{
    (void) data1, (void) data2;

    struct lock *lock;

    lock = lock_create("locktests_det_reacquire");
    if (lock == NULL)
        fail("lock_create failed");

    lock_acquire(lock);
    lock_release(lock);

    lock_acquire(lock);
    lock_release(lock);

    lock_destroy(lock);
    success();
    return 0;
}

int locktests_det_reacquire(int nargs, char **args)
{
    int spl;
    (void) nargs, (void) args;

    if (startup(&spl, "locktest.reacquire", 1))
        return 1;

    thread_fork("locktests_det_reacquire.t0", NULL, reacquire_thread, NULL, 0);

    return cleanup(spl, 1);
}

/**********************************************************/
/* Test 2: Check do_i_hold
 *  + false at beginning
 *  + true after acquire
 *  + false after acquire in other thread
 *  + false after release
 */
struct do_i_hold_param {
    struct lock *l;
    volatile int v;
};

static int do_i_hold_primary(void *data1, unsigned long data2)
{
    struct do_i_hold_param *p = data1;
    (void) data2;

    /* first we acquire and free lock on this thread and make sure that
       do_i_hold makes sense */
    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true with un-held lock");

    lock_acquire(p->l);

    if (!lock_do_i_hold(p->l))
        fail("lock_do_i_hold false with held lock");

    lock_release(p->l);

    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true after releasing lock");

    /* okay, now let the secondary thread acquire the lock */
    p->v = 1;
    wait_for_value(&p->v, 2);

    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true with other thread holding the lock");

    p->v = 3;
    wait_for_value(&p->v, 4);

    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true after lock released by other thread");

    success();
    return 0;
}

static int do_i_hold_secondary(void *data1, unsigned long data2)
{
    struct do_i_hold_param *p = data1;
    (void) data2;

    wait_for_value(&p->v, 1);

    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true with un-held lock");

    lock_acquire(p->l);

    if (!lock_do_i_hold(p->l))
        fail("lock_do_i_hold false with held lock");

    p->v = 2;
    wait_for_value(&p->v, 3);
    lock_release(p->l);

    if (lock_do_i_hold(p->l))
        fail("lock_do_i_hold true after releasing lock");

    p->v = 4;
    success();
    return 0;
}

int locktests_det_do_i_hold(int nargs, char **args)
{
    int spl, ret;
    struct do_i_hold_param p;
    (void) nargs, (void) args;

    p.v = 0;
    p.l = lock_create("locktest_det_do_i_hold_lock");
    if (p.l == NULL) {
        kprintf("Test do_i_hold failed: lock_create returned NULL\n");
        return 1;
    }

    if (startup(&spl, "locktest_det_do_i_hold", 2)) {
        ret = 1;
        goto exit;
    }

    thread_fork("locktests_det_do_i_hold.t0", NULL, do_i_hold_primary, &p, 0);
    thread_fork("locktests_det_do_i_hold.t1", NULL, do_i_hold_secondary, &p, 0);

    ret = cleanup(spl, 2);
exit:
    lock_destroy(p.l);
    return ret;
}

/**********************************************************/
/* Test 3: Check only one thread can acquire lock */
struct mutex_param {
    struct lock *l;
    volatile unsigned int v;
};

static int mutex_thread(void *data1, unsigned long data2)
{
    struct mutex_param *p = data1;
    int i;

    lock_acquire(p->l);
    p->v = data2;

    for (i = 0; i < 42; i++) {
        thread_yield();
        if (p->v != data2)
            fail("Mutual exclusion not guaranteed after acquiring lock");
    }

    lock_release(p->l);

    success();
    return 0;
}

int locktests_det_mutex(int nargs, char **args)
{
    int spl, ret;
    struct mutex_param p;
    (void) nargs, (void) args;

    p.v = 0;
    p.l = lock_create("locktest_det_mutex_lock");
    if (p.l == NULL) {
        kprintf("Test mutex failed: lock_create returned NULL\n");
        return 1;
    }

    if (startup(&spl, "locktest_det_mutex", 2)) {
        ret = 1;
        goto exit;
    }

    thread_fork("locktests_mutex.t0", NULL, mutex_thread, &p, 1);
    thread_fork("locktests_mutex.t1", NULL, mutex_thread, &p, 2);

    ret = cleanup(spl, 2);
exit:
    lock_destroy(p.l);
    return ret;
}

/**********************************************************/
/* Test 4: Check that with n competing threads all eventually get the lock */

int locktests_det_competing(int nargs, char **args)
{
    int spl, ret;
    struct mutex_param p;
    unsigned N = 5, i;
    const char *tname_templ = "locktests_mutex.tX";
    char tname[strlen(tname_templ) + 1];
    (void) nargs, (void) args;

    p.v = 0;
    p.l = lock_create("locktest_det_competing_lock");
    if (p.l == NULL) {
        kprintf("Test competing failed: lock_create returned NULL\n");
        return 1;
    }

    if (startup(&spl, "locktest_det_competing", N)) {
        ret = 1;
        goto exit;
    }

    for (i = 0; i < N; i++) {
        strcpy(tname, tname_templ);
        tname[strlen(tname_templ) - 1] = '0' + i;
        thread_fork(tname, NULL, mutex_thread, &p, i + 1);
    }

    ret = cleanup(spl, N);
exit:
    lock_destroy(p.l);
    return ret;
}

/******************************************************************************/
/* Tests: CVs
 *   - check nothing bad happens with signal/broadcast while no-one is waiting
 *   - check signal wakes up one
 *   - check broadcast wakes up all
 */

/**********************************************************/
/* Test 1: Check nothing bad happens with signal/broadcast while no-one is
   waiting */

static int cvnoop_thread(void *data1, unsigned long data2)
{
    struct cv *cv;
    struct lock *lock;

    (void) data1, (void) data2;

    cv = cv_create("cvtest_noopwakeup");
    if (cv == NULL)
        fail("cv_create failed");

    lock = lock_create("cvtest_noopwakeup");
    if (lock == NULL) {
        cv_destroy(cv);
        fail("lock_creat failed");
    }

    lock_acquire(lock);
    if (!lock_do_i_hold(lock)) {
        lock_destroy(lock);
        cv_destroy(cv);
        fail("lock_do_i_hold not true after acquire");
    }

    cv_signal(cv, lock);

    if (!lock_do_i_hold(lock)) {
        lock_destroy(lock);
        cv_destroy(cv);
        fail("lock_do_i_hold not true after cv_signal");
    }

    cv_broadcast(cv, lock);

    if (!lock_do_i_hold(lock)) {
        lock_destroy(lock);
        cv_destroy(cv);
        fail("lock_do_i_hold not true after cv_broadcast");
    }

    cv_destroy(cv);
    success();
    return 0;
}

int cvtests_det_noopwakeup(int nargs, char **args)
{
    int spl;
    (void) nargs, (void) args;

    if (startup(&spl, "cvtest_det_noopwakeup", 1))
        return 1;

    thread_fork("cvtests_det_noopwakeup.t0", NULL, cvnoop_thread, NULL, 0);

    return cleanup(spl, 1);
}

/**********************************************************/
/* Test 2: Check signal wakes up one */

struct cv_param {
    struct lock *l;
    struct cv *cv;
    volatile int v;
};

static int signal_thread(void *data1, unsigned long N)
{
    struct cv_param *p = data1;
    unsigned i, j;

    wait_for_value(&p->v, (int) N);

    for (i = 0; i < N; i++) {
        lock_acquire(p->l);
        cv_signal(p->cv, p->l);
        check_failure();
        if (!lock_do_i_hold(p->l))
            fail("Do not hold lock after signal");
        lock_release(p->l);
        check_failure();

        if (p->v + i + 1 < N)
            fail("Woke up more than 1");
        for (j = 0; j < 42; j++) {
            thread_yield();
            check_failure();

            if (p->v + i + 1 < N)
                fail("Woke up more than 1");
        }
    }

    success();
    return 0;
}

static int waiting_thread(void *data1, unsigned long data2)
{
    struct cv_param *p = data1;

    (void) data2;

    lock_acquire(p->l);
    p->v++;
    cv_wait(p->cv, p->l);

    if (!lock_do_i_hold(p->l))
        fail("Do not hold lock after wait");

    p->v--;
    lock_release(p->l);
    success();
    return 0;
}

int cvtests_det_signal_one(int nargs, char **args)
{
    int spl, ret = 0;
    struct cv_param p;
    unsigned N = 5, i;
    const char *tname_templ = "cvtests_signal_one.tX";
    char tname[strlen(tname_templ) + 1];
    (void) nargs, (void) args;

    p.v = 0;
    p.l = lock_create("cvtests_signal_one");
    if (p.l == NULL) {
        kprintf("Test signal_one failed: lock_create returned NULL\n");
        return 1;
    }
    p.cv = cv_create("cvtests_signal_one");
    if (p.cv == NULL) {
        kprintf("Test signal_one failed: cv_create returned NULL\n");
        lock_destroy(p.l);
        return 1;
    }

    if (startup(&spl, "locktest_det_competing", N + 1)) {
        ret = 1;
        goto exit;
    }

    for (i = 0; i < N; i++) {
        strcpy(tname, tname_templ);
        tname[strlen(tname_templ) - 1] = '0' + i;
        thread_fork(tname, NULL, waiting_thread, &p, i + 1);
    }
    thread_fork("cvtests_signal_one.signal", NULL, signal_thread, &p, N);

    ret = cleanup(spl, N + 1);
exit:
    cv_destroy(p.cv);
    lock_destroy(p.l);
    return ret;
}

/**********************************************************/
/* Test 3: Check broadcast wakes up all */

static int broadcast_thread(void *data1, unsigned long N)
{
    struct cv_param *p = data1;

    wait_for_value(&p->v, (int) N);

    lock_acquire(p->l);
    if (!lock_do_i_hold(p->l))
        fail("Do not hold lock after acquire");

    cv_broadcast(p->cv, p->l);
    if (!lock_do_i_hold(p->l))
        fail("Do not hold lock after signal");
    lock_release(p->l);
    check_failure();

    wait_for_value(&p->v, (int) 0);

    success();
    return 0;
}

int cvtests_det_broadcast(int nargs, char **args)
{
    int spl, ret = 0;
    struct cv_param p;
    unsigned N = 5, i;
    const char *tname_templ = "cvtests_broadcast.tX";
    char tname[strlen(tname_templ) + 1];
    (void) nargs, (void) args;

    p.v = 0;
    p.l = lock_create("cvtests_broadcast");
    if (p.l == NULL) {
        kprintf("Test broadcast failed: lock_create returned NULL\n");
        return 1;
    }
    p.cv = cv_create("cvtests_broadcast");
    if (p.cv == NULL) {
        kprintf("Test broadcast failed: cv_create returned NULL\n");
        lock_destroy(p.l);
        return 1;
    }

    if (startup(&spl, "locktest_det_competing", N + 1)) {
        ret = 1;
        goto exit;
    }

    for (i = 0; i < N; i++) {
        strcpy(tname, tname_templ);
        tname[strlen(tname_templ) - 1] = '0' + i;
        thread_fork(tname, NULL, waiting_thread, &p, i + 1);
    }
    thread_fork("cvtests_broadcast.signal", NULL, broadcast_thread, &p, N);

    ret = cleanup(spl, N + 1);
exit:
    cv_destroy(p.cv);
    lock_destroy(p.l);
    return ret;
}

/******************************************************************************/
/* Tests: Thread Join
 *      Check joinable thread can be joined
 *      test1: when child finishes before parent joins
 *      test2: when child finishes after parent joins
 */

/**********************************************************/
/* Test 1: child finishes before parent join
 */ 
static int quick_thread(void *data1, unsigned long data2)
{
    (void) data1;
    (void) data2;
    return 1;
}

int jointest_child_earlyfinish(int nargs, char **args)
{
    (void) nargs, (void) args;
    struct thread * newthread = NULL;
    KASSERT(thread_fork_joinable("earlyfinish", NULL, quick_thread, NULL, 0, &newthread) == 0);
    KASSERT(newthread != NULL);
    clocksleep(1);
    KASSERT(thread_join(newthread) == 1);
    kprintf("Test thread_join when child finishes before parent joins succeeded\n");
    return 0;
}

static int slow_thread(void *data1, unsigned long data2)
{
    (void) data1;
    (void) data2;
    clocksleep(1);
    return 1;
}

int jointest_child_latefinish(int nargs, char **args)
{
    (void) nargs, (void) args;
    struct thread * newthread = NULL;
    KASSERT(thread_fork_joinable("latefinish", NULL, slow_thread, NULL, 0, &newthread) == 0);
    KASSERT(newthread != NULL);
    KASSERT(thread_join(newthread) == 1);
    kprintf("Test thread_join when child finishes after parent joins succeeded\n");
    return 0;
}

static int tryjoin_thread(void *data1, unsigned long data2)
{
    (void) data2;
    struct thread * target = (struct thread *) data1;
    kprintf("This test should crash\n");
    thread_join(target);
    return 0;
}

int jointest_onlyparent_canjoin(int nargs, char **args)
{
    (void) nargs, (void) args;
    struct thread * newthread = NULL;
    KASSERT(thread_fork_joinable("latefinish", NULL, slow_thread, NULL, 0, &newthread) == 0);
    KASSERT(thread_fork("tryjoin", NULL, tryjoin_thread, (void *) newthread, 0) == 0);
    return 0;
}


/******************************************************************************/
/* helpers */

static inline int startup(int *spl, const char *test, unsigned num_threads)
{
    unsigned i;
    test_success = 0;
    donesem = sem_create("locktests_det_donesem", num_threads);
    if (donesem == NULL) {
        kprintf("Test %s startup failed\n", test);
        return 1;
    }

    for (i = 0; i < num_threads; i++) {
        P(donesem);
    }

    *spl = splhigh();
    return 0;
}

static inline int cleanup(int spl, unsigned num_threads)
{
    unsigned i;
    for (i = 0; i < num_threads; i++) {
        P(donesem);
    }

    sem_destroy(donesem);
    splx(spl);

    if (test_success == 0)
        kprintf("SUCCESS\n");
    else
        kprintf("FAILED\n");

    return test_success;
}

static inline void wait_for_value(volatile int *p, int x)
{
    while (*p != x) {
        check_failure();
        thread_yield();
    }
    check_failure();
}

static inline void check_failure(void)
{
    if (test_success != 0) {
        kprintf("  Test thread terminating because of failure in other thread "
                "[%s]\n", curthread->t_name);
        V(donesem);
        thread_exit();
    }
}

static void fail(const char *msg)
{
    kprintf("  Test thread failed [%s]: %s\n", curthread->t_name, msg);
    test_success = 1;
    V(donesem);
    thread_exit();
}

static void success(void)
{
    kprintf("  Test thread succeeded [%s]\n", curthread->t_name);
    V(donesem);
    thread_exit();
}

