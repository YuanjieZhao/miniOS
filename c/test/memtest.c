#include <xeroskernel.h>
#include <i386.h>

/*-----------------------------------------------------------------------------------
 * Tests for mem.c.
 *
 * List of functions that are called from outside this file:
 * - run_mem_test
 *   - Runs the test suite for mem.c
 *-----------------------------------------------------------------------------------
 */

extern unsigned long freemem_aligned;
extern unsigned long hole_start_aligned;
extern unsigned long hole_end_aligned;
extern unsigned long max_addr_aligned;

/*-----------------------------------------------------------------------------------
 * Runs the test suite for mem.c.
 *-----------------------------------------------------------------------------------
 */
void run_mem_test(void) {
    kprintf("Testing memory management...\n");

    // Test: kmeminit initializes with 2 free blocks
    assert_equal(get_free_list_length(), 2);

    // Test: kmalloc rejects 0
    assert_equal((int) kmalloc(0), 0);
    // Test: kmalloc rejects negative
    assert_equal((int) kmalloc(-1), 0);

    // Test: kmalloc rejects size larger than maximum allocatable memory
    // https://stackoverflow.com/questions/3472311/what-is-a-portable-method-to-find-the-maximum-value-of-size-t
    size_t max_size = (size_t) - 1; // Upper bound of size_t is (2^32 - 1) in this system
    assert_equal((int) kmalloc(max_size), 0);

    // Test: kfree rejects NULL
    assert_equal(kfree(NULL), 0);

    // Test: kfree rejects address not on paragraph boundary
    assert_equal(kfree((void *) 10), 0);

    // kmalloc and kfree tests
    unsigned long *p1 = (unsigned long *) kmalloc(16);
    unsigned long *p2 = (unsigned long *) kmalloc(16);
    unsigned long *p3 = (unsigned long *) kmalloc(16);
    unsigned long *p4 = (unsigned long *) kmalloc(16);
    assert_equal(get_free_list_length(), 2);

    // Test: kfree merges adjacent free blocks
    assert_equal(kfree(p1), 1);
    assert_equal(get_free_list_length(), 3);
    assert_equal(kfree(p2), 1);
    assert_equal(get_free_list_length(), 3);
    assert_equal(kfree(p4), 1);
    assert_equal(get_free_list_length(), 3);
    assert_equal(kfree(p3), 1);
    assert_equal(get_free_list_length(), 2);

    // Test: Cannot free block twice
    assert_equal(kfree(p4), 0);

    // Test: Allocate all available free memory
    unsigned long *p5 = (unsigned long *) kmalloc(hole_start_aligned - freemem_aligned - 16);
    unsigned long *p6 = (unsigned long *) kmalloc(max_addr_aligned - hole_end_aligned - 16);
    assert_equal((int) kmalloc(1), 0);
    assert_equal(get_free_list_length(), 0);
    assert_equal(kfree(p5), 1);
    assert_equal(get_free_list_length(), 1);
    assert_equal(kfree(p6), 1);
    assert_equal(get_free_list_length(), 2);

    // Test: kmalloc aligns on paragraph boundary
    // Need 16 bytes for header, then 10 bytes to satisfy request
    unsigned long *p7 = (unsigned long *) kmalloc(10);
    assert_equal((unsigned long) p7, freemem_aligned + 16);

    // 26 bytes was allocated, so next header starts at 32 and next block starts at 48
    unsigned long *p8 = (unsigned long *) kmalloc(10);
    assert_equal((unsigned long) p8, freemem_aligned + 48);
}
