/*
 * server.c - Sifirdan yazilmis, frameworksuz basit bir web sitesi.
 *
 * Sadece standart C kutuphanesi ve POSIX soketleri kullanilir.
 * HTTP istegini okuma, ayristirma, yonlendirme, HTML/CSS uretimi,
 * form verisi cozme ve ziyaretci defteri kaydi tamamen elle yapilir.
 *
 * Derleme:  make            (ya da: cc -O2 -Wall -o server server.c)
 * Calistir: ./server [port] (varsayilan port: 8080)
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_HEADER   16384
#define MAX_BODY     8192
#define MAX_ENTRIES  100
#define NAME_LEN     64
#define MSG_LEN      512
#define DEFTER_FILE  "defter.txt"

/* ------------------------------------------------------------------ */
/* Dinamik metin tamponu                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buf;

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
        exit(1);
    }
    b->data = p;
    b->cap = cap;
}

static void buf_add(Buf *b, const char *s, size_t n)
{
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_puts(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

static void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    buf_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

/* HTML icine guvenli yazmak icin ozel karakterleri kacirir (XSS'e karsi). */
static void buf_html(Buf *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&':  buf_puts(b, "&amp;");  break;
        case '<':  buf_puts(b, "&lt;");   break;
        case '>':  buf_puts(b, "&gt;");   break;
        case '"':  buf_puts(b, "&quot;"); break;
        case '\'': buf_puts(b, "&#39;");  break;
        default:   buf_add(b, s, 1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HTTP istegi                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    char   method[8];
    char   path[1024];
    char  *body;
    size_t body_len;
} Request;

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* application/x-www-form-urlencoded cozumu: '+' -> ' ', %XX -> bayt */
static void url_decode(const char *src, size_t n, char *dst, size_t dst_size)
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

/* "ad=Ali&mesaj=Selam" gibi bir govdeden anahtarin degerini bulur. */
static int form_get(const char *body, size_t len, const char *key,
                    char *out, size_t out_size)
{
    size_t klen = strlen(key);
    const char *p = body, *end = body + len;
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

/* Ham baytlari soketten okur, istek satirini ve govdeyi ayristirir.
 * Basarida 0, hatali istekte -1 doner. */
static int read_request(int fd, Request *req, char *raw, size_t raw_size)
{
    size_t got = 0;
    char *header_end = NULL;

    while (!header_end) {
        if (got + 1 >= raw_size)
            return -1;
        ssize_t n = recv(fd, raw + got, raw_size - got - 1, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
        raw[got] = '\0';
        header_end = strstr(raw, "\r\n\r\n");
    }

    /* Istek satiri: METHOD /yol HTTP/1.1 */
    char version[16];
    if (sscanf(raw, "%7s %1023s %15s", req->method, req->path, version) != 3)
        return -1;
    if (strncmp(version, "HTTP/", 5) != 0)
        return -1;

    /* Sorgu dizesini (?a=b) yonlendirme icin at */
    char *q = strchr(req->path, '?');
    if (q)
        *q = '\0';

    /* Content-Length basligini bul */
    size_t content_length = 0;
    for (char *line = strstr(raw, "\r\n") + 2; line < header_end;) {
        char *eol = strstr(line, "\r\n");
        if (strncasecmp(line, "Content-Length:", 15) == 0)
            content_length = strtoul(line + 15, NULL, 10);
        line = eol + 2;
    }
    if (content_length > MAX_BODY)
        return -1;

    /* Govdenin geri kalanini oku */
    char *body = header_end + 4;
    size_t have = got - (size_t)(body - raw);
    size_t header_len = (size_t)(body - raw);
    if (header_len + content_length + 1 > raw_size)
        return -1;
    while (have < content_length) {
        ssize_t n = recv(fd, body + have, content_length - have, 0);
        if (n <= 0)
            return -1;
        have += (size_t)n;
    }
    body[content_length] = '\0';
    req->body = body;
    req->body_len = content_length;
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP yaniti                                                         */
/* ------------------------------------------------------------------ */

static void send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return;
        }
        data += n;
        len -= (size_t)n;
    }
}

static void send_response(int fd, const Request *req, int status,
                          const char *status_text, const char *content_type,
                          const char *extra_headers, const Buf *body)
{
    Buf h = {0};
    size_t body_len = body ? body->len : 0;
    buf_printf(&h,
               "HTTP/1.1 %d %s\r\n"
               "Server: el-yapimi-c/1.0\r\n"
               "Content-Type: %s\r\n"
               "Content-Length: %zu\r\n"
               "Connection: close\r\n"
               "%s"
               "\r\n",
               status, status_text, content_type, body_len,
               extra_headers ? extra_headers : "");
    send_all(fd, h.data, h.len);
    if (body && strcmp(req->method, "HEAD") != 0)
        send_all(fd, body->data, body->len);
    free(h.data);
}

/* ------------------------------------------------------------------ */
/* Site verisi: ziyaret sayaci ve ziyaretci defteri                    */
/* ------------------------------------------------------------------ */

typedef struct {
    time_t when;
    char   name[NAME_LEN];
    char   msg[MSG_LEN];
} Entry;

static Entry         entries[MAX_ENTRIES];
static int           entry_count;
static unsigned long page_views;
static time_t        started_at;

/* Dosyaya tek satir yazabilmek icin sekme/satir sonlarini bosluga cevir. */
static void flatten(char *s)
{
    for (; *s; s++)
        if (*s == '\t' || *s == '\r' || *s == '\n')
            *s = ' ';
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    memmove(s, s + i, n - i + 1);
}

static void add_entry(time_t when, const char *name, const char *msg)
{
    if (entry_count == MAX_ENTRIES) {
        memmove(entries, entries + 1, sizeof(Entry) * (MAX_ENTRIES - 1));
        entry_count--;
    }
    Entry *e = &entries[entry_count++];
    e->when = when;
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->msg, sizeof e->msg, "%s", msg);
}

static void load_entries(void)
{
    FILE *f = fopen(DEFTER_FILE, "r");
    if (!f)
        return;
    char line[NAME_LEN + MSG_LEN + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t1 = *t2 = '\0';
        add_entry((time_t)strtoll(line, NULL, 10), t1 + 1, t2 + 1);
    }
    fclose(f);
}

static void save_entry(const Entry *e)
{
    FILE *f = fopen(DEFTER_FILE, "a");
    if (!f) {
        perror(DEFTER_FILE);
        return;
    }
    fprintf(f, "%lld\t%s\t%s\n", (long long)e->when, e->name, e->msg);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Sayfalar                                                            */
/* ------------------------------------------------------------------ */

static const char CSS[] =
    ":root{--bg:#f6f4ef;--card:#fff;--ink:#1f2328;--muted:#5f6670;"
    "--accent:#0f766e;--line:#e3dfd6}"
    "@media (prefers-color-scheme:dark){:root{--bg:#14171a;--card:#1c2024;"
    "--ink:#e8e6e1;--muted:#9aa3ad;--accent:#2dd4bf;--line:#2b3036}}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--ink);"
    "font:16px/1.6 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    "header{border-bottom:1px solid var(--line);background:var(--card)}"
    ".wrap{max-width:760px;margin:0 auto;padding:0 16px}"
    "header .wrap{display:flex;align-items:center;justify-content:space-between;"
    "flex-wrap:wrap;gap:8px;padding-top:14px;padding-bottom:14px}"
    ".logo{font-weight:700;font-size:18px;color:var(--ink);text-decoration:none}"
    ".logo span{color:var(--accent);font-family:ui-monospace,monospace}"
    "nav a{color:var(--muted);text-decoration:none;margin-left:16px}"
    "nav a:first-child{margin-left:0}"
    "nav a.on,nav a:hover{color:var(--accent)}"
    "main.wrap{padding-top:32px;padding-bottom:48px}"
    "h1{font-size:30px;line-height:1.2;margin:0 0 12px}"
    "h2{font-size:20px;margin:32px 0 12px}"
    "p{margin:0 0 14px}"
    ".muted{color:var(--muted)}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:10px;"
    "padding:16px 18px;margin:0 0 12px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}"
    ".stat{font-size:28px;font-weight:700;color:var(--accent)}"
    "code{font-family:ui-monospace,monospace;font-size:14px;background:var(--line);"
    "padding:1px 5px;border-radius:4px}"
    "label{display:block;font-weight:600;margin:12px 0 4px}"
    "input,textarea{width:100%;font:inherit;color:inherit;background:var(--bg);"
    "border:1px solid var(--line);border-radius:8px;padding:10px}"
    "textarea{min-height:110px;resize:vertical}"
    "button{margin-top:14px;font:inherit;font-weight:600;border:0;border-radius:8px;"
    "padding:10px 18px;background:var(--accent);color:#fff;cursor:pointer}"
    ".entry b{display:block}.entry small{color:var(--muted)}"
    ".entry p{margin:6px 0 0;white-space:pre-wrap;overflow-wrap:anywhere}"
    ".err{color:#b42318;font-weight:600}"
    "footer{border-top:1px solid var(--line);padding:18px 0;color:var(--muted);font-size:14px}";

static void page_begin(Buf *b, const char *title, const char *active)
{
    static const struct { const char *href, *label; } links[] = {
        {"/", "Ana Sayfa"},
        {"/hakkinda", "Hakkında"},
        {"/defter", "Ziyaretçi Defteri"},
    };
    buf_puts(b, "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>");
    buf_html(b, title);
    buf_puts(b, " · C Sitesi</title><link rel=\"stylesheet\" href=\"/style.css\"></head>"
                "<body><header><div class=\"wrap\">"
                "<a class=\"logo\" href=\"/\"><span>&lt;C&gt;</span> Sitesi</a><nav>");
    for (size_t i = 0; i < sizeof links / sizeof links[0]; i++)
        buf_printf(b, "<a href=\"%s\"%s>%s</a>", links[i].href,
                   strcmp(links[i].href, active) == 0 ? " class=\"on\"" : "",
                   links[i].label);
    buf_puts(b, "</nav></div></header><main class=\"wrap\">");
}

static void page_end(Buf *b)
{
    buf_puts(b, "</main><footer><div class=\"wrap\">"
                "Bu sayfa saf C ile yazılmış bir sunucu tarafından üretildi. "
                "Framework yok, kütüphane yok.</div></footer></body></html>");
}

static void format_time(time_t t, char *out, size_t n)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, n, "%d.%m.%Y %H:%M", &tm);
}

static void page_home(Buf *b)
{
    char now[32];
    format_time(time(NULL), now, sizeof now);
    long up = (long)(time(NULL) - started_at);

    page_begin(b, "Ana Sayfa", "/");
    buf_puts(b, "<h1>Merhaba, dünya!</h1>"
                "<p>Bu web sitesi hiçbir framework kullanmadan, tamamen <b>C dili</b> ile "
                "yazıldı. Sunucu, TCP soketini kendisi açıyor, HTTP isteklerini "
                "satır satır kendisi okuyor ve bu HTML'i kendisi üretiyor.</p>"
                "<div class=\"grid\">");
    buf_printf(b, "<div class=\"card\"><div class=\"stat\">%lu</div>"
                  "<div class=\"muted\">sayfa görüntülenme</div></div>", page_views);
    buf_printf(b, "<div class=\"card\"><div class=\"stat\">%d</div>"
                  "<div class=\"muted\">defter mesajı</div></div>", entry_count);
    buf_printf(b, "<div class=\"card\"><div class=\"stat\">%ldg %02ld:%02ld</div>"
                  "<div class=\"muted\">çalışma süresi</div></div>",
               up / 86400, (up / 3600) % 24, (up / 60) % 60);
    buf_puts(b, "</div>");
    buf_printf(b, "<p class=\"muted\">Sunucu saati: %s</p>", now);
    buf_puts(b, "<h2>Neler var?</h2>"
                "<div class=\"card\"><b>Hakkında</b><p class=\"muted\">"
                "Sunucunun nasıl çalıştığını adım adım anlatıyor.</p></div>"
                "<div class=\"card\"><b>Ziyaretçi Defteri</b><p class=\"muted\">"
                "Form gönderip mesaj bırakabilirsin; mesajlar bir metin dosyasına "
                "kaydediliyor.</p></div>");
    page_end(b);
}

static void page_about(Buf *b)
{
    page_begin(b, "Hakkında", "/hakkinda");
    buf_puts(b, "<h1>Nasıl çalışıyor?</h1>"
                "<p>Tüm kod tek bir dosyada: <code>server.c</code>. "
                "Sadece standart C kütüphanesi ve POSIX soket çağrıları kullanılıyor.</p>"
                "<div class=\"card\"><b>1. Soket</b><p class=\"muted\">"
                "<code>socket()</code>, <code>bind()</code> ve <code>listen()</code> ile "
                "bir port açılır, <code>accept()</code> ile her bağlantı kabul edilir.</p></div>"
                "<div class=\"card\"><b>2. İsteği okuma</b><p class=\"muted\">"
                "<code>recv()</code> ile gelen baytlar boş satıra (<code>\\r\\n\\r\\n</code>) "
                "kadar okunur; istek satırı ve <code>Content-Length</code> başlığı ayrıştırılır.</p></div>"
                "<div class=\"card\"><b>3. Yönlendirme</b><p class=\"muted\">"
                "Yol (<code>/</code>, <code>/hakkinda</code>, <code>/defter</code>) basit "
                "<code>strcmp</code> karşılaştırmalarıyla ilgili fonksiyona gönderilir.</p></div>"
                "<div class=\"card\"><b>4. HTML üretimi</b><p class=\"muted\">"
                "Sayfalar büyüyebilen bir metin tamponuna yazılır. Kullanıcıdan gelen her "
                "metin HTML'e konmadan önce kaçırılır, böylece XSS engellenir.</p></div>"
                "<div class=\"card\"><b>5. Yanıt</b><p class=\"muted\">"
                "Durum satırı, başlıklar ve gövde <code>send()</code> ile gönderilir, "
                "bağlantı kapatılır.</p></div>");
    page_end(b);
}

static void page_guestbook(Buf *b, const char *error, const char *name, const char *msg)
{
    page_begin(b, "Ziyaretçi Defteri", "/defter");
    buf_puts(b, "<h1>Ziyaretçi Defteri</h1>"
                "<p class=\"muted\">Bir mesaj bırak. Form verisi C tarafında "
                "URL-çözülüp bir dosyaya yazılıyor.</p>"
                "<form class=\"card\" method=\"post\" action=\"/defter\">");
    if (error) {
        buf_puts(b, "<p class=\"err\">");
        buf_html(b, error);
        buf_puts(b, "</p>");
    }
    buf_printf(b, "<label for=\"ad\">Adın</label>"
                  "<input id=\"ad\" name=\"ad\" maxlength=\"%d\" required value=\"",
               NAME_LEN - 1);
    buf_html(b, name);
    buf_printf(b, "\"><label for=\"mesaj\">Mesajın</label>"
                  "<textarea id=\"mesaj\" name=\"mesaj\" maxlength=\"%d\" required>",
               MSG_LEN - 1);
    buf_html(b, msg);
    buf_puts(b, "</textarea><button type=\"submit\">Gönder</button></form>");

    buf_printf(b, "<h2>Mesajlar (%d)</h2>", entry_count);
    if (entry_count == 0)
        buf_puts(b, "<p class=\"muted\">Henüz mesaj yok. İlk sen yaz!</p>");
    for (int i = entry_count - 1; i >= 0; i--) {
        char when[32];
        format_time(entries[i].when, when, sizeof when);
        buf_puts(b, "<div class=\"card entry\"><b>");
        buf_html(b, entries[i].name);
        buf_printf(b, "</b><small>%s</small><p>", when);
        buf_html(b, entries[i].msg);
        buf_puts(b, "</p></div>");
    }
    page_end(b);
}

static void page_not_found(Buf *b, const char *path)
{
    page_begin(b, "Bulunamadı", "");
    buf_puts(b, "<h1>404 · Sayfa bulunamadı</h1><p class=\"muted\"><code>");
    buf_html(b, path);
    buf_puts(b, "</code> adresinde bir şey yok.</p><p><a href=\"/\">Ana sayfaya dön</a></p>");
    page_end(b);
}

/* ------------------------------------------------------------------ */
/* Yonlendirici                                                        */
/* ------------------------------------------------------------------ */

static const char HTML_TYPE[] = "text/html; charset=utf-8";

static void handle_guestbook_post(int fd, const Request *req)
{
    char name[NAME_LEN], msg[MSG_LEN];
    form_get(req->body, req->body_len, "ad", name, sizeof name);
    form_get(req->body, req->body_len, "mesaj", msg, sizeof msg);
    flatten(name);
    trim(name);
    trim(msg);
    flatten(msg);

    if (name[0] == '\0' || msg[0] == '\0') {
        Buf b = {0};
        page_guestbook(&b, "Lütfen hem adını hem mesajını yaz.", name, msg);
        send_response(fd, req, 400, "Bad Request", HTML_TYPE, NULL, &b);
        free(b.data);
        return;
    }

    add_entry(time(NULL), name, msg);
    save_entry(&entries[entry_count - 1]);

    /* POST-Redirect-GET: sayfa yenilenince form tekrar gonderilmesin */
    send_response(fd, req, 303, "See Other", HTML_TYPE, "Location: /defter\r\n", NULL);
}

static void route(int fd, const Request *req)
{
    int is_get = strcmp(req->method, "GET") == 0 || strcmp(req->method, "HEAD") == 0;
    int is_post = strcmp(req->method, "POST") == 0;
    Buf b = {0};

    if (strcmp(req->path, "/style.css") == 0 && is_get) {
        buf_add(&b, CSS, sizeof CSS - 1);
        send_response(fd, req, 200, "OK", "text/css; charset=utf-8",
                      "Cache-Control: max-age=3600\r\n", &b);
    } else if (strcmp(req->path, "/") == 0 && is_get) {
        page_views++;
        page_home(&b);
        send_response(fd, req, 200, "OK", HTML_TYPE, NULL, &b);
    } else if (strcmp(req->path, "/hakkinda") == 0 && is_get) {
        page_views++;
        page_about(&b);
        send_response(fd, req, 200, "OK", HTML_TYPE, NULL, &b);
    } else if (strcmp(req->path, "/defter") == 0 && is_get) {
        page_views++;
        page_guestbook(&b, NULL, "", "");
        send_response(fd, req, 200, "OK", HTML_TYPE, NULL, &b);
    } else if (strcmp(req->path, "/defter") == 0 && is_post) {
        handle_guestbook_post(fd, req);
    } else if (!is_get && !is_post) {
        buf_puts(&b, "405 Method Not Allowed\n");
        send_response(fd, req, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                      "Allow: GET, HEAD, POST\r\n", &b);
    } else {
        page_not_found(&b, req->path);
        send_response(fd, req, 404, "Not Found", HTML_TYPE, NULL, &b);
    }
    free(b.data);
}

/* ------------------------------------------------------------------ */
/* main: soket dongusu                                                 */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Gecersiz port: %s\n", argv[1]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);  /* kopan istemci sunucuyu oldurmesin */
    struct sigaction sa = {0};
    sa.sa_handler = on_signal; /* SA_RESTART yok: accept() kesilsin */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 64) < 0) {
        perror("listen");
        return 1;
    }

    started_at = time(NULL);
    load_entries();
    printf("Sunucu calisiyor: http://localhost:%d  (durdurmak icin Ctrl+C)\n", port);
    fflush(stdout);

    static char raw[MAX_HEADER + MAX_BODY + 1];
    while (running) {
        struct sockaddr_in client;
        socklen_t clen = sizeof client;
        int fd = accept(srv, (struct sockaddr *)&client, &clen);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        /* Yavas/sessiz istemciler sunucuyu kilitlemesin */
        struct timeval tv = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        Request req = {0};
        if (read_request(fd, &req, raw, sizeof raw) == 0) {
            route(fd, &req);
            printf("%s %s %s\n", inet_ntoa(client.sin_addr), req.method, req.path);
            fflush(stdout);
        } else {
            static const char bad[] =
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n"
                "Connection: close\r\n\r\nBad Request\n";
            send_all(fd, bad, sizeof bad - 1);
        }
        close(fd);
    }

    close(srv);
    printf("\nSunucu durduruldu.\n");
    return 0;
}
