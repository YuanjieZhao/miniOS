/* syscall.c : syscalls
 */

#include <xeroskernel.h>
#include <stdarg.h>

/*-----------------------------------------------------------------------------------
 * The application side of system calls and the associated supporting
 * functions. These functions run in the application space and perform the
 * steps needed to transition to the kernel which will perform the
 * corresponding task on behalf of the calling process.
 *
 * List of functions that are called from outside this file:
 * - syscreate
 *   - Creates a new process, returns the process ID of the created process,
 *     -1 if create was unsuccessful
 * - sysyield
 *   - Yields the processor to the next ready process
 * - sysstop
 *   - Terminates and cleans up the process
 * - sysgetpid
 *   - Returns the PID of the current process
 * - sysputs
 *   - Performs output
 * - syskill
 *   - Terminates the process identified by the argument to the call,
 *     returns 0 on success, -1 if the target process does not exist
 * - syssetprio
 *   - Sets the priority of the process, returns -1 if the call failed
 *     because the requested priority was out of range, otherwise it
 *     returns the priority the process had when this call was made. There
 *     is one special case and that is priority -1. If the requested
 *     priority is -1, then the call simply returns the current priority
 *     of the process and does not update the process's priority
 * - syssend
 *   - Sends an unsigned long integer message
 * - sysrecv
 *   - Receives an unsigned long integer message
 * - syssleep
 *   - Sleeps for a requested number of milliseconds
 * - sysgetcputimes
 *   - Fill a given processStatuses structure with process information
 * - syssighandler
 *   - Registers the provided function as the handler for the indicated signal
 * - syssigreturn
 *   - Sets the stack pointer so that a context switch back to the target will pick
 *     up the old context at the time of the signal
 * - syswait
 *   - Waits for the process with the given PID to terminate
 * - sysopen
 *   - Opens a device
 * - sysclose
 *   - Closes a file descriptor
 * - syswrite
 *   - Performs a write operation to the device associated with the provided file
 *     descriptor
 * - sysread
 *   - Performs a read operation to the device associated with the provided file
 *     descriptor
 * - sysioctl
 *   - Executes the specified control command
 *-----------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------------
 * Takes a system request identifier and arguments for the system call.
 * Each system call (i.e. syscreate(), sysyield(), and sysstop()) will
 * call this function to manage the transition from the application space
 * to the kernel and back.
 *
 * @param call A system request identifier, such as SYSCREATE, SYSYIELD,
 *             or SYSSTOP
 * @param ...  Arguments for the system call
 *-----------------------------------------------------------------------------------
 */
int syscall(int call, ...) {
    int return_value;

    va_list va_args;
    va_start(va_args, call);

/*-----------------------------------------------------------------------------------
 * In-line asm:
 *     Store the value of call in %eax
 *     Store a pointer to the start of the additional parameters in %edx
 *     Execute an interrupt instruction to context switch into the kernel
 *     Upon return from the kernel, return the result stored in register
 *     %eax
 *-----------------------------------------------------------------------------------
 */
    __asm__ __volatile__(
    "movl %1, %%eax;"
            "movl %2, %%edx;"
            "int $67;"
            "movl %%eax, %0;"
    : "=r" (return_value)
    :"r"(call), "r" (va_args)
    :"%eax", "%edx"
    );

    va_end(va_args);
    return return_value;
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to create a new process.
 *
 * @param func  A function pointer to the address to start execution at
 * @param stack The size of the new process's stack in bytes
 * @return      The process ID of the created process, -1 if create was
 *              unsuccessful (violates A1 spec, but A1 solution's
 *              syscreate returns -1)
 *-----------------------------------------------------------------------------------
 */
PID_t syscreate(void (*func)(void), int stack) {
    return syscall(SYSCREATE, func, stack);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to yield the processor to the next ready process.
 *-----------------------------------------------------------------------------------
 */
void sysyield(void) {
    syscall(SYSYIELD);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to terminate and clean up the process.
 *-----------------------------------------------------------------------------------
 */
void sysstop(void) {
    syscall(SYSSTOP);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call that returns the PID of the current process.
 *
 * @return The PID of the current process
 *-----------------------------------------------------------------------------------
 */
PID_t sysgetpid(void) {
    return syscall(SYSGETPID);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to perform output. As processes are preemptable,
 * and the screen is a shared resource, access to the screen must be
 * synchronized via this system call.
 *
 * @param str A null terminated string that is to be displayed by the
 *            kernel via kprintf
 *-----------------------------------------------------------------------------------
 */
void sysputs(char *str) {
    syscall(SYSPUTS, str);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to request that a signal be delivered to a process. If the
 * signal is marked for delivery or if the signal is marked "to be ignored," that is
 * considered a success.
 *
 * @param pid           The PID of the process to deliver the signal to
 * @param signal_number The number of the signal to be delivered (ie. 0 to 31)
 * @return              0 on success
 *                      -514 if the target process does not exist
 *                      -583 if the signal number is invalid
 *-----------------------------------------------------------------------------------
 */
int syskill(int pid, int signal_number) {
    return syscall(SYSKILL, pid, signal_number);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call which allows a process to set its priority.
 *
 * @param priority The requested priority, from 0 to 3 with 3 being the
 *                 lowest priority and 0 being the highest priority
 * @return         -1 if the call failed because the requested priority
 *                 was out of range, otherwise it returns the priority the
 *                 process had when this call was made. There is one
 *                 special case and that is priority -1. If the requested
 *                 priority is -1, then the call simply returns the current
 *                 priority of the process and does not update the
 *                 process's priority
 *-----------------------------------------------------------------------------------
 */
int syssetprio(int priority) {
    return syscall(SYSSETPRIO, priority);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to send an unsigned long integer message. This
 * call blocks until the send operation completes.
 *
 * @param dest_pid The PID of the receiving process
 * @param num      The integer to send
 * @return         0 on success and a negative value if the operation
 *                 failed:
 *                   - −1 if the receiving process terminates before the
 *                     matching receive is performed
 *                   - −2 if the receiving process does not exist
 *                   - −3 if the sending process tries to send a message
 *                     to itself
 *                   - −100 if any other problem is detected
 *-----------------------------------------------------------------------------------
 */
int syssend(unsigned int dest_pid, unsigned long num) {
    return syscall(SYSSEND, dest_pid, num);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to receive an unsigned long integer message.
 *
 * @param from_pid The address containing the PID of the sending process:
 *                 - If from_pid is non-zero, then it specifies the PID of
 *                   the sending process
 *                 - If from_pid is 0, then the process is willing to
 *                   receive from any process
 * @param num      The address to store the received value into
 * @return         When from_pid is non-zero:
 *                   - 0 on success
 *                   - −1 if the sending process terminates before a
 *                     matching send is performed
 *                   - −2 if the sending process does not exist
 *                   - −3 if the receiving process tries to receive from
 *                     itself
 *                   - −10 if the receiving process is the only user process
 *                     - See: https://piazza.com/class/jjrv7w7o1jv1tt?cid=236
 *                 When from_pid is 0:
 *                   - 0 on success
 *                 In both cases:
 *                   - −5 if from_pid is invalid
 *                     - See: https://piazza.com/class/jjrv7w7o1jv1tt?cid=177
 *                   - −4 if num is invalid
 *                   - −100 is any other problem is detected
 *-----------------------------------------------------------------------------------
 */
int sysrecv(unsigned int *from_pid, unsigned int *num) {
    return syscall(SYSRECV, from_pid, num);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to sleep for a requested number of milliseconds.
 *
 * @param milliseconds The number of milliseconds the process wishes to sleep.
 *                     The kernel will treat this as the minimum amount of time to
 *                     sleep for and wake a process on the next clock tick after this
 *                     minimum amount of time has elapsed
 * @return             0 if the call was blocked for the amount of time
 *                     requested, otherwise it is the amount of time the
 *                     process still had to sleep at the time it was unblocked
 *-----------------------------------------------------------------------------------
 */
unsigned int syssleep(unsigned int milliseconds) {
    return syscall(SYSSLEEP, milliseconds);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to fill the given ps table starting with table element 0.
 * For each process the PID, current process state and the number of milliseconds
 * that have been charged to the process are recorded. Process 0 is always reported
 * and it is the NULL/idle process.
 *
 * @param ps A pointer to a processStatuses structure
 * @return   The last slot used on success
 *           -1 if the given address is in the hole
 *           -2 if the structure being pointed to goes beyond the end of main memory
 *-----------------------------------------------------------------------------------
 */
int sysgetcputimes(processStatuses *ps) {
    return syscall(SYSGETCPUTIMES, ps);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call that registers the provided function as the handler for
 * the indicated signal.
 *
 * When this call successfully installs a new handler, it copies the address of the
 * old handler to the location pointed to by oldHandler. By doing this it is possible
 * for the application code to remember a previously installed handler in case it
 * needs/wants to restore that handler.
 *
 * @param  signal      The signal to register the handler for
 *         new_handler The function being registered as the handler. Takes a single
 *                     argument. NULL if the signal delivery for the identified signal
 *                     is to be disabled (the signal is to be ignored).
 *         old_handler A pointer to a variable that points to a handler
 * @return             0 if the handler is successfully installed
 *                     -1 if signal is invalid or signal 31
 *                     -2 if the handler resides at an invalid address
 *                     -3 if the address pointed to be oldHandler is an invalid address
 *-----------------------------------------------------------------------------------
 */
int syssighandler(int signal, signal_handler_funcptr new_handler, signal_handler_funcptr *old_handler) {
    return syscall(SYSSIGHANDLER, signal, new_handler, old_handler);
}

/*-----------------------------------------------------------------------------------
 * This call is to be used only by the signal trampoline code. Sets the stack pointer
 * so that a context switch back to the target will pick up the old context at the
 * time of the signal. This call does not return.
 *
 * @param old_sp The location in the application stack of the context frame to switch
 *               this process to (the value that was passed to the trampoline code)
 *-----------------------------------------------------------------------------------
 */
void syssigreturn(void *old_sp) {
    syscall(SYSSIGRETURN, old_sp);
    assert(0, "syssigreturn returned");
}

/*-----------------------------------------------------------------------------------
 * Generates a system call that causes the calling process to wait for the process
 * with PID to terminate.
 *
 * @param  pid The PID of the process to wait for termination
 * @return 0 if the call terminates normally
 *         -1 if the process to be waited for does not exist
 *         -666 if the call was interrupted by a signal
 *-----------------------------------------------------------------------------------
 */
int syswait(int pid) {
    return syscall(SYSWAIT, pid);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call used to open a device. device_no (the major device number)
 * will be used by the kernel to index, perhaps after some adjustment, into the
 * device table.
 *
 * @param device_no The major device number
 * @return          A file descriptor in the range 0 to 3 (inclusive) on success,
 *                  -1 if the open fails
 *-----------------------------------------------------------------------------------
 */
int sysopen(int device_no) {
    return syscall(SYSOPEN, device_no);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call used to close a file descriptor. Subsequent system calls
 * that make use of the file descriptor return a failure.
 *
 * @param fd The file descriptor (fd) from a previously successful open call
 * @return   0 on success, -1 on failure
 *-----------------------------------------------------------------------------------
 */
int sysclose(int fd) {
    return syscall(SYSCLOSE, fd);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to perform a write operation to the device associated with
 * the provided file descriptor, fd. Up to buflen bytes are written from buf.
 * Depending upon the device, the number of bytes written may be less than buflen.
 *
 * @param fd     The provided file descriptor
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       The number of bytes written on success, -1 if there was an error
 *-----------------------------------------------------------------------------------
 */
int syswrite(int fd, void *buf, int buflen) {
    return syscall(SYSWRITE, buf, buflen);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to read up to buflen bytes from the previously opened
 * device associated with fd into the buffer area pointed to by buf. Depending upon
 * the device, the number of bytes read may be less than buflen.
 *
 * @param fd     The provided file descriptor
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       The number of bytes read on success
 *               0 to indicate end-of-file (EOF)
 *               -1 if there was an error
 *
 *               If interrupted by a signal:
 *                 The number of bytes read if the number of bytes read is non-zero
 *                 -666 if no bytes were read
 *-----------------------------------------------------------------------------------
 */
int sysread(int fd, void *buf, int buflen) {
    return syscall(SYSREAD, fd, buf, buflen);
}

/*-----------------------------------------------------------------------------------
 * Generates a system call to execute the specified control command. The action taken
 * is device specific and depends upon the control command. Additional parameters are
 * device specific.
 *
 * @param fd      The provided file descriptor
 * @param command The control command
 * @param ...     Additional device specific parameters
 * @return        0 on success, -1 if there was an error
 *-----------------------------------------------------------------------------------
 */
int sysioctl(int fd, unsigned long command, ...) {
    int return_value;

    va_list ioctl_args;
    va_start(ioctl_args, command);
    return_value = syscall(SYSIOCTL, fd, command, ioctl_args);
    va_end(ioctl_args);

    return return_value;
}
