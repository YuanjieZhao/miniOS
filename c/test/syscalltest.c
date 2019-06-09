#include <xeroskernel.h>
#include <deltalist.h>
#include <i386.h>

/*-----------------------------------------------------------------------------------
 * Tests for syscall.c. This test suite assumes that the root process and dispatcher
 * are running.
 *
 * List of functions that are called from outside this file:
 *  - run_syscall_test
 *    - Runs the test suite for syscall.c
 *-----------------------------------------------------------------------------------
 */

static void sysgetpid_test(void);
static void process_for_sysgetpid_test(void);
static void sysputs_test(void);
static void syssetprio_test(void);
static void process_for_syssetprio_test(void);

static void syscreate_test_bad_params(void);
static void syscreate_test_max_processes(void);
static void syscall_test_factorial(void);
static int factorial(int n);
static void factorial_test1(void);
static void factorial_test2(void);
static void syssleep_test(void);
static void sleep_process(void);
static void sysgetcputimes_test(void);

// Used for sysgetcputimes_test
extern char *maxaddr;

// Used for sysgetpid_test
static PID_t next_pid = NULL;
static int const debug = 0;

// Used for syssleep_test
extern DeltaList sleep_queue;

/*-----------------------------------------------------------------------------------
 * Runs the test suite for syscall.c.
 *-----------------------------------------------------------------------------------
 */
void run_syscall_test(void) {
    kprintf("Running %s\n", __func__);

    sysgetpid_test();
    sysputs_test();
    syssetprio_test();
    syscreate_test_bad_params();
    syscall_test_factorial();
    syssleep_test();
    sysgetcputimes_test();
    syscreate_test_max_processes();

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Creates a process and checks that sysgetpid returns the PID of the
 * calling process.
 *
 * This test must be run before all other tests for simplified internal
 * testing logic.
 *-----------------------------------------------------------------------------------
 */
static void sysgetpid_test(void) {
    kprintf("Running %s\n", __func__);

    next_pid = syscreate(&process_for_sysgetpid_test, PROCESS_STACK_SIZE);
    sysyield(); // Yield control to process just created

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Used by sysgetpid_test to check that sysgetpid returns the PID of the
 * calling process.
 *-----------------------------------------------------------------------------------
 */
static void process_for_sysgetpid_test(void) {
    assert_equal(next_pid, sysgetpid());
    sysstop();
    assert(0, "process_for_sysgetpid_test is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Tests sysputs.
 *-----------------------------------------------------------------------------------
 */
static void sysputs_test(void) {
    kprintf("Running %s\n", __func__);

    sysputs("Hello cat!\n");
    char *str = "Hello fish!\n";
    sysputs(str);
    if (debug) kprintf("Invalid input to sysputs...\n");
    sysputs((char *) -3);
    sysputs((char *) NULL);

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests syssetprio.
 *-----------------------------------------------------------------------------------
 */
static void syssetprio_test(void) {
    kprintf("Running %s\n", __func__);

    syscreate(&process_for_syssetprio_test, PROCESS_STACK_SIZE);
    sysyield();

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Used by syssetprio_test.
 *-----------------------------------------------------------------------------------
 */
static void process_for_syssetprio_test(void) {
    int curr_priority = syssetprio(-1);
    // 3 is the default priority when a process is created
    assert_equal(curr_priority, 3);

    // Test: Error cases where requested priority is out of range
    if (debug) sysputs("Request priorities out of range...\n");
    assert_equal(syssetprio(-2), -1);
    assert_equal(syssetprio(4), -1);

    if (debug) sysputs("Request priorities in the range...\n");
    assert_equal(syssetprio(2), 3);
    assert_equal(syssetprio(1), 2);
    assert_equal(syssetprio(-1), 1);
    assert_equal(syssetprio(0), 1);
    assert_equal(syssetprio(-1), 0);

    sysstop();
    assert(0, "process_for_syssetprio_test is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Keeps creating new processes until the maximum is reached. Verifies
 * that the correct error code is returned.
 *-----------------------------------------------------------------------------------
 */
static void syscreate_test_max_processes(void) {
    kprintf("Running %s\n", __func__);
    // Set priority of root process to be higher than created processes
    syssetprio(2);
    int result;
    // # of processes created
    int count = 0;

    do {
        result = syscreate(&dummy_process, PROCESS_STACK_SIZE);
        if (result == -1) {
            if (debug) sysputs("syscreate returned -1\n");
        } else {
            count++;
            if (debug) kprintf("%dth process is created with pid = %d\t", count, result);
        }
    } while (result >= 1);
    assert_equal(result, -1);
    // The root process is the only process before this test runs,
    // so we should be able to create 31 processes
    assert_equal(count, PCB_TABLE_SIZE - 1);

    // Clean up created dummy processes
    yield_to_all();
    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests calling syscreate with bad parameters. Passes invalid parameters
 * and verifies that the correct result code is produced. On failure, -1
 * is the result code.
 *-----------------------------------------------------------------------------------
 */
static void syscreate_test_bad_params(void) {
    kprintf("Running %s\n", __func__);
    int result;

    result = syscreate((funcptr) - 1, PROCESS_STACK_SIZE);
    assert_equal(result, -1);

    result = syscreate((funcptr) NULL, PROCESS_STACK_SIZE);
    assert_equal(result, -1);

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests function used to trigger many sysyield calls.
 *-----------------------------------------------------------------------------------
 */
static int factorial(int n) {
    sysyield();

    if (n == 0) {
        return 1;
    }

    return n * factorial(n - 1);
}

/*-----------------------------------------------------------------------------------
 * Tests using factorial.
 *-----------------------------------------------------------------------------------
 */
static void factorial_test1(void) {
    // Test: Make sure syscreate and sysyield don't mess up stack
    int result1 = factorial(1);
    int result2 = factorial(2);
    int result3 = factorial(4);
    int result4 = factorial(8);
    if (debug) {
        kprintf("%s: factorial(1) = %d\n", __func__, result1);
        kprintf("%s: factorial(2) = %d\n", __func__, result2);
        kprintf("%s: factorial(4) = %d\n", __func__, result3);
        kprintf("%s: factorial(8) = %d\n", __func__, result4);
    }
    sysstop();

    assert(0, "Process is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Tests using factorial.
 *-----------------------------------------------------------------------------------
 */
static void factorial_test2(void) {
    // Test: Make sure syscreate and sysyield don't mess up stack
    int result1 = factorial(2);
    int result2 = factorial(2);
    int result3 = factorial(2);
    int result4 = factorial(2);
    if (debug) {
        kprintf("%s: factorial(2) = %d\n", __func__, result1);
        kprintf("%s: factorial(2) = %d\n", __func__, result2);
        kprintf("%s: factorial(2) = %d\n", __func__, result3);
        kprintf("%s: factorial(2) = %d\n", __func__, result4);
    }
    sysstop();

    assert(0, "Process is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Tests cooperative multitasking and stack usage. Has several processes
 * making many nested function calls while interleaving their execution.
 *-----------------------------------------------------------------------------------
 */
static void syscall_test_factorial(void) {
    kprintf("Running %s\n", __func__);
    syscreate(&factorial_test1, PROCESS_STACK_SIZE);
    syscreate(&factorial_test2, PROCESS_STACK_SIZE);

    yield_to_all();
    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests syssleep.
 *-----------------------------------------------------------------------------------
 */
static void syssleep_test(void) {
    kprintf("Running %s\n", __func__);
    unsigned int pid = syscreate(&sleep_process, PROCESS_STACK_SIZE);
    assert(delta_is_empty(&sleep_queue), "sleep_queue is not empty");
    assert_equal(delta_size(&sleep_queue), 0);
    sysyield();
    assert_equal(delta_size(&sleep_queue), 1);
    syskill(pid, 31);
    assert(delta_is_empty(&sleep_queue), "sleep_queue is not empty");

    kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A process that sleeps for a long time.
 *-----------------------------------------------------------------------------------
 */
static void sleep_process(void) {
    syssleep(90000000);
}

/*-----------------------------------------------------------------------------------
 * Tests sysgetcputimes.
 *-----------------------------------------------------------------------------------
 */
static void sysgetcputimes_test(void) {
    kprintf("Running %s\n", __func__);
    // Make sure all other processes have died
    yield_to_all();

    // Invalid address
    assert(sysgetcputimes((processStatuses *) (maxaddr + 1)) == -2, "Invalid address, beyond the end of main memory");
    assert(sysgetcputimes((processStatuses *) HOLESTART) == -1, "Invalid address, in the hole region");

    processStatuses ps;

    // Valid cases
    int last_slot_used1 = sysgetcputimes(&ps);
    assert(last_slot_used1 >= 0 && last_slot_used1 < PCB_TABLE_SIZE, "Return value of sysgetcputimes is not expected");
    // Check that the printed information is reasonable
    call_sysgetcputimes();

    sysputs("Creating more processes...\n");

    syscreate(&dummy_process, PROCESS_STACK_SIZE);
    syscreate(&dummy_process, PROCESS_STACK_SIZE);
    call_sysgetcputimes();

    yield_to_all();
    kprintf("Finished %s\n", __func__);
}
