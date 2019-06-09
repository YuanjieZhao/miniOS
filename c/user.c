/* user.c : User processes
 */

#include <xeroskernel.h>
#include <xeroslib.h>
#include <stdarg.h>
#include <kbd.h>

/*-----------------------------------------------------------------------------------
 *  User processes reside here. root is the first process started by the
 *  kernel. These user processes test the kernel.
 *-----------------------------------------------------------------------------------
 */

static void shell(void);
static void alarm_handler(void *arg);
static void alarm_process(void);
static void t_process(void);
static void remove_newline(char *str);
static int parse_command(char *input_buf, char *command_buf, char *arg_buf);
static int is_empty(char *buf);
static void run_root_tests(void);
static char *printable_state(process_state_t state, blocked_queue_t blocked_queue);

// The time and shell pid for "a" command
static unsigned int g_time;
static PID_t g_shell_pid;

/*-----------------------------------------------------------------------------------
 * The first process started by the kernel. Controls access to the console. Provides
 * the functionality similar to the init program on Unix.
 *-----------------------------------------------------------------------------------
 */
void init(void) {
    run_root_tests();

    // The only username to support is cs415
    char *username = "cs415";
    char *password = "EveryonegetsanA";

    while (1) {
        char username_buf[32];
        memset(username_buf, '\0', sizeof(username_buf));
        char password_buf[32];
        memset(password_buf, '\0', sizeof(password_buf));

        // Print a banner
        sysputs("\nWelcome to Xeros - a not so experimental OS\n");

        // Open the keyboard
        int fd = sysopen(KBD_1);
        sysputs("Username: ");
        // Read the username
        sysread(fd, username_buf, 32);

        // Turn keyboard echoing off
        sysioctl(fd, IOCTL_ECHO_OFF);
        sysputs("Password: ");
        // Read the password
        sysread(fd, password_buf, 32);

        // Close the keyboard
        sysclose(fd);

        remove_newline(username_buf);
        remove_newline(password_buf);

        // Verify the username and password
        int correct_username = strcmp(username_buf, username) == 0;
        int correct_password = strcmp(password_buf, password) == 0;
        if (correct_username && correct_password) {
            sysputs("\nAuthenticated!\n");
            // Create the shell program
            int shell_pid = syscreate(&shell, PROCESS_STACK_SIZE);
            // Wait for the shell program to exit
            syswait(shell_pid);
        } else {
            if (!correct_username && !correct_password) {
                sysputs("\nIncorrect username and password pair!\n");
            } else if (!correct_username) {
                sysputs("\nIncorrect username!\n");
            } else {
                sysputs("\nIncorrect password!\n");
            }
        }
    }
}

/*-----------------------------------------------------------------------------------
 * A simple shell.
 *-----------------------------------------------------------------------------------
 */
static void shell(void) {
    g_shell_pid = sysgetpid();

    // Open the keyboard
    int fd = sysopen(KBD_1);
    char input_buf[50];
    char command_buf[50];
    char arg_buf[50];

    sysputs("\n");
    while (1) {
        memset(input_buf, '\0', sizeof(input_buf));
        memset(command_buf, '\0', sizeof(command_buf));
        memset(arg_buf, '\0', sizeof(arg_buf));

        // Print the prompt >
        sysputs("> ");

        // Read the command - each command ends when the enter key is pressed
        int bytes_read = sysread(fd, input_buf, 49);
        if (bytes_read == 0) {
            // EOF indicator, causes the shell to exit
            // Close the keyboard
            sysputs("Goodbye! Exiting shell...\n");
            sysclose(fd);
            sysstop();
        }
        if (bytes_read == -666) {
            continue;
        }
        remove_newline(input_buf);

        // 1 if the command line ended with '&'
        int parse_command_return = parse_command(input_buf, command_buf, arg_buf);

        // Commands designated as builtin are run by the shell directly
        if (strcmp(command_buf, "ps") == 0) {
            // ps - Builtin
            if (!is_empty(arg_buf) || parse_command_return == -1) {
                sysputs("Usage: ps\n");
            } else {
                call_sysgetcputimes();
            }
        } else if (strcmp(command_buf, "ex") == 0) {
            // ex - Builtin
            // Causes the shell to exit
            if (!is_empty(arg_buf) || parse_command_return == -1) {
                sysputs("Usage: ex\n");
            } else {
                sysputs("Goodbye! Exiting shell...\n");
                // Close the keyboard
                sysclose(fd);
                sysstop();
            }
        } else if (strcmp(command_buf, "k") == 0) {
            // k - Builtin
            // Takes a parameter, the PID of the process to terminate, and kills that process
            int proc_to_kill = atoi(arg_buf);
            if (proc_to_kill <= 0 || parse_command_return == -1) {
                sysputs("Usage: k pid\n");
            } else {
                // Shell is killing itself
                if (proc_to_kill == g_shell_pid) {
                    sysputs("Goodbye! Exiting shell...\n");
                    // Close the keyboard
                    sysclose(fd);
                    syskill(proc_to_kill, 31);
                }
                if (syskill(proc_to_kill, 31) == -514) {
                    // The process does not exist
                    sysputs("No such process\n");
                }
            }
        } else if (strcmp(command_buf, "a") == 0) {
            // a - Partially builtin
            // Takes a parameter that is the number of milliseconds before signal 18 is to be sent
            g_time = atoi(arg_buf);
            if (g_time <= 0 || parse_command_return == -1) {
                sysputs("Usage: a number_of_milliseconds\n");
            } else {
                // Install a handler that prints "ALARM ALARM ALARM"
                signal_handler_funcptr old_handler;
                syssighandler(18, &alarm_handler, &old_handler);
                PID_t alarm_proc_pid = syscreate(&alarm_process, PROCESS_STACK_SIZE);
                if (parse_command_return != 1) {
                    // If the command line ends with '&': run the alarm process in the background
                    // Otherwise, wait for the alarm process to terminate
                    syswait(alarm_proc_pid);
                }
            }
        } else if (strcmp(command_buf, "t") == 0) {
            if (!is_empty(arg_buf) || parse_command_return == -1) {
                sysputs("Usage: t\n");
            } else {
                // t - Starts the t process which simply prints, on a new line, a "T" every 10 seconds or so
                PID_t t_process_pid = syscreate(t_process, PROCESS_STACK_SIZE);
                if (parse_command_return != 1) {
                    syswait(t_process_pid);
                }
            }
        } else {
            // The command does not exist
            sysputs("Command not found\n");
        }
    }
}

/*-----------------------------------------------------------------------------------
 * Used to service "a" command. Prints "ALARM ALARM ALARM."
 *-----------------------------------------------------------------------------------
 */
static void alarm_handler(void *arg) {
    signal_handler_funcptr old_handler;
    sysputs("ALARM ALARM ALARM\n");
    // Disable signal 18 once the alarm has been delivered
    syssighandler(18, NULL, &old_handler);
}

/*-----------------------------------------------------------------------------------
 * Used to service "a" command. Sleeps for the required number of ticks and then sends the signal 18 to the shell.
 *-----------------------------------------------------------------------------------
 */
static void alarm_process(void) {
    syssleep(g_time);
    syskill(g_shell_pid, 18);
}

/*-----------------------------------------------------------------------------------
 * Used to service "t" command. Prints, on a new line, a "T" every 10 seconds or so.
 *-----------------------------------------------------------------------------------
 */
static void t_process(void) {
    while (1) {
        sysputs("T\n");
        syssleep(10000);
    }
}

/*-----------------------------------------------------------------------------------
 * Sets the first newline encountered in the given buffer to a null terminator.
 *
 * @param buf The given buffer
 *-----------------------------------------------------------------------------------
 */
static void remove_newline(char *buf) {
    while (*buf != '\0') {
        if (*buf == '\n') {
            *buf = '\0';
            break;
        }
        buf++;
    }
}

/*-----------------------------------------------------------------------------------
 * Parses the given input buffer into the given command and arg buffers.
 *
 * @param input_buf   The given input buffer
 * @param command_buf The given command buffer
 * @param arg_buf     The given arg buffer
 * @return            -1 if the there was more than 1 arg
 *                    1 if the command was terminated with &
 *                    0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int parse_command(char *input_buf, char *command_buf, char *arg_buf) {
    int amp_termed = 0;
    int pos = 0;

    // Move to command
    while ((*input_buf != '\0') && (*input_buf == ' ')) {
        input_buf++;
        pos++;
    }
    // Parse the given input buffer into the given command buffer
    while (*input_buf != '\0' && *input_buf != ' ' && *input_buf != '&') {
        *command_buf = *input_buf;
        input_buf++;
        command_buf++;
        pos++;
    }
    *command_buf = '\0';

    // Parse the given input buffer into the given arg buffer
    // Move to arg
    while ((*input_buf != '\0') && (*input_buf == ' ' || *input_buf == '&')) {
        input_buf++;
        pos++;
    }
    // Read the arg
    while (*input_buf != '\0' && *input_buf != ' ' && *input_buf != '&') {
        *arg_buf = *input_buf;
        input_buf++;
        arg_buf++;
        pos++;
    }
    *arg_buf = '\0';

    // Move to end of command
    while (*input_buf != '\0') {
        if (*input_buf != ' ' && *input_buf != '&') return -1;
        input_buf++;
        pos++;
    }
    input_buf--;
    pos--;

    // Check if the command line ended with '&'
    while ((pos > 0) && (*input_buf == ' ' || *input_buf == '&')) {
        if (*input_buf == '&') amp_termed = 1;
        if (*input_buf != ' ') break;
        input_buf--;
        pos--;
    }

    return amp_termed;
}

/*-----------------------------------------------------------------------------------
 * Checks if a buffer is empty.
 *
 * @buf The given buffer
 * @return 1 if the buffer is empty, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int is_empty(char *buf) {
    return *buf == '\0';
}

/*-----------------------------------------------------------------------------------
 * Runs test suites that depend on a root process.
 *-----------------------------------------------------------------------------------
 */
static void run_root_tests(void) {
    run_create_test();
    // Test suite assumes pre-emption is disabled
    if (!PREEMPTION_ENABLED) {
        // Commented out for Assignment 3: Not applicable - run_msg_test();
        run_signal_test();
    }
    // Test suite assumes pre-emption is enabled
    if (PREEMPTION_ENABLED) {
        run_preemption_test();
        run_device_test();
    }
    run_syscall_test();
}

/*-----------------------------------------------------------------------------------
 * Calls sysgetcputimes and lists all the current living/active processes one per
 * line. Each line consists of 3 spaced columns. The columns, which are to have
 * headings, are the PID, the current state of the process, and the amount of time
 * the process has run in milliseconds.
 *
 * Note that the state of the process running the ps command (shell) is reported as
 * RUNNING.
 *-----------------------------------------------------------------------------------
 */
void call_sysgetcputimes(void) {
    char print_buf[1024];
    processStatuses psTab;
    int procs;

    procs = sysgetcputimes(&psTab);

    sysputs("PID  | STATE                | CPU TIME  \n");
    for (int j = 0; j <= procs; j++) {
        sprintf(print_buf, "%-4d | %-20s | %-10d\n", psTab.pid[j],
                printable_state(psTab.state[j], psTab.blocked_queue[j]),
                psTab.cpuTime[j]);
        sysputs(print_buf);
    }
}

/*-----------------------------------------------------------------------------------
 * Returns a meaningful state name given a process state.
 *
 * @param state         The process state
 * @param blocked_queue The process blocked queue
 * @return              A meaningful state name
 *-----------------------------------------------------------------------------------
 */
static char *printable_state(process_state_t state, blocked_queue_t blocked_queue) {
    switch (state) {
        case (RUNNING):
            return "Running";
        case (READY):
            return "Ready";
        case (BLOCKED):
            switch (blocked_queue) {
                case (SENDER):
                    return "Blocked: Sending";
                case (RECEIVER):
                    return "Blocked: Receiving";
                case (WAIT):
                    return "Blocked: Waiting";
                case (RECEIVE_ANY):
                    return "Blocked: Receive-any";
                case (SLEEP):
                    return "Blocked: Sleeping";
                case (READ):
                    return "Blocked: I/O read";
                default:
                    assert(0, "Process state is blocked with no blocked queue");
                    return "";
            }
        case (STOPPED):
            return "Stopped";
        default:
            assert(0, "Unknown process state");
            return "";
    }
}
