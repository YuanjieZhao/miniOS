/* queue.c : process queue
 */

#include <xeroskernel.h>
#include <queue.h>

/*-----------------------------------------------------------------------------------
 * Represents a process queue. Queue data structure is implemented as a
 * doubly-linked list to enable O(1) removal of processes from their queue
 * when they are terminated by other processes.
 *
 * References:
 * - Java Queue interface
 * - https://stackoverflow.com/questions/39047190/creating-a-queue-with-structs-in-c
 *
 * List of functions that are called from outside this file:
 * - init_queue
 *   - Initializes the queue
 * - enqueue
 *   - Adds a process to the queue
 * - dequeue
 *   - Returns the first process in the queue
 * - peek_tail
 *   - Returns the last process in the queue
 * - remove
 *   - Removes a process from the queue
 *-----------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------------
 * Initializes the fields of the given queue.
 *-----------------------------------------------------------------------------------
 */
void init_queue(Queue *queue) {
    assert(queue != NULL, "queue passed to init_queue was null");

    queue->size = 0;
    queue->head = NULL;
    queue->tail = NULL;
}

/*-----------------------------------------------------------------------------------
 * Adds a process to the queue.
 *-----------------------------------------------------------------------------------
 */
void enqueue(Queue *queue, pcb_t *proc) {
    assert(queue != NULL, "queue passed to enqueue was null");
    assert(proc != NULL, "proc passed to enqueue was null");
    // Enqueue at tail
    if (queue->tail == NULL) {
        // The queue is empty
        proc->prev = NULL;
        proc->next = NULL;
        queue->head = proc;
        queue->tail = proc;
    } else {
        // Enqueue at tail
        proc->prev = queue->tail;
        proc->prev->next = proc;
        proc->next = NULL;
        queue->tail = proc;
    }
    queue->size++;
}

/*-----------------------------------------------------------------------------------
 * Removes the first process from the queue and returns it.
 * @return A pointer to the process, or NULL if queue is empty
 *-----------------------------------------------------------------------------------
 */
pcb_t *dequeue(Queue *queue) {
    assert(queue != NULL, "queue passed to dequeue was null");

    if (queue->size == 0) {
        return NULL;
    }

    // Dequeue at head
    pcb_t *proc = queue->head;
    queue->head = proc->next;
    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        // Process to dequeue is tail
        queue->tail = proc->prev;
    }

    proc->prev = NULL;
    proc->next = NULL;
    queue->size--;
    return proc;
}

/*-----------------------------------------------------------------------------------
 * Returns, but does not remove, the last process in the queue.
 * @return A pointer to the process, or NULL if queue is empty
 *-----------------------------------------------------------------------------------
 */
pcb_t *peek_tail(Queue *queue) {
    assert(queue != NULL, "queue passed to peek_tail was null");
    if (queue->size == 0) {
        return NULL;
    }
    return queue->tail;
}

/*-----------------------------------------------------------------------------------
 * Checks if the queue is empty.
 * @return 1 if the queue is empty, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int is_empty(Queue *queue) {
    assert(queue != NULL, "queue passed to is_empty was null");
    return queue->size == 0;
}

/*-----------------------------------------------------------------------------------
 * Returns the number of elements in the queue.
 * @return The number of elements in the queue
 *-----------------------------------------------------------------------------------
 */
int size(Queue *queue) {
    assert(queue != NULL, "queue passed to size was null");
    return queue->size;
}

/*-----------------------------------------------------------------------------------
 * Prints the PID of each process in the queue, with the leftmost element
 * being the one that will be returned by the next dequeue operation.
 *-----------------------------------------------------------------------------------
 */
void print_queue(Queue *queue) {
    if (queue == NULL) {
        kprintf("Queue is NULL\n");
        return;
    }

    if (queue->size == 0) {
        kprintf("Queue is empty\n");
        return;
    }

    pcb_t *curr = queue->head;
    kprintf("[");
    while (curr != NULL) {
        // Print the PID of each process in the queue
        kprintf("%d ", curr->pid);
        curr = curr->next;
    }
    kprintf("]\n");
}

/*-----------------------------------------------------------------------------------
 * Removes the given process from the given queue.
 * Assumes that the given process is in the given queue.
 *-----------------------------------------------------------------------------------
 */
void remove(Queue *queue, pcb_t *proc) {
    assert(queue != NULL, "remove: queue was null");
    assert(proc != NULL, "remove: proc was null");
    assert(queue->size != 0, "remove: Attempting to remove process from empty queue");

    if (proc->prev) {
        proc->prev->next = proc->next;
    } else {
        // Process to remove is head
        queue->head = proc->next;
    }
    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        // Process to remove is tail
        queue->tail = proc->prev;
    }

    proc->prev = NULL;
    proc->next = NULL;
    queue->size--;
}
