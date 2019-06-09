#include <xeroskernel.h>

/*-----------------------------------------------------------------------------------
 * Tests for IPC. This test suite assumes that the root process and dispatcher
 * are running, and that pre-emption is disabled.
 *
 * List of functions that are called from outside this file:
 * - run_msg_test
 *   - Runs the test suite for msg.c
 *-----------------------------------------------------------------------------------
 */

#define SUCCESS 0
#define ERR_TARGET_TERMINATED -1
#define ERR_TARGET_NOT_EXIST -2
#define ERR_TARGET_IS_ITSELF -3
#define ERR_OTHER_PROB -100
// The following errors are specific to sysrecv
#define ERR_INVALID_PID -5
#define ERR_INVALID_NUM -4
#define ERR_RECV_IS_THE_ONLY_PROCESS -10

extern unsigned long hole_start_aligned;
extern unsigned long max_addr_aligned;

// The message sent by the sender process
static unsigned long g_msg = 0;
static unsigned int g_sender_pid = 0;
static unsigned int g_receiver_pid = 0;

static int const debug = 0;

static void send_then_recv_test(void);
static void recv_then_send_test(void);
static void send_then_recv_any_test(void);
static void send_failure_test(void);
static void recv_failure_test(void);
static void sender_process(void);
static void receiver_process(void);
static void bad_receiver_process(void);
static void bad_sender_process(void);
static void receiver_process_killed_on_block(void);
static void sender_process_killed_on_block(void);

/*-----------------------------------------------------------------------------------
 * Runs the test suite for msg.c.
 *-----------------------------------------------------------------------------------
 */
void run_msg_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    send_then_recv_test();
    recv_then_send_test();
    send_then_recv_any_test();
    send_failure_test();
    recv_failure_test();

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests that send before receive works.
 *-----------------------------------------------------------------------------------
 */
static void send_then_recv_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    g_sender_pid = syscreate(&sender_process, PROCESS_STACK_SIZE);
    g_receiver_pid = syscreate(&receiver_process, PROCESS_STACK_SIZE);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Tests that receive before send works.
 *-----------------------------------------------------------------------------------
 */
static void recv_then_send_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    g_receiver_pid = syscreate(&receiver_process, PROCESS_STACK_SIZE);
    g_sender_pid = syscreate(&sender_process, PROCESS_STACK_SIZE);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Have 10 sender processes each send a message, and then call receive-any.
 *-----------------------------------------------------------------------------------
 */
static void send_then_recv_any_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int sender_pids[10] = {0};
    unsigned int receive_any_pid = 0;
    unsigned int num = 0;

    g_receiver_pid = sysgetpid();
    for (int i = 0; i < 10; i++) {
        sender_pids[i] = syscreate(&sender_process, PROCESS_STACK_SIZE);
        sysyield();
    }

    for (int i = 0; i < 10; i++) {
        int result = sysrecv(&receive_any_pid, &num);
        assert_equal(result, SUCCESS);
        assert_equal(receive_any_pid, sender_pids[i]);
        assert_equal(num, sender_pids[i]);
        // Reset to 0 to enable receive-any in the next iteration
        receive_any_pid = 0;
    }

    yield_to_all();
    // Return -10 if the receiving process is the only user process
    int result = sysrecv(&receive_any_pid, &num);
    assert_equal(result, ERR_RECV_IS_THE_ONLY_PROCESS);

    // Return -10 if the receiving process becomes the only user process
    syscreate(&bad_sender_process, PROCESS_STACK_SIZE);
    result = sysrecv(&receive_any_pid, &num);
    sysyield();
    assert_equal(result, ERR_RECV_IS_THE_ONLY_PROCESS);

    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Test syssend failure cases.
 *-----------------------------------------------------------------------------------
 */
static void send_failure_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int result = 0;
    unsigned long msg = 1;
    unsigned int my_pid = sysgetpid();

    // Send msg to itself
    result = syssend(my_pid, msg);
    assert_equal(result, ERR_TARGET_IS_ITSELF);

    // Send msg to non-existent process
    result = syssend(200, msg);
    assert_equal(result, ERR_TARGET_NOT_EXIST);

    // Receiver process terminates before the matching receive is performed
    unsigned int bad_receiver_pid = syscreate(&bad_receiver_process, PROCESS_STACK_SIZE);
    result = syssend(bad_receiver_pid, msg);
    assert_equal(result, ERR_TARGET_TERMINATED);

    // syskill on process blocked on receive
    unsigned int receiver_pid = syscreate(&receiver_process_killed_on_block, PROCESS_STACK_SIZE);
    g_sender_pid = my_pid;
    sysyield();
    syskill(receiver_pid, 31);
    result = syssend(receiver_pid, msg);
    assert_equal(result, ERR_TARGET_NOT_EXIST);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * Test sysrecv failure cases.
 *-----------------------------------------------------------------------------------
 */
static void recv_failure_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int result = 0;
    unsigned int msg = 1;
    unsigned int my_pid = sysgetpid();

    // Receive msg from itself
    result = sysrecv(&my_pid, &msg);
    assert_equal(result, ERR_TARGET_IS_ITSELF);

    // Sender process does not exist
    unsigned int invalid_pid = 200;
    result = sysrecv(&invalid_pid, &msg);
    assert_equal(result, ERR_TARGET_NOT_EXIST);

    // from_pid is null
    result = sysrecv(NULL, &msg);
    assert_equal(result, ERR_INVALID_PID);
    // from_pid is not in memory range
    result = sysrecv((unsigned int *) max_addr_aligned + PARAGRAPH_SIZE, &msg);
    assert_equal(result, ERR_INVALID_PID);
    // from_pid is in the hole
    result = sysrecv((unsigned int *) hole_start_aligned + PARAGRAPH_SIZE, &msg);
    assert_equal(result, ERR_INVALID_PID);
    // from_pid is in kernel memory
    result = sysrecv(&g_sender_pid, &msg);
    assert_equal(result, ERR_INVALID_PID);

    // num is null
    unsigned int sender_pid = syscreate(&sender_process, PROCESS_STACK_SIZE);
    result = sysrecv(&sender_pid, NULL);
    assert_equal(result, ERR_INVALID_NUM);
    // num is not in memory range
    result = sysrecv(&sender_pid, (unsigned int *) max_addr_aligned + PARAGRAPH_SIZE);
    assert_equal(result, ERR_INVALID_NUM);
    // num is in the hole
    result = sysrecv(&sender_pid, (unsigned int *) hole_start_aligned + PARAGRAPH_SIZE);
    assert_equal(result, ERR_INVALID_NUM);
    // from_pid is in kernel memory
    result = sysrecv(&sender_pid, (unsigned int *) &g_msg);
    assert_equal(result, ERR_INVALID_NUM);
    syskill(sender_pid, 31);

    if (debug) sysputs("Sender process terminates\n");
    // Sender process terminates before a matching send is performed
    unsigned int bad_sender_pid = syscreate(&bad_sender_process, PROCESS_STACK_SIZE);
    result = sysrecv(&bad_sender_pid, &msg);
    assert_equal(result, ERR_TARGET_TERMINATED);

    if (debug) sysputs("syskill on process blocked on send\n");
    sender_pid = syscreate(&sender_process_killed_on_block, PROCESS_STACK_SIZE);
    g_receiver_pid = my_pid;
    // Transfer control to sender process, which executes syssend and blocks
    sysyield();
    syskill(sender_pid, 31);
    result = sysrecv(&sender_pid, &msg);
    assert_equal(result, ERR_TARGET_NOT_EXIST);

    // Receiver process is the only user process
    syscreate(&bad_sender_process, PROCESS_STACK_SIZE);
    syscreate(&bad_receiver_process, PROCESS_STACK_SIZE);
    unsigned int receive_any_pid = 0;
    // Block and yield control after sysrecv
    result = sysrecv(&receive_any_pid, &msg);
    assert_equal(result, ERR_RECV_IS_THE_ONLY_PROCESS);

    yield_to_all();
    if (debug) kprintf("Finished %s\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A sender process used by tests.
 *-----------------------------------------------------------------------------------
 */
static void sender_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    // Sender process sends its PID to receiver process
    unsigned int receiver_pid = g_receiver_pid;
    unsigned long num = (unsigned long) sysgetpid();
    g_msg = num;
    if (debug) kprintf("%s sending a message\n", __func__);
    int result = syssend(receiver_pid, num);
    if (debug) kprintf("%s received a message\n", __func__);
    assert_equal(result, SUCCESS);
}

/*-----------------------------------------------------------------------------------
 * A receiver process used by tests.
 *-----------------------------------------------------------------------------------
 */
static void receiver_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    unsigned int from_pid = g_sender_pid;
    unsigned int num = 0;
    int result = sysrecv(&from_pid, &num);
    assert_equal(result, SUCCESS);
    assert(num == g_msg, "Received msg is different from the sent msg");
}

/*-----------------------------------------------------------------------------------
 * A receiver process that never calls sysrecv.
 *-----------------------------------------------------------------------------------
 */
static void bad_receiver_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A sender process that never calls syssend.
 *-----------------------------------------------------------------------------------
 */
static void bad_sender_process(void) {
    if (debug) kprintf("%s starts...\n", __func__);
}

/*-----------------------------------------------------------------------------------
 * A sender process killed when it is blocked on send, used by failure tests.
 *-----------------------------------------------------------------------------------
 */
static void sender_process_killed_on_block(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    unsigned int receiver_pid = g_receiver_pid;
    unsigned long num = (unsigned long) sysgetpid();
    g_msg = num;
    syssend(receiver_pid, num);
    assert(0, "sender_process_killed_on_block is still alive after being killed");
}

/*-----------------------------------------------------------------------------------
 * A receiver process killed when it is blocked on send, used by failure tests.
 *-----------------------------------------------------------------------------------
 */
static void receiver_process_killed_on_block(void) {
    if (debug) kprintf("%s starts...\n", __func__);
    unsigned int from_pid = g_sender_pid;
    unsigned int num = 0;
    sysrecv(&from_pid, &num);
    assert(0, "receiver_process_killed_on_block is still alive after being killed");
}
