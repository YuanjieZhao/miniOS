#include <xeroskernel.h>
#include <queue.h>

/*-----------------------------------------------------------------------------------
 * Tests for queue.c.
 *
 * List of functions that are called from outside this file:
 * - run_queue_test
 *   - Runs the test suite for queue.c
 *-----------------------------------------------------------------------------------
 */

static int const debug = 0;

/*-----------------------------------------------------------------------------------
 * Runs the test suite for queue.c.
 *-----------------------------------------------------------------------------------
 */
void run_queue_test(void) {
    kprintf("Running queuetest...\n");

    Queue q;

    // Test: init_queue
    init_queue(&q);

    // Test: is_empty
    assert_equal(is_empty(&q), 1);
    if (debug) print_queue(&q);

    // Initialize 64 PCBs to test process queue
    // The PID is the index of the pcbs array
    pcb_t pcbs[2 * PCB_TABLE_SIZE];
    for (int i = 0; i < 2 * PCB_TABLE_SIZE; i++) {
        pcbs[i].pid = i;
    }

    // Test: enqueue behaves correctly
    for (int i = 0; i < PCB_TABLE_SIZE; i++) {
        enqueue(&q, &pcbs[i]);
        if (debug) print_queue(&q);
    }
    assert_equal(size(&q), PCB_TABLE_SIZE);

    // Test: peek_tail
    pcb_t *proc = peek_tail(&q);
    assert(proc != NULL, "peek_tail() should not return NULL");
    assert_equal(proc->pid, PCB_TABLE_SIZE - 1);

    // Test: is_empty
    assert_equal(is_empty(&q), 0);

    if (debug) busy_wait();
    // Test: enqueue and dequeue coordinate correctly
    for (int i = PCB_TABLE_SIZE; i < 2 * PCB_TABLE_SIZE; i++) {
        pcb_t *proc = dequeue(&q);
        if (debug) kprintf("dequeued process w/ pid = %d\n", proc->pid);
        enqueue(&q, &pcbs[i]);
        if (debug) kprintf("enqueued process w/ pid = %d\n", proc->pid);
        if (debug) print_queue(&q);
        if (debug) wait();
    }
    assert_equal(size(&q), PCB_TABLE_SIZE);

    if (debug) busy_wait();

    // Test: dequeue behaves correctly
    for (int i = 0; i < PCB_TABLE_SIZE; ++i) {
        dequeue(&q);
        if (debug) print_queue(&q);
        if (debug) wait();
    }

    // Test: dequeue from empty queue
    assert_equal((int) dequeue(&q), (int) NULL);
    assert_equal(size(&q), 0);

    // Test: peek_tail on empty queue
    assert(peek_tail(&q) == NULL, "peek_tail() should return NULL");

    // Test: remove
    for (int i = 0; i < PCB_TABLE_SIZE; i++) {
        enqueue(&q, &pcbs[i]);
        if (debug) print_queue(&q);
        if (debug) wait();
    }

    // Remove elements from right to left
    if (debug) kprintf("Start removing elements from queue...\n");
    for (int i = PCB_TABLE_SIZE - 1; i >= 0; i--) {
        remove(&q, &pcbs[i]);
        if (debug) print_queue(&q);
        if (debug) wait();
    }

    kprintf("Finished %s\n", __func__);
}
