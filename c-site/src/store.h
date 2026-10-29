/* store.h - Sunucunun paylasilan verisi: ziyaretci defteri, istatistikler,
 * oyun oturumlari.
 *
 * Kural: bu dosyadaki veriye erismeden once store_lock() cagrilmalidir.
 * Baglanti thread'leri soket okuma/yazma isini paralel yapar; sayfa uretimi
 * ise tek bir mutex altinda, sirayla calisir. (Istisna: conn_* fonksiyonlari
 * atomiktir ve kilit gerektirmez.) */
#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <time.h>

#define NAME_LEN      128   /* bayt (40 karakter UTF-8 icin yeterli) */
#define MSG_LEN       1200  /* bayt (280 karakter UTF-8 icin yeterli) */
#define MAX_ENTRIES   500
#define POST_COOLDOWN 15   /* ayni IP'den iki mesaj arasi en az saniye */
#define RECENT_LOG    25
#define MAX_ROUTES    32
#define MAX_SESSIONS  128
#define MAX_GUESSES   32
#define SESSION_ID_LEN 32

typedef struct {
    time_t when;
    char   name[NAME_LEN];
    char   msg[MSG_LEN];
} Entry;

typedef struct {
    time_t when;
    char   ip[64];
    char   method[8];
    char   path[128];
    int    status;
    size_t bytes;
    double ms;
} LogEntry;

typedef struct {
    const char   *label;
    unsigned long count;
} RouteStat;

typedef struct {
    time_t             started_at;
    unsigned long      requests;
    unsigned long long bytes_sent;
    double             total_ms;
    unsigned long      by_class[6];  /* [2] = 2xx, [3] = 3xx ... */
    RouteStat          routes[MAX_ROUTES];
    int                nroutes;
} Stats;

typedef struct {
    char   id[SESSION_ID_LEN + 1];
    int    target;
    int    guesses[MAX_GUESSES];
    int    nguess;
    int    finished;   /* 0: suruyor, 1: bildi, 2: hak bitti */
    char   flash[96];  /* bir sonraki sayfada bir kez gosterilecek not */
    time_t last_seen;
} GameSession;

void store_lock(void);
void store_unlock(void);
void store_init(const char *defter_path);

/* Ziyaretci defteri */
int          gb_count(void);
const Entry *gb_newest(int i);  /* 0 = en yeni */
/* Mesaji ekler ve dosyaya yazar. 0: eklendi, >0: kac saniye beklenmeli. */
int          gb_add(const char *ip, const char *name, const char *msg);

/* Istatistikler */
Stats          *stats(void);
void            stats_record(const char *label, const char *ip, const char *method,
                             const char *path, int status, size_t bytes, double ms);
int             stats_recent_count(void);
const LogEntry *stats_recent(int i);  /* 0 = en yeni */

/* Sayi tahmin oyunu */
GameSession *game_find(const char *id);
GameSession *game_create(void);
void         game_new_round(GameSession *g);
int          game_best(void);  /* en az denemeyle bilme rekoru, yoksa 0 */
void         game_offer_best(int tries);

/* Atomik baglanti sayaci (kilit gerektirmez) */
void conn_enter(void);
void conn_leave(void);
int  conn_active(void);

/* /dev/urandom'dan rastgele bayt */
void random_bytes(void *out, size_t n);

#endif
