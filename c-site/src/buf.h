/* buf.h - Buyuyebilen metin tamponu ve kacirma (escape) yardimcilari. */
#ifndef BUF_H
#define BUF_H

#include <stddef.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buf;

void buf_add(Buf *b, const char *s, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_reset(Buf *b);
void buf_free(Buf *b);

/* Metni HTML icine guvenle yazar: & < > " ' kacirilir (XSS korumasi). */
void buf_html(Buf *b, const char *s);

/* Metni tirnakli bir JSON dizesi olarak yazar. */
void buf_json(Buf *b, const char *s);

#endif
