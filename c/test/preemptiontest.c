#include <xeroskernel.h>
#include <i386.h>

/*-----------------------------------------------------------------------------------
 * Tests for pre-emption. This test suite assumes that the root process and dispatcher
 * are running, and that pre-emption is enabled.
 *
 * List of functions that are called from outside this file:
 * - run_preemption_test
 *   - Runs the test suite for pre-emption
 *-----------------------------------------------------------------------------------
 */

static void uncooperative_process_test(void);
static int factorial(int n);
static void factorial_test1(void);
static void factorial_test2(void);
static void cooperative_process_test(void);
static void sub1_process(void);
static void add2_process(void);
static void wait_for_a_time_slice(void);

// Used for cooperative_process_test
static unsigned int g_add2_pid;
static unsigned int g_sub1_pid;
// The number of communication rounds between sender and receiver processes
static int TOTAL_ROUNDS = 5;

static int const debug = 0;

void run_preemption_test(void) {
    kprintf("Running %s\n", __func__);

    uncooperative_process_test();
    cooperative_process_test();

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Creates several uncooperative processes and tests that preemption won't disrupt
 * the work of each process. This test is similar to syscall_test_factorial in
 * syscalltest.c except that the process itself no longer calls sysyield to yield
 * control voluntarily.
 *-----------------------------------------------------------------------------------
 */
static void uncooperative_process_test(void) {
    if (debug) kprintf("Running %s\n", __func__);
    syscreate(&factorial_test1, PROCESS_STACK_SIZE);
    syscreate(&factorial_test2, PROCESS_STACK_SIZE);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A factorial function that waits for a time slice in each recursive call.
 *-----------------------------------------------------------------------------------
 */
static int factorial(int n) {
    if (n == 0) {
        return 1;
    }

    wait_for_a_time_slice();
    return n * factorial(n - 1);
}

/*-----------------------------------------------------------------------------------
 * Tests using factorial.
 *-----------------------------------------------------------------------------------
 */
static void factorial_test1(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    // Test: Make sure preemption doesn't mess up stack
    int result1 = factorial(20);
    int result2 = factorial(30);
    int result3 = factorial(20);
    int result4 = factorial(30);
    assert(result1 == result3, "factorial_test1 computes wrong value");
    assert(result2 == result4, "factorial_test1 computes wrong value");

    sysstop();
    assert(0, "Process is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Tests using factorial.
 *-----------------------------------------------------------------------------------
 */
static void factorial_test2(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    int result1 = factorial(20);
    int result2 = factorial(20);
    int result3 = factorial(20);
    int result4 = factorial(20);
    assert(result1 == result2, "factorial_test2 computes wrong value");
    assert(result2 == result3, "factorial_test2 computes wrong value");
    assert(result3 == result4, "factorial_test2 computes wrong value");

    // Test return
    return;
    assert(0, "Process is still executing after return");
}

/*-----------------------------------------------------------------------------------
 * Create two cooperative processes and test preemption won't disrupt the work of
 * each process. The two processes try to count from 0 to TOTAL_ROUNDS via message passing.
 *-----------------------------------------------------------------------------------
 */
static void cooperative_process_test(void) {
    if (debug) kprintf("Running %s\n", __func__);
    g_add2_pid = syscreate(&add2_process, PROCESS_STACK_SIZE);
    g_sub1_pid = syscreate(&sub1_process, PROCESS_STACK_SIZE);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * add2_process starts counting from 0. In each iteration, it increments the counter
 * by 2 and sends the counter value to sub1_process.
 *-----------------------------------------------------------------------------------
 */
static void add2_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    unsigned int sub1_pid = g_sub1_pid;
    unsigned int num = 0;
    int result = -1;

    for (int i = 1; i <= TOTAL_ROUNDS; i++) {
        num = num + 2;
        wait_for_a_time_slice();

        if (debug) sysputs("add2_process sends to sub1_process\n");
        result = syssend(sub1_pid, num);
        assert(result == 0, "add2_process: syssend result is not expected");
        if (debug) sysputs("add2_process receives from sub1_process\n");
        result = sysrecv(&sub1_pid, &num);
        assert(result == 0, "add2_process: sysrecv result is not expected");

        assert(num == i, "add2_process received a wrong num");
    }
}

/*-----------------------------------------------------------------------------------
 * sub1_process decrements the counter value by 1 in each iteration and sends the
 * updated counter value to add2_process.
 *-----------------------------------------------------------------------------------
 */
static void sub1_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    unsigned int add2_pid = g_add2_pid;
    unsigned int num = 0;
    int result = -1;
    for (int i = 1; i <= TOTAL_ROUNDS; i++) {
        if (debug) sysputs("sub1_process receives from add2_process\n");
        result = sysrecv(&add2_pid, &num);
        assert(result == 0, "sub1_process: sysrecv result is not expected");

        num = num - 1;
        wait_for_a_time_slice();

        assert(num == i, "sub1_process received a wrong num");
        if (debug) sysputs("sub1_process sends to add2_process\n");
        result = syssend(add2_pid, num);
        assert(result == 0, "sub1_process: syssend result is not expected");
    }
}

/*-----------------------------------------------------------------------------------
 * Pause the kernel for a time slice.
 *-----------------------------------------------------------------------------------
 */
static void wait_for_a_time_slice(void) {
    for (int i = 0; i < TIME_SLICE; i++);
}
