/* xeroskernel.h - disable, enable, halt, restore, isodd, min, max */

#ifndef XEROSKERNEL_H
#define XEROSKERNEL_H

/* Symbolic constants used throughout Xinu */

typedef char Bool;         /* Boolean type                  */
typedef unsigned long size_t; /* Something that can hold the value of
                              * theoretical maximum number of bytes 
                              * addressable in this architecture.
                              */
#define    FALSE   0       /* Boolean constants             */
#define    TRUE    1
#define    EMPTY   (-1)    /* an illegal gpq                */
#define    NULL    0       /* Null pointer for linked lists */
#define    NULLCH '\0'     /* The null character            */

/* Universal return constants */

#define    OK            1         /* system call ok               */
#define    SYSERR       -1         /* system call failed           */
#define    EOF          -2         /* End-of-file (usu. from read)	*/
#define    TIMEOUT      -3         /* time out  (usu. recvtim)     */
#define    INTRMSG      -4         /* keyboard "intr" key pressed	*/
/*  (usu. defined as ^B)        */
#define    BLOCKERR     -5         /* non-blocking op would block  */

/* Functions defined by startup code */

void bzero(void *base, int cnt);
void bcopy(const void *src, void *dest, unsigned int n);
void disable(void);
unsigned short getCS(void);
unsigned char inb(unsigned int);
void init8259(void);
int kprintf(char *fmt, ...);
void lidt(void);
void outb(unsigned int, unsigned char);
void set_evec(unsigned int xnum, unsigned long handler);

/* Constants */
#define PREEMPTION_ENABLED 1
#define PARAGRAPH_SIZE 16
/* System can support 32 processes */
#define PCB_TABLE_SIZE 32
/* System can support 2 devices */
#define DEVICE_TABLE_SIZE 2
#define PROCESS_STACK_SIZE 8192
#define IDLE_PROCESS_STACK_SIZE 512
#define NUM_PRIORITIES 4
// By default a process is created with priority 3
#define INIT_PRIORITY 3
#define IDLE_PROC_PID 0
#define BUFFER_SIZE sizeof(unsigned long)
#define TIME_SLICE 10
#define SYSCALL_INTERRUPT_NUMBER 67
#define TIMER_INTERRUPT_NUMBER 32
#define KEYBOARD_INTERRUPT_NUMBER 33
#define EFLAGS 0x00003200
#define SIGNAL_TABLE_SIZE 32
// Allow 4 devices to be opened at once by each process
#define FD_TABLE_SIZE 4

// Debug flag for logging
#define DEBUG 0

typedef enum {
    RUNNING,
    READY,
    BLOCKED,
    STOPPED
} process_state_t;

typedef struct context_frame {
    unsigned long edi;
    unsigned long esi;
    unsigned long ebp;
    unsigned long esp;
    unsigned long ebx;
    unsigned long edx;
    unsigned long ecx;
    unsigned long eax;
    unsigned long iret_eip;
    unsigned long iret_cs;
    unsigned long eflags;
} context_frame_t;

typedef enum blocked_queue {
    SENDER = 0,
    RECEIVER = 1,
    WAIT = 2,
    RECEIVE_ANY,
    SLEEP,
    READ,
    NONE
} blocked_queue_t;

struct pcb;
typedef struct queue {
    size_t size;
    // Dequeue at head
    struct pcb *head;
    // Enqueue at tail
    struct pcb *tail;
} Queue;

// funcptr = void (*func)(void), for readability
typedef void (*funcptr)(void);
typedef void (*signal_handler_funcptr)(void *);

typedef struct signal_delivery_context {
    context_frame_t context_frame;

    funcptr *return_address;
    signal_handler_funcptr handler;
    void *cntx;
    // The current value of the current signal processing level
    int last_signal_delivered;
    // Remember the value that needs to be returned for the system call as any context
    // switches from the trampoline/handler to the kernel could result in register %eax
    // being improperly set upon return
    int saved_result_code;

} signal_delivery_context_t;

typedef enum {
    KBD_0 = 0,
    KBD_1 = 1
} dev_t;

struct devsw;
typedef struct pcb {
    int pid;
    process_state_t state;
    // prev and next pointers for implementing process queues as doubly-linked lists
    struct pcb *prev;
    struct pcb *next;
    void *mem_start;
    void *esp;
    // Return value of system call
    int result_code;
    // A process can have a priority from 0 to 3
    // with 3 being the lowest priority and 0 being the highest priority
    // By default a process is created with priority 3
    int priority;

    // The process that this process is blocked on
    struct pcb *blocked_on;
    // The blocked queue that this process is on
    blocked_queue_t blocked_queue;
    // blocked_queues[0] = queue of senders
    // blocked_queues[1] = queue of receivers
    // blocked_queues[2] = queue of waiting processes
    Queue blocked_queues[3];
    unsigned long *ipc_args;
    // Used in deltalist to service syssleep
    // Specifies the delay relative to the time when the predecessor is woken
    int key;

    // CPU time consumed in ticks
    long cpuTime;

    signal_handler_funcptr signal_table[SIGNAL_TABLE_SIZE];

    // Records all of the signals currently targeted to the process
    int pending_signals;
    // The last signal delivered, -1 if there are no pending signals
    // Indicates the range of signals the process will respond to taking priorities into account
    // Signals numbered > last_signal_delivered will be delivered
    int last_signal_delivered;

    // File descriptor table that allows 4 devices to be opened at once
    // Each entry in the table identifies the device associated with the descriptor
    // as a pointer to the device in device block table
    struct devsw *fd_table[FD_TABLE_SIZE];
} pcb_t;

/*-----------------------------------------------------------------------------------
 * The device structure: The design of the structure is capable of supporting a wide
 * range of both physical and virtual devices
 *
 * Design notes:
 * - How this device structure differs from the device structure given in class:
 *   - dvseek, dvgetc, dvputc, dvcntl, dvcsr, dvivec, dvovec, dviint, and dvoint were
 *     removed as there are no corresponding system calls to access these services
 *   - dvioblk was removed as it is not necessary to have device specific data
 *   - dvioctl was added to perform the sysioctl service
 * - Each function to perform the various services (aside from dvinit) has as its
 *   parameters a pointer to the PCB of the process that called the corresponding
 *   system call, along with the parameters of the corresponding system call
 *   necessary to perform the service
 * - As this device structure is to be capable of supporting a wide range of both
 *   physical and virtual devices, the structure is not specific to the keyboard,
 *   which means that parameters may be unused in the keyboard device driver
 *-----------------------------------------------------------------------------------
 */
typedef struct devsw {
    // Basic device identification data
    // Data required to manage access to the device
    int dvnum;
    char *dvname;
    int dvminor;

    // Called to setup the device
    int (*dvinit)(void);
    // Sets up device access
    int (*dvopen)(pcb_t *proc, int device_no);
    // Terminates device access
    int (*dvclose)(pcb_t *proc);

    int (*dvread)(pcb_t *proc, void *buf, int buflen);
    int (*dvwrite)(pcb_t *proc, void *buf, int buflen);

    // Pass special control information
    int (*dvioctl)(pcb_t *proc, unsigned long command, void *ioctl_args);
} devsw_t;

//  Each element in the structure corresponds to a process in the system
typedef struct struct_ps {
    // Last entry used in the table
    int entries;
    // The process ID
    int pid[PCB_TABLE_SIZE];

    // The process state
    process_state_t state[PCB_TABLE_SIZE];
    // The process blocked queue if state is BLOCKED
    blocked_queue_t blocked_queue[PCB_TABLE_SIZE];

    // CPU time used in milliseconds
    long cpuTime[PCB_TABLE_SIZE];
} processStatuses;

typedef unsigned int PID_t;

typedef enum {
    SYSCREATE,
    SYSYIELD,
    SYSSTOP,
    SYSGETPID,
    SYSPUTS,
    SYSKILL,
    SYSSETPRIO,
    SYSSEND,
    SYSRECV,
    SYSSLEEP,
    SYSGETCPUTIMES,
    SYSSIGHANDLER,
    SYSSIGRETURN,
    SYSWAIT,
    SYSOPEN,
    SYSCLOSE,
    SYSWRITE,
    SYSREAD,
    SYSIOCTL,
    TIMER_INT,
    KEYBOARD_INT
} request_t;

/* mem.c */
typedef struct mem_header mem_header_t;
void kmeminit(void);
void *kmalloc(size_t size);
int kfree(void *ptr);
int valid_ptr(void *ptr);
int valid_buf(void *ptr, unsigned long length);

/* mem.c testing */
int get_free_list_length(void);
void print_free_list(void);

/* disp.c */
void kdispinit(void);
void dispatch(void);
void ready(pcb_t *proc);
pcb_t *get_unused_pcb(void);
void enqueue_blocked_queue(pcb_t *proc, pcb_t *blocked_on_proc, blocked_queue_t blocked_queue);
int remove_from_blocked_queue(pcb_t *proc, pcb_t *blocked_on_proc, blocked_queue_t blocked_queue);
int remove_from_receive_any_queue(pcb_t *proc);
void idleproc(void);

/* ctsw.c */
void contextinit(void);
request_t contextswitch(pcb_t *proc);

/* create.c */
int create(void (*func)(void), int stack);
void create_idle_proc(pcb_t *idle_proc);

/* syscall.c */
int syscall(int call, ...);
PID_t syscreate(void (*func)(void), int stack);
void sysyield(void);
void sysstop(void);
PID_t sysgetpid(void);
void sysputs(char *str);
int syskill(int pid, int signal_number);
int syssetprio(int priority);
int syssend(unsigned int dest_pid, unsigned long num);
int sysrecv(unsigned int *from_pid, unsigned int *num);
unsigned int syssleep(unsigned int milliseconds);
int sysgetcputimes(processStatuses *ps);
int syssighandler(int signal, signal_handler_funcptr new_handler, signal_handler_funcptr *old_handler);
void syssigreturn(void *old_sp);
int syswait(int pid);
int sysopen(int device_no);
int sysclose(int fd);
int syswrite(int fd, void *buf, int buflen);
int sysread(int fd, void *buf, int buflen);
int sysioctl(int fd, unsigned long command, ...);

/* user.c */
void init(void);
void call_sysgetcputimes(void);

/* msg.c */
int send(pcb_t *send_proc, pcb_t *recv_proc, unsigned long *send_buf);
int recv(pcb_t *recv_proc, pcb_t *send_proc, unsigned int *from_pid, unsigned int *recv_buf);

/* sleep.c */
void ksleepinit(void);
void sleep(pcb_t *proc, unsigned int milliseconds);
void tick(void);

/* signal.c */
void sigtramp(signal_handler_funcptr handler, void *cntx);
int signal(pcb_t *proc_to_signal, int signal_number);
void handle_pending_signals(pcb_t *proc);
int set_signal_bit(int bitmask, int signal_number);
int is_signal_bit_set(int bitmask, int signal_number);
int clear_signal_bit(int bitmask, int signal_number);

/* di_calls.c */
void kdiinit(void);
int di_open(pcb_t *current_proc, int device_no);
int di_close(pcb_t *current_proc, int fd);
int di_write(pcb_t *current_proc, int fd, void *buf, int buflen);
int di_read(pcb_t *current_proc, int fd, void *buf, int buflen);
int di_ioctl(pcb_t *current_proc, int fd, unsigned long command, void *ioctl_args);

/* Functions for testing */
void run_device_test(void);
void run_mem_test(void);
void run_queue_test(void);
void run_deltalist_test(void);
void run_syscall_test(void);
void run_create_test(void);
void run_msg_test(void);
void run_preemption_test(void);
void run_signal_test(void);
void assert(int to_assert, char *message);
void assert_equal(int actual, int expected);
void busy_wait(void);
void wait(void);
void m(int n);
void dummy_process(void);
void yield_to_all(void);

/* Anything you add must be between the #define and this comment */
#endif
