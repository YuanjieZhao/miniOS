/* sleep.c : sleep device
 */

#include <xeroskernel.h>
#include <xeroslib.h>
#include <deltalist.h>

/*-----------------------------------------------------------------------------------
 * This is a sleep device for processing sleep requests and timer ticks.
 *
 * List of functions that are called from outside this file:
 * - ksleepinit
 *   - Initializes the delta list of sleeping processes
 * - tick
 *   - Notifies the sleep device that a time slice has occurred
 *-----------------------------------------------------------------------------------
 */

static int ms_to_time_slices(unsigned int milliseconds);

DeltaList sleep_queue;

/*------------------------------------------------------------------------
 * Initializes the delta list of sleeping processes.
 *------------------------------------------------------------------------
 */
void ksleepinit(void) {
    kprintf("Starting ksleepinit...\n");
    init_delta_list(&sleep_queue);
    kprintf("Finished ksleepinit\n");
}

/*-----------------------------------------------------------------------------------
 * Places a process onto the delta list of sleeping processes.
 *
 * @param proc         The process to put to sleep
 * @param milliseconds The number of milliseconds to sleep for
 *-----------------------------------------------------------------------------------
 */
void sleep(pcb_t *proc, unsigned int milliseconds) {
    int time_slices_to_sleep = ms_to_time_slices(milliseconds);
    insert(&sleep_queue, proc, time_slices_to_sleep);

    proc->state = BLOCKED;
    proc->blocked_queue = SLEEP;
}

/*-----------------------------------------------------------------------------------
 * Converts the amount of time to sleep into time slices.
 *-----------------------------------------------------------------------------------
 */
static int ms_to_time_slices(unsigned int milliseconds) {
    return milliseconds / TIME_SLICE + (milliseconds % TIME_SLICE ? 1 : 0);
}

/*-----------------------------------------------------------------------------------
 * Notifies the sleep device that a time slice has occurred. Wakes up any processes
 * that need to be woken up by placing them on the ready queue associated with their
 * priority (and marking them as ready), and updates internal counters.
 *-----------------------------------------------------------------------------------
 */
void tick(void) {
    pcb_t *proc = delta_peek(&sleep_queue);
    if (proc != NULL) {
        proc->key--;
        while (proc != NULL && proc->key <= 0) {
            pcb_t *proc_to_wake = poll(&sleep_queue);
            proc_to_wake->result_code = 0;
            ready(proc_to_wake);

            proc = delta_peek(&sleep_queue);
        }
    }
}
