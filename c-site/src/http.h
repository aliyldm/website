/* http.h - HTTP/1.1 istek ayristirma ve yanit gonderme. */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#include "buf.h"

#define MAX_HEADER 16384
#define MAX_BODY   16384

typedef struct {
    char   method[8];
    char   path[512];      /* sorgu dizesi olmadan: /blog */
    char   query[1024];    /* ? isaretinden sonrasi: etiket=c */
    char   cookies[1024];  /* Cookie basliginin ham degeri */
    char   referer[512];
    char   ip[64];
    char  *body;
    size_t body_len;
} Request;

typedef struct {
    int         status;
    const char *content_type;  /* NULL ise text/html */
    char        headers[1024]; /* ek basliklar, her biri \r\n ile biter */
    size_t      headers_len;
    Buf         body;
    const char *label;         /* istatistik sayfasi icin rota adi */
} Response;

/* Soketten bir istek okur. 0: basarili, -1: istemci hicbir sey
 * gondermeden kapatti, >0: gonderilmesi gereken HTTP hata kodu. */
int http_read_request(int fd, Request *req, char *raw, size_t raw_size);

/* Yaniti gonderir, gonderilen toplam bayti dondurur. */
size_t http_send(int fd, const Request *req, const Response *res);

const char *http_status_text(int status);

void res_header(Response *res, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void res_redirect(Response *res, const char *location);

/* application/x-www-form-urlencoded: '+' -> ' ', %XX -> bayt */
void url_decode(const char *src, size_t n, char *dst, size_t dst_size);

/* "a=1&b=2" bicimindeki veriden anahtarin cozulmus degerini alir.
 * Anahtar bulunursa 1, yoksa 0 doner (out bos dize olur). */
int param_get(const char *data, size_t len, const char *key, char *out, size_t out_size);
int query_get(const Request *req, const char *key, char *out, size_t out_size);
int form_get(const Request *req, const char *key, char *out, size_t out_size);
int cookie_get(const Request *req, const char *name, char *out, size_t out_size);

#endif
