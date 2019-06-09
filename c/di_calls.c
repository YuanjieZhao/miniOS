/* di_calls.c : DI calls of device independent interface (DII)
 */

#include <xeroskernel.h>
#include <kbd.h>

/*-----------------------------------------------------------------------------------
 * This where the DI (device independent) calls of the DII (device independent
 * interface) live. These are corresponding calls to be invoked by the dispatcher
 * for each device application level system call.
 *
 * The parameters to these calls are the process that called the corresponding system
 * call, along with the parameters of the corresponding system call.
 *
 * List of functions that are called from outside this file:
 * - kdiinit
 *   - Initializes the device table
 * - di_open
 *   - DII call for sysopen
 * - di_close
 *   - DII call for sysclose
 * - di_write
 *   - DII call for syswrite
 * - di_read
 *   - DII call for sysread
 * - di_ioctl
 *   - DII call for sysioctl
 *-----------------------------------------------------------------------------------
 */

static int is_valid_fd(pcb_t *proc, int fd);

/*-----------------------------------------------------------------------------------
 * The device table contains 2 devices. Both entries are for the keyboard.
 *
 * The device 0 version of the keyboard will not, by default, echo the characters as
 * they arrive. This means that if the characters need to be displayed, the
 * application will have to do it.
 *
 * Device 1 will, by default, echo the characters. This means that the character
 * could be displayed before the application has actually read the character.
 *
 * Even though these are separate devices, only one of them is allowed to be open at
 * a time.
 *-----------------------------------------------------------------------------------
 */
static devsw_t dev_table[DEVICE_TABLE_SIZE];

/*-----------------------------------------------------------------------------------
 * Initializes the device table.
 *-----------------------------------------------------------------------------------
 */
void kdiinit(void) {
    kprintf("Starting kdiinit...\n");
    kbd_devsw_init(&dev_table[KBD_0], KBD_0);
    kbd_devsw_init(&dev_table[KBD_1], KBD_1);

    dev_table[KBD_0].dvinit();
    dev_table[KBD_1].dvinit();
    kprintf("Finished kdiinit\n");
}

/*-----------------------------------------------------------------------------------
 * DII call for sysopen.
 *
 * @param proc      The process that called sysopen
 * @param device_no The major device number
 * @return          A file descriptor in the range 0 to 3 (inclusive) on success,
 *                  -1 if the open fails
 *-----------------------------------------------------------------------------------
 */
int di_open(pcb_t *proc, int device_no) {
    // Verify that the major number is in the valid range
    if (device_no >= 0 && device_no < DEVICE_TABLE_SIZE) {
        // Check if there is a FDT entry available
        int fd;
        for (fd = 0; fd < FD_TABLE_SIZE; fd++) {
            if (proc->fd_table[fd] == NULL) break;
        }
        if (fd == FD_TABLE_SIZE) {
            return -1;
        }
        // Allocate FDT entry
        // Locate the device block with major device number
        devsw_t *devsw = &dev_table[device_no];
        // Call the device specific dvopen function pointed to by the device block
        if (devsw->dvopen(proc, device_no)) {
            return -1;
        }
        // Add the entry to the file descriptor table in the PCB
        proc->fd_table[fd] = devsw;
        // Return index of selected FDT to process
        return fd;
    } else {
        return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * DII call for sysclose.
 *
 * @param proc The process that called sysclose
 * @param fd   The file descriptor (fd) from a previously successful open call
 * @return     0 on success, -1 on failure
 *-----------------------------------------------------------------------------------
 */
int di_close(pcb_t *proc, int fd) {
    if (is_valid_fd(proc, fd)) {
        devsw_t *devsw = proc->fd_table[fd];
        if (devsw->dvclose(proc)) {
            return -1;
        }
        proc->fd_table[fd] = NULL;
        return 0;
    } else {
        return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * DII call for syswrite.
 *
 * @param proc   The process that called syswrite
 * @param fd     The provided file descriptor
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       The number of bytes written on success, -1 if there was an error
 *-----------------------------------------------------------------------------------
 */
int di_write(pcb_t *proc, int fd, void *buf, int buflen) {
    if (valid_buf(buf, buflen) && is_valid_fd(proc, fd)) {
        devsw_t *devsw = proc->fd_table[fd];
        return devsw->dvwrite(proc, buf, buflen);
    } else {
        return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * DII call for sysread.
 *
 * @param proc   The process that called sysread
 * @param fd     The provided file descriptor
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       The number of bytes read on success
 *               0 to indicate end-of-file (EOF)
 *               -1 if there was an error
 *               -2 if the sysread call should block
 *-----------------------------------------------------------------------------------
 */
int di_read(pcb_t *proc, int fd, void *buf, int buflen) {
    if (valid_buf(buf, buflen) && is_valid_fd(proc, fd)) {
        devsw_t *devsw = proc->fd_table[fd];
        return devsw->dvread(proc, buf, buflen);
    } else {
        return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * DII call for sysioctl.
 *
 * @param proc       The process that called sysioctl
 * @param fd         The provided file descriptor
 * @param command    The control command
 * @param ioctl_args Additional device specific parameters
 * @return           0 on success, -1 if there was an error
 *-----------------------------------------------------------------------------------
 */
int di_ioctl(pcb_t *proc, int fd, unsigned long command, void *ioctl_args) {
    if (is_valid_fd(proc, fd)) {
        devsw_t *devsw = proc->fd_table[fd];
        return devsw->dvioctl(proc, command, ioctl_args);
    } else {
        return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * Verifies that the passed in file descriptor is in the valid range and corresponds
 * to an opened device.
 *
 * @param proc The process with fd open
 * @param fd   The provided file descriptor
 * @return     1 if the passed in file descriptor is valid, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int is_valid_fd(pcb_t *proc, int fd) {
    return fd >= 0 && fd < FD_TABLE_SIZE && proc->fd_table[fd] != NULL;
}
