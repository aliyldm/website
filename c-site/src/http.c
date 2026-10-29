/* http.c - HTTP/1.1 isteklerini bayt bayt okuyup ayristirir, yanit gonderir.
 *
 * Desteklenenler: istek satiri, Content-Length ile govde, Cookie ve Referer
 * basliklari, sorgu dizesi. Desteklenmeyenler (chunked govde, keep-alive)
 * acikca reddedilir ya da kapatilir. */
#include "http.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>

/* Baslik degerini bastaki/sondaki bosluklari atarak kopyalar. */
static void header_copy(const char *val, const char *end, char *out, size_t size)
{
    while (val < end && (*val == ' ' || *val == '\t'))
        val++;
    while (end > val && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    size_t n = (size_t)(end - val);
    if (n >= size)
        n = size - 1;
    memcpy(out, val, n);
    out[n] = '\0';
}

static int recv_error(size_t got_any)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return got_any ? 408 : -1;  /* zaman asimi */
    return -1;
}

int http_read_request(int fd, Request *req, char *raw, size_t raw_size)
{
    size_t got = 0;
    char *header_end = NULL;

    /* 1) Bos satira (\r\n\r\n) kadar oku */
    while (!header_end) {
        if (got >= MAX_HEADER)
            return 431;
        ssize_t n = recv(fd, raw + got, raw_size - 1 - got, 0);
        if (n == 0)
            return got ? 400 : -1;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return recv_error(got);
        }
        got += (size_t)n;
        raw[got] = '\0';
        header_end = strstr(raw, "\r\n\r\n");
    }
    if ((size_t)(header_end - raw) + 4 > MAX_HEADER)
        return 431;  /* tek bir recv() ile sinirin otesi gelmis olabilir */

    /* 2) Istek satiri: METHOD /hedef HTTP/1.x */
    char *eol = strstr(raw, "\r\n");
    char *sp1 = memchr(raw, ' ', (size_t)(eol - raw));
    if (!sp1)
        return 400;
    char *sp2 = memchr(sp1 + 1, ' ', (size_t)(eol - sp1 - 1));
    if (!sp2)
        return 400;

    size_t mlen = (size_t)(sp1 - raw);
    if (mlen == 0)
        return 400;
    if (mlen >= sizeof req->method)
        return 501;
    for (size_t i = 0; i < mlen; i++)
        if (!isupper((unsigned char)raw[i]))
            return 400;
    memcpy(req->method, raw, mlen);
    req->method[mlen] = '\0';

    const char *version = sp2 + 1;
    if (eol - version != 8 || strncmp(version, "HTTP/1.", 7) != 0)
        return 400;

    const char *target = sp1 + 1;
    size_t tlen = (size_t)(sp2 - target);
    if (tlen == 0 || target[0] != '/')
        return 400;
    const char *qm = memchr(target, '?', tlen);
    size_t plen = qm ? (size_t)(qm - target) : tlen;
    size_t qlen = qm ? tlen - plen - 1 : 0;
    if (plen >= sizeof req->path || qlen >= sizeof req->query)
        return 414;
    memcpy(req->path, target, plen);
    req->path[plen] = '\0';
    if (qm)
        memcpy(req->query, qm + 1, qlen);
    req->query[qlen] = '\0';

    /* 3) Basliklar */
    size_t content_length = 0;
    for (char *line = eol + 2; line < header_end;) {
        char *le = strstr(line, "\r\n");
        char *colon = memchr(line, ':', (size_t)(le - line));
        if (colon) {
            size_t nlen = (size_t)(colon - line);
            const char *val = colon + 1;
            if (nlen == 14 && strncasecmp(line, "Content-Length", 14) == 0) {
                char tmp[32], *end;
                header_copy(val, le, tmp, sizeof tmp);
                errno = 0;
                unsigned long v = strtoul(tmp, &end, 10);
                if (!isdigit((unsigned char)tmp[0]) || *end || errno)
                    return 400;
                content_length = v;
            } else if (nlen == 17 && strncasecmp(line, "Transfer-Encoding", 17) == 0) {
                return 501;  /* chunked govde desteklenmiyor */
            } else if (nlen == 6 && strncasecmp(line, "Cookie", 6) == 0) {
                header_copy(val, le, req->cookies, sizeof req->cookies);
            } else if (nlen == 7 && strncasecmp(line, "Referer", 7) == 0) {
                header_copy(val, le, req->referer, sizeof req->referer);
            }
        }
        line = le + 2;
    }

    /* 4) Govde: tam olarak Content-Length kadar bayt */
    if (content_length > MAX_BODY)
        return 413;
    char *body = header_end + 4;
    size_t header_len = (size_t)(body - raw);
    if (header_len + content_length + 1 > raw_size)
        return 413;
    size_t have = got - header_len;
    while (have < content_length) {
        ssize_t n = recv(fd, body + have, content_length - have, 0);
        if (n == 0)
            return 400;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return recv_error(1);
        }
        have += (size_t)n;
    }
    body[content_length] = '\0';
    req->body = body;
    req->body_len = content_length;
    return 0;
}

const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 301: return "Moved Permanently";
    case 303: return "See Other";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default:  return "Internal Server Error";
    }
}

static size_t send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        sent += (size_t)n;
    }
    return sent;
}

size_t http_send(int fd, const Request *req, const Response *res)
{
    char date[64];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S GMT", &tm);

    Buf h = {0};
    buf_printf(&h,
               "HTTP/1.1 %d %s\r\n"
               "Server: el-yapimi-c/2.0\r\n"
               "Date: %s\r\n"
               "Content-Type: %s\r\n"
               "Content-Length: %zu\r\n"
               "Connection: close\r\n"
               "X-Content-Type-Options: nosniff\r\n"
               "X-Frame-Options: DENY\r\n"
               "Referrer-Policy: same-origin\r\n"
               "Content-Security-Policy: default-src 'self'; img-src 'self' data:; "
               "form-action 'self'; frame-ancestors 'none'\r\n",
               res->status, http_status_text(res->status), date,
               res->content_type ? res->content_type : "text/html; charset=utf-8",
               res->body.len);
    buf_add(&h, res->headers, res->headers_len);
    buf_puts(&h, "\r\n");

    size_t sent = send_all(fd, h.data, h.len);
    if (strcmp(req->method, "HEAD") != 0 && res->body.len)
        sent += send_all(fd, res->body.data, res->body.len);
    buf_free(&h);
    return sent;
}

void res_header(Response *res, const char *fmt, ...)
{
    size_t room = sizeof res->headers - res->headers_len;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(res->headers + res->headers_len, room, fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n < room)
        res->headers_len += (size_t)n;
    else
        res->headers[res->headers_len] = '\0';  /* sigmadi: yok say */
}

void res_redirect(Response *res, const char *location)
{
    res->status = 303;
    res_header(res, "Location: %s\r\n", location);
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode(const char *src, size_t n, char *dst, size_t dst_size)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < dst_size; i++) {
        if (src[i] == '+') {
            dst[o++] = ' ';
        } else if (src[i] == '%' && i + 2 < n &&
                   hex_value(src[i + 1]) >= 0 && hex_value(src[i + 2]) >= 0) {
            dst[o++] = (char)(hex_value(src[i + 1]) * 16 + hex_value(src[i + 2]));
            i += 2;
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

int param_get(const char *data, size_t len, const char *key, char *out, size_t out_size)
{
    size_t klen = strlen(key);
    const char *p = data, *end = data + len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *pair_end = amp ? amp : end;
        const char *eq = memchr(p, '=', (size_t)(pair_end - p));
        if (eq && (size_t)(eq - p) == klen && memcmp(p, key, klen) == 0) {
            url_decode(eq + 1, (size_t)(pair_end - eq - 1), out, out_size);
            return 1;
        }
        p = pair_end + 1;
    }
    out[0] = '\0';
    return 0;
}

int query_get(const Request *req, const char *key, char *out, size_t out_size)
{
    return param_get(req->query, strlen(req->query), key, out, out_size);
}

int form_get(const Request *req, const char *key, char *out, size_t out_size)
{
    if (!req->body) {
        out[0] = '\0';
        return 0;
    }
    return param_get(req->body, req->body_len, key, out, out_size);
}

int cookie_get(const Request *req, const char *name, char *out, size_t out_size)
{
    size_t nlen = strlen(name);
    const char *p = req->cookies;
    while (*p) {
        while (*p == ' ' || *p == ';')
            p++;
        const char *end = strchr(p, ';');
        if (!end)
            end = p + strlen(p);
        if ((size_t)(end - p) > nlen && strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            size_t vlen = (size_t)(end - p) - nlen - 1;
            if (vlen >= out_size)
                vlen = out_size - 1;
            memcpy(out, p + nlen + 1, vlen);
            out[vlen] = '\0';
            return 1;
        }
        p = end;
    }
    out[0] = '\0';
    return 0;
}
