/* util.c : utility functions */

#include <xeroskernel.h>

/*-----------------------------------------------------------------------------------
 * This is where utility functions for testing live.
 *
 * List of functions that are called from outside this file:
 * - assert
 *   - Asserts that a given value is true
 * - assert_equal
 *   - Compares two values
 * - busy_wait
 *   - Pauses the kernel for a few seconds
 *-----------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------------
 * Asserts that the given value is true. If assertion fails, it will
 * print the given error message and pause the kernel.
 * @param to_assert The value to assert
 * @param message   The message to print if assertion fails
 *-----------------------------------------------------------------------------------
 */
void assert(int to_assert, char *message) {
    if (!to_assert) {
        kprintf("Assertion failed: %s\n", message);
        while (1);
    }
}

/*-----------------------------------------------------------------------------------
 * Asserts that the given values are equal. If assertion fails, it will
 * print an error message and pause the kernel.
 * @param actual   The actual value to compare
 * @param expected The expected value to compare
 *-----------------------------------------------------------------------------------
 */
void assert_equal(int actual, int expected) {
    if (actual != expected) {
        kprintf("Assertion failed: actual = %d, expected = %d\n", actual, expected);
        while (1);
    }
}

/*-----------------------------------------------------------------------------------
 * Pauses the kernel for a few seconds.
 *-----------------------------------------------------------------------------------
 */
void busy_wait(void) {
    kprintf("Busy wait...\n");
    for (int i = 0; i < 10000000; i++);
}

/*-----------------------------------------------------------------------------------
 * Pauses the kernel for 0.5s, to be used when we don't want to pause for
 * too long.
 *-----------------------------------------------------------------------------------
 */
void wait(void) {
    for (int i = 0; i < 1000000; i++);
}

/*-----------------------------------------------------------------------------------
 * Prints a message when program execution has passed this point. m stands
 * for "mark"
 * @param n The message to print
 *-----------------------------------------------------------------------------------
 */
void m(int n) {
    kprintf("Reached %d\n", n);
}

/*-----------------------------------------------------------------------------------
 * A dummy process for testing.
 *-----------------------------------------------------------------------------------
 */
void dummy_process(void) {
    sysstop();
    assert(0, "dummy_process is still executing after sysstop");
}

/*-----------------------------------------------------------------------------------
 * Sets priority of calling process to 3, and sysyield to give up control
 * to dummy processes, which call sysstop.
 *-----------------------------------------------------------------------------------
 */
void yield_to_all() {
    syssetprio(3);
    for (int i = 0; i < 200; i++) sysyield();
}
