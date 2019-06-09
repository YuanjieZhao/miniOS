/* disp.c : dispatcher
 */

#include <xeroskernel.h>
#include <i386.h>
#include <queue.h>
#include <deltalist.h>
#include <xeroslib.h>
#include <kbd.h>

/*-----------------------------------------------------------------------------------
 * This is the dispatcher, responsible for processing system calls and
 * dispatching the next ready process. It services all events occurring in
 * kernel.
 *
 * Notes on PCB selection algorithm and how PIDs are assigned:
 * - Initially, the PID is the 1-index of the PCB table
 * - When a PCB is marked for reuse, the old PID is used to calculate the
 *   new PID:
 *   new_pid = prev_pid + PCB_TABLE_SIZE
 *   If this overflows, we "wrap around" and calculate the new PID:
 *   new_pid = prev_pid % PCB_TABLE_SIZE
 *   - This makes it possible to index into the PCB in small, constant time:
 *     pcb_t *proc = &pcb_table[(pid - 1) % PCB_TABLE_SIZE]
 *   - This makes it easy to determine whether or not a PID is valid, by
 *     retrieving the PCB and validating that it is not stopped
 *   - To minimize the problems with process interactions based on PIDs,
 *     the PID reuse interval is large
 *
 * List of functions that are called from outside this file:
 * - kdispinit
 *   - Initializes the process queues and PCB table
 * - dispatch
 *   - Enters the dispatcher
 * - ready
 *   - Adds a process to the ready queue for its priority
 * - get_unused_pcb
 *   - Returns a pointer to a free process control block from the process table,
 *     or NULL if all PCBs are in use
 * - enqueue_blocked_queue
 *   - Adds a process to the queue of senders/receivers of the process it
 *     is blocked on
 * - remove_from_blocked_queue
 *   - Returns 1 if the process was removed from the queue of senders/receivers of
 *     the process it is blocked on, 0 otherwise
 * - remove_from_receive_any_queue
 *   - Returns 1 if the process was removed from the queue of processes waiting on
 *     a receive-any, 0 otherwise
 * - only_process
 *   - Returns 1 if the current running process is the only user process, 0
 *     otherwise
 * - idleproc
 *   - The idle process
 *-----------------------------------------------------------------------------------
 */

extern char *maxaddr;

static pcb_t pcb_table[PCB_TABLE_SIZE];
// Multiple ready queues, one for each priority
static Queue ready_queues[NUM_PRIORITIES];
static Queue stopped_queue;
static pcb_t *current_proc;
static pcb_t idle_proc;
int user_proc_count;

// System call arguments set by the context switcher
extern unsigned long *args;

// The list of sleeping processes
extern DeltaList sleep_queue;
// The list of processes waiting on a receive-any
extern Queue receive_any_queue;

static void yield(void);
static void service_syscreate(void);
static void service_sysputs(void);
static void service_syskill(void);
static void service_syssetprio(void);
static void service_syssend(void);
static void service_sysrecv(void);
static void service_syssleep(void);
static void service_sysgetcputimes(void);
static void service_syssighandler(void);
static void service_syssigreturn(void);
static void service_syswait(void);
static void service_sysopen(void);
static void service_sysclose(void);
static void service_syswrite(void);
static void service_sysread(void);
static void service_sysioctl(void);
static int get_cpu_times(processStatuses *ps);
static void cleanup_current_process_and_next(void);
static pcb_t *get_pcb(PID_t pid);
static pcb_t *next(void);
static void stop(pcb_t *proc);
static void cleanup(pcb_t *proc);
static void unblock(pcb_t *proc, int result_code);
static int only_process(void);

/*-----------------------------------------------------------------------------------
 * To be called before entering the dispatcher. Initializes the process
 * queues and PCB table, and creates the idle process.
 *-----------------------------------------------------------------------------------
 */
void kdispinit(void) {
    kprintf("Starting kdispinit...\n");

    // Initially, all process queues are empty
    for (int i = 0; i < NUM_PRIORITIES; i++) {
        Queue *ready_queue = &ready_queues[i];
        init_queue(ready_queue);
    }
    init_queue(&stopped_queue);

    // Initialize the PCB table
    for (int i = 0; i < PCB_TABLE_SIZE; i++) {
        pcb_t *proc = &pcb_table[i];
        // Set process IDs starting from 1 (0 is reserved for idle process)
        proc->pid = i + 1;
        Queue *queue_of_senders = &proc->blocked_queues[0];
        init_queue(queue_of_senders);
        Queue *queue_of_receivers = &proc->blocked_queues[1];
        init_queue(queue_of_receivers);

        // Add process to stopped queue
        stop(proc);
    }

    init_queue(&receive_any_queue);

    create_idle_proc(&idle_proc);
    user_proc_count = 0;
    kprintf("Finished kdispinit\n");
}

/*-----------------------------------------------------------------------------------
 * An infinite loop that processes system calls, schedules the next process,
 * and then calls the context switcher to switch into the next scheduled
 * process. Does not return as it passes control to the context switcher.
 *-----------------------------------------------------------------------------------
 */
void dispatch(void) {
    kprintf("Running dispatcher...\n");
    // Schedule the next process
    current_proc = next();
    for (;;) {
        // Handle pending signals
        handle_pending_signals(current_proc);
        // Call the context switcher to switch into the current process
        request_t request = contextswitch(current_proc);

        // Determine the nature of the service request and process request
        switch (request) {
            case (SYSCREATE):
                service_syscreate();
                break;
            case (SYSYIELD):
                yield();
                break;
            case (SYSSTOP):
                cleanup_current_process_and_next();
                break;
            case (SYSGETPID):
                current_proc->result_code = current_proc->pid;
                break;
            case (SYSPUTS):
                service_sysputs();
                break;
            case (SYSKILL):
                service_syskill();
                break;
            case (SYSSETPRIO):
                service_syssetprio();
                break;
            case (SYSSEND):
                service_syssend();
                break;
            case (SYSRECV):
                service_sysrecv();
                break;
            case (SYSSLEEP):
                service_syssleep();
                break;
            case (SYSGETCPUTIMES):
                service_sysgetcputimes();
                break;
            case (SYSSIGHANDLER):
                service_syssighandler();
                break;
            case (SYSSIGRETURN):
                service_syssigreturn();
                break;
            case (SYSWAIT):
                service_syswait();
                break;
            case (SYSOPEN):
                service_sysopen();
                break;
            case (SYSCLOSE):
                service_sysclose();
                break;
            case (SYSWRITE):
                service_syswrite();
                break;
            case (SYSREAD):
                service_sysread();
                break;
            case (SYSIOCTL):
                service_sysioctl();
                break;
            case (TIMER_INT):
                current_proc->cpuTime++;
                tick();
                yield();
                end_of_intr();
                break;
            case (KEYBOARD_INT):
                kbd_isr();
                end_of_intr();
                break;
            default:
                assert(0, "Invalid request");
        }
    }
}

/*-----------------------------------------------------------------------------------
 * Places the current process at the end of the ready queue for its
 * priority and selects the next available process.
 *-----------------------------------------------------------------------------------
 */
void yield(void) {
    ready(current_proc);
    current_proc = next();
}

/*-----------------------------------------------------------------------------------
 * Services a syscreate request. Checks if start of process provided is
 * not NULL and is in the range of possible allocatable memory. If
 * arguments provided are not correct, an error result is saved as the
 * result code in the PCB. Otherwise, the create request is executed and
 * the result code is saved in the PCB.
 *-----------------------------------------------------------------------------------
 */
static void service_syscreate(void) {
    void (*func)(void) = (void *) args[0];
    int stack = args[1];

    int create_result_code = create(func, stack);
    if (create_result_code == 1) {
        // Get PID of newly created process by peeking at the end of the ready queue for its priority
        pcb_t *created_proc = peek_tail(&ready_queues[INIT_PRIORITY]);
        current_proc->result_code = created_proc->pid;
    } else {
        current_proc->result_code = -1;
    }
}

/*-----------------------------------------------------------------------------------
 * Services a sysputs request. Since the kernel cannot be pre-empted, the
 * kernel side of sysputs can use kprintf to print the string
 *-----------------------------------------------------------------------------------
 */
static void service_sysputs(void) {
    char *str = (char *) args[0];
    if (valid_ptr(str)) {
        kprintf(str);
    }
}

/*-----------------------------------------------------------------------------------
 * Services a syskill request.
 *-----------------------------------------------------------------------------------
 */
static void service_syskill(void) {
    int pid = args[0];
    int signal_number = args[1];

    pcb_t *proc_to_signal = get_pcb(pid);
    current_proc->result_code = signal(proc_to_signal, signal_number);
}

/*-----------------------------------------------------------------------------------
 * Services a syssetprio request.
 *-----------------------------------------------------------------------------------
 */
static void service_syssetprio(void) {
    int current_priority = current_proc->priority;
    int req_priority = args[0];
    int valid_req_priority = req_priority >= 0 && req_priority <= 3;
    if (valid_req_priority) {
        current_proc->priority = req_priority;
    }
    if (valid_req_priority || req_priority == -1) {
        current_proc->result_code = current_priority;
    } else {
        current_proc->result_code = -1;
    }
}

/*-----------------------------------------------------------------------------------
 * Services a syssend request.
 *-----------------------------------------------------------------------------------
 */
static void service_syssend(void) {
    current_proc->ipc_args = args;
    unsigned int dest_pid = (unsigned int) args[0];
    unsigned long num = (unsigned long) args[1];

    int send_result_code = NULL;
    if (current_proc->pid == dest_pid) {
        // The sending process is trying to send to itself
        send_result_code = -3;
    } else {
        pcb_t *receiving_proc = get_pcb(dest_pid);
        if (receiving_proc == NULL) {
            // The receiving process does not exist
            send_result_code = -2;
        } else {
            send_result_code = send(current_proc, receiving_proc, &num);
        }
    }

    current_proc->result_code = send_result_code;

    if (send_result_code == -1) {
        // The sending process was blocked
        // Select the next available process to run
        current_proc = next();
    }
}

/*-----------------------------------------------------------------------------------
 * Services a sysrecv request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysrecv(void) {
    current_proc->ipc_args = args;
    unsigned int *from_pid = (unsigned int *) args[0];
    unsigned int *num = (unsigned int *) args[1];

    int recv_result_code = NULL;
    if (!valid_buf(from_pid, BUFFER_SIZE)) {
        recv_result_code = -5;
    } else if (!valid_buf(num, BUFFER_SIZE)) {
        // The address of num is invalid
        recv_result_code = -4;
    } else {
        unsigned int sender_pid = *from_pid;
        if (sender_pid == 0) {
            // The receiving process is willing to receive from any process
            if (only_process()) {
                recv_result_code = -10;
            } else {
                recv_result_code = recv(current_proc, NULL, from_pid, num);
            }
        } else {
            // pid specifies the PID of the sending process
            if (current_proc->pid == sender_pid) {
                // The receiving process is trying to receive from itself
                recv_result_code = -3;
            } else {
                pcb_t *sending_proc = get_pcb(sender_pid);
                if (sending_proc == NULL) {
                    // The sending process does not exist
                    recv_result_code = -2;
                } else {
                    recv_result_code = recv(current_proc, sending_proc, from_pid, num);
                }
            }
        }
    }
    current_proc->result_code = recv_result_code;
    if (recv_result_code == -1) {
        // The receiving process was blocked
        // Select the next available process to run
        current_proc = next();
    }
}

/*-----------------------------------------------------------------------------------
 * Services a syssleep request.
 *-----------------------------------------------------------------------------------
 */
static void service_syssleep(void) {
    unsigned int milliseconds = args[0];
    if (milliseconds > 0) {
        sleep(current_proc, milliseconds);
        // Select the next available process to run
        current_proc = next();
    }
}

/*-----------------------------------------------------------------------------------
 * Services a sysgetcputimes request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysgetcputimes(void) {
    processStatuses *ps = (processStatuses *) args[0];
    int result_code = get_cpu_times(ps);
    current_proc->result_code = result_code;
}

/*-----------------------------------------------------------------------------------
 * Services a syssighandler request.
 *-----------------------------------------------------------------------------------
 */
static void service_syssighandler(void) {
    int signal = args[0];
    signal_handler_funcptr new_handler = (signal_handler_funcptr) args[1];
    signal_handler_funcptr *old_handler = (signal_handler_funcptr *) args[2];

    int signal_31 = SIGNAL_TABLE_SIZE - 1;
    if (signal < 0 || signal >= signal_31) {
        current_proc->result_code = -1;
    } else if (new_handler != NULL && !valid_ptr(new_handler)) {
        current_proc->result_code = -2;
    } else if (!valid_ptr(old_handler)) {
        current_proc->result_code = -3;
    } else {
        // Copy the address of the old handler to the location pointed to by old_handler
        *old_handler = current_proc->signal_table[signal];

        current_proc->signal_table[signal] = new_handler;
        current_proc->result_code = 0;
    }
}

/*-----------------------------------------------------------------------------------
 * Services a syssigreturn request.
 *-----------------------------------------------------------------------------------
 */
static void service_syssigreturn(void) {
    void *old_sp = (void *) args[0];

    // Replace the stack pointer in the process's PCB with the old stack pointer
    current_proc->esp = old_sp;

    // Retrieve any saved return value
    current_proc->result_code = *(int *) ((unsigned long) old_sp - sizeof(int));

    // Indicate what signals can again be delivered
    current_proc->last_signal_delivered = *(int *) ((unsigned long) old_sp - 2 * sizeof(int));
}

/*-----------------------------------------------------------------------------------
 * Services a syswait request.
 *-----------------------------------------------------------------------------------
 */
static void service_syswait(void) {
    int pid = args[0];

    pcb_t *proc_to_wait_on = get_pcb(pid);
    if (proc_to_wait_on != NULL && pid != current_proc->pid) {
        current_proc->state = BLOCKED;
        enqueue_blocked_queue(current_proc, proc_to_wait_on, WAIT);
        current_proc = next();
    } else {
        current_proc->result_code = -1;
    }
}

/*-----------------------------------------------------------------------------------
 * Services a sysopen request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysopen(void) {
    int device_no = args[0];
    current_proc->result_code = di_open(current_proc, device_no);
}

/*-----------------------------------------------------------------------------------
 * Services a sysclose request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysclose(void) {
    int fd = args[0];
    current_proc->result_code = di_close(current_proc, fd);
}

/*-----------------------------------------------------------------------------------
 * Services a syswrite request.
 *-----------------------------------------------------------------------------------
 */
static void service_syswrite(void) {
    int fd = args[0];
    void *buf = (void *) args[1];
    int buflen = args[2];

    current_proc->result_code = di_write(current_proc, fd, buf, buflen);
}

/*-----------------------------------------------------------------------------------
 * Services a sysread request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysread(void) {
    int fd = args[0];
    void *buf = (void *) args[1];
    int buflen = args[2];

    int di_read_return = di_read(current_proc, fd, buf, buflen);

    if (di_read_return == -2) {
        current_proc->state = BLOCKED;
        current_proc->blocked_queue = READ;
        current_proc = next();
    } else {
        current_proc->result_code = di_read_return;
    }
}

/*-----------------------------------------------------------------------------------
 * Services a sysioctl request.
 *-----------------------------------------------------------------------------------
 */
static void service_sysioctl(void) {
    int fd = args[0];
    unsigned long command = (unsigned long) args[1];
    void *ioctl_args = (void *) args[2];

    current_proc->result_code = di_ioctl(current_proc, fd, command, ioctl_args);
}

/*-----------------------------------------------------------------------------------
 * This function is the system side of the sysgetcputimes call. It places
 * into a structure being pointed to information about each currently
 * active process.
 *
 * @param ps A pointer to a processStatuses structure that is filled with
 *           information about all the processes currently in the system
 *-----------------------------------------------------------------------------------
 */
static int get_cpu_times(processStatuses *ps) {
    int i, currentSlot;
    currentSlot = -1;

    // Check if address is in the hole
    if (((unsigned long) ps) >= HOLESTART && ((unsigned long) ps <= HOLEEND))
        return -1;

    // Check if address of the data structure is beyond the end of main memory
    if ((((char *) ps) + sizeof(processStatuses)) > maxaddr)
        return -2;

    // There are probably other address checks that can be done, but this is OK for now

    for (i = 0; i < PCB_TABLE_SIZE; i++) {
        if (pcb_table[i].state != STOPPED) {
            // Fill in the table entry
            currentSlot++;
            ps->pid[currentSlot] = pcb_table[i].pid;
            ps->state[currentSlot] = current_proc->pid == pcb_table[i].pid ? RUNNING : pcb_table[i].state;
            ps->blocked_queue[currentSlot] = pcb_table[i].blocked_queue;
            ps->cpuTime[currentSlot] = pcb_table[i].cpuTime * TIME_SLICE;
        }
    }
    // Fill in the table entry for idle process
    currentSlot++;
    ps->pid[currentSlot] = 0;
    ps->state[currentSlot] = READY;
    ps->cpuTime[currentSlot] = idle_proc.cpuTime * TIME_SLICE;

    return currentSlot;
}

/*-----------------------------------------------------------------------------------
 * Cleans up the current process and selects the next available process to
 * run.
 *-----------------------------------------------------------------------------------
 */
static void cleanup_current_process_and_next(void) {
    cleanup(current_proc);
    current_proc = next();
}

/*-----------------------------------------------------------------------------------
 * Takes a pointer to a process control block and adds it to its ready
 * queue.
 *
 * @param proc A pointer to the PCB to add to the ready queue for its
 *             priority
 *-----------------------------------------------------------------------------------
 */
void ready(pcb_t *proc) {
    // Idle process should not ever be on a ready queue
    if (proc->pid != IDLE_PROC_PID) {
        proc->blocked_on = NULL;
        proc->blocked_queue = NONE;
        proc->state = READY;
        int priority = proc->priority;
        Queue *ready_queue = &ready_queues[priority];
        enqueue(ready_queue, proc);
    }
}

/*-----------------------------------------------------------------------------------
 * Removes the next process from the stopped queue, assigns it a PID, and
 * returns a pointer to its process control block.
 *
 * @return A pointer to an unused PCB, or NULL if unavailable
 *-----------------------------------------------------------------------------------
 */
pcb_t *get_unused_pcb(void) {
    if (is_empty(&stopped_queue)) {
        return NULL;
    }

    pcb_t *unused_pcb = dequeue(&stopped_queue);

    // See the notes at the start of the file on the PCB selection algorithm and how PIDs are assigned
    int prev_pid = unused_pcb->pid;
    int new_pid = prev_pid + PCB_TABLE_SIZE;
    // Reference: https://stackoverflow.com/questions/2633661/how-to-check-for-signed-integer-overflow-in-c-without-undefined-behaviour
    if (new_pid < 1) {
        new_pid = prev_pid % PCB_TABLE_SIZE;
    }
    assert(new_pid >= 1, "Calculated new PID is not >= 1");
    unused_pcb->pid = new_pid;

    unused_pcb->cpuTime = 0;

    // Clear signal table
    int signal_31 = SIGNAL_TABLE_SIZE - 1;
    for (int i = 0; i < signal_31; i++) {
        unused_pcb->signal_table[i] = NULL;
    }
    // Signal 31 is a special signal that has as its handler sysstop
    unused_pcb->signal_table[signal_31] = (signal_handler_funcptr) & sysstop;

    unused_pcb->pending_signals = 0;
    unused_pcb->last_signal_delivered = -1;

    // Clear FD table
    for (int i = 0; i < FD_TABLE_SIZE; i++) {
        unused_pcb->fd_table[i] = NULL;
    }

    return unused_pcb;
}

/*-----------------------------------------------------------------------------------
 * Retrieves a PCB given a PID by indexing into the PCB table.
 *
 * @param pid The PID of the PCB to retrieve
 * @return    The PCB with the given pid, or NULL if the PID is not valid
 *-----------------------------------------------------------------------------------
 */
static pcb_t *get_pcb(PID_t pid) {
    if (pid >= 1) {
        pcb_t *proc = &pcb_table[(pid - 1) % PCB_TABLE_SIZE];
        if (proc->pid == pid && proc->state != STOPPED) {
            return proc;
        }
    }
    // Given PID was not valid
    return NULL;
}

/*-----------------------------------------------------------------------------------
 * Removes the next process from the ready queues and returns a pointer to
 * its process control block. The scheduling policy is that higher
 * priority processes (with a lower priority number) are always run first
 * and round-robin scheduling is used within a priority.
 *
 * @return A pointer to the PCB of the next process from the ready queues
 *-----------------------------------------------------------------------------------
 */
static pcb_t *next(void) {
    pcb_t *proc = NULL;
    // To select next process, scan queues, highest to lowest priority
    int i = 0;
    while (i < NUM_PRIORITIES && proc == NULL) {
        Queue *ready_queue = &ready_queues[i];
        if (!is_empty(ready_queue)) {
            proc = dequeue(ready_queue);
        }
        i++;
    }

    if (proc == NULL) {
        // The idle process will run only if no other process is available
        if (DEBUG) kprintf("Running idle process...\n");
        proc = &idle_proc;
    }

    proc->state = RUNNING;
    return proc;
}

/*-----------------------------------------------------------------------------------
 * Takes a pointer to a process control block and adds it to the stopped
 * queue.
 *
 * @param proc A pointer to the PCB to add to the stopped queue
 *-----------------------------------------------------------------------------------
 */
static void stop(pcb_t *proc) {
    proc->state = STOPPED;
    enqueue(&stopped_queue, proc);
    user_proc_count--;
}

/*-----------------------------------------------------------------------------------
 * Takes a pointer to a process control block and performs process
 * destruction. Frees all resources associated with the process.
 *
 * @param proc A pointer to the PCB of the process to destroy
 *-----------------------------------------------------------------------------------
 */
static void cleanup(pcb_t *proc) {
    // Unblock all senders waiting on the process
    Queue *queue_of_senders = &proc->blocked_queues[SENDER];
    pcb_t *blocked_sender = dequeue(queue_of_senders);
    while (blocked_sender != NULL) {
        // Return −1 if the receiving process terminates before the matching receive is performed
        unblock(blocked_sender, -1);
        blocked_sender = dequeue(queue_of_senders);
    }
    // Unblock all receivers waiting on the process
    Queue *queue_of_receivers = &proc->blocked_queues[RECEIVER];
    pcb_t *blocked_receiver = dequeue(queue_of_receivers);
    while (blocked_receiver != NULL) {
        // Return −1 if the sending process terminates before a matching send is performed
        unblock(blocked_receiver, -1);
        blocked_receiver = dequeue(queue_of_receivers);
    }

    // Unblock all processes waiting on the process to terminate
    Queue *queue_of_waiting_processes = &proc->blocked_queues[WAIT];
    pcb_t *blocked_waiting_process = dequeue(queue_of_waiting_processes);
    while (blocked_waiting_process != NULL) {
        unblock(blocked_waiting_process, 0);
        blocked_waiting_process = dequeue(queue_of_waiting_processes);
    }

    // Mark PCB as unused
    stop(proc);
    if (only_process() && size(&receive_any_queue) == 1) {
        // The receiving process is the only user process
        pcb_t *blocked_receive_any = dequeue(&receive_any_queue);
        unblock(blocked_receive_any, -10);
    }
    // Free allocated stack
    kfree(proc->mem_start);
}

/*-----------------------------------------------------------------------------------
 * Sets the given result code and unblocks the process.
 *
 * @param proc        The process to unblock
 * @param result_code The result code to return
 *-----------------------------------------------------------------------------------
 */
static void unblock(pcb_t *proc, int result_code) {
    proc->result_code = result_code;
    ready(proc);
}

/*-----------------------------------------------------------------------------------
 * Adds a process to the queue of senders/receivers/wait of the process it
 * is blocked on.
 *
 * @param proc            The process to add
 * @param blocked_on_proc The process with the queue of senders/receivers
 * @param blocked_queue   The blocked queue to add the process to, either
 *                        SENDER, RECEIVER, or WAIT
 *-----------------------------------------------------------------------------------
 */
void enqueue_blocked_queue(pcb_t *proc, pcb_t *blocked_on_proc, blocked_queue_t blocked_queue) {
    enqueue(&blocked_on_proc->blocked_queues[blocked_queue], proc);

    proc->blocked_on = blocked_on_proc;
    proc->blocked_queue = blocked_queue;
    proc->state = BLOCKED;
}

/*-----------------------------------------------------------------------------------
 * Removes a process from the queue of senders/receivers of the process
 * it is blocked on.
 *
 * @param proc            The process to remove
 * @param blocked_on_proc The process with the queue of senders/receivers
 * @param blocked_queue   The blocked queue to remove from, either SENDER
 *                        or RECEIVER
 * @return                1 if the process was on the specified blocked
 *                        queue and was removed, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int remove_from_blocked_queue(pcb_t *proc, pcb_t *blocked_on_proc, blocked_queue_t blocked_queue) {
    if (proc->blocked_on == blocked_on_proc && proc->blocked_queue == blocked_queue) {
        remove(&blocked_on_proc->blocked_queues[blocked_queue], proc);
        return 1;
    } else {
        return 0;
    }
}

/*-----------------------------------------------------------------------------------
 * Removes a process from the queue of processes waiting on a receive-any.
 *
 * @param proc The process to remove
 * @return     1 if the process was on the receive-any queue and was
 *             removed, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
int remove_from_receive_any_queue(pcb_t *proc) {
    if (proc->blocked_queue == RECEIVE_ANY) {
        remove(&receive_any_queue, proc);
        return 1;
    } else {
        return 0;
    }
}

/*-----------------------------------------------------------------------------------
 * Checks if the current running process is the only user process.
 *
 * @return 1 if the current running process is the only user process, 0
 *         otherwise
 *-----------------------------------------------------------------------------------
 */
static int only_process(void) {
    return user_proc_count == 1;
}

/*-----------------------------------------------------------------------------------
 * The idle process. Runs only if no other process is available.
 *-----------------------------------------------------------------------------------
 */
void idleproc(void) {
    for (;;);
}
