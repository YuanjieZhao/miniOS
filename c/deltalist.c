#include <xeroskernel.h>
#include <deltalist.h>

/*-----------------------------------------------------------------------------------
 * This is a delta list to store the list of sleeping processes, ordered by
 * the time at which a process should be woken. The delta list is implemented
 * by a singly linked list.
 *
 * References:
 * - https://everything2.com/title/delta+list
 * - https://www.cs.purdue.edu/homes/dec/xinu/page-239.pdf
 *
 * List of functions that are called from outside this file:
 * - init_delta_list
 *   - Initializes the delta list
 * - insert
 *   - Adds a process to the delta list
 * - poll
 *   - Returns and removes the first process in the delta list
 * - delta_peek
 *   - Returns the first process in the delta list
 * - delta_remove
 *   - Removes a process from the delta list
 *-----------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------------
 * Initializes the fields of the delta list.
 *-----------------------------------------------------------------------------------
 */
void init_delta_list(DeltaList *list) {
    assert(list != NULL, "list passed to init_delta_list was null");
    list->size = 0;
    list->head = NULL;
}

/*-----------------------------------------------------------------------------------
 * Adds a process with a specified delay (in time slices) to the delta list. A
 * process's place in the list corresponds to when it needs to be woken.
 * Two processes with the same delay are ordered by the time they are inserted into the
 * list (the one inserted earlier is closer to the head and will be woken first).
 *
 * @param proc  The process to put to sleep
 * @param delay The delay in time slices to sleep for
 *-----------------------------------------------------------------------------------
 */
void insert(DeltaList *list, pcb_t *proc, int delay) {
    assert(list != NULL, "list passed to insert was null");
    assert(proc != NULL, "proc passed to insert was null");
    assert(delay >= 0, "delay passed to insert was negative");
    assert(proc->next == NULL, "proc passed to insert is on another process queue");

    if (list->head == NULL) {
        proc->key = delay;
        list->head = proc;
        list->size++;
        return;
    }

    pcb_t *prev = NULL;
    pcb_t *curr = list->head;
    while (curr != NULL) {
        if (delay < curr->key) {
            // Insert before curr
            // Delay is the sleep time relative to the previous process in the list
            proc->key = delay;
            if (prev != NULL) {
                prev->next = proc;
            } else {
                list->head = proc;
            }
            proc->next = curr;
            curr->key = curr->key - delay;
            list->size++;
            return;
        }

        delay = delay - curr->key;
        prev = curr;
        curr = curr->next;
    }

    // curr is NULL, insert proc at tail
    proc->key = delay;
    prev->next = proc;
    list->size++;
}

/*-----------------------------------------------------------------------------------
 * Removes the first process from the list and returns it.
 *
 * @return A pointer to the process, or NULL if the list is empty
 *-----------------------------------------------------------------------------------
 */
pcb_t *poll(DeltaList *list) {
    assert(list != NULL, "list passed to poll was null");

    if (list->size == 0) {
        return NULL;
    }

    pcb_t *proc = list->head;
    list->head = proc->next;
    proc->next->key += proc->key;
    list->size--;
    proc->prev = NULL;
    proc->next = NULL;
    return proc;
}

/*-----------------------------------------------------------------------------------
 * Returns, but does not remove, the first process in the list.
 *
 * @return A pointer to the process, or NULL if the list is empty
 *-----------------------------------------------------------------------------------
 */
pcb_t *delta_peek(DeltaList *list) {
    assert(list != NULL, "list passed to delta_peek was null");
    if (list->size == 0) {
        return NULL;
    }
    return list->head;
}

/*-----------------------------------------------------------------------------------
 * Checks if the list is empty.
 *
 * @return 1 if the list is empty, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int delta_is_empty(DeltaList *list) {
    assert(list != NULL, "list passed to is_empty was null");
    return list->size == 0;
}

/*-----------------------------------------------------------------------------------
 * Returns the number of elements in the list.
 *
 * @return The number of elements in the list
 *-----------------------------------------------------------------------------------
 */
int delta_size(DeltaList *list) {
    assert(list != NULL, "list passed to size was null");
    return list->size;
}

/*-----------------------------------------------------------------------------------
 * Prints the relative sleep time and PID of each process in the list from left to right.
 *-----------------------------------------------------------------------------------
 */
void delta_print_list(DeltaList *list) {
    if (list == NULL) {
        kprintf("delta list is NULL\n");
        return;
    }

    if (list->size == 0) {
        kprintf("delta list is empty\n");
        return;
    }

    pcb_t *curr = list->head;
    kprintf("[");
    while (curr != NULL) {
        // Print (PID, relative sleep time) of each process in the list
        kprintf("(%d, %d) ", curr->pid, curr->key);
        curr = curr->next;
    }
    kprintf("]\n");
}

/*------------------------------------------------------------------------
 * Removes the given process from the delta list.
 *
 * @return The absolute sleep time remaining for the process that was removed
 *------------------------------------------------------------------------
 */
int delta_remove(DeltaList *list, pcb_t *proc) {
    assert(list != NULL, "delta_remove: list was null");
    assert(proc != NULL, "delta_remove: proc was null");
    assert(list->size != 0, "delta_remove: Attempting to remove process from empty list");

    // Case 1: Process to be removed is head
    // Case 2: Process to be removed is in the middle
    // Case 3: Process to be removed is tail
    // Case 4: Process is not in the list: assert fail

    if (list->head == proc) {
        poll(list);
        return proc->key;
    }

    pcb_t *prev = list->head;
    pcb_t *curr = list->head->next;
    // Accumulator of relative sleep time, used to compute the absolute sleep time of the process to be removed
    int acc = prev->key;

    while (curr != NULL) {
        if (curr == proc) {
            prev->next = curr->next;
            list->size--;

            if (curr->next != NULL) {
                curr->next->key += curr->key;
            }

            proc->next = NULL;
            acc += proc->key;
            return acc;
        }

        prev = curr;
        curr = curr->next;
        acc += prev->key;
    }

    assert(0, "delta_remove: Attempting to remove a process that is not in the list");
    // Get rid of compiler warning
    return 0;
}
