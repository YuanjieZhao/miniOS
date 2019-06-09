/* signal.c - support for signal handling
 */

#include <xeroskernel.h>
#include <xeroslib.h>
#include <deltalist.h>

/*-----------------------------------------------------------------------------------
 * This is the signalling system. The signalling system supports 32 signals, numbered
 * 0 to 31. Signal 31 (SIGKILL) is a special signal that has as its handler sysstop().
 * In addition, signal 31 cannot be overridden or ignored. Signals are delivered in
 * priority order, with signals of a higher number (having a higher priority) being
 * delivered first.
 *
 * Higher priority signals are also allowed to interrupt the execution of lower priority
 * signal handlers. For example, if signal 23 is being handled by a process, that
 * signal's processing can be interrupted by signals numbered 24 to 31 while signals
 * 0 to 23 would be held in abeyance until the handling of signal 23 is finished.
 *
 * The only way a signal can be posted (signalled) is via the syskill call.
 *
 * The default action for all signals is to ignore the signal.
 *
 * List of functions that are called from outside this file:
 * - signal
 *   - Registers a signal for delivery to a process
 * - handle_pending_signals
 *   - Handles pending signals for a process before context switch, delivering any
 *     necessary signals
 * - set_signal_bit
 *   - Returns the given bitmask with the specified signal bit set
 * - is_signal_bit_set
 *   - Returns 1 if the specified signal bit is set, 0 otherwise
 * - clear_signal_bit
 *   - Returns the given bitmask with the specified signal bit cleared
 *-----------------------------------------------------------------------------------
 */

// The list of sleeping processes
extern DeltaList sleep_queue;
// Chars transferred to application read buffer
extern int chars_transferred;

static void unblock_on_signal(pcb_t *proc_to_signal);

/*-----------------------------------------------------------------------------------
 * When the kernel decides to deliver a signal to a process, the kernel modifies the
 * application's stack so that sigtramp is executed when this process is switched to.
 *
 * The sigtramp code runs in user space as part of the application and is used to
 * control the signal processing in the application.
 *
 * @param handler The provided handler
 * @param cntx    The context at the time of the signal
 *-----------------------------------------------------------------------------------
 */
void sigtramp(signal_handler_funcptr handler, void *cntx) {
    handler(cntx);
    syssigreturn(cntx);
    assert(0, "syssigreturn returned to sigtramp");
}

/*-----------------------------------------------------------------------------------
 * Registers a signal for delivery to a process.
 *
 * @param proc_to_signal The process to register a signal for
 * @param signal_number  The signal to register
 * @return               0 on success
 *                       -514 if the target process does not exist
 *                       -583 if the signal number is invalid
 *-----------------------------------------------------------------------------------
 */
int signal(pcb_t *proc_to_signal, int signal_number) {
    if (proc_to_signal != NULL) {
        if (signal_number >= 0 && signal_number < SIGNAL_TABLE_SIZE) {
            if (proc_to_signal->signal_table[signal_number]) {
                // Mark signal for delivery
                proc_to_signal->pending_signals = set_signal_bit(proc_to_signal->pending_signals, signal_number);

                if (proc_to_signal->state == BLOCKED) {
                    // Process is blocked on a system call when it is targeted to receive a signal
                    // Unblock target process
                    unblock_on_signal(proc_to_signal);
                    ready(proc_to_signal);
                }
                return 0;
            } else {
                // NULL signal handler indicates that signal delivery for the identified signal is disabled
                // Ignore signal
                return 0;
            }
        } else {
            return -583;
        }
    } else {
        return -514;
    }
}

/*------------------------------------------------------------------------
 * Removes the process from whichever blocked queue it is on and sets the
 * result code.
 *------------------------------------------------------------------------
 */
static void unblock_on_signal(pcb_t *proc_to_signal) {
    // Remove process from the blocked queue it is on
    assert_equal(proc_to_signal->state, BLOCKED);

    // The return value for the system call which is being unblocked is -666 unless otherwise specified (sleep)
    int interrupted_by_signal = -666;
    switch (proc_to_signal->blocked_queue) {
        case (SENDER):
            remove_from_blocked_queue(proc_to_signal, proc_to_signal->blocked_on, SENDER);
            proc_to_signal->result_code = interrupted_by_signal;
            break;
        case (RECEIVER):
            remove_from_blocked_queue(proc_to_signal, proc_to_signal->blocked_on, RECEIVER);
            proc_to_signal->result_code = interrupted_by_signal;
            break;
        case (RECEIVE_ANY):
            remove_from_receive_any_queue(proc_to_signal);
            proc_to_signal->result_code = interrupted_by_signal;
            break;
        case (SLEEP):;
            // Return the time left to sleep if the call is interrupted
            int time_left = delta_remove(&sleep_queue, proc_to_signal);
            proc_to_signal->result_code = time_left * TIME_SLICE;
            break;
        case (WAIT):
            remove_from_blocked_queue(proc_to_signal, proc_to_signal->blocked_on, WAIT);
            proc_to_signal->result_code = interrupted_by_signal;
            break;
        case (READ):
            // Return the number of chars that have been placed in the buffer supplied by the application
            // If that value is zero, then return -666
            if (chars_transferred == 0) {
                proc_to_signal->result_code = interrupted_by_signal;
            } else {
                proc_to_signal->result_code = chars_transferred;
            }
            break;
        default:
            assert(0, "signal target is blocked, but is not on a blocked queue");
    }
    proc_to_signal->blocked_on = NULL;
    proc_to_signal->blocked_queue = NONE;
}

/*-----------------------------------------------------------------------------------
 * Handles pending signals. Delivers the highest priority pending
 * signal if it is higher priority than the last signal delivered.
 *-----------------------------------------------------------------------------------
 */
void handle_pending_signals(pcb_t *proc) {
    int signal_number = SIGNAL_TABLE_SIZE - 1;
    while (!is_signal_bit_set(proc->pending_signals, signal_number) && signal_number != -1) {
        signal_number--;
    }
    if (signal_number != -1) {
        // signal_number is now the highest priority pending signal
        if (signal_number > proc->last_signal_delivered) {
            // Deliver signal
            proc->pending_signals = clear_signal_bit(proc->pending_signals, signal_number);

            void *old_esp = proc->esp;
            void *new_esp = (void *) ((unsigned long) old_esp - sizeof(signal_delivery_context_t));

            // Store the new stack pointer as the value for the stack pointer in PCB
            proc->esp = new_esp;

            // Initialize signal delivery context
            signal_delivery_context_t *signal_delivery_context = new_esp;
            memset(signal_delivery_context, 0, sizeof(signal_delivery_context));
            signal_delivery_context->context_frame.ebp =
                    ((unsigned long) signal_delivery_context) + sizeof(context_frame_t);
            signal_delivery_context->context_frame.iret_eip = (unsigned long) &sigtramp;
            signal_delivery_context->context_frame.iret_cs = getCS();
            signal_delivery_context->context_frame.eflags = EFLAGS;

            // Set sigtramp arguments
            signal_delivery_context->handler = proc->signal_table[signal_number];
            // cntx is the start of the context at the time the signal is delivered
            signal_delivery_context->cntx = old_esp;
            signal_delivery_context->last_signal_delivered = proc->last_signal_delivered;
            // Saved return value
            signal_delivery_context->saved_result_code = proc->result_code;

            proc->last_signal_delivered = signal_number;
        }
    }
}

/*-----------------------------------------------------------------------------------
 * Returns the given bitmask with the specified signal bit set.
 *-----------------------------------------------------------------------------------
 */
int set_signal_bit(int bitmask, int signal_number) {
    return bitmask | (1 << signal_number);
}

/*-----------------------------------------------------------------------------------
 * Returns 1 if the specified signal bit is set, 0 otherwise.
 *-----------------------------------------------------------------------------------
 */
int is_signal_bit_set(int bitmask, int signal_number) {
    return 0x01 & (bitmask >> signal_number);
}

/*-----------------------------------------------------------------------------------
 * Returns the given bitmask with the specified signal bit cleared.
 *-----------------------------------------------------------------------------------
 */
int clear_signal_bit(int bitmask, int signal_number) {
    return bitmask &= ~(0x01 << signal_number);
}
