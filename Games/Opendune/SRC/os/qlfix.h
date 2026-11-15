/** @file src/os/qlfix.h
 * Force-included into every file of the QL build (-include in CFLAGS).
 *
 * The QDOS libc reads only 16 bits for printf's 'h' length modifier
 * (%hu, %hd). C passes shorts as 32-bit ints, so on the big-endian 68000
 * it picks up the upper half and prints 0. These macros route the printf
 * family to versions in qlcompat.c that remove the 'h' first.
 */
#ifndef QLFIX_H
#define QLFIX_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

extern int ql_printf(const char *fmt, ...);
extern int ql_fprintf(FILE *f, const char *fmt, ...);
extern int ql_sprintf(char *buf, const char *fmt, ...);
extern int ql_snprintf(char *buf, size_t size, const char *fmt, ...);
extern int ql_vprintf(const char *fmt, va_list ap);
extern int ql_vfprintf(FILE *f, const char *fmt, va_list ap);
extern int ql_vsprintf(char *buf, const char *fmt, va_list ap);
extern int ql_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#define printf    ql_printf
#define fprintf   ql_fprintf
#define sprintf   ql_sprintf
#define snprintf  ql_snprintf
#define vprintf   ql_vprintf
#define vfprintf  ql_vfprintf
#define vsprintf  ql_vsprintf
#define vsnprintf ql_vsnprintf

#endif /* QLFIX_H */
