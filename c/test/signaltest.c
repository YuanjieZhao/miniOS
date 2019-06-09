#include <xeroskernel.h>
#include <i386.h>

/*-----------------------------------------------------------------------------------
 * Tests for signal handling. This test suite assumes that the root process and dispatcher
 * are running, and that pre-emption is disabled.
 *
 * List of functions that are called from outside this file:
 * - run_signal_test
 *   - Runs the test suite for signal handling
 *-----------------------------------------------------------------------------------
 */

#define ERR_UNBLOCKED_ON_SIGNAL -666
#define ERR_TARGET_NOT_EXIST -514
#define ERR_INVALID_SIGNAL_NUMBER -583

#define LOW_PRI_SIGNAL 1
#define MEDIUM_PRI_SIGNAL 15
#define HIGH_PRI_SIGNAL 30

static void syssighandler_test(void);
static void low_pri_sighandler(void *arg);
static void high_pri_sighandler(void *arg);
static void syskill_test(void);
static void test_process(void);
static void syswait_test(void);
static void blocked_process(void);
static void signal_blocked_process_test(void);
static void signal_priority_interrupt_test(void);
static void low_pri_interrupt_sighandler(void *arg);

static unsigned int g_sender_pid = 0;

// Used to check if the signal fired matches the signal being received
static int g_signal_fired = 0;

static int const debug = 0;

void run_signal_test(void) {
    kprintf("Running %s\n", __func__);

    // Make sure previous tests won't affect this test
    yield_to_all();
    syssighandler_test();
    syskill_test();
    syswait_test();
    signal_priority_interrupt_test();
    while (1);
    signal_blocked_process_test();
    yield_to_all();
    // Expect: No blocked process
    if (debug) call_sysgetcputimes();

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests syssighandler.
 *-----------------------------------------------------------------------------------
 */
static void syssighandler_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    signal_handler_funcptr oldHandler = NULL;
    // Error case
    if (debug) sysputs("Testing error cases\n");
    assert_equal(syssighandler(-1, &low_pri_sighandler, &oldHandler), -1);
    assert_equal(syssighandler(32, &low_pri_sighandler, &oldHandler), -1);
    assert_equal(syssighandler(31, &low_pri_sighandler, &oldHandler), -1);
    assert_equal(syssighandler(0, (signal_handler_funcptr) - 1, &oldHandler), -2);
    assert_equal(syssighandler(0, (signal_handler_funcptr) HOLESTART, &oldHandler), -2);
    assert_equal(syssighandler(0, &low_pri_sighandler, NULL), -3);
    assert_equal(syssighandler(0, &low_pri_sighandler, (signal_handler_funcptr *) HOLESTART), -3);

    if (debug) sysputs("Testing normal cases\n");
    // To avoid interference between different tests, the signal number 13 is chosen
    // such that no other tests will use that signal
    assert_equal(syssighandler(13, &low_pri_sighandler, &oldHandler), 0);
    assert_equal(syssighandler(13, &high_pri_sighandler, &oldHandler), 0);
    assert(oldHandler == low_pri_sighandler, "Address of the old handler is not expected");
    assert_equal(syssighandler(0, NULL, &oldHandler), 0);

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A low priority signal handler.
 *-----------------------------------------------------------------------------------
 */
static void low_pri_sighandler(void *arg) {
    kprintf("Signal %d handler starts\n", LOW_PRI_SIGNAL);
    g_signal_fired = LOW_PRI_SIGNAL;
    assert(g_signal_fired == LOW_PRI_SIGNAL, "low_pri_sighandler");
}

/*-----------------------------------------------------------------------------------
 * A high priority signal handler.
 *-----------------------------------------------------------------------------------
 */
static void high_pri_sighandler(void *arg) {
    kprintf("Signal %d handler starts\n", HIGH_PRI_SIGNAL);
    g_signal_fired = HIGH_PRI_SIGNAL;
    assert(g_signal_fired == HIGH_PRI_SIGNAL, "high_pri_sighandler");
}

/*-----------------------------------------------------------------------------------
 * Tests syskill.
 *-----------------------------------------------------------------------------------
 */
static void syskill_test(void) {
    if (debug) kprintf("Running %s\n", __func__);
    PID_t mypid = sysgetpid();

    // Error cases
    assert_equal(syskill(999, 31), ERR_TARGET_NOT_EXIST);
    // PID is >= 1
    assert_equal(syskill(mypid, -1), ERR_INVALID_SIGNAL_NUMBER);
    assert_equal(syskill(mypid, 32), ERR_INVALID_SIGNAL_NUMBER);

    // Success case
    PID_t pid = syscreate(&test_process, PROCESS_STACK_SIZE);

    // Yield to test_process so that it can set up signal handler
    sysyield();

    assert_equal(syskill(pid, 0), 0);
    assert_equal(syskill(pid, HIGH_PRI_SIGNAL), 0);

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A test process used by syskill_test.
 *-----------------------------------------------------------------------------------
 */
static void test_process(void) {
    if (debug) kprintf("%s starts running\n", __func__);

    signal_handler_funcptr oldHandler = NULL;
    // Register a signal handler
    assert_equal(syssighandler(HIGH_PRI_SIGNAL, &high_pri_sighandler, &oldHandler), 0);

    sysyield();

    assert(g_signal_fired == HIGH_PRI_SIGNAL, "Received signal is not expected");

    // Check syscalls work correctly after signal handling
    sysstop();
    assert(0, "test_process is still running after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Tests syswait.
 *-----------------------------------------------------------------------------------
 */
static void syswait_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    // Error case
    // Returns -1 if the process to be waited for does not exist
    assert_equal(syswait(999), -1);
    assert_equal(syswait(0), -1);

    // Success case
    PID_t pid = syscreate(&dummy_process, PROCESS_STACK_SIZE);
    assert_equal(syswait(pid), 0);

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests return value of system call when a signal is targeted at blocked process.
 *-----------------------------------------------------------------------------------
 */
static void signal_blocked_process_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    PID_t pid = syscreate(&blocked_process, PROCESS_STACK_SIZE);
    g_sender_pid = sysgetpid();

    for (int i = 0; i < 4; i++) {
        sysyield();
        if (debug) kprintf("Sending signal %d\n", LOW_PRI_SIGNAL);
        int res = syskill(pid, LOW_PRI_SIGNAL);
        assert_equal(res, 0);
    }
    // Ensure that blocked_process is unblocked from the 4th system call
    sysyield();

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Helper process in signal_blocked_process_test.
 *-----------------------------------------------------------------------------------
 */
static void blocked_process(void) {
    if (debug) kprintf("%s starts\n", __func__);

    unsigned int target_pid = g_sender_pid;
    unsigned int receive_any_pid = 0;
    unsigned int num = 0;
    signal_handler_funcptr oldHandler = NULL;
    // This process must have a signal handler for the signal being sent by syskill,
    // otherwise, the signal will be ignored and the blocking system calls will remain blocked
    syssighandler(LOW_PRI_SIGNAL, &low_pri_sighandler, &oldHandler);

    assert_equal(syswait(target_pid), ERR_UNBLOCKED_ON_SIGNAL);
    // Test return values of send, receive, and receive-any
    assert_equal(syssend(target_pid, num), ERR_UNBLOCKED_ON_SIGNAL);
    assert_equal(sysrecv(&target_pid, &num), ERR_UNBLOCKED_ON_SIGNAL);
    assert_equal(sysrecv(&receive_any_pid, &num), ERR_UNBLOCKED_ON_SIGNAL);

    if (debug) kprintf("%s terminates\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests prioritization of signal handling when signals interrupt each other.
 *-----------------------------------------------------------------------------------
 */
static void signal_priority_interrupt_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    g_signal_fired = 0;
    if (debug) sysputs("Setting up signal handlers...\n");

    signal_handler_funcptr oldHandler;

    int res = syssighandler(LOW_PRI_SIGNAL, &low_pri_interrupt_sighandler, &oldHandler);
    assert_equal(res, 0);

    res = syssighandler(HIGH_PRI_SIGNAL, &high_pri_sighandler, &oldHandler);
    assert_equal(res, 0);

    if (debug) sysputs("Starts sending signals...\n");

    // Send a low priority signal to itself
    int pid = sysgetpid();
    if (debug) kprintf("%s sends signal %d\n", __func__, LOW_PRI_SIGNAL);
    res = syskill(pid, LOW_PRI_SIGNAL);
    assert_equal(res, 0);

    // Yield so that this process can execute its signal handler when it is scheduled to run next time
    sysyield();

    assert(g_signal_fired == LOW_PRI_SIGNAL,
           "signal_priority_interrupt_test expects the last signal handled is LOW_PRI_SIGNAL");
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A low priority signal handler that will send a high priority signal to itself.
 *-----------------------------------------------------------------------------------
 */
static void low_pri_interrupt_sighandler(void *arg) {
    sysputs("low_pri_interrupt_sighandler starts\n");

    int pid = sysgetpid();
    if (debug) kprintf("%s sends signal %d\n", __func__, HIGH_PRI_SIGNAL);
    // We expect that high_pri_sighandler will execute after sending this signal
    int res = syskill(pid, HIGH_PRI_SIGNAL);
    // Yield while low priority signal handler is running with pending high priority signal
    sysyield();
    sysputs("low_pri_interrupt_sighandler resumes\n");
    assert_equal(res, 0);

    g_signal_fired = LOW_PRI_SIGNAL;
    assert(g_signal_fired == LOW_PRI_SIGNAL, "low_pri_interrupt_sighandler");
}
