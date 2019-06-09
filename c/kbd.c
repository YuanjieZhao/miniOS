/* kbd.c : keyboard specific device driver calls */

#include <xeroskernel.h>
#include <kbd.h>
#include <xeroslib.h>
#include <i386.h>
#include <stdarg.h>

/*-----------------------------------------------------------------------------------
 * This is where keyboard specific device driver calls live. Each of the device
 * independent calls will call the corresponding device specific device driver
 * function.
 *
 * List of functions that are called from outside this file:
 * - kbd_devsw_init
 *   - Initializes the keyboard device
 * - kbdinit
 *   - Keyboard specific call for di_init
 * - kbdopen
 *   - Keyboard specific call for di_open
 * - kbdclose
 *   - Keyboard specific call for di_close
 * - kbdread
 *   - Keyboard specific call for di_read
 * - kbdwrite
 *   - Keyboard specific call for di_write
 * - kbdioctl
 *   - Keyboard specific call for di_ioctl
 * - kbd_isr
 *   - Keyboard ISR
 *-----------------------------------------------------------------------------------
 */

static void finish_read(void);
static void kbd_reset(void);
static void flush_kbd_buf(void);
static int is_kbd_buf_full(void);
static void write_to_kbd_buf(char c);
static int transfer_to_read_buf(void);
static int copy_char_to_read_buf(char c);
static void handle_eof(void);
static void disable_keyboard_hardware(void);
static int kbdioctl_change_eof(void *ioctl_args);
static unsigned int kbtoa(unsigned char code);

// Internally buffer up to 4 chars via a circular buffer
// Use 1 index to indicate a full buffer
static char kbd_buf[KBD_BUFFER_SIZE];
// The next position in the buffer to place new char (moving from left to right)
static int kbd_buf_head;
// The ending position of buffered chars
static int kbd_buf_tail;

// Application read buffer, NULL if sysread has not been called
static char *read_buf;
// Application read buffer length
static int read_buflen;
// Chars transferred to application read buffer
int chars_transferred;
// Whether the read request has been fully serviced
static int read_finished;

// Current EOF char
static unsigned char eof;
static int eof_flag;

// echo_flag and kbd_proc are not reset by kbd_reset
static int echo_flag;
// The process that is using the keyboard, NULL if it is not in use
static pcb_t *kbd_proc;

static int kbd_state; /* the state of the keyboard */

/*===================== KEYBOARD DEVICE DRIVER UPPER HALF =========================*/

/*-----------------------------------------------------------------------------------
 * Initializes the given keyboard device structure with the given kbd.
 *
 * @param kbd   The keyboard device type, one of KBD_0 or KBD_1
 * @param devsw The device structure
 *-----------------------------------------------------------------------------------
 */
void kbd_devsw_init(devsw_t *devsw, dev_t kbd) {
    if (kbd == KBD_0) {
        devsw->dvname = "/dev/keyboard0";
        devsw->dvnum = KBD_0;
    } else {
        devsw->dvname = "/dev/keyboard1";
        devsw->dvnum = KBD_1;
    }
    devsw->dvinit = &kbdinit;
    devsw->dvopen = &kbdopen;
    devsw->dvclose = &kbdclose;
    devsw->dvread = &kbdread;
    devsw->dvwrite = &kbdwrite;
    devsw->dvioctl = &kbdioctl;
}

/*-----------------------------------------------------------------------------------
 * Initializes the keyboard device.
 *
 * @return 0 on success
 *-----------------------------------------------------------------------------------
 */
int kbdinit(void) {
    kbd_reset();
    // Read from data and control ports to handle previous interrupts
    inb(DATA_PORT);
    inb(CONTROL_PORT);
    return 0;
}

/*-----------------------------------------------------------------------------------
 * Keyboard specific call for di_open. Sets up the device access.
 *
 * @param proc      The process that called sysopen
 * @param device_no The major device number
 * @return          0 on success, -1 on failure
 *-----------------------------------------------------------------------------------
 */
int kbdopen(pcb_t *proc, int device_no) {
    // Only one process is allowed to use the keyboard at a time
    if (kbd_proc) {
        return -1;
    }

    kbd_reset();
    echo_flag = device_no == KBD_0 ? 0 : 1;
    kbd_proc = proc;

    // Enable keyboard
    outb(CONTROL_PORT, 0xAE);

    // Enable the keyboard interrupts through the APIC
    enable_irq(KEYBOARD_IRQ, 0);

    return 0;
}

/*-----------------------------------------------------------------------------------
 * Keyboard specific call for di_close. Terminates device access.
 *
 * @param proc The process that called sysclose
 * @return     0 on success, -1 on failure
 *-----------------------------------------------------------------------------------
 */
int kbdclose(pcb_t *proc) {
    kbd_reset();
    echo_flag = 0;
    kbd_proc = NULL;

    disable_keyboard_hardware();

    return 0;
}

/*-----------------------------------------------------------------------------------
 * Keyboard specific call for di_write. Since writes are not supported to the
 * keyboard, all calls to syswrite will result in an error indication (-1) being
 * returned.
 *
 * @param proc   The process that called syswrite
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       -1 as writes are not supported to the keyboard
 *-----------------------------------------------------------------------------------
 */
int kbdwrite(pcb_t *proc, void *buf, int buflen) {
    return -1;
}

/*-----------------------------------------------------------------------------------
 * Keyboard specific call for di_read.
 *
 * @param proc   The process that called sysread
 * @param buf    The buffer to write from
 * @param buflen The upper limit of bytes to write from buf
 * @return       The number of bytes read on success
 *               0 to indicate end-of-file (EOF)
 *               -1 if there was an error
 *               -2 if the sysread call should block
 *-----------------------------------------------------------------------------------
 */
int kbdread(pcb_t *proc, void *buf, int buflen) {
    // An EOF was typed: no more input follows, return an end-of-file (EOF) indication (i.e. a 0 on a sysread call)
    // Subsequent sysread operations on this descriptor should continue to return the EOF indication
    if (eof_flag) {
        return 0;
    }

    read_buf = (char *) buf;
    read_buflen = buflen;

    transfer_to_read_buf();
    if (chars_transferred == read_buflen) {
        return read_buflen;
    } else {
        // Block calling process until the request is fully serviced
        read_finished = 0;
        return -2;
    }
}

/*-----------------------------------------------------------------------------------
 * Keyboard specific call for di_ioctl.
 *
 * @param proc       The process that called sysioctl
 * @param command    The control command
 * @param ioctl_args Additional parameters
 * @return           0 on success, -1 if there was an error
 *-----------------------------------------------------------------------------------
 */
int kbdioctl(pcb_t *proc, unsigned long command, void *ioctl_args) {
    switch (command) {
        case (IOCTL_CHANGE_EOF):
            // Change the char that indicates an EOF
            return kbdioctl_change_eof(ioctl_args);
        case (IOCTL_ECHO_OFF):
            // Turn echoing off
            echo_flag = 0;
            return 0;
        case (IOCTL_ECHO_ON):
            // Turn echoing on
            echo_flag = 1;
            return 0;
        default:
            // Invalid IOCTL request
            return -1;
    }
}

/*-----------------------------------------------------------------------------------
 * Finishes a sysread request.
 *-----------------------------------------------------------------------------------
 */
static void finish_read(void) {
    kbd_proc->result_code = chars_transferred;
    chars_transferred = 0;
    ready(kbd_proc);
}

/*===================== KEYBOARD DEVICE DRIVER LOWER HALF =========================*/

/*-----------------------------------------------------------------------------------
 * Resets the keyboard to its initial state.
 *-----------------------------------------------------------------------------------
 */
static void kbd_reset(void) {
    flush_kbd_buf();

    read_buf = NULL;
    read_buflen = 0;
    chars_transferred = 0;
    read_finished = 0;

    eof = DEFAULT_EOF;
    eof_flag = 0;

    kbd_state = 0;
}

/*-----------------------------------------------------------------------------------
 * Flushes the kernel read buffer.
 *-----------------------------------------------------------------------------------
 */
static void flush_kbd_buf(void) {
    for (int i = 0; i < KBD_BUFFER_SIZE; i++) {
        kbd_buf[i] = 0;
    }
    kbd_buf_head = 0;
    kbd_buf_tail = 0;
}

/*-----------------------------------------------------------------------------------
 * The keyboard interrupt service routine (ISR). Called whenever there is a keyboard
 * interrupt.
 *
 * Assumes that kbd_proc != NULL as kbd_isr should only be triggered if kbdopen has
 * been called.
 *-----------------------------------------------------------------------------------
 */
void kbd_isr(void) {
    assert(kbd_proc != NULL, "kbd_isr: kbd_proc is NULL");
    // To see if there is data present read a byte from port 0x64
    // If the low order bit is 1 then there is data ready to be read from port 0x60
    int is_data_present = CONTROL_PORT_READY_MASK & inb(CONTROL_PORT);

    if (is_data_present) {
        // Read a byte from port 0x60
        int data = inb(DATA_PORT);
        unsigned int c = kbtoa(data);

        // kbtoa returns an unsigned int
        // In the case of a key-up event, the return value of kbtoa is larger than the upper bound of char type
        if (c > 0 && c <= 127) {
            write_to_kbd_buf(c);
            // If the echo version of the keyboard is opened, then each typed
            // char is echoed to the screen as soon as it arrives
            if (echo_flag) kprintf("%c", c);
            if (read_buf && !read_finished) {
                read_finished = transfer_to_read_buf();
                if (read_finished && kbd_proc->blocked_queue == READ) finish_read();
            }
        }
    }
}

static int is_kbd_buf_full(void) {
    return ((kbd_buf_head + 1) % KBD_BUFFER_SIZE) == kbd_buf_tail;
}

/*-----------------------------------------------------------------------------------
 * Writes a char to the internal buffer.
 *
 * @param c The char to write to the internal buffer
 *-----------------------------------------------------------------------------------
 */
static void write_to_kbd_buf(char c) {
    // If the buffer is full, subsequent char arrivals are discarded and not
    // displayed until buffer space becomes available
    // If an EOF is received while the buffer is full, it is discarded like any other char
    if (!is_kbd_buf_full()) {
        kbd_buf[kbd_buf_head] = c;
        kbd_buf_head = (kbd_buf_head + 1) % (KBD_BUFFER_SIZE);
    }
}

/*-----------------------------------------------------------------------------------
 * Copy data from the kernel buffer to the application buffer passed as the parameter
 * to sysread.
 *
 * Requires that kbd_proc != NULL && read_buf != NULL, ie. kbd_proc is blocked on a
 * sysread.
 *
 * @return 1 if the read has been fully serviced, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int transfer_to_read_buf(void) {
    assert(kbd_proc != NULL, "transfer_to_read_buf: kbd_proc is NULL");
    assert(read_buf != NULL, "transfer_to_read_buf: read_buf is NULL");
    while (kbd_buf_tail != kbd_buf_head) {
        char c = kbd_buf[kbd_buf_tail];
        kbd_buf_tail = (kbd_buf_tail + 1) % (KBD_BUFFER_SIZE);
        if (c == eof) {
            handle_eof();
            return 1;
        }
        if (copy_char_to_read_buf(c)) return 1;
    }
    return 0;
}

/*-----------------------------------------------------------------------------------
 * Copy a byte to the application buffer.
 *
 * @return 1 if the read has been fully serviced, 0 otherwise
 *-----------------------------------------------------------------------------------
 */
static int copy_char_to_read_buf(char c) {
    read_buf[chars_transferred] = c;
    chars_transferred++;
    // The buffer passed in to sysread is full as specified by the size parameter to the read call,
    // or the "Enter key" is encountered
    return chars_transferred == read_buflen || c == '\n';
}

/*-----------------------------------------------------------------------------------
 * Handle EOF char.
 *-----------------------------------------------------------------------------------
 */
static void handle_eof(void) {
    disable_keyboard_hardware();
    eof_flag = 1;
}

/*-----------------------------------------------------------------------------------
 * Disables the keyboard hardware. Disables keyboard and interrupts for the keyboard
 * controller.
 *-----------------------------------------------------------------------------------
 */
static void disable_keyboard_hardware(void) {
    // Disable keyboard
    outb(CONTROL_PORT, 0xAD);

    // Disable the keyboard interrupts through the APIC
    enable_irq(KEYBOARD_IRQ, 1);
}

/*-----------------------------------------------------------------------------------
 * Attempts to change the EOF char given the additional parameters.
 * @param ioctl_args The IOCTL args
 *-----------------------------------------------------------------------------------
 */
static int kbdioctl_change_eof(void *ioctl_args) {
    if (ioctl_args == NULL) {
        return -1;
    } else {
        va_list change_eof_args = (va_list) ioctl_args;
        int c = va_arg(change_eof_args, int);
        va_end(change_eof_args);
        if (c > 0 && c <= 127) {
            eof = (char) c;
            return 0;
        } else {
            return -1;
        }
    }
}

// GIVEN scanCodesToAscii.txt CODE STARTS (minus extended mode and uses of printf)

/*  Normal table to translate scan code  */
static unsigned char kbcode[] = {0,
                                 27, '1', '2', '3', '4', '5', '6', '7', '8', '9',
                                 '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't',
                                 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a',
                                 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
                                 '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
                                 ',', '.', '/', 0, 0, 0, ' '};

/* captialized ascii code table to tranlate scan code */
static unsigned char kbshift[] = {0,
                                  0, '!', '@', '#', '$', '%', '^', '&', '*', '(',
                                  ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T',
                                  'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A',
                                  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',
                                  '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M',
                                  '<', '>', '?', 0, 0, 0, ' '};

/* extended ascii code table to translate scan code */
static unsigned char kbctl[] = {0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 31, 0, '\b', '\t', 17, 23, 5, 18, 20,
                                25, 21, 9, 15, 16, 27, 29, '\n', 0, 1,
                                19, 4, 6, 7, 8, 10, 11, 12, 0, 0,
                                0, 0, 28, 26, 24, 3, 22, 2, 14, 13};

/* Converts a keyboard scan code to an ascii character code */
static unsigned int kbtoa(unsigned char code) {
    unsigned int ch;

    if (code & KEY_UP) {
        switch (code & 0x7f) {
            case LSHIFT:
            case RSHIFT:
                kbd_state &= ~INSHIFT;
                break;
            case CAPSL:
                // Capslock off detected
                kbd_state &= ~CAPSLOCK;
                break;
            case LCTL:
                kbd_state &= ~INCTL;
                break;
            case LMETA:
                kbd_state &= ~INMETA;
                break;
        }

        return NOCHAR;
    }

    /* check for special keys */
    switch (code) {
        case LSHIFT:
        case RSHIFT:
            kbd_state |= INSHIFT;
            // Shift detected
            return NOCHAR;
        case CAPSL:
            kbd_state |= CAPSLOCK;
            // Capslock ON detected
            return NOCHAR;
        case LCTL:
            kbd_state |= INCTL;
            return NOCHAR;
        case LMETA:
            kbd_state |= INMETA;
            return NOCHAR;
    }

    ch = NOCHAR;

    if (code < sizeof(kbcode)) {
        if (kbd_state & CAPSLOCK)
            ch = kbshift[code];
        else
            ch = kbcode[code];
    }
    if (kbd_state & INSHIFT) {
        if (code >= sizeof(kbshift))
            return NOCHAR;
        if (kbd_state & CAPSLOCK)
            ch = kbcode[code];
        else
            ch = kbshift[code];
    }
    if (kbd_state & INCTL) {
        if (code >= sizeof(kbctl))
            return NOCHAR;
        ch = kbctl[code];
    }
    if (kbd_state & INMETA)
        ch += 0x80;
    return ch;
}

// GIVEN scanCodesToAscii.txt CODE ENDS
