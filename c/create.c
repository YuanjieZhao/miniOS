/* create.c : create a process
 */

#include <xeroskernel.h>
#include <xeroslib.h>
#include <i386.h>

/*-----------------------------------------------------------------------------------
 * This where new processes are created and added to the ready queue.
 *
 * List of functions that are called from outside this file:
 * - create
 *   - Creates a process and adds it to the ready queue
 * - create_idle_proc
 *   - Creates the idle process
 *-----------------------------------------------------------------------------------
 */

extern unsigned long maxaddr;
extern int user_proc_count;

/*-----------------------------------------------------------------------------------
 * Creates a new process and adds it to the ready queue.
 *
 * @param func  A function pointer to the start of the process code
 * @param stack The amount of stack to allocate for the process
 * @return      1 on success, 0 on failure (https://piazza.com/class/jjrv7w7o1jv1tt?cid=106)
 *-----------------------------------------------------------------------------------
 */
int create(funcptr func, int stack) {
    if (!valid_ptr(func)) {
        return 0;
    }

    if (stack < PROCESS_STACK_SIZE) {
        stack = PROCESS_STACK_SIZE;
    }

    if (DEBUG) kprintf("Creating a process...\n");

    // Allocate the stack
    void *proc_mem_start = kmalloc(stack);
    if (proc_mem_start == NULL) {
        kprintf("ERROR: Not enough memory to allocate stack\n");
        return 0;
    }

    // Acquire a free process control block from the process table
    pcb_t *proc = get_unused_pcb();
    if (proc == NULL) {
        if (DEBUG) kprintf("ERROR: No free PCBs available\n");
        kfree(proc_mem_start);
        return 0;
    }

    // Initialize the PCB
    // By default a process is created with priority 3
    proc->mem_start = proc_mem_start;
    proc->priority = INIT_PRIORITY;

    // Position process context
    // Point to the end of the allocated memory chunk
    unsigned long mem_end = (unsigned long) proc_mem_start + (unsigned long) stack;
    // Set-up the stack of the process so that if the process does a return, either explicitly
    // through the return statement or by running off the end of its code, it transfers control to sysstop
    funcptr *return_addr;
    return_addr = (funcptr *)(mem_end - sizeof(*return_addr));
    // The spot that contains the return address for the function call contains the address of sysstop
    *return_addr = &sysstop;

    // Subtract the size of the context frame: stack pointer starts here
    void *esp = (void *) ((unsigned long) return_addr - sizeof(context_frame_t));
    // Store this value as the value for the stack pointer in PCB
    proc->esp = esp;

    // Initialize process context
    context_frame_t *context_frame = esp;
    memset(context_frame, 0, sizeof(context_frame));
    context_frame->ebp = ((unsigned long) context_frame) + sizeof(context_frame);
    context_frame->iret_eip = (unsigned long) func;
    context_frame->iret_cs = getCS();
    context_frame->eflags = EFLAGS;

    // Place the process on the ready queue
    ready(proc);
    user_proc_count++;
    return 1;
}

/*-----------------------------------------------------------------------------------
 * Creates the idle process.
 *
 * @param idle_proc A pointer to the PCB of the idle process
 *-----------------------------------------------------------------------------------
 */
void create_idle_proc(pcb_t *idle_proc) {
    kprintf("Creating idle process...\n");

    // Allocate a very small stack
    void *proc_mem_start = kmalloc(PROCESS_STACK_SIZE);
    assert(proc_mem_start != NULL, "Not enough memory to allocate stack for idle process");

    // Position process context
    // Point to the end of the allocated memory chunk
    unsigned long mem_end = (unsigned long) proc_mem_start + (unsigned long) IDLE_PROCESS_STACK_SIZE;
    // Subtract the size of the context frame: stack pointer starts here
    void *esp = (void *) ((unsigned long) mem_end - sizeof(context_frame_t));
    // Store this value as the value for the stack pointer in PCB
    idle_proc->esp = esp;
    // PID of idle process is 0
    idle_proc->pid = 0;

    // Initialize process context
    context_frame_t *context_frame = esp;
    memset(context_frame, 0, sizeof(context_frame));
    context_frame->iret_eip = (unsigned long) &idleproc;
    context_frame->iret_cs = getCS();
    context_frame->eflags = EFLAGS;
}
