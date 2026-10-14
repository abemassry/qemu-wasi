#ifndef _WASI_TERMIOS_H_STUB
#define _WASI_TERMIOS_H_STUB
/* Comprehensive termios stub for WASI — covers all constants QEMU needs */

/* Also define the standard guard so nobody else tries to include it */
#ifndef _TERMIOS_H
#define _TERMIOS_H
#endif

#include <sys/types.h>

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* c_iflag bits */
#define IGNBRK  0x001
#define BRKINT  0x002
#define IGNPAR  0x004
#define PARMRK  0x008
#define INPCK   0x010
#define ISTRIP  0x020
#define INLCR   0x040
#define IGNCR   0x080
#define ICRNL   0x100
#define IUCLC   0x200
#define IXON    0x400
#define IXANY   0x800
#define IXOFF   0x1000
#define IMAXBEL 0x2000
#define IUTF8   0x4000

/* c_oflag bits */
#define OPOST   0x001
#define OLCUC   0x002
#define ONLCR   0x004
#define OCRNL   0x008
#define ONOCR   0x010
#define ONLRET  0x020
#define OFILL   0x040
#define OFDEL   0x080

/* c_cflag bits */
#define CSIZE   0x030
#define CS5     0x000
#define CS6     0x010
#define CS7     0x020
#define CS8     0x030
#define CSTOPB  0x040
#define CREAD   0x080
#define PARENB  0x100
#define PARODD  0x200
#define HUPCL   0x400
#define CLOCAL  0x800
#define CRTSCTS 0x80000000

/* c_lflag bits */
#define ISIG    0x001
#define ICANON  0x002
#define XCASE   0x004
#define ECHO    0x008
#define ECHOE   0x010
#define ECHOK   0x020
#define ECHONL  0x040
#define NOFLSH  0x080
#define TOSTOP  0x100
#define ECHOCTL 0x200
#define ECHOPRT 0x400
#define ECHOKE  0x800
#define IEXTEN  0x8000

/* c_cc indices — use enum to avoid macro collisions with QEMU identifiers
 * (e.g., VMIN conflicts with x86 VMIN instruction name in i386 TCG decoder
 *  when used in macro token pasting) */
enum {
    VINTR    = 0,
    VQUIT    = 1,
    VERASE   = 2,
    VKILL    = 3,
    VEOF     = 4,
    VTIME    = 5,
    VMIN     = 6,
    VSWTC    = 7,
    VSTART   = 8,
    VSTOP    = 9,
    VSUSP    = 10,
    VEOL     = 11,
    VREPRINT = 12,
    VDISCARD = 13,
    VWERASE  = 14,
    VLNEXT   = 15,
    VEOL2    = 16
};

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflow/tcflush */
#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* Baud rates */
#define B0       0x0000
#define B50      0x0001
#define B75      0x0002
#define B110     0x0003
#define B134     0x0004
#define B150     0x0005
#define B200     0x0006
#define B300     0x0007
#define B600     0x0008
#define B1200    0x0009
#define B1800    0x000A
#define B2400    0x000B
#define B4800    0x000C
#define B9600    0x000D
#define B19200   0x000E
#define B38400   0x000F
#define B57600   0x1001
#define B115200  0x1002
#define B230400  0x1003
#define B460800  0x1004
#define B500000  0x1005
#define B576000  0x1006
#define B921600  0x1007
#define B1000000 0x1008
#define B1152000 0x1009
#define B1500000 0x100A
#define B2000000 0x100B
#define B2500000 0x100C
#define B3000000 0x100D
#define B3500000 0x100E
#define B4000000 0x100F

/* Stub function implementations */
static inline int tcgetattr(int fd, struct termios *t) {
    (void)fd; (void)t;
    return -1;
}

static inline int tcsetattr(int fd, int actions, const struct termios *t) {
    (void)fd; (void)actions; (void)t;
    return -1;
}

static inline speed_t cfgetispeed(const struct termios *t) {
    return t ? t->c_ispeed : 0;
}

static inline speed_t cfgetospeed(const struct termios *t) {
    return t ? t->c_ospeed : 0;
}

static inline int cfsetispeed(struct termios *t, speed_t speed) {
    if (t) t->c_ispeed = speed;
    return 0;
}

static inline int cfsetospeed(struct termios *t, speed_t speed) {
    if (t) t->c_ospeed = speed;
    return 0;
}

static inline void cfmakeraw(struct termios *t) {
    if (!t) return;
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

static inline int tcsendbreak(int fd, int duration) {
    (void)fd; (void)duration;
    return -1;
}

static inline int tcdrain(int fd) {
    (void)fd;
    return -1;
}

static inline int tcflush(int fd, int queue_selector) {
    (void)fd; (void)queue_selector;
    return -1;
}

static inline int tcflow(int fd, int action) {
    (void)fd; (void)action;
    return -1;
}

/* PTY stubs */
static inline int openpty(int *amaster, int *aslave, char *name,
                          const struct termios *termp, void *winp) {
    (void)amaster; (void)aslave; (void)name; (void)termp; (void)winp;
    return -1;
}

static inline char *ptsname(int fd) {
    (void)fd;
    return (char *)0;
}

static inline int grantpt(int fd) {
    (void)fd;
    return -1;
}

static inline int unlockpt(int fd) {
    (void)fd;
    return -1;
}

#endif /* _WASI_TERMIOS_H_STUB */
