/* pages.c - Yonlendirici, sayfa sablonu, CSS ve butun sayfalar.
 *
 * Her sayfa bir Buf'a parca parca yazilir. Kullanicidan gelen her metin
 * buf_html() ile kacirilir; sadece programa gomulu sabit HTML kacirilmaz. */
#include "pages.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blog.h"
#include "store.h"

#define PER_PAGE       8     /* defterde sayfa basina mesaj */
#define NAME_MAX_CHARS 40
#define MSG_MAX_CHARS  280
#define TEXT_MAX_BYTES 8000  /* metin analizi icin */

/* ================================================================== */
/* Bicimlendirme yardimcilari                                          */
/* ================================================================== */

static const char *const MONTHS[] = {
    "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
    "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık",
};

static void put_date(Buf *b, int y, int m, int d)
{
    buf_printf(b, "%d %s %d", d, MONTHS[(m - 1) % 12], y);
}

static void put_datetime(Buf *b, time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    buf_printf(b, "%d %s %d, %02d:%02d", tm.tm_mday, MONTHS[tm.tm_mon],
               tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
}

static void put_iso_time(Buf *b, time_t t)
{
    char s[40];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(s, sizeof s, "%Y-%m-%dT%H:%M:%S%z", &tm);
    buf_puts(b, s);
}

static void put_duration(Buf *b, long s)
{
    long d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
    if (d)
        buf_printf(b, "%ld g %ld sa", d, h);
    else if (h)
        buf_printf(b, "%ld sa %ld dk", h, m);
    else if (m)
        buf_printf(b, "%ld dk %ld sn", m, s % 60);
    else
        buf_printf(b, "%ld sn", s);
}

static void put_bytes(Buf *b, unsigned long long n)
{
    if (n < 1024)
        buf_printf(b, "%llu B", n);
    else if (n < 1024 * 1024)
        buf_printf(b, "%.1f KB", (double)n / 1024.0);
    else
        buf_printf(b, "%.1f MB", (double)n / (1024.0 * 1024.0));
}

/* Oran cubugu: CSP inline stile izin vermedigi icin SVG ozniteligiyle cizilir. */
static void put_bar(Buf *b, unsigned long part, unsigned long whole)
{
    int pct = whole ? (int)(part * 100 / whole) : 0;
    if (part && pct == 0)
        pct = 1;
    buf_printf(b,
               "<svg class=\"bar\" viewBox=\"0 0 100 6\" preserveAspectRatio=\"none\" "
               "aria-hidden=\"true\"><rect class=\"bg\" width=\"100\" height=\"6\"/>"
               "<rect width=\"%d\" height=\"6\"/></svg>",
               pct);
}

/* UTF-8 dizedeki karakter (kod noktasi) sayisi */
static size_t utf8_len(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

/* Bir UTF-8 karakter cozer; gecersiz baytlar U+FFFD olarak tek tek okunur. */
static unsigned utf8_decode(const unsigned char *s, int *len)
{
    unsigned c = s[0];
    int n;
    if (c < 0x80) {
        *len = 1;
        return c;
    }
    if (c >= 0xC2 && c <= 0xDF)
        n = 2;
    else if (c >= 0xE0 && c <= 0xEF)
        n = 3;
    else if (c >= 0xF0 && c <= 0xF4)
        n = 4;
    else {
        *len = 1;
        return 0xFFFD;
    }
    unsigned cp = c & (0xFFu >> (n + 1));
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *len = 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *len = n;
    return cp;
}

static int utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Kullanici metnini temizler: \r atilir, kontrol karakterleri bosluk olur,
 * bastaki/sondaki bosluklar kirpilir. keep_newlines=0 ise \n de bosluk olur. */
static void clean_text(char *s, int keep_newlines)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\r')
            continue;
        if (c == '\n' && keep_newlines)
            *o++ = '\n';
        else if (c < 0x20 || c == 0x7F)
            *o++ = ' ';
        else
            *o++ = (char)c;
    }
    *o = '\0';
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    memmove(s, s + i, n - i + 1);
}

static int reading_minutes(const char *html)
{
    int words = 0, in_tag = 0, in_word = 0;
    for (; *html; html++) {
        if (*html == '<') {
            in_tag = 1;
            in_word = 0;
        } else if (*html == '>') {
            in_tag = 0;
        } else if (!in_tag) {
            if (isspace((unsigned char)*html)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    return words < 200 ? 1 : (words + 199) / 200;
}

/* "http,ag" listesinde etiket var mi? */
static int has_tag(const char *tags, const char *tag)
{
    size_t n = strlen(tag);
    while (*tags) {
        const char *end = strchr(tags, ',');
        size_t len = end ? (size_t)(end - tags) : strlen(tags);
        if (len == n && strncmp(tags, tag, n) == 0)
            return 1;
        if (!end)
            break;
        tags = end + 1;
    }
    return 0;
}

static void put_tags(Buf *b, const char *tags)
{
    while (*tags) {
        const char *end = strchr(tags, ',');
        size_t len = end ? (size_t)(end - tags) : strlen(tags);
        buf_puts(b, "<a class=\"tag\" href=\"/blog?etiket=");
        buf_add(b, tags, len);
        buf_puts(b, "\">#");
        buf_add(b, tags, len);
        buf_puts(b, "</a>");
        if (!end)
            break;
        tags = end + 1;
    }
}

/* ================================================================== */
/* Stil dosyasi ve simge                                               */
/* ================================================================== */

#define LIGHT_TOKENS                                                            \
    "--bg:#f7f5f0;--card:#fff;--ink:#1d2125;--muted:#5d6670;--accent:#0f766e;"  \
    "--accent-ink:#fff;--soft:#e3f1ee;--line:#e4e0d7;--code:#efece5;"           \
    "--err:#b42318;--err-bg:#fdecea;--ok:#067647;--ok-bg:#e6f4ea;"             \
    "--warn:#93370d;--warn-bg:#fef0c7;color-scheme:light"

#define DARK_TOKENS                                                             \
    "--bg:#121518;--card:#1a1e22;--ink:#e6e4df;--muted:#9aa4ae;--accent:#2dd4bf;" \
    "--accent-ink:#06201c;--soft:#15332f;--line:#2a3036;--code:#22272c;"        \
    "--err:#f97066;--err-bg:#3a1714;--ok:#47cd89;--ok-bg:#0f2e1f;"             \
    "--warn:#fdb022;--warn-bg:#33240b;color-scheme:dark"

static const char CSS[] =
    ":root{" LIGHT_TOKENS "}"
    "@media (prefers-color-scheme:dark){:root:not([data-theme=light]){" DARK_TOKENS "}}"
    ":root[data-theme=dark]{" DARK_TOKENS "}"
    "*{box-sizing:border-box}"
    "html{-webkit-text-size-adjust:100%}"
    "[id]{scroll-margin-top:84px}"
    "body{margin:0;background:var(--bg);color:var(--ink);"
    "font:16px/1.65 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    "a{color:var(--accent)}"
    ".wrap{max-width:880px;margin:0 auto;padding:0 16px}"
    "header{background:var(--card);border-bottom:1px solid var(--line);"
    "position:sticky;top:0;z-index:5}"
    ".bar-top{display:flex;align-items:center;justify-content:space-between;"
    "gap:8px 16px;flex-wrap:wrap;padding-top:10px;padding-bottom:10px}"
    ".logo{display:flex;align-items:center;gap:8px;font-weight:700;font-size:18px;"
    "color:var(--ink);text-decoration:none}"
    ".logo img{width:26px;height:26px}"
    "nav{display:flex;flex-wrap:wrap;gap:2px}"
    "nav a{color:var(--muted);text-decoration:none;padding:6px 10px;border-radius:8px;font-size:15px}"
    "nav a:hover{color:var(--ink);background:var(--bg)}"
    "nav a.on{color:var(--accent);background:var(--soft);font-weight:600}"
    "main.wrap{padding-top:36px;padding-bottom:56px}"
    "h1{font-size:34px;line-height:1.15;margin:0 0 12px;letter-spacing:-.02em}"
    "h2{font-size:22px;line-height:1.3;margin:40px 0 12px}"
    "h3{font-size:17px;line-height:1.35;margin:0 0 6px}"
    "h3 a{color:inherit;text-decoration:none}h3 a:hover{color:var(--accent)}"
    "p{margin:0 0 14px}"
    ".lead{font-size:18px;color:var(--muted);max-width:660px}"
    ".muted{color:var(--muted)}.small{font-size:14px}"
    ".eyebrow{display:inline-block;font:600 13px/1 ui-monospace,SFMono-Regular,Menlo,monospace;"
    "color:var(--accent);background:var(--soft);padding:7px 10px;border-radius:999px;margin-bottom:16px}"
    ".actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}"
    ".btn{display:inline-block;font:inherit;font-weight:600;font-size:15px;line-height:1.4;"
    "border:1px solid var(--accent);border-radius:9px;padding:9px 16px;background:var(--accent);"
    "color:var(--accent-ink);cursor:pointer;text-decoration:none}"
    ".btn.ghost{background:transparent;color:var(--accent)}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}"
    ".grid.wide{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px 18px}"
    ".card>:last-child{margin-bottom:0}"
    ".stack>*+*{margin-top:12px}"
    "a.card{display:block;color:inherit;text-decoration:none;transition:border-color .15s}"
    "a.card:hover{border-color:var(--accent)}"
    "a.card .go{color:var(--accent);font-weight:600;font-size:14px}"
    ".stat{font-size:28px;font-weight:700;color:var(--accent);line-height:1.2;"
    "font-variant-numeric:tabular-nums}"
    ".label{color:var(--muted);font-size:14px}"
    ".section-head{display:flex;justify-content:space-between;align-items:baseline;"
    "gap:12px;margin:40px 0 12px}"
    ".section-head h2{margin:0}"
    ".tags{margin:0 0 20px}"
    ".tag{display:inline-block;font:500 13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;"
    "color:var(--accent);background:var(--soft);padding:2px 9px;border-radius:999px;"
    "text-decoration:none;margin:0 4px 4px 0}"
    ".tag.on{background:var(--accent);color:var(--accent-ink)}"
    ".meta{color:var(--muted);font-size:14px;margin-bottom:6px}"
    ".meta a{color:var(--muted)}"
    "article.post{max-width:700px}"
    "article.post p,article.post li{font-size:17px}"
    "article.post h2{font-size:22px;margin-top:32px}"
    "pre{background:var(--code);border:1px solid var(--line);border-radius:10px;"
    "padding:14px 16px;overflow-x:auto;font-size:14px;line-height:1.55;margin:0 0 16px}"
    "code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.88em}"
    ":not(pre)>code{background:var(--code);padding:1px 5px;border-radius:5px}"
    "blockquote{margin:0 0 16px;padding:12px 16px;border-left:3px solid var(--accent);"
    "background:var(--soft);border-radius:0 10px 10px 0}"
    "ul{padding-left:22px;margin:0 0 16px}li{margin:4px 0}"
    ".pager{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:36px;max-width:700px}"
    ".pager .next{text-align:right;grid-column:2}"
    ".alert{border-radius:10px;padding:12px 14px;margin:0 0 16px;font-weight:500}"
    ".alert.err{background:var(--err-bg);color:var(--err)}"
    ".alert.ok{background:var(--ok-bg);color:var(--ok)}"
    ".alert.info{background:var(--soft);color:var(--ink)}"
    "label{display:block;font-weight:600;margin:14px 0 6px}"
    "form>label:first-child,.row label{margin-top:0}"
    "input,textarea{width:100%;font:inherit;color:inherit;background:var(--bg);"
    "border:1px solid var(--line);border-radius:9px;padding:10px 12px}"
    "input:focus,textarea:focus{outline:2px solid var(--accent);outline-offset:1px}"
    "textarea{min-height:130px;resize:vertical}"
    ".hint{color:var(--muted);font-size:13px;margin:6px 0 0}"
    "form.card{margin-bottom:12px}"
    "h3.small{margin:22px 0 8px;text-transform:uppercase;letter-spacing:.05em;font-size:13px}"
    ".game h3.small{margin-top:20px}.game form+form .btn{margin-top:12px}"
    "form .btn{margin-top:16px}"
    ".row{display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap}"
    ".row>div{flex:1;min-width:180px}"
    ".row .btn{margin-top:0}"
    ".inline{display:inline}"
    ".hp{position:absolute;left:-10000px;width:1px;height:1px;overflow:hidden}"
    ".entry .who{font-weight:700}"
    ".entry .when{color:var(--muted);font-size:13px;margin-left:8px}"
    ".entry p{margin:6px 0 0;white-space:pre-wrap;overflow-wrap:anywhere}"
    ".pages{display:flex;gap:6px;flex-wrap:wrap;margin-top:16px}"
    ".pages a,.pages span{min-width:38px;text-align:center;padding:6px 10px;"
    "border:1px solid var(--line);border-radius:8px;text-decoration:none;background:var(--card)}"
    ".pages span{background:var(--accent);color:var(--accent-ink);border-color:var(--accent)}"
    ".table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:12px;background:var(--card)}"
    "table{width:100%;border-collapse:collapse;font-size:14px}"
    "th,td{text-align:left;padding:9px 12px;border-bottom:1px solid var(--line);white-space:nowrap}"
    "th{color:var(--muted);font-weight:600;font-size:13px}"
    "tr:last-child td{border-bottom:0}"
    ".num{text-align:right;font-variant-numeric:tabular-nums}"
    "td.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;white-space:normal;word-break:break-all}"
    ".badge{display:inline-block;font:600 12px/1.7 ui-monospace,monospace;padding:0 7px;border-radius:6px}"
    ".s2{background:var(--ok-bg);color:var(--ok)}.s3{background:var(--soft);color:var(--accent)}"
    ".s4{background:var(--warn-bg);color:var(--warn)}.s5{background:var(--err-bg);color:var(--err)}"
    ".bar{display:block;width:100%;height:8px;border-radius:4px;overflow:hidden}"
    ".bar rect{fill:var(--accent)}.bar .bg{fill:var(--line)}"
    ".bars .item{display:grid;grid-template-columns:150px 1fr 56px;gap:12px;align-items:center;padding:5px 0}"
    ".bars .item>span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    ".chips{display:flex;flex-wrap:wrap;gap:6px}"
    ".chip{font:600 14px/1 ui-monospace,SFMono-Regular,Menlo,monospace;padding:8px 10px;"
    "border-radius:8px;background:var(--code);border:1px solid var(--line)}"
    ".chip.hit{background:var(--ok-bg);color:var(--ok);border-color:transparent}"
    ".flow{list-style:none;padding:0;margin:0 0 16px;display:grid;"
    "grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;counter-reset:s}"
    ".flow li{background:var(--card);border:1px solid var(--line);border-radius:12px;"
    "padding:12px 14px;margin:0;counter-increment:s}"
    ".flow li::before{content:counter(s);display:inline-grid;place-items:center;width:22px;height:22px;"
    "border-radius:50%;background:var(--accent);color:var(--accent-ink);font-size:12px;font-weight:700;"
    "margin-bottom:8px}"
    ".flow b{display:block;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:14px}"
    ".flow span{display:block;color:var(--muted);font-size:14px;line-height:1.45;margin-top:2px}"
    ".err-code{font:800 76px/1 ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--accent);margin-bottom:12px}"
    ".rev{white-space:pre-wrap;overflow-wrap:anywhere}"
    "footer{border-top:1px solid var(--line);padding:22px 0 32px;color:var(--muted);font-size:14px}"
    "footer .wrap{display:flex;justify-content:space-between;gap:8px 16px;flex-wrap:wrap}"
    ".theme a{color:var(--muted);text-decoration:none;margin-left:10px}"
    ".theme a.on{color:var(--accent);font-weight:600}"
    "@media (max-width:560px){header{position:static}[id]{scroll-margin-top:12px}"
    "h1{font-size:28px}.lead{font-size:17px}"
    ".bars .item{grid-template-columns:96px 1fr 44px;gap:8px}nav a{padding:5px 8px}}";

static const char FAVICON[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">"
    "<rect width=\"64\" height=\"64\" rx=\"14\" fill=\"#0f766e\"/>"
    "<text x=\"32\" y=\"45\" font-family=\"ui-monospace,Menlo,monospace\" font-size=\"38\" "
    "font-weight=\"700\" text-anchor=\"middle\" fill=\"#fff\">C</text></svg>";

/* ================================================================== */
/* Sayfa sablonu                                                       */
/* ================================================================== */

static const char *theme_of(const Request *req)
{
    char t[16];
    if (cookie_get(req, "tema", t, sizeof t)) {
        if (strcmp(t, "light") == 0)
            return "light";
        if (strcmp(t, "dark") == 0)
            return "dark";
    }
    return NULL;
}

static void page_begin(Buf *b, const Request *req, const char *title, const char *active)
{
    static const struct { const char *href, *label; } NAV[] = {
        {"/", "Ana Sayfa"},     {"/blog", "Blog"},           {"/defter", "Defter"},
        {"/oyun", "Oyun"},      {"/araclar", "Araçlar"},     {"/istatistik", "İstatistik"},
        {"/hakkinda", "Hakkında"},
    };
    const char *theme = theme_of(req);

    buf_puts(b, "<!DOCTYPE html><html lang=\"tr\"");
    if (theme)
        buf_printf(b, " data-theme=\"%s\"", theme);
    buf_puts(b, "><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>");
    buf_html(b, title);
    buf_puts(b, " · C Sitesi</title>"
                "<meta name=\"description\" content=\"Hiçbir framework kullanmadan, "
                "saf C ile yazılmış bir web sitesi.\">"
                "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">"
                "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                "<header><div class=\"wrap bar-top\">"
                "<a class=\"logo\" href=\"/\"><img src=\"/favicon.svg\" alt=\"\">C Sitesi</a><nav>");
    for (size_t i = 0; i < sizeof NAV / sizeof NAV[0]; i++)
        buf_printf(b, "<a href=\"%s\"%s>%s</a>", NAV[i].href,
                   strcmp(NAV[i].href, active) == 0 ? " class=\"on\" aria-current=\"page\"" : "",
                   NAV[i].label);
    buf_puts(b, "</nav></div></header><main class=\"wrap\">");
}

static void page_end(Buf *b, const Request *req)
{
    const char *theme = theme_of(req);
    buf_puts(b, "</main><footer><div class=\"wrap\">"
                "<span>Saf C ile elle yazıldı · framework yok · kütüphane yok</span>"
                "<span class=\"theme\">Tema:");
    buf_printf(b, "<a href=\"/tema?t=oto\"%s>Otomatik</a>", !theme ? " class=\"on\"" : "");
    buf_printf(b, "<a href=\"/tema?t=acik\"%s>Açık</a>",
               theme && strcmp(theme, "light") == 0 ? " class=\"on\"" : "");
    buf_printf(b, "<a href=\"/tema?t=koyu\"%s>Koyu</a>",
               theme && strcmp(theme, "dark") == 0 ? " class=\"on\"" : "");
    buf_puts(b, "</span></div></footer></body></html>");
}

static void put_stat_card(Buf *b, const char *value, const char *label)
{
    buf_printf(b, "<div class=\"card\"><div class=\"stat\">%s</div>"
                  "<div class=\"label\">%s</div></div>", value, label);
}

static void put_entry(Buf *b, const Entry *e)
{
    buf_puts(b, "<div class=\"card entry\"><span class=\"who\">");
    buf_html(b, e->name);
    buf_puts(b, "</span><span class=\"when\">");
    put_datetime(b, e->when);
    buf_puts(b, "</span><p>");
    buf_html(b, e->msg);
    buf_puts(b, "</p></div>");
}

static void put_post_card(Buf *b, const Post *p)
{
    buf_puts(b, "<div class=\"card\"><div class=\"meta\">");
    put_date(b, p->y, p->m, p->d);
    buf_printf(b, " · %d dk okuma</div><h3><a href=\"/blog/%s\">", reading_minutes(p->body), p->slug);
    buf_html(b, p->title);
    buf_puts(b, "</a></h3><p class=\"muted\">");
    buf_html(b, p->summary);
    buf_puts(b, "</p><div>");
    put_tags(b, p->tags);
    buf_puts(b, "</div></div>");
}

/* ================================================================== */
/* Hata sayfasi                                                        */
/* ================================================================== */

void render_error(const Request *req, Response *res, int status, const char *detail)
{
    const char *title, *text;
    switch (status) {
    case 400: title = "Hatalı istek";        text = "Sunucu bu isteği anlayamadı."; break;
    case 404: title = "Sayfa bulunamadı";    text = "Aradığın adreste bir şey yok. Belki taşınmış ya da hiç var olmamıştır."; break;
    case 405: title = "Yönteme izin yok";    text = "Bu adres, kullandığın HTTP yöntemini desteklemiyor."; break;
    case 408: title = "Zaman aşımı";         text = "İstek zamanında tamamlanmadı."; break;
    case 413: title = "Veri çok büyük";      text = "Gönderdiğin veri sunucunun sınırını aşıyor."; break;
    case 414: title = "Adres çok uzun";      text = "İstenen adres sunucunun sınırını aşıyor."; break;
    case 431: title = "Başlıklar çok büyük"; text = "İsteğin başlıkları sunucunun sınırını aşıyor."; break;
    case 501: title = "Desteklenmiyor";      text = "Bu sunucu istenen özelliği desteklemiyor."; break;
    case 503: title = "Sunucu meşgul";       text = "Şu an çok fazla bağlantı var, birazdan tekrar dene."; break;
    default:  title = "Sunucu hatası";       text = "Beklenmeyen bir hata oluştu."; break;
    }

    res->status = status;
    res->content_type = NULL;
    Buf *b = &res->body;
    buf_reset(b);
    page_begin(b, req, title, "");
    buf_printf(b, "<div class=\"err-code\">%d</div><h1>%s</h1><p class=\"lead\">%s</p>",
               status, title, text);
    if (detail) {
        buf_puts(b, "<p class=\"muted\"><code>");
        buf_html(b, detail);
        buf_puts(b, "</code></p>");
    }
    buf_puts(b, "<div class=\"actions\"><a class=\"btn\" href=\"/\">Ana sayfaya dön</a>"
                "<a class=\"btn ghost\" href=\"/blog\">Bloga göz at</a></div>");
    page_end(b, req);
}

/* ================================================================== */
/* Ana sayfa                                                           */
/* ================================================================== */

static void h_home(const Request *req, Response *res)
{
    Buf *b = &res->body;
    Stats *s = stats();
    char v[64];

    page_begin(b, req, "Ana Sayfa", "/");
    buf_puts(b, "<section><span class=\"eyebrow\">#include &lt;sys/socket.h&gt;</span>"
                "<h1>Saf C ile yazılmış bir web sitesi</h1>"
                "<p class=\"lead\">Burada framework, kütüphane ya da hazır bir sunucu yok. "
                "TCP soketini açan, HTTP isteğini ayrıştıran, bu HTML'i ve CSS'i üreten her "
                "satır C ile elle yazıldı.</p>"
                "<div class=\"actions\"><a class=\"btn\" href=\"/hakkinda\">Nasıl çalışıyor?</a>"
                "<a class=\"btn ghost\" href=\"/blog\">Blogu oku</a></div></section>");

    buf_puts(b, "<h2>Şu an</h2><div class=\"grid\">");
    snprintf(v, sizeof v, "%lu", s->requests);
    put_stat_card(b, v, "toplam istek");
    snprintf(v, sizeof v, "%d", gb_count());
    put_stat_card(b, v, "defter mesajı");
    snprintf(v, sizeof v, "%d", POST_COUNT);
    put_stat_card(b, v, "blog yazısı");
    Buf d = {0};
    put_duration(&d, (long)(time(NULL) - s->started_at));
    put_stat_card(b, d.data, "çalışma süresi");
    buf_free(&d);
    buf_puts(b, "</div>");

    buf_puts(b,
        "<h2>Keşfet</h2><div class=\"grid wide\">"
        "<a class=\"card\" href=\"/blog\"><h3>Blog</h3><p class=\"muted\">Sunucunun nasıl "
        "yazıldığını anlatan yazılar: soketler, HTTP, bellek, güvenlik.</p>"
        "<span class=\"go\">Yazılara git →</span></a>"
        "<a class=\"card\" href=\"/defter\"><h3>Ziyaretçi Defteri</h3><p class=\"muted\">"
        "Bir mesaj bırak. Form verisi C'de çözülüp dosyaya kaydediliyor.</p>"
        "<span class=\"go\">Mesaj yaz →</span></a>"
        "<a class=\"card\" href=\"/oyun\"><h3>Sayı Tahmin Oyunu</h3><p class=\"muted\">"
        "1 ile 100 arasındaki sayıyı bul. Oyun durumu çerezle hatırlanıyor.</p>"
        "<span class=\"go\">Oyna →</span></a>"
        "<a class=\"card\" href=\"/araclar\"><h3>Araçlar</h3><p class=\"muted\">"
        "Taban dönüştürücü ve UTF-8 farkında metin analizi.</p>"
        "<span class=\"go\">Araçları aç →</span></a>"
        "<a class=\"card\" href=\"/istatistik\"><h3>İstatistik</h3><p class=\"muted\">"
        "Sunucunun canlı sayaçları, durum kodları ve son istekler.</p>"
        "<span class=\"go\">Sayılara bak →</span></a>"
        "<a class=\"card\" href=\"/hakkinda#api\"><h3>JSON API</h3><p class=\"muted\">"
        "Durum ve defter mesajları makinenin okuyabileceği biçimde.</p>"
        "<span class=\"go\">API'yi incele →</span></a>"
        "</div>");

    buf_puts(b, "<div class=\"section-head\"><h2>Son yazılar</h2>"
                "<a href=\"/blog\">Tümü →</a></div><div class=\"stack\">");
    for (int i = 0; i < POST_COUNT && i < 3; i++)
        put_post_card(b, &POSTS[i]);
    buf_puts(b, "</div>");

    buf_puts(b, "<div class=\"section-head\"><h2>Defterden</h2>"
                "<a href=\"/defter\">Tüm mesajlar →</a></div><div class=\"stack\">");
    if (gb_count() == 0)
        buf_puts(b, "<p class=\"muted\">Henüz mesaj yok. <a href=\"/defter\">İlk mesajı sen yaz!</a></p>");
    for (int i = 0; i < gb_count() && i < 3; i++)
        put_entry(b, gb_newest(i));
    buf_puts(b, "</div>");
    page_end(b, req);
}

/* ================================================================== */
/* Blog                                                                */
/* ================================================================== */

static void h_blog_list(const Request *req, Response *res)
{
    Buf *b = &res->body;
    char tag[32];
    query_get(req, "etiket", tag, sizeof tag);

    /* Etiket bulutu: tum yazilardaki etiketleri say */
    struct { char name[32]; int n; } tags[32];
    int ntags = 0;
    for (int i = 0; i < POST_COUNT; i++) {
        const char *t = POSTS[i].tags;
        while (*t) {
            const char *end = strchr(t, ',');
            size_t len = end ? (size_t)(end - t) : strlen(t);
            int k;
            for (k = 0; k < ntags; k++)
                if (strlen(tags[k].name) == len && strncmp(tags[k].name, t, len) == 0)
                    break;
            if (k == ntags && ntags < 32 && len < sizeof tags[0].name) {
                memcpy(tags[k].name, t, len);
                tags[k].name[len] = '\0';
                tags[k].n = 0;
                ntags++;
            }
            if (k < ntags)
                tags[k].n++;
            if (!end)
                break;
            t = end + 1;
        }
    }

    page_begin(b, req, "Blog", "/blog");
    buf_puts(b, "<h1>Blog</h1><p class=\"lead\">Bu sunucunun nasıl yazıldığını adım adım "
                "anlatan kısa yazılar.</p><div class=\"tags\">");
    buf_printf(b, "<a class=\"tag%s\" href=\"/blog\">tümü (%d)</a>", tag[0] ? "" : " on", POST_COUNT);
    for (int k = 0; k < ntags; k++) {
        buf_printf(b, "<a class=\"tag%s\" href=\"/blog?etiket=%s\">#%s (%d)</a>",
                   strcmp(tag, tags[k].name) == 0 ? " on" : "", tags[k].name, tags[k].name,
                   tags[k].n);
    }
    buf_puts(b, "</div><div class=\"stack\">");

    int shown = 0;
    for (int i = 0; i < POST_COUNT; i++) {
        if (tag[0] && !has_tag(POSTS[i].tags, tag))
            continue;
        put_post_card(b, &POSTS[i]);
        shown++;
    }
    if (!shown) {
        buf_puts(b, "<p class=\"muted\"><code>#");
        buf_html(b, tag);
        buf_puts(b, "</code> etiketli yazı yok. <a href=\"/blog\">Tüm yazılar</a></p>");
    }
    buf_puts(b, "</div>");
    page_end(b, req);
}

static void h_blog_post(const Request *req, Response *res)
{
    const char *slug = req->path + strlen("/blog/");
    if (!*slug) {
        res_redirect(res, "/blog");
        res->status = 301;
        return;
    }
    int idx = -1;
    for (int i = 0; i < POST_COUNT; i++)
        if (strcmp(POSTS[i].slug, slug) == 0)
            idx = i;
    if (idx < 0) {
        render_error(req, res, 404, req->path);
        return;
    }

    const Post *p = &POSTS[idx];
    Buf *b = &res->body;
    page_begin(b, req, p->title, "/blog");
    buf_puts(b, "<article class=\"post\"><p class=\"meta\"><a href=\"/blog\">← Tüm yazılar</a></p><h1>");
    buf_html(b, p->title);
    buf_puts(b, "</h1><p class=\"meta\">");
    put_date(b, p->y, p->m, p->d);
    buf_printf(b, " · %d dk okuma</p><div class=\"tags\">", reading_minutes(p->body));
    put_tags(b, p->tags);
    buf_puts(b, "</div>");
    buf_puts(b, p->body);  /* guvenilir, gomulu HTML */
    buf_puts(b, "</article><nav class=\"pager\" aria-label=\"Diğer yazılar\">");
    if (idx > 0) {
        buf_printf(b, "<a class=\"card\" href=\"/blog/%s\"><span class=\"label\">← Daha yeni</span><h3>",
                   POSTS[idx - 1].slug);
        buf_html(b, POSTS[idx - 1].title);
        buf_puts(b, "</h3></a>");
    }
    if (idx + 1 < POST_COUNT) {
        buf_printf(b, "<a class=\"card next\" href=\"/blog/%s\"><span class=\"label\">Daha eski →</span><h3>",
                   POSTS[idx + 1].slug);
        buf_html(b, POSTS[idx + 1].title);
        buf_puts(b, "</h3></a>");
    }
    buf_puts(b, "</nav>");
    page_end(b, req);
}

/* ================================================================== */
/* Ziyaretci defteri                                                   */
/* ================================================================== */

static void guestbook_page(const Request *req, Response *res, const char *error,
                           const char *name, const char *msg)
{
    Buf *b = &res->body;
    char sp[16], added[4];
    query_get(req, "sayfa", sp, sizeof sp);
    query_get(req, "eklendi", added, sizeof added);
    int total = gb_count();
    int pages = total ? (total + PER_PAGE - 1) / PER_PAGE : 1;
    int page = atoi(sp);
    if (page < 1)
        page = 1;
    if (page > pages)
        page = pages;

    page_begin(b, req, "Ziyaretçi Defteri", "/defter");
    buf_puts(b, "<h1>Ziyaretçi Defteri</h1><p class=\"lead\">Bir mesaj bırak. Form verisi "
                "C tarafında URL-çözülüyor, temizleniyor ve bir metin dosyasına kaydediliyor.</p>");
    if (error) {
        buf_puts(b, "<div class=\"alert err\" role=\"alert\">");
        buf_html(b, error);
        buf_puts(b, "</div>");
    } else if (added[0] == '1') {
        buf_puts(b, "<div class=\"alert ok\" role=\"status\">Mesajın deftere eklendi. Teşekkürler!</div>");
    }

    buf_printf(b, "<form class=\"card\" method=\"post\" action=\"/defter\">"
                  "<label for=\"ad\">Adın</label>"
                  "<input id=\"ad\" name=\"ad\" maxlength=\"%d\" required autocomplete=\"name\" value=\"",
               NAME_MAX_CHARS);
    buf_html(b, name);
    buf_printf(b, "\"><label for=\"mesaj\">Mesajın</label>"
                  "<textarea id=\"mesaj\" name=\"mesaj\" maxlength=\"%d\" required>",
               MSG_MAX_CHARS);
    buf_html(b, msg);
    buf_printf(b, "</textarea><p class=\"hint\">En fazla %d karakter. Satır sonları korunur; "
                  "HTML etiketleri düz metin olarak görünür. Aynı adresten %d saniyede bir "
                  "mesaj gönderilebilir.</p>"
                  "<div class=\"hp\" aria-hidden=\"true\"><label for=\"web\">Bu alanı boş bırak</label>"
                  "<input id=\"web\" name=\"web\" tabindex=\"-1\" autocomplete=\"off\"></div>"
                  "<button class=\"btn\" type=\"submit\">Gönder</button></form>",
               MSG_MAX_CHARS, POST_COOLDOWN);

    buf_printf(b, "<div class=\"section-head\"><h2>Mesajlar</h2><span class=\"muted\">%d mesaj",
               total);
    if (pages > 1)
        buf_printf(b, " · sayfa %d / %d", page, pages);
    buf_puts(b, "</span></div><div class=\"stack\">");
    if (total == 0)
        buf_puts(b, "<p class=\"muted\">Henüz mesaj yok. İlk sen yaz!</p>");
    for (int i = (page - 1) * PER_PAGE; i < total && i < page * PER_PAGE; i++)
        put_entry(b, gb_newest(i));
    buf_puts(b, "</div>");

    if (pages > 1) {
        buf_puts(b, "<nav class=\"pages\" aria-label=\"Sayfalar\">");
        for (int p = 1; p <= pages; p++) {
            if (p == page)
                buf_printf(b, "<span aria-current=\"page\">%d</span>", p);
            else
                buf_printf(b, "<a href=\"/defter?sayfa=%d\">%d</a>", p, p);
        }
        buf_puts(b, "</nav>");
    }
    page_end(b, req);
}

static void h_guestbook(const Request *req, Response *res)
{
    guestbook_page(req, res, NULL, "", "");
}

static void h_guestbook_post(const Request *req, Response *res)
{
    char name[512], msg[4096], honeypot[64];
    form_get(req, "ad", name, sizeof name);
    form_get(req, "mesaj", msg, sizeof msg);
    form_get(req, "web", honeypot, sizeof honeypot);
    clean_text(name, 0);
    clean_text(msg, 1);

    if (honeypot[0]) {  /* gizli alani sadece botlar doldurur: sessizce yok say */
        res_redirect(res, "/defter?eklendi=1");
        return;
    }

    char err[160];
    if (!name[0] || !msg[0]) {
        snprintf(err, sizeof err, "Lütfen hem adını hem mesajını yaz.");
    } else if (utf8_len(name) > NAME_MAX_CHARS || strlen(name) >= NAME_LEN) {
        snprintf(err, sizeof err, "Adın en fazla %d karakter olabilir.", NAME_MAX_CHARS);
    } else if (utf8_len(msg) > MSG_MAX_CHARS || strlen(msg) >= MSG_LEN) {
        snprintf(err, sizeof err, "Mesajın en fazla %d karakter olabilir.", MSG_MAX_CHARS);
    } else {
        int wait = gb_add(req->ip, name, msg);
        if (wait == 0) {
            res_redirect(res, "/defter?eklendi=1");  /* POST-Redirect-GET */
            return;
        }
        res->status = 429;
        res_header(res, "Retry-After: %d\r\n", wait);
        snprintf(err, sizeof err, "Biraz yavaş! Yeni bir mesaj için %d saniye beklemen gerekiyor.", wait);
        guestbook_page(req, res, err, name, msg);
        return;
    }
    res->status = 400;
    guestbook_page(req, res, err, name, msg);
}

/* ================================================================== */
/* Sayi tahmin oyunu                                                   */
/* ================================================================== */

static GameSession *game_session(const Request *req, Response *res)
{
    char id[64];
    GameSession *g = NULL;
    if (cookie_get(req, "oturum", id, sizeof id))
        g = game_find(id);
    if (!g) {
        g = game_create();
        res_header(res, "Set-Cookie: oturum=%s; Path=/; Max-Age=86400; HttpOnly; SameSite=Lax\r\n",
                   g->id);
    }
    return g;
}

static void h_game(const Request *req, Response *res)
{
    Buf *b = &res->body;
    GameSession *g = game_session(req, res);
    char v[32];

    /* Su ana kadarki tahminlerden olasi araligi hesapla */
    int lo = 1, hi = 100;
    for (int i = 0; i < g->nguess; i++) {
        if (g->guesses[i] < g->target && g->guesses[i] + 1 > lo)
            lo = g->guesses[i] + 1;
        if (g->guesses[i] > g->target && g->guesses[i] - 1 < hi)
            hi = g->guesses[i] - 1;
    }

    page_begin(b, req, "Sayı Tahmin Oyunu", "/oyun");
    buf_puts(b, "<h1>Sayı Tahmin Oyunu</h1><p class=\"lead\">1 ile 100 arasında bir sayı tuttum. "
                "Her tahmininden sonra sayının daha büyük mü küçük mü olduğunu söyleyeceğim.</p>"
                "<div class=\"grid\">");
    snprintf(v, sizeof v, "%d", g->nguess);
    put_stat_card(b, v, "deneme");
    if (g->finished == 1)
        snprintf(v, sizeof v, "%d", g->target);
    else
        snprintf(v, sizeof v, "%d – %d", lo, hi);
    put_stat_card(b, v, g->finished == 1 ? "tutulan sayı" : "olası aralık");
    if (game_best())
        snprintf(v, sizeof v, "%d", game_best());
    else
        snprintf(v, sizeof v, "—");
    put_stat_card(b, v, "rekor (en az deneme)");
    buf_puts(b, "</div><div class=\"card game\">");

    if (g->flash[0]) {
        buf_puts(b, "<div class=\"alert err\" role=\"alert\">");
        buf_html(b, g->flash);
        buf_puts(b, "</div>");
        g->flash[0] = '\0';
    }

    if (g->finished == 1) {
        buf_printf(b, "<div class=\"alert ok\" role=\"status\">Tebrikler! <b>%d</b> sayısını "
                      "<b>%d</b> denemede buldun.%s</div>",
                   g->target, g->nguess,
                   g->nguess <= 7 ? " İkili arama kadar verimli!" : "");
    } else if (g->finished == 2) {
        buf_printf(b, "<div class=\"alert err\" role=\"status\">Hakların bitti. Tuttuğum sayı "
                      "<b>%d</b> idi.</div>", g->target);
    } else if (g->nguess > 0) {
        int last = g->guesses[g->nguess - 1];
        buf_printf(b, "<div class=\"alert info\" role=\"status\"><b>%d</b> %s</div>", last,
                   last < g->target ? "çok küçük, daha büyük bir sayı dene. ↑"
                                    : "çok büyük, daha küçük bir sayı dene. ↓");
    }

    if (g->finished) {
        buf_puts(b, "<form method=\"post\" action=\"/oyun\"><input type=\"hidden\" name=\"yeni\" value=\"1\">"
                    "<button class=\"btn\" type=\"submit\">Yeni oyun</button></form>");
    } else {
        buf_printf(b, "<form method=\"post\" action=\"/oyun\"><div class=\"row\"><div>"
                      "<label for=\"tahmin\">Tahminin</label>"
                      "<input id=\"tahmin\" name=\"tahmin\" type=\"number\" min=\"1\" max=\"100\" "
                      "inputmode=\"numeric\" required autofocus placeholder=\"%d – %d\"></div>"
                      "<button class=\"btn\" type=\"submit\">Tahmin et</button></div></form>",
                   lo, hi);
    }

    if (g->nguess > 0) {
        buf_puts(b, "<h3 class=\"small muted\">Tahminlerin</h3><div class=\"chips\">");
        for (int i = 0; i < g->nguess; i++) {
            int x = g->guesses[i];
            if (x == g->target)
                buf_printf(b, "<span class=\"chip hit\">%d ✓</span>", x);
            else
                buf_printf(b, "<span class=\"chip\">%d %s</span>", x, x < g->target ? "↑" : "↓");
        }
        buf_puts(b, "</div>");
        if (!g->finished)
            buf_puts(b, "<form method=\"post\" action=\"/oyun\"><input type=\"hidden\" name=\"yeni\" value=\"1\">"
                        "<button class=\"btn ghost\" type=\"submit\">Baştan başla</button></form>");
    }
    buf_puts(b, "</div>");

    buf_puts(b, "<h2>Nasıl çalışıyor?</h2><div class=\"card\"><p>Tarayıcın ilk ziyarette "
                "<code>Set-Cookie</code> ile rastgele bir oturum kimliği aldı. Tuttuğum sayı ve "
                "tahminlerin çerezde değil, sunucunun belleğindeki bir tabloda duruyor; çerez "
                "sadece o tablodaki satırı bulmaya yarıyor.</p>");
    buf_printf(b, "<p class=\"muted small\">Oturum kimliğin: <code>%.8s…</code> "
                  "(<code>HttpOnly</code>, <code>SameSite=Lax</code>)</p>", g->id);
    buf_puts(b, "<p><a href=\"/blog/cerezler-ve-oturumlar\">Çerezler ve oturumlar yazısını oku →</a></p></div>");
    page_end(b, req);
}

static void h_game_post(const Request *req, Response *res)
{
    GameSession *g = game_session(req, res);
    char v[32];
    if (form_get(req, "yeni", v, sizeof v)) {
        game_new_round(g);
    } else if (!g->finished) {
        form_get(req, "tahmin", v, sizeof v);
        char *end;
        long n = strtol(v, &end, 10);
        if (!v[0] || *end || n < 1 || n > 100) {
            snprintf(g->flash, sizeof g->flash, "Lütfen 1 ile 100 arasında bir tam sayı gir.");
        } else {
            g->guesses[g->nguess++] = (int)n;
            if (n == g->target) {
                g->finished = 1;
                game_offer_best(g->nguess);
            } else if (g->nguess >= MAX_GUESSES) {
                g->finished = 2;
            }
        }
    }
    res_redirect(res, "/oyun");
}

/* ================================================================== */
/* Araclar                                                             */
/* ================================================================== */

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "255", "0xff", "0b1010", "0o17" bicimlerini okur. 0: tamam, -1: gecersiz, -2: tasma */
static int parse_number(const char *s, unsigned long long *out, int *base_out)
{
    while (isspace((unsigned char)*s))
        s++;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        base = 16;
    else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
        base = 2;
    else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
        base = 8;
    if (base != 10)
        s += 2;
    if (!*s)
        return -1;
    unsigned long long v = 0;
    int digits = 0;
    for (; *s && !isspace((unsigned char)*s); s++) {
        if (*s == '_' && digits)
            continue;  /* 1_000_000 gibi ayiricilar */
        int d = digit_value((unsigned char)*s);
        if (d < 0 || d >= base)
            return -1;
        if (v > (ULLONG_MAX - (unsigned)d) / (unsigned)base)
            return -2;
        v = v * (unsigned)base + (unsigned)d;
        digits++;
    }
    while (isspace((unsigned char)*s))
        s++;
    if (*s || !digits)
        return -1;
    *out = v;
    *base_out = base;
    return 0;
}

static int bit_length(unsigned long long v)
{
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static int popcount(unsigned long long v)
{
    int n = 0;
    while (v) {
        v &= v - 1;  /* en sagdaki 1 bitini sil (Kernighan) */
        n++;
    }
    return n;
}

static void put_binary(Buf *b, unsigned long long v)
{
    int bits = bit_length(v);
    if (bits == 0)
        bits = 1;
    int padded = (bits + 3) / 4 * 4;
    for (int i = padded - 1; i >= 0; i--) {
        buf_add(b, (v >> i) & 1 ? "1" : "0", 1);
        if (i && i % 4 == 0)
            buf_add(b, " ", 1);
    }
}

static unsigned tr_lower(unsigned cp)
{
    switch (cp) {
    case 'I':   return 0x131;  /* I -> ı */
    case 0x130: return 'i';    /* İ -> i */
    case 0x11E: return 0x11F;  /* Ğ -> ğ */
    case 0x15E: return 0x15F;  /* Ş -> ş */
    }
    if (cp >= 'A' && cp <= 'Z')
        return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)  /* Ç Ö Ü ve diger Latin-1 */
        return cp + 32;
    return cp;
}

static int is_letter(unsigned cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7);
}

static void put_number_result(Buf *b, const char *input)
{
    unsigned long long v;
    int base;
    int rc = parse_number(input, &v, &base);
    if (rc == -1) {
        buf_puts(b, "<div class=\"alert err\" role=\"alert\">Bu bir sayı değil: <code>");
        buf_html(b, input);
        buf_puts(b, "</code>. Örnekler: <code>255</code>, <code>0xff</code>, <code>0b1010</code>.</div>");
        return;
    }
    if (rc == -2) {
        buf_puts(b, "<div class=\"alert err\" role=\"alert\">Sayı 64 bite sığmıyor "
                    "(en fazla 18446744073709551615).</div>");
        return;
    }
    int bits = bit_length(v);
    buf_puts(b, "<div class=\"table-wrap\"><table><tbody>");
    buf_printf(b, "<tr><th>Onluk (10)</th><td class=\"mono\">%llu</td></tr>", v);
    buf_printf(b, "<tr><th>Onaltılık (16)</th><td class=\"mono\">0x%llX</td></tr>", v);
    buf_printf(b, "<tr><th>Sekizlik (8)</th><td class=\"mono\">0o%llo</td></tr>", v);
    buf_puts(b, "<tr><th>İkilik (2)</th><td class=\"mono\">");
    put_binary(b, v);
    buf_puts(b, "</td></tr>");
    buf_printf(b, "<tr><th>Anlamlı bit</th><td>%d bit · %d bayt</td></tr>", bits,
               bits ? (bits + 7) / 8 : 1);
    buf_printf(b, "<tr><th>1 olan bitler</th><td>%d</td></tr>", popcount(v));
    buf_printf(b, "<tr><th>İkinin kuvveti mi?</th><td>%s</td></tr>",
               v && !(v & (v - 1)) ? "Evet" : "Hayır");
    if (v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF) && v >= 0x20 && v != 0x7F &&
        !(v >= 0x80 && v < 0xA0)) {
        char ch[5] = {0};
        utf8_encode((unsigned)v, ch);
        buf_printf(b, "<tr><th>Unicode karakter</th><td>U+%04llX → <b>", v);
        buf_html(b, ch);
        buf_puts(b, "</b></td></tr>");
    }
    buf_puts(b, "</tbody></table></div>");
}

static void put_text_result(Buf *b, const char *text)
{
    size_t bytes = strlen(text);
    size_t chars = 0, words = 0, lines = 1, sentences = 0, letters_total = 0;
    int in_word = 0;
    struct { unsigned cp; unsigned long n; } letters[128];
    int nletters = 0;

    unsigned *cps = malloc((bytes + 1) * sizeof *cps);
    if (!cps)
        return;

    const unsigned char *s = (const unsigned char *)text;
    while (*s) {
        int len;
        unsigned cp = utf8_decode(s, &len);
        cps[chars++] = cp;
        s += len;

        if (cp == '\n')
            lines++;
        if (cp == ' ' || cp == '\n' || cp == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
        if ((cp == '.' || cp == '!' || cp == '?') && *s != '.' && *s != '!' && *s != '?')
            sentences++;
        if (is_letter(cp)) {
            unsigned low = tr_lower(cp);
            int k;
            for (k = 0; k < nletters; k++)
                if (letters[k].cp == low)
                    break;
            if (k == nletters && nletters < 128)
                letters[nletters++].cp = low, letters[k].n = 0;
            if (k < nletters)
                letters[k].n++;
            letters_total++;
        }
    }
    if (sentences == 0 && words > 0)
        sentences = 1;

    char v[48];
    buf_puts(b, "<div class=\"grid\">");
    snprintf(v, sizeof v, "%zu", chars);
    put_stat_card(b, v, "karakter");
    snprintf(v, sizeof v, "%zu", bytes);
    put_stat_card(b, v, "bayt (UTF-8)");
    snprintf(v, sizeof v, "%zu", words);
    put_stat_card(b, v, "kelime");
    snprintf(v, sizeof v, "%zu", lines);
    put_stat_card(b, v, "satır");
    snprintf(v, sizeof v, "%zu", sentences);
    put_stat_card(b, v, "cümle");
    snprintf(v, sizeof v, "%.1f", words ? (double)letters_total / (double)words : 0.0);
    put_stat_card(b, v, "harf / kelime");
    buf_puts(b, "</div>");

    if (nletters) {
        buf_puts(b, "<h3 class=\"muted small\">En sık harfler</h3><div class=\"card bars\">");
        for (int shown = 0; shown < 8 && shown < nletters; shown++) {
            int best = shown;  /* secmeli siralama: en buyugu one al */
            for (int k = shown + 1; k < nletters; k++)
                if (letters[k].n > letters[best].n)
                    best = k;
            unsigned cp = letters[best].cp;
            unsigned long n = letters[best].n;
            letters[best] = letters[shown];
            letters[shown].cp = cp;
            letters[shown].n = n;

            char ch[5] = {0};
            utf8_encode(cp, ch);
            buf_printf(b, "<div class=\"item\"><span><code>%s</code></span>", ch);
            put_bar(b, n, letters[0].n);  /* en sik harfe gore */
            buf_printf(b, "<span class=\"num\">%lu</span></div>", n);
        }
        buf_puts(b, "</div>");
    }

    /* UTF-8 farkinda ters cevirme: baytlari degil karakterleri ters cevir */
    Buf rev = {0};
    for (size_t i = chars; i > 0; i--) {
        char ch[4];
        int n = utf8_encode(cps[i - 1], ch);
        buf_add(&rev, ch, (size_t)n);
    }
    buf_puts(b, "<h3 class=\"muted small\">Tersten</h3><div class=\"card rev\">");
    buf_html(b, rev.data ? rev.data : "");
    buf_puts(b, "</div>");
    buf_free(&rev);
    free(cps);
}

static void tools_page(const Request *req, Response *res, const char *text, const char *error)
{
    Buf *b = &res->body;
    char num[128];
    int has_num = query_get(req, "sayi", num, sizeof num) && num[0];

    page_begin(b, req, "Araçlar", "/araclar");
    buf_puts(b, "<h1>Araçlar</h1><p class=\"lead\">İki küçük araç. Hesaplamaların tamamı "
                "sunucuda, C ile yapılıyor; sayfada tek satır JavaScript yok.</p>");

    buf_puts(b, "<h2>Taban dönüştürücü</h2><p class=\"muted\">Onluk bir sayı, <code>0x</code> "
                "ile onaltılık, <code>0b</code> ile ikilik ya da <code>0o</code> ile sekizlik bir "
                "sayı gir. Form GET ile gönderilir, yani sonuç adres çubuğundaki sorgu dizesinden "
                "okunur.</p><form class=\"card\" method=\"get\" action=\"/araclar\"><div class=\"row\"><div>"
                "<label for=\"sayi\">Sayı</label><input id=\"sayi\" name=\"sayi\" "
                "placeholder=\"255, 0xff, 0b1010\" autocomplete=\"off\" value=\"");
    if (has_num)
        buf_html(b, num);
    buf_puts(b, "\"></div><button class=\"btn\" type=\"submit\">Dönüştür</button></div></form>");
    if (has_num)
        put_number_result(b, num);

    buf_printf(b, "<h2 id=\"metin\">Metin analizi</h2><p class=\"muted\">Bir metin yapıştır. "
                  "Metin UTF-8 olarak çözülür; Türkçe harfler (ç, ğ, ı, İ, ö, ş, ü) doğru sayılır "
                  "ve büyük/küçük harf dönüşümü Türkçe kurallarına göre yapılır. En fazla %d bayt.</p>",
               TEXT_MAX_BYTES);
    if (error) {
        buf_puts(b, "<div class=\"alert err\" role=\"alert\">");
        buf_html(b, error);
        buf_puts(b, "</div>");
    }
    buf_puts(b, "<form class=\"card\" method=\"post\" action=\"/araclar#metin\">"
                "<label for=\"metin-alani\">Metin</label>"
                "<textarea id=\"metin-alani\" name=\"metin\" required>");
    if (text)
        buf_html(b, text);
    buf_puts(b, "</textarea><button class=\"btn\" type=\"submit\">Analiz et</button></form>");
    if (text && !error && text[0]) {
        buf_puts(b, "<h3 class=\"muted small\">Sonuç</h3>");
        put_text_result(b, text);
    }
    page_end(b, req);
}

static void h_tools(const Request *req, Response *res)
{
    tools_page(req, res, NULL, NULL);
}

static void h_tools_post(const Request *req, Response *res)
{
    static const char too_long[] = "Metin çok uzun.";
    char text[MAX_BODY + 1];
    form_get(req, "metin", text, sizeof text);
    clean_text(text, 1);
    if (strlen(text) > TEXT_MAX_BYTES) {
        res->status = 413;
        tools_page(req, res, "", too_long);
        return;
    }
    tools_page(req, res, text, NULL);
}

/* ================================================================== */
/* Istatistik                                                          */
/* ================================================================== */

static void put_masked_ip(Buf *b, const char *ip)
{
    const char *dot = strrchr(ip, '.');
    if (dot) {
        buf_add(b, ip, (size_t)(dot - ip));
        buf_puts(b, ".x");
    } else {
        buf_html(b, ip);
    }
}

static void h_stats(const Request *req, Response *res)
{
    Buf *b = &res->body;
    Stats *s = stats();
    char v[64];

    page_begin(b, req, "İstatistik", "/istatistik");
    buf_puts(b, "<h1>İstatistik</h1><p class=\"lead\">Sunucunun kendi tuttuğu canlı sayaçlar. "
                "Başka sayfaları gezip buraya dön; sayıların değiştiğini göreceksin.</p>"
                "<div class=\"grid\">");
    snprintf(v, sizeof v, "%lu", s->requests);
    put_stat_card(b, v, "toplam istek");
    Buf t = {0};
    put_bytes(&t, s->bytes_sent);
    put_stat_card(b, t.data, "gönderilen veri");
    buf_reset(&t);
    snprintf(v, sizeof v, "%.2f ms", s->requests ? s->total_ms / (double)s->requests : 0.0);
    put_stat_card(b, v, "ortalama yanıt süresi");
    put_duration(&t, (long)(time(NULL) - s->started_at));
    put_stat_card(b, t.data, "çalışma süresi");
    buf_free(&t);
    snprintf(v, sizeof v, "%d", conn_active());
    put_stat_card(b, v, "açık bağlantı");
    snprintf(v, sizeof v, "%d", gb_count());
    put_stat_card(b, v, "defter mesajı");
    buf_puts(b, "</div>");

    static const char *const CLASS_NAMES[] = {
        NULL, NULL, "2xx başarılı", "3xx yönlendirme", "4xx istemci hatası", "5xx sunucu hatası",
    };
    buf_puts(b, "<h2>Durum kodları</h2><div class=\"card bars\">");
    for (int c = 2; c <= 5; c++) {
        buf_printf(b, "<div class=\"item\"><span>%s</span>", CLASS_NAMES[c]);
        put_bar(b, s->by_class[c], s->requests);
        buf_printf(b, "<span class=\"num\">%lu</span></div>", s->by_class[c]);
    }
    buf_puts(b, "</div>");

    /* Rotalari ziyaret sayisina gore sirala (eklemeli siralama) */
    RouteStat sorted[MAX_ROUTES];
    int n = s->nroutes;
    memcpy(sorted, s->routes, sizeof(RouteStat) * (size_t)n);
    for (int i = 1; i < n; i++) {
        RouteStat key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j].count < key.count) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    buf_puts(b, "<h2>Rotalar</h2><div class=\"card bars\">");
    if (n == 0)
        buf_puts(b, "<p class=\"muted\">Henüz kayıt yok.</p>");
    for (int i = 0; i < n; i++) {
        buf_puts(b, "<div class=\"item\"><span><code>");
        buf_html(b, sorted[i].label);
        buf_puts(b, "</code></span>");
        put_bar(b, sorted[i].count, sorted[0].count);
        buf_printf(b, "<span class=\"num\">%lu</span></div>", sorted[i].count);
    }
    buf_puts(b, "</div>");

    buf_printf(b, "<h2>Son %d istek</h2><div class=\"table-wrap\"><table><thead><tr>"
                  "<th>Saat</th><th>IP</th><th>Yöntem</th><th>Yol</th><th>Durum</th>"
                  "<th class=\"num\">Boyut</th><th class=\"num\">Süre</th></tr></thead><tbody>",
               RECENT_LOG);
    int count = stats_recent_count();
    if (count == 0)
        buf_puts(b, "<tr><td colspan=\"7\" class=\"muted\">Henüz istek yok.</td></tr>");
    for (int i = 0; i < count; i++) {
        const LogEntry *e = stats_recent(i);
        struct tm tm;
        localtime_r(&e->when, &tm);
        buf_printf(b, "<tr><td>%02d:%02d:%02d</td><td>", tm.tm_hour, tm.tm_min, tm.tm_sec);
        put_masked_ip(b, e->ip);
        buf_puts(b, "</td><td>");
        buf_html(b, e->method);
        buf_puts(b, "</td><td class=\"mono\">");
        buf_html(b, e->path);
        buf_printf(b, "</td><td><span class=\"badge s%d\">%d</span></td><td class=\"num\">",
                   e->status / 100, e->status);
        put_bytes(b, e->bytes);
        buf_printf(b, "</td><td class=\"num\">%.2f ms</td></tr>", e->ms);
    }
    buf_puts(b, "</tbody></table></div><p class=\"muted small\">Bir istek, yanıtı gönderildikten "
                "sonra kaydedilir; bu yüzden bu sayfanın kendi isteği listede bir sonraki "
                "yenilemede görünür. IP adreslerinin son kısmı gizlenir.</p>");
    page_end(b, req);
}

/* ================================================================== */
/* Hakkinda                                                            */
/* ================================================================== */

static void h_about(const Request *req, Response *res)
{
    Buf *b = &res->body;
    page_begin(b, req, "Hakkında", "/hakkinda");
    buf_puts(b,
        "<h1>Nasıl çalışıyor?</h1>"
        "<p class=\"lead\">Bu site, standart C kütüphanesi ve POSIX sistem çağrıları dışında hiçbir "
        "şey kullanmayan küçük bir HTTP sunucusu. Aşağıda bir isteğin baştan sona yolculuğu var.</p>"

        "<h2>Bir isteğin yolculuğu</h2>"
        "<ol class=\"flow\">"
        "<li><b>accept()</b><span>Ana döngü yeni bağlantıyı kabul eder.</span></li>"
        "<li><b>pthread_create()</b><span>Bağlantı kendi thread'ine verilir.</span></li>"
        "<li><b>recv()</b><span>Baytlar <code>\\r\\n\\r\\n</code> görülene kadar okunur.</span></li>"
        "<li><b>http_read_request()</b><span>İstek satırı, başlıklar ve gövde ayrıştırılır.</span></li>"
        "<li><b>route_request()</b><span>Yol ve yönteme göre sayfa fonksiyonu seçilir.</span></li>"
        "<li><b>Buf</b><span>HTML, büyüyen bir tampona parça parça yazılır.</span></li>"
        "<li><b>send()</b><span>Başlıklar ve gövde gönderilir, bağlantı kapanır.</span></li>"
        "</ol>"

        "<h2>Kaynak kodu</h2>"
        "<div class=\"table-wrap\"><table><thead><tr><th>Dosya</th><th>Görevi</th></tr></thead><tbody>"
        "<tr><td class=\"mono\">src/main.c</td><td>Soket açma, bağlantı kabul döngüsü, thread'ler, "
        "erişim günlüğü, düzgün kapanış</td></tr>"
        "<tr><td class=\"mono\">src/http.c</td><td>HTTP isteğini ayrıştırma, yanıt gönderme, URL ve "
        "form çözme, çerez okuma</td></tr>"
        "<tr><td class=\"mono\">src/buf.c</td><td>Büyüyen metin tamponu, HTML ve JSON kaçırma</td></tr>"
        "<tr><td class=\"mono\">src/store.c</td><td>Ziyaretçi defteri ve dosyaya kayıt, istatistikler, "
        "oyun oturumları, mutex</td></tr>"
        "<tr><td class=\"mono\">src/pages.c</td><td>Yönlendirici, sayfa şablonu, CSS ve bütün "
        "sayfalar</td></tr>"
        "<tr><td class=\"mono\">src/blog.c</td><td>Programa gömülü blog yazıları</td></tr>"
        "</tbody></table></div>"

        "<h2>Eşzamanlılık</h2>"
        "<p>Her bağlantı ayrı bir thread'de işlenir, böylece yavaş bir istemci diğerlerini "
        "bekletmez. Ağdan okuma ve ağa yazma paralel yürür; paylaşılan veriye (defter, sayaçlar, "
        "oturumlar) dokunan sayfa üretimi ise tek bir <code>pthread_mutex</code> altında sırayla "
        "çalışır. Sayfa üretimi mikrosaniyeler sürdüğü için bu kilit darboğaz olmaz ve veri "
        "yarışlarını (data race) tamamen ortadan kaldırır. Aynı anda en fazla 64 bağlantı kabul "
        "edilir; fazlası <code>503</code> alır.</p>"

        "<h2>Güvenlik önlemleri</h2>"
        "<ul>"
        "<li><b>XSS:</b> Kullanıcıdan gelen her metin HTML'e yazılmadan önce kaçırılır.</li>"
        "<li><b>Content-Security-Policy:</b> Yalnızca bu sunucudan gelen kaynaklara izin verilir; "
        "satır içi betikler çalışmaz.</li>"
        "<li><b>Boyut sınırları:</b> Başlıklar 16 KB, gövde 16 KB, adres 512 baytla sınırlı "
        "(<code>431</code>, <code>413</code>, <code>414</code>).</li>"
        "<li><b>Zaman aşımı:</b> 10 saniye içinde isteğini tamamlamayan istemci <code>408</code> alır.</li>"
        "<li><b>Spam koruması:</b> Defterde gizli bir tuzak alan (honeypot) ve IP başına "
        "15 saniyelik bekleme süresi (<code>429</code>) var.</li>"
        "<li><b>Çerezler:</b> Oturum çerezi <code>HttpOnly</code> ve <code>SameSite=Lax</code>; "
        "kimlik <code>/dev/urandom</code>'dan üretilir.</li>"
        "<li><b>Açık yönlendirme yok:</b> Tema değiştirildikten sonra yalnızca aynı sitedeki bir "
        "yola geri dönülür.</li>"
        "</ul>"

        "<h2 id=\"api\">JSON API</h2>"
        "<p>İki uç nokta, JSON metnini de yine elle üretir:</p>"
        "<div class=\"table-wrap\"><table><thead><tr><th>Adres</th><th>Döndürdüğü</th></tr></thead><tbody>"
        "<tr><td class=\"mono\"><a href=\"/api/durum\">/api/durum</a></td><td>Çalışma süresi, istek "
        "sayısı, gönderilen bayt, mesaj ve yazı sayısı</td></tr>"
        "<tr><td class=\"mono\"><a href=\"/api/mesajlar?limit=5\">/api/mesajlar?limit=5</a></td>"
        "<td>Defterdeki en yeni mesajlar (varsayılan 20, en fazla 100)</td></tr>"
        "</tbody></table></div>"
        "<pre><code>$ curl -s http://localhost:8080/api/durum\n"
        "{\n"
        "  \"durum\": \"calisiyor\",\n"
        "  \"calisma_suresi_sn\": 3605,\n"
        "  \"toplam_istek\": 142,\n"
        "  ...\n"
        "}</code></pre>"

        "<h2>Kendi bilgisayarında çalıştır</h2>"
        "<pre><code>cd c-site\n"
        "make            # derle\n"
        "./server 8080   # http://localhost:8080\n"
        "\n"
        "make debug      # AddressSanitizer ile derle</code></pre>"
        "<p class=\"muted\">Linux ve macOS'ta çalışır. Gereken tek şey bir C derleyicisi "
        "(gcc ya da clang).</p>");
    page_end(b, req);
}

/* ================================================================== */
/* JSON API, tema, statik kaynaklar                                    */
/* ================================================================== */

static void h_api_status(const Request *req, Response *res)
{
    (void)req;
    Stats *s = stats();
    Buf *b = &res->body;
    res->content_type = "application/json; charset=utf-8";
    res_header(res, "Cache-Control: no-store\r\n");
    buf_printf(b,
               "{\n"
               "  \"durum\": \"calisiyor\",\n"
               "  \"calisma_suresi_sn\": %ld,\n"
               "  \"toplam_istek\": %lu,\n"
               "  \"gonderilen_bayt\": %llu,\n"
               "  \"acik_baglanti\": %d,\n"
               "  \"defter_mesaj_sayisi\": %d,\n"
               "  \"blog_yazi_sayisi\": %d,\n"
               "  \"sunucu_saati\": \"",
               (long)(time(NULL) - s->started_at), s->requests, s->bytes_sent, conn_active(),
               gb_count(), POST_COUNT);
    put_iso_time(b, time(NULL));
    buf_puts(b, "\"\n}\n");
}

static void h_api_messages(const Request *req, Response *res)
{
    Buf *b = &res->body;
    char l[16];
    int limit = query_get(req, "limit", l, sizeof l) && l[0] ? atoi(l) : 20;
    if (limit < 1)
        limit = 1;
    if (limit > 100)
        limit = 100;

    res->content_type = "application/json; charset=utf-8";
    res_header(res, "Cache-Control: no-store\r\n");
    buf_printf(b, "{\n  \"toplam\": %d,\n  \"mesajlar\": [", gb_count());
    for (int i = 0; i < gb_count() && i < limit; i++) {
        const Entry *e = gb_newest(i);
        buf_puts(b, i ? ",\n    {\"ad\": " : "\n    {\"ad\": ");
        buf_json(b, e->name);
        buf_puts(b, ", \"mesaj\": ");
        buf_json(b, e->msg);
        buf_puts(b, ", \"zaman\": \"");
        put_iso_time(b, e->when);
        buf_puts(b, "\"}");
    }
    buf_puts(b, gb_count() ? "\n  ]\n}\n" : "]\n}\n");
}

/* Referer'dan sadece yolu alir; baska siteye yonlendirmeyi (open redirect) engeller. */
static void safe_back_path(const Request *req, char *out, size_t size)
{
    snprintf(out, size, "/");
    const char *p = strstr(req->referer, "://");
    if (!p || !(p = strchr(p + 3, '/')) || p[1] == '/' || strlen(p) >= size)
        return;
    for (const char *c = p; *c; c++)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c >= 0x7F || *c == '"' ||
            *c == '<' || *c == '>' || *c == '\\')
            return;
    if (strncmp(p, "/tema", 5) == 0)
        return;
    snprintf(out, size, "%s", p);
}

static void h_theme(const Request *req, Response *res)
{
    char t[16], back[256];
    query_get(req, "t", t, sizeof t);
    if (strcmp(t, "acik") == 0)
        res_header(res, "Set-Cookie: tema=light; Path=/; Max-Age=31536000; SameSite=Lax\r\n");
    else if (strcmp(t, "koyu") == 0)
        res_header(res, "Set-Cookie: tema=dark; Path=/; Max-Age=31536000; SameSite=Lax\r\n");
    else
        res_header(res, "Set-Cookie: tema=; Path=/; Max-Age=0; SameSite=Lax\r\n");
    safe_back_path(req, back, sizeof back);
    res_redirect(res, back);
}

static void h_css(const Request *req, Response *res)
{
    (void)req;
    res->content_type = "text/css; charset=utf-8";
    res_header(res, "Cache-Control: max-age=300\r\n");
    buf_add(&res->body, CSS, sizeof CSS - 1);
}

static void h_favicon(const Request *req, Response *res)
{
    (void)req;
    res->content_type = "image/svg+xml";
    res_header(res, "Cache-Control: max-age=86400\r\n");
    buf_add(&res->body, FAVICON, sizeof FAVICON - 1);
}

static void h_favicon_ico(const Request *req, Response *res)
{
    (void)req;
    res_redirect(res, "/favicon.svg");
    res->status = 301;
}

static void h_robots(const Request *req, Response *res)
{
    (void)req;
    res->content_type = "text/plain; charset=utf-8";
    buf_puts(&res->body, "User-agent: *\nAllow: /\nDisallow: /api/\n");
}

/* ================================================================== */
/* Yonlendirici                                                        */
/* ================================================================== */

typedef void (*Handler)(const Request *, Response *);

typedef struct {
    const char *method;  /* "GET" ayni zamanda HEAD'i de karsilar */
    const char *path;
    int         prefix;  /* 1: path ile baslayan her adres */
    Handler     fn;
    const char *label;
} Route;

static const Route ROUTES[] = {
    {"GET",  "/",             0, h_home,           "/"},
    {"GET",  "/blog",         0, h_blog_list,      "/blog"},
    {"GET",  "/blog/",        1, h_blog_post,      "/blog/*"},
    {"GET",  "/defter",       0, h_guestbook,      "/defter"},
    {"POST", "/defter",       0, h_guestbook_post, "/defter (POST)"},
    {"GET",  "/oyun",         0, h_game,           "/oyun"},
    {"POST", "/oyun",         0, h_game_post,      "/oyun (POST)"},
    {"GET",  "/araclar",      0, h_tools,          "/araclar"},
    {"POST", "/araclar",      0, h_tools_post,     "/araclar (POST)"},
    {"GET",  "/istatistik",   0, h_stats,          "/istatistik"},
    {"GET",  "/hakkinda",     0, h_about,          "/hakkinda"},
    {"GET",  "/tema",         0, h_theme,          "/tema"},
    {"GET",  "/api/durum",    0, h_api_status,     "/api/durum"},
    {"GET",  "/api/mesajlar", 0, h_api_messages,   "/api/mesajlar"},
    {"GET",  "/style.css",    0, h_css,            "/style.css"},
    {"GET",  "/favicon.svg",  0, h_favicon,        "/favicon.svg"},
    {"GET",  "/favicon.ico",  0, h_favicon_ico,    "/favicon.ico"},
    {"GET",  "/robots.txt",   0, h_robots,         "/robots.txt"},
};

void route_request(const Request *req, Response *res)
{
    const char *method = strcmp(req->method, "HEAD") == 0 ? "GET" : req->method;
    char allow[64] = "";
    int path_found = 0;

    res->status = 200;
    for (size_t i = 0; i < sizeof ROUTES / sizeof ROUTES[0]; i++) {
        const Route *r = &ROUTES[i];
        int match = r->prefix ? strncmp(req->path, r->path, strlen(r->path)) == 0
                              : strcmp(req->path, r->path) == 0;
        if (!match)
            continue;
        if (strcmp(method, r->method) == 0) {
            res->label = r->label;
            r->fn(req, res);
            return;
        }
        path_found = 1;
        size_t len = strlen(allow);
        snprintf(allow + len, sizeof allow - len, "%s%s", len ? ", " : "",
                 strcmp(r->method, "GET") == 0 ? "GET, HEAD" : r->method);
    }

    if (path_found) {
        res_header(res, "Allow: %s\r\n", allow);
        render_error(req, res, 405, NULL);
        res->label = "(405)";
    } else {
        render_error(req, res, 404, req->path);
        res->label = "(404)";
    }
}
