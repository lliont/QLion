/** @file src/os/qlcompat.c Small libc additions and fixes for the Sinclair QL build. */

/* qlfix.h is force-included; undo its renames here, this file implements them */
#undef printf
#undef fprintf
#undef sprintf
#undef snprintf
#undef vprintf
#undef vfprintf
#undef vsprintf
#undef vsnprintf

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

int strcasecmp(const char *s1, const char *s2)
{
	int c1, c2;
	do {
		c1 = tolower((unsigned char)*s1++);
		c2 = tolower((unsigned char)*s2++);
	} while (c1 == c2 && c1 != 0);
	return c1 - c2;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
	int c1 = 0, c2 = 0;
	while (n-- > 0) {
		c1 = tolower((unsigned char)*s1++);
		c2 = tolower((unsigned char)*s2++);
		if (c1 != c2 || c1 == 0) break;
	}
	return c1 - c2;
}

/* ------------------------------------------------------------------ */
/* printf family without the 'h' length modifier                       */
/* ------------------------------------------------------------------ */

#define QL_FMT_MAX  512
#define QL_OUT_MAX  2048

/* Copy fmt to out, dropping 'h'/'hh' length modifiers from conversions.
 * C promotes short arguments to int, so %hu == %u and %hd == %d. */
static const char *QL_FixFormat(const char *fmt, char *out, size_t outsize)
{
	size_t o = 0;

	if (strchr(fmt, 'h') == NULL) return fmt;	/* nothing to do (fast path) */

	while (*fmt != '\0' && o + 1 < outsize) {
		char c = *fmt++;
		out[o++] = c;
		if (c != '%') continue;
		if (*fmt == '%') {	/* literal %% */
			if (o + 1 < outsize) out[o++] = *fmt;
			fmt++;
			continue;
		}
		/* flags, width, precision */
		while (*fmt != '\0' && strchr("-+ #0123456789.*", *fmt) != NULL && o + 1 < outsize) {
			out[o++] = *fmt++;
		}
		/* length modifier : drop h and hh */
		while (*fmt == 'h') fmt++;
	}
	out[o] = '\0';
	return out;
}

int ql_vsprintf(char *buf, const char *fmt, va_list ap)
{
	char f[QL_FMT_MAX];
	return vsprintf(buf, QL_FixFormat(fmt, f, sizeof(f)), ap);
}

int ql_vfprintf(FILE *fp, const char *fmt, va_list ap)
{
	char f[QL_FMT_MAX];
	return vfprintf(fp, QL_FixFormat(fmt, f, sizeof(f)), ap);
}

int ql_vprintf(const char *fmt, va_list ap)
{
	return ql_vfprintf(stdout, fmt, ap);
}

/* The QDOS libc has no vsnprintf : format into a large buffer, copy truncated.
 * OpenDUNE only formats short strings (paths, messages). */
static char s_outbuf[QL_OUT_MAX];

int ql_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	int len = ql_vsprintf(s_outbuf, fmt, ap);
	if (len < 0) return len;
	if (size > 0) {
		size_t n = ((size_t)len < size) ? (size_t)len : size - 1;
		memcpy(buf, s_outbuf, n);
		buf[n] = '\0';
	}
	return len;
}

int ql_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = ql_vsprintf(buf, fmt, ap);
	va_end(ap);
	return ret;
}

int ql_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = ql_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return ret;
}

int ql_fprintf(FILE *fp, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = ql_vfprintf(fp, fmt, ap);
	va_end(ap);
	return ret;
}

int ql_printf(const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = ql_vfprintf(stdout, fmt, ap);
	va_end(ap);
	return ret;
}
