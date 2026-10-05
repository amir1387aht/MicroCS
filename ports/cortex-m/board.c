/* MicroCS Cortex-M reference port - virtual board drivers + newlib stubs. */
#include <stddef.h>
#include <errno.h>
#include <sys/stat.h>
#include "board.h"

void board_putc(char c) { EMU_UART_TX = (uint8_t)c; }
void board_write(const char* s, unsigned n) { while (n--) EMU_UART_TX = (uint8_t)*s++; }
void board_puts(const char* s) { while (*s) EMU_UART_TX = (uint8_t)*s++; }
uint32_t board_ticks_ms(void) { return EMU_TICKS_MS; }
uint32_t board_insns(void) { return EMU_INSNS; }
void board_exit(int code) { EMU_EXIT = (uint32_t)code; for (;;) {} }
int board_getc(uint32_t timeout_ms) {
    uint32_t t0 = EMU_TICKS_MS;
    for (;;) {
        uint32_t st = EMU_UART_RXST;
        if (st == 1) return (int)(EMU_UART_RX & 0xFF);
        if (st == 2) return -2;
        if (EMU_TICKS_MS - t0 >= timeout_ms) return -1;
    }
}

/* ---- newlib syscall stubs (no OS). malloc is not used by MicroCS (pool heap),
 * but newlib's printf family may request a small buffer. */
extern char _heap_start, _heap_end;
static char* g_brk = &_heap_start;
void* _sbrk(ptrdiff_t inc) {
    if (g_brk + inc > &_heap_end) { errno = ENOMEM; return (void*)-1; }
    char* p = g_brk; g_brk += inc; return p;
}
int _write(int fd, const char* buf, int n) { (void)fd; board_write(buf, (unsigned)n); return n; }
int _read(int fd, char* buf, int n) { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int w) { (void)fd; (void)off; (void)w; return 0; }
int _fstat(int fd, struct stat* st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
void _exit(int code) { board_exit(code); }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
#include <sys/times.h>
clock_t _times(struct tms* t) { (void)t; return (clock_t)-1; }
int _gettimeofday(void* tv, void* tz) { (void)tv; (void)tz; errno = ENOSYS; return -1; }
