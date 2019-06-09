#include <xeroskernel.h>

/*-----------------------------------------------------------------------------------
 * Tests for create.c. This test suite assumes that the root process and dispatcher
 * are running.
 *
 * List of functions that are called from outside this file:
 * - run_create_test
 *   - Runs the test suite for create.c
 *-----------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------------
 * Runs the test suite for create.c.
 *-----------------------------------------------------------------------------------
 */
void run_create_test(void) {
    kprintf("Running %s\n", __func__);
    // Test: Bad parameters
    assert_equal(create((funcptr) NULL, PROCESS_STACK_SIZE), 0);
    assert_equal(create((funcptr) - 1, PROCESS_STACK_SIZE), 0);

    // Test: Stack too small
    // If the stack is too small, the stack will be set to a
    // default stack size and create will continue
    assert_equal(create(&dummy_process, 0), 1);
    assert_equal(create(&dummy_process, -1), 1);
    assert_equal(create(&dummy_process, 50), 1);

    // sysyield to stop dummy processes
    for (int i = 0; i < PCB_TABLE_SIZE; i++) sysyield();

    yield_to_all();
    kprintf("Finished %s\n", __func__);
}
