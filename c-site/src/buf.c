/* buf.c - Buyuyebilen metin tamponu.
 *
 * Kapasite her doldugunda iki katina cikar; boylece n baytlik bir sayfa
 * en fazla log2(n) kez realloc edilir (amortize O(1) ekleme). */
#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void buf_grow(Buf *b, size_t need)
{
    if (b->len + need + 1 <= b->cap)
        return;
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < b->len + need + 1)
        cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p) {
        perror("realloc");
        abort();
    }
    b->data = p;
    b->cap = cap;
}

void buf_add(Buf *b, const char *s, size_t n)
{
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_puts(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    char probe[1];
    va_start(ap, fmt);
    int n = vsnprintf(probe, sizeof probe, fmt, ap);  /* once gereken boyutu olc */
    va_end(ap);
    if (n < 0)
        return;
    buf_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void buf_reset(Buf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = '\0';
}

void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void buf_html(Buf *b, const char *s)
{
    const char *start = s;
    for (; *s; s++) {
        const char *rep = NULL;
        switch (*s) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&#39;";  break;
        }
        if (rep) {
            buf_add(b, start, (size_t)(s - start));
            buf_puts(b, rep);
            start = s + 1;
        }
    }
    buf_add(b, start, (size_t)(s - start));
}

void buf_json(Buf *b, const char *s)
{
    buf_puts(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n");  break;
        case '\r': buf_puts(b, "\\r");  break;
        case '\t': buf_puts(b, "\\t");  break;
        default:
            if (c < 0x20)
                buf_printf(b, "\\u%04x", c);
            else
                buf_add(b, s, 1);
        }
    }
    buf_puts(b, "\"");
}
