#include <xeroskernel.h>
#include <deltalist.h>

/*------------------------------------------------------------------------
 * Tests for deltalist.c.
 *
 * List of functions that are called from outside this file:
 * - run_delta_list_test
 *   - Runs the test suite for deltalist.c
 *------------------------------------------------------------------------
 */

static void check_sleep_time_in_delta_list(DeltaList *list);

static int const debug = 0;

/*------------------------------------------------------------------------
 * Runs the test suite for deltalist.c.
 *------------------------------------------------------------------------
 */
void run_deltalist_test(void) {
    kprintf("Running delta_list_test...\n");

    DeltaList q;

    init_delta_list(&q);

    // Test: delta_is_empty
    assert_equal(delta_is_empty(&q), 1);
    if (debug) delta_print_list(&q);

    const int n = 5;

    // Initialize 2n PCBs to test process delta list
    // The PID is the index of the pcbs array
    pcb_t pcbs[2 * n];
    for (int i = 0; i < 2 * n; i++) {
        pcbs[i].pid = i;
        pcbs[i].next = NULL;
    }

    // Test: insert
    for (int i = 0; i < n; i++) {
        int sleep_time = i * i;
        insert(&q, &pcbs[i], sleep_time);
        if (debug) delta_print_list(&q);
        if (debug) wait();
    }
    assert_equal(delta_size(&q), n);

    // Test: delta_peek
    pcb_t *proc = delta_peek(&q);
    assert(proc != NULL, "delta_peek should not return NULL");
    assert_equal(proc->pid, 0);

    // Test: delta_is_empty
    assert_equal(delta_is_empty(&q), 0);

    if (debug) busy_wait();

    // Test: insert and poll coordinate correctly
    int base_sleep_time = (n - 1) * (n - 1);

    for (int i = 0; i < n; i++) {
        pcb_t *proc = poll(&q);
        if (debug) kprintf("polled process w/ pid = %d, key = %d\n", proc->pid, proc->key);
        if (debug) delta_print_list(&q);
        if (debug) wait();

        int sleep_time = base_sleep_time + i;
        insert(&q, &pcbs[n + i], sleep_time);
        if (debug) kprintf("inserted process w/ pid = %d, sleep = %d\n", pcbs[n + i].pid, sleep_time);
        if (debug) delta_print_list(&q);
        if (debug) wait();
    }
    assert_equal(delta_size(&q), n);

    if (debug) busy_wait();

    // Test: poll
    for (int i = 0; i < n; ++i) {
        poll(&q);
        if (debug) delta_print_list(&q);
        if (debug) wait();
    }

    // Test: poll from empty delta list
    assert_equal((int) poll(&q), (int) NULL);
    assert_equal(delta_size(&q), 0);

    // Test: delta_peek on empty delta list
    assert(delta_peek(&q) == NULL, "delta_peek() should return NULL");

    // Test: delta_remove
    if (debug) kprintf("Start testing delta_remove...\n");
    for (int i = 0; i < n; i++) {
        int sleep_time = i * i;
        insert(&q, &pcbs[i], sleep_time);
        if (debug) delta_print_list(&q);
        if (debug) wait();
    }

    // Remove element in the middle
    assert_equal(delta_remove(&q, &pcbs[n - 3]), (n - 3) * (n - 3));
    if (debug) delta_print_list(&q);
    if (debug) wait();
    check_sleep_time_in_delta_list(&q);

    // Remove element at tail
    assert_equal(delta_remove(&q, &pcbs[n - 1]), (n - 1) * (n - 1));
    if (debug) delta_print_list(&q);
    if (debug) wait();
    check_sleep_time_in_delta_list(&q);

    // Remove element at head
    assert_equal(delta_remove(&q, &pcbs[0]), 0);
    if (debug) delta_print_list(&q);
    if (debug) wait();
    check_sleep_time_in_delta_list(&q);
    assert_equal(delta_remove(&q, &pcbs[1]), 1);
    if (debug) delta_print_list(&q);
    if (debug) wait();
    check_sleep_time_in_delta_list(&q);

    kprintf("Finished %s\n", __func__);
}

/*------------------------------------------------------------------------
 * Checks the absolute sleep time of each process, used to test
 * delta_remove and poll. Assumes that the sleep time of a process = pid^2.
 *------------------------------------------------------------------------
 */
static void check_sleep_time_in_delta_list(DeltaList *list) {
    if (list == NULL || list->size == 0) {
        if (debug) kprintf("%s: given list is null or empty\n", __func__);
        return;
    }

    pcb_t *curr = list->head;
    int accumulated_sleep_time = 0;
    int expected_sleep_time = 0;
    int pid;

    while (curr != NULL) {
        pid = curr->pid;
        expected_sleep_time = pid * pid;
        accumulated_sleep_time += curr->key;
        assert(accumulated_sleep_time == expected_sleep_time, "Accumulated sleep time does not match expected");
        curr = curr->next;
    }
}
