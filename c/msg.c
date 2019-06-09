/* msg.c : messaging system
 */

#include <xeroskernel.h>
#include <xeroslib.h>
#include <queue.h>

/*-----------------------------------------------------------------------------------
 * This is the messaging system used by the kernel for servicing IPC requests.
 *
 * List of functions that are called from outside this file:
 * - send
 *   - Implements the kernel side of syssend
 * - recv
 *   - Implements the kernel side of sysrecv
 *-----------------------------------------------------------------------------------
 */

// The list of processes waiting on a receive-any
Queue receive_any_queue;

/*-----------------------------------------------------------------------------------
 * Implements the kernel side of syssend. Called by the dispatcher upon
 * receipt of a syssend request to perform the actual work of the system
 * call.
 *
 * @param send_proc A pointer to the PCB of the sending process
 * @param recv_proc A pointer to the PCB of the receiving process
 *                  - Validated by the dispatcher to not be the sending process
 *                    or invalid
 * @param send_buf  The buffer containing the data to send
 * @return          0 on success, −1 if the sending process was blocked, or
 *                  −100 if any other problem is detected
 *-----------------------------------------------------------------------------------
 */
int send(pcb_t *send_proc, pcb_t *recv_proc, unsigned long *send_buf) {
    // If the receiving process is on the queue of receivers of the sending process, or
    // if the receiving process is willing to receive from any process
    if (remove_from_blocked_queue(recv_proc, send_proc, RECEIVER) || remove_from_receive_any_queue(recv_proc)) {
        // The receiving process is blocked on a receive from the sending process
        unsigned int *from_pid = (unsigned int *) recv_proc->ipc_args[0];
        unsigned int *recv_buff = (unsigned int *) recv_proc->ipc_args[1];
        *from_pid = send_proc->pid;
        // Copy the message into the receive buffer
        strncpy((char *) recv_buff, (char *) send_buf, BUFFER_SIZE);

        // Unblock receiving process
        recv_proc->result_code = 0;
        ready(recv_proc);
        return 0;
    }
    // syssend was called before the matching sysrecv:
    // The receiving process is not blocked on a matching receive
    // The sending process is blocked until the matching receive occurs
    // Add the sending process to the queue of senders of the receiving process
    enqueue_blocked_queue(send_proc, recv_proc, SENDER);
    return -1;
}

/*-----------------------------------------------------------------------------------
 * Implements the kernel side of sysrecv. Called by the dispatcher upon
 * receipt of a sysrecv request to perform the actual work of the system
 * call.
 *
 * @param recv_proc A pointer to the PCB of the receiving process
 * @param send_proc A pointer to the PCB of the sending process, NULL if the
 *                  receiving process is willing to receive from any process
 *                  - If not NULL, validated by the dispatcher to not be the
 *                    receiving process or invalid
 * @param from_pid  The address containing the PID of the sending process
 *                  - Validated by the dispatcher to not be invalid
 * @param recv_buf  The buffer to receive the data, validated by the dispatcher
 *                  - Validated by the dispatcher to not be invalid
 * @return          0 on success, −1 if the receiving process was blocked, or
 *                  −100 if any other problem is detected
 *-----------------------------------------------------------------------------------
 */
int recv(pcb_t *recv_proc, pcb_t *send_proc, unsigned int *from_pid, unsigned int *recv_buf) {
    if (send_proc != NULL) {
        // If the sending process is on the queue of senders of the receiving process
        if (remove_from_blocked_queue(send_proc, recv_proc, SENDER)) {
            // The sending process is blocked on a send to the receiving process
            unsigned long *send_buf = &send_proc->ipc_args[1];
            // Copy the message into the receive buffer
            strncpy((char *) recv_buf, (char *) send_buf, BUFFER_SIZE);

            // Unblock sending process
            send_proc->result_code = 0;
            ready(send_proc);
            return 0;
        } else {
            // sysrecv was called before the matching syssend:
            // The sending process is not blocked on a matching send
            // The receiving process is blocked until the matching send occurs
            // Add the receiving process to the queue of receivers of the sending process
            enqueue_blocked_queue(recv_proc, send_proc, RECEIVER);
            return -1;
        }
    } else {
        // The receiving process is willing to receive from any process
        // The earliest unreceived send to the receiving process is the matching send to the receive
        pcb_t *send_proc = dequeue(&recv_proc->blocked_queues[SENDER]);
        if (send_proc != NULL) {
            // A process is waiting to send to the receiving process
            unsigned long *send_buf = &send_proc->ipc_args[1];
            // Copy the message into the receive buffer
            strncpy((char *) recv_buf, (char *) send_buf, BUFFER_SIZE);

            // Update from_pid to reflect the PID of the sending process
            *from_pid = send_proc->pid;

            // Unblock sending process
            send_proc->result_code = 0;
            ready(send_proc);
            return 0;
        } else {
            // No process is ready to send to the receiving process
            // The receiving process is blocked until a process sends to it
            recv_proc->state = BLOCKED;
            recv_proc->blocked_queue = RECEIVE_ANY;
            enqueue(&receive_any_queue, recv_proc);
            return -1;
        }
    }
}
