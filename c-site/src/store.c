/* store.c - Paylasilan veri ve kalicilik. */
#include "store.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int      active_connections;

static const char *defter_file = "defter.txt";

static Entry entries[MAX_ENTRIES];  /* eskiden yeniye */
static int   entry_count;

static struct {
    char   ip[64];
    time_t last;
} post_times[64];

static Stats    st;
static LogEntry recent[RECENT_LOG];  /* dairesel tampon */
static int      recent_count, recent_head;

static GameSession sessions[MAX_SESSIONS];
static int         best_tries;

void store_lock(void)   { pthread_mutex_lock(&lock); }
void store_unlock(void) { pthread_mutex_unlock(&lock); }

void conn_enter(void)  { atomic_fetch_add(&active_connections, 1); }
void conn_leave(void)  { atomic_fetch_sub(&active_connections, 1); }
int  conn_active(void) { return atomic_load(&active_connections); }

void random_bytes(void *out, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f && fread(out, 1, n, f) == n) {
        fclose(f);
        return;
    }
    if (f)
        fclose(f);
    unsigned char *p = out;  /* yedek: guvenli degil ama calisir */
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)rand();
}

/* ------------------------------------------------------------------ */
/* Ziyaretci defteri                                                   */
/* ------------------------------------------------------------------ */

static void push_entry(time_t when, const char *name, const char *msg)
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

/* Dosya satir tabanli: mesajdaki satir sonlari \\n olarak saklanir. */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1] == 'n') {
            *o++ = '\n';
            s++;
        } else if (*s == '\\' && s[1] == '\\') {
            *o++ = '\\';
            s++;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

static void write_escaped(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            fputs("\\n", f);
        else if (*s == '\\')
            fputs("\\\\", f);
        else
            fputc(*s, f);
    }
}

static void load_entries(void)
{
    FILE *f = fopen(defter_file, "r");
    if (!f)
        return;
    char line[NAME_LEN + MSG_LEN + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        char *t1 = strchr(line, '\t');
        if (!t1)
            continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2)
            continue;
        *t1 = *t2 = '\0';
        unescape(t2 + 1);
        push_entry((time_t)strtoll(line, NULL, 10), t1 + 1, t2 + 1);
    }
    fclose(f);
}

int gb_count(void) { return entry_count; }

const Entry *gb_newest(int i)
{
    if (i < 0 || i >= entry_count)
        return NULL;
    return &entries[entry_count - 1 - i];
}

int gb_add(const char *ip, const char *name, const char *msg)
{
    time_t now = time(NULL);
    int slot = -1, oldest = 0;
    for (int i = 0; i < 64; i++) {
        if (strcmp(post_times[i].ip, ip) == 0) {
            slot = i;
            break;
        }
        if (post_times[i].last < post_times[oldest].last)
            oldest = i;
    }
    if (slot >= 0 && now - post_times[slot].last < POST_COOLDOWN)
        return (int)(POST_COOLDOWN - (now - post_times[slot].last));
    if (slot < 0) {
        slot = oldest;
        snprintf(post_times[slot].ip, sizeof post_times[slot].ip, "%s", ip);
    }
    post_times[slot].last = now;

    push_entry(now, name, msg);
    FILE *f = fopen(defter_file, "a");
    if (f) {
        /* sayfa katmani sekmeleri temizler; ad alaninda satir sonu olmaz */
        fprintf(f, "%lld\t%s\t", (long long)now, name);
        write_escaped(f, msg);
        fputc('\n', f);
        fclose(f);
    } else {
        perror(defter_file);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Istatistikler                                                       */
/* ------------------------------------------------------------------ */

Stats *stats(void) { return &st; }

void stats_record(const char *label, const char *ip, const char *method,
                  const char *path, int status, size_t bytes, double ms)
{
    st.requests++;
    st.bytes_sent += bytes;
    st.total_ms += ms;
    if (status >= 100 && status < 600)
        st.by_class[status / 100]++;

    int i;
    for (i = 0; i < st.nroutes; i++)
        if (strcmp(st.routes[i].label, label) == 0)
            break;
    if (i == st.nroutes && st.nroutes < MAX_ROUTES)
        st.routes[st.nroutes++] = (RouteStat){label, 0};
    if (i < st.nroutes)
        st.routes[i].count++;

    LogEntry *e = &recent[recent_head];
    e->when = time(NULL);
    snprintf(e->ip, sizeof e->ip, "%s", ip);
    snprintf(e->method, sizeof e->method, "%s", method);
    snprintf(e->path, sizeof e->path, "%s", path);
    e->status = status;
    e->bytes = bytes;
    e->ms = ms;
    recent_head = (recent_head + 1) % RECENT_LOG;
    if (recent_count < RECENT_LOG)
        recent_count++;
}

int stats_recent_count(void) { return recent_count; }

const LogEntry *stats_recent(int i)
{
    if (i < 0 || i >= recent_count)
        return NULL;
    return &recent[(recent_head - 1 - i + RECENT_LOG) % RECENT_LOG];
}

/* ------------------------------------------------------------------ */
/* Oyun oturumlari                                                     */
/* ------------------------------------------------------------------ */

GameSession *game_find(const char *id)
{
    if (strlen(id) != SESSION_ID_LEN)
        return NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].id, id) == 0) {
            sessions[i].last_seen = time(NULL);
            return &sessions[i];
        }
    }
    return NULL;
}

void game_new_round(GameSession *g)
{
    unsigned int r;
    random_bytes(&r, sizeof r);
    g->target = (int)(r % 100) + 1;
    g->nguess = 0;
    g->finished = 0;
    g->flash[0] = '\0';
}

GameSession *game_create(void)
{
    /* Bos ya da en uzun suredir kullanilmayan yuvayi al */
    int slot = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].id[0] == '\0') {
            slot = i;
            break;
        }
        if (sessions[i].last_seen < sessions[slot].last_seen)
            slot = i;
    }
    GameSession *g = &sessions[slot];
    memset(g, 0, sizeof *g);

    unsigned char raw[SESSION_ID_LEN / 2];
    random_bytes(raw, sizeof raw);
    for (size_t i = 0; i < sizeof raw; i++)
        snprintf(g->id + i * 2, 3, "%02x", raw[i]);
    g->last_seen = time(NULL);
    game_new_round(g);
    return g;
}

int game_best(void) { return best_tries; }

void game_offer_best(int tries)
{
    if (best_tries == 0 || tries < best_tries)
        best_tries = tries;
}

/* ------------------------------------------------------------------ */

void store_init(const char *defter_path)
{
    if (defter_path)
        defter_file = defter_path;
    st.started_at = time(NULL);
    load_entries();
}
