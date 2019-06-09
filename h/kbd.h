/* kbd.h */

#include <xeroskernel.h>

#ifndef KBD_H
#define KBD_H

// GIVEN scanCodesToAscii.txt CODE STARTS (minus extended mode macros)

#define KEY_UP   0x80            /* If this bit is on then it is a key   */
/* up event instead of a key down event */

/* Control code */
#define LSHIFT  0x2a
#define RSHIFT  0x36
#define LMETA   0x38

#define LCTL    0x1d
#define CAPSL   0x3a

/* scan state flags */
#define INCTL           0x01    /* control key is down          */
#define INSHIFT         0x02    /* shift key is down            */
#define CAPSLOCK        0x04    /* caps lock mode               */
#define INMETA          0x08    /* meta (alt) key is down       */

#define NOCHAR  256

// GIVEN scanCodesToAscii.txt CODE ENDS

#define DEFAULT_EOF 0x04

// Internally buffer up to 4 chars via a circular buffer
// Use 1 index to indicate a full buffer
#define KBD_BUFFER_SIZE (4 + 1)
#define KEYBOARD_IRQ 1

// Port 0x60 is where data is read from
#define DATA_PORT 0x60
// Commands and control information are read/written to port 0x64
#define CONTROL_PORT 0x64
#define CONTROL_PORT_READY_MASK 0x01

#define IOCTL_CHANGE_EOF 53
#define IOCTL_ECHO_OFF 55
#define IOCTL_ECHO_ON 56

/*===================== KEYBOARD DEVICE DRIVER UPPER HALF =========================*/
void kbd_devsw_init(devsw_t *devsw, dev_t kbd);
// Called to setup the device
int kbdinit(void);
// Sets up device access
int kbdopen(pcb_t *proc, int device_no);
// Terminates device access
int kbdclose(pcb_t *proc);
int kbdread(pcb_t *proc, void *buf, int buflen);
int kbdwrite(pcb_t *proc, void *buf, int buflen);
// Passes special control information
int kbdioctl(pcb_t *proc, unsigned long command, void *ioctl_args);

/*===================== KEYBOARD DEVICE DRIVER LOWER HALF =========================*/
// ISR for keyboard
void kbd_isr(void);

#endif
