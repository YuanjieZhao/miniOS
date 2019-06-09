#include <xeroskernel.h>
#include <i386.h>
#include <kbd.h>
#include <xeroslib.h>

/*-----------------------------------------------------------------------------------
 * Tests for device functionality. This test suite assumes that the root process and
 * dispatcher are running, and that pre-emption is enabled.
 *
 * List of functions that are called from outside this file:
 *  - run_device_test
 *    - Runs the test suite for device functionality
 *-----------------------------------------------------------------------------------
 */
static void sysopen_sysclose_test(void);
static void syswrite_test(void);
static void sysread_test(void);
static void sysioctl_test(void);

static int const debug = 0;

void run_device_test(void) {
    kprintf("Running %s\n", __func__);

    sysopen_sysclose_test();
    syswrite_test();
    if (debug) {
        // This test requires user inputs
        sysread_test();
        busy_wait();
    }
    sysioctl_test();

    kprintf("Finished %s\n", __func__);
}

static void sysopen_sysclose_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int fd;
    int fd2;

    if (debug) sysputs("Failure tests: close invalid device...\n");
    assert_equal(sysclose(-1), SYSERR);
    assert_equal(sysclose(2), SYSERR);

    if (debug) sysputs("Failure tests: open invalid devices...\n");
    fd = sysopen(-1);
    assert_equal(fd, SYSERR);
    fd = sysopen(40);
    assert_equal(fd, SYSERR);

    if (debug) sysputs("Open then close a keyboard...\n");
    fd = sysopen(KBD_1);
    assert_equal(fd, 0);
    assert_equal(sysclose(fd), 0);

    if (debug) sysputs("Failure tests: close a keyboard device twice...\n");
    assert_equal(sysclose(fd), SYSERR);

    if (debug) sysputs("Failure tests: Open the same keyboard twice then close...\n");
    fd = sysopen(KBD_1);
    fd2 = sysopen(KBD_1);
    assert_equal(fd, 0);
    assert_equal(fd2, SYSERR);
    assert_equal(sysclose(fd), 0);
    assert_equal(sysclose(fd2), SYSERR);

    fd = sysopen(KBD_0);
    fd2 = sysopen(KBD_0);
    assert_equal(fd, 0);
    assert_equal(fd2, SYSERR);
    assert_equal(sysclose(fd), 0);
    assert_equal(sysclose(fd2), SYSERR);

    if (debug) sysputs("Failure tests: open two different keyboard devices then close...\n");
    fd = sysopen(KBD_1);
    fd2 = sysopen(KBD_0);
    assert_equal(fd, 0);
    assert_equal(fd2, SYSERR);
    assert_equal(sysclose(fd), 0);
    assert_equal(sysclose(fd2), SYSERR);

    fd = sysopen(KBD_0);
    fd2 = sysopen(KBD_1);
    assert_equal(fd, 0);
    assert_equal(fd2, SYSERR);
    assert_equal(sysclose(fd), 0);
    assert_equal(sysclose(fd2), SYSERR);

    if (debug) kprintf("Finished %s\n", __func__);
}

static void syswrite_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int fd;
    char buf[20];
    sprintf(buf, "cs415");

    if (debug) sysputs("Write to opened device...\n");
    fd = sysopen(KBD_1);
    assert_equal(fd, 0);
    // syswrite is not supported by the keyboard device
    assert_equal(syswrite(fd, buf, strlen(buf)), -1);

    if (debug) sysputs("Write to closed device...\n");
    assert_equal(sysclose(fd), 0);
    assert_equal(syswrite(fd, buf, strlen(buf)), SYSERR);

    if (debug) sysputs("Write to invalid FD...\n");
    assert_equal(syswrite(-1, buf, strlen(buf)), SYSERR);
    assert_equal(syswrite(2, buf, strlen(buf)), SYSERR);

    if (debug) kprintf("Finished %s\n", __func__);
}

static void sysread_test(void) {
    kprintf("Running %s\n", __func__);

    int fd;
    char buf[20] = {'\0'};

    fd = sysopen(KBD_1);
    sysputs("Please type on the keyboard...\n");

    // Reset read buffer
    memset(buf, '\0', sizeof(buf));
    sysputs("Fill up kernel buffer\n");
    busy_wait();

    int bytes = sysread(fd, buf, 1);
    kprintf("\nBytes read = %d, typed characters = %s\n", bytes, buf);

    memset(buf, '\0', sizeof(buf));
    bytes = sysread(fd, buf, 2);
    kprintf("\nBytes read = %d, typed characters = %s\n", bytes, buf);

    memset(buf, '\0', sizeof(buf));
    bytes = sysread(fd, buf, 4);
    kprintf("\nBytes read = %d, typed characters = %s\n", bytes, buf);

    memset(buf, '\0', sizeof(buf));
    bytes = sysread(fd, buf, 8);
    kprintf("\nBytes read = %d, typed characters = %s\n", bytes, buf);

    memset(buf, '\0', sizeof(buf));
    bytes = sysread(fd, buf, 16);
    kprintf("\nBytes read = %d, typed characters = %s\n", bytes, buf);

    sysclose(fd);

    kprintf("Finished %s\n", __func__);
}

static void sysioctl_test(void) {
    if (debug) kprintf("Running %s\n", __func__);

    int fd;
    fd = sysopen(KBD_1);

    if (debug) sysputs("IOCTL on a valid FD...\n");
    assert_equal(sysioctl(fd, IOCTL_CHANGE_EOF, 'a'), 0);
    assert_equal(sysioctl(fd, IOCTL_ECHO_OFF), 0);
    assert_equal(sysioctl(fd, IOCTL_ECHO_ON), 0);

    if (debug) sysputs("Failure tests: IOCTL with invalid command code...\n");
    assert_equal(sysioctl(fd, 1), SYSERR);
    assert_equal(sysioctl(fd, -1), SYSERR);
    assert_equal(sysioctl(fd, 0), SYSERR);

    if (debug) sysputs("Failure tests: IOCTL with NULL command parameters...\n");
    assert_equal(sysioctl(fd, IOCTL_CHANGE_EOF, NULL), -1);
    assert_equal(sysioctl(fd, IOCTL_CHANGE_EOF, NULL, NULL), -1);

    if (debug) sysputs("Failure tests: IOCTL with closed FD...\n");
    assert_equal(sysclose(fd), 0);
    assert_equal(sysioctl(fd, IOCTL_CHANGE_EOF, 'a'), SYSERR);
    assert_equal(sysioctl(fd, IOCTL_ECHO_OFF), SYSERR);
    assert_equal(sysioctl(fd, IOCTL_ECHO_ON), SYSERR);

    if (debug) kprintf("Finished %s\n", __func__);
}
