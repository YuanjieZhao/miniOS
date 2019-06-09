/* ctsw.c : context switcher
 */

#include <xeroskernel.h>

/*------------------------------------------------------------------------
 * This is the context switcher, responsible for switching between the
 * kernel and user processes (and vice versa).
 *
 * List of functions that are called from outside this file:
 * - contextinit
 *   - Sets interrupt service routine entry points in the interrupt table
 * - contextswitch
 *   - Takes a pointer to the current process and switches into the context of
 *     that process, letting it run until an event occurs, in which it will
 *     return to the dispatcher with the type of request that was made
 *------------------------------------------------------------------------
 */

void _SysCallEntryPoint(void);
void _TimerEntryPoint(void);
void _KBDEntryPoint(void);

static void *k_stack;
static unsigned long *ESP;

// Save arguments for the dispatcher, so the dispatcher
// does not need to know about the context part of the stack
// Not used when processing an interrupt
unsigned long *args;

static int EAX;
static int interrupt;

/*------------------------------------------------------------------------
 * Sets the interrupt service routine entry points in the interrupt table.
 *------------------------------------------------------------------------
 */
void contextinit(void) {
    kprintf("Starting contextinit...\n");
    // Remove compiler warning for unused ISR functions
    (void) _SysCallEntryPoint;
    (void) _TimerEntryPoint;
    (void) _KBDEntryPoint;

    // Remove compiler warning for unused k_stack
    (void) k_stack;
    // Remove compiler warning for unused args
    (void) args;
    set_evec(SYSCALL_INTERRUPT_NUMBER, (unsigned long) _SysCallEntryPoint);
    set_evec(TIMER_INTERRUPT_NUMBER, (unsigned long) _TimerEntryPoint);
    set_evec(KEYBOARD_INTERRUPT_NUMBER, (unsigned long) _KBDEntryPoint);
    kprintf("Finished contextinit\n");
}

/*------------------------------------------------------------------------
 * Takes a pointer to the current process, switches into the context of
 * that process, and lets it run.
 *
 * Contains entry points whose references are stored in the interrupt
 * table. When an event occurs, the interrupt mechanism starts running the
 * code at the respective entry point. Determines the type of request that
 * was made, and stores any arguments where they can be accessed by the
 * dispatcher. Returns to the dispatcher with the request.
 *
 * Note: Uses the gnu conventions for specifying the instructions.
 *
 * @param proc A pointer to current process
 * @return     The type of request that was made
 *------------------------------------------------------------------------
 */
request_t contextswitch(pcb_t *proc) {
    ESP = proc->esp;
    EAX = proc->result_code;

    /*------------------------------------------------------------------------
     * In-line asm: (switch from kernel to process)
     *      push kernel state onto the kernel stack
     *      save kernel stack pointer
     *      switch to process stack
     *      pop process state from the process stack
     *      save return value in %eax
     *      iret
     * _TimerEntryPoint (switch from process to kernel):
     *      disable interrupts
     *      save indication that this is a timer interrupt
     *      jump to _CommonEntryPoint
     * _SysCallEntryPoint (switch from process to kernel):
     *      disable interrupts
     *      save indication that this is a system call
     * _CommonEntryPoint:
     *      push process state onto process stack
     *      save process stack pointer
     *      switch to kernel stack
     *      save %eax
     *      retrieve system call arguments from %edx
     *      pop kernel state from kernel stack
     *------------------------------------------------------------------------
     */
    __asm__ volatile(
    "pushf;"
            "pusha;"
            "movl %%esp, k_stack;"
            "movl ESP, %%esp;"
            "popa;"
            "movl EAX, %%eax;"
            "iret;"

            "_TimerEntryPoint:"
            "cli;"
            "movl $32, interrupt;"
            "jmp _CommonEntryPoint;"

            "_KBDEntryPoint:"
            "cli;"
            "movl $33, interrupt;"
            "jmp _CommonEntryPoint;"

            "_SysCallEntryPoint:"
            "cli;"
            "movl $0, interrupt;"

            "_CommonEntryPoint:"
            "pusha;"
            "movl %%esp, ESP;"
            "movl k_stack, %%esp;"
            "movl %%eax, EAX;"
            "movl %%edx, args;"
            "popa;"
            "popf;"
    :
    :
    : "%eax", "%ecx"
    );

    int request;

    if (interrupt == TIMER_INTERRUPT_NUMBER) {
        // The request is a timer interrupt
        // The return value of a hardware interrupt is the value of eax when the interrupt occurred
        proc->result_code = EAX;
        request = TIMER_INT;
    } else if (interrupt == KEYBOARD_INTERRUPT_NUMBER) {
        // The request is a keyboard interrupt
        // The return value of a hardware interrupt is the value of eax when the interrupt occurred
        proc->result_code = EAX;
        request = KEYBOARD_INT;
    } else {
        // The request is a system call, in which the request is in EAX
        request = EAX;
    }

    proc->esp = ESP;
    return request;
}
