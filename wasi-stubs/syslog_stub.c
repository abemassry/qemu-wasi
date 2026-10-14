/*
 * syslog stub implementation for WASI.
 * Variadic functions can't be static inline, so provide in a .c file.
 */

#include <stdio.h>
#include <stdarg.h>

/* Matches declaration in wasi_process_stubs.h */
void syslog(int priority, const char *format, ...) {
    (void)priority;
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}
