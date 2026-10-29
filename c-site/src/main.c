/* main.c - Saf C ile yazilmis web sitesinin giris noktasi.
 *
 * Sorumluluklar: dinleyen soketi acmak, baglantilari kabul etmek, her
 * baglantiyi ayri bir thread'de islemek, erisim gunlugu yazmak ve
 * Ctrl+C ile duzgunce kapanmak.
 *
 * Kullanim: ./server [port] [defter-dosyasi]
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "http.h"
#include "pages.h"
#include "store.h"

#define MAX_CONNECTIONS 64
#define IO_TIMEOUT_SEC  10

static volatile sig_atomic_t running = 1;

typedef struct {
    int  fd;
    char ip[64];
} Conn;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void log_request(const Request *req, int status, size_t bytes, double ms)
{
    char when[32];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    printf("[%s] %-15s %-4s %-24s %d %6zuB %7.2fms\n", when, req->ip,
           req->method[0] ? req->method : "-", req->path[0] ? req->path : "-", status, bytes, ms);
    fflush(stdout);
}

static void *handle_connection(void *arg)
{
    Conn *c = arg;
    double t0 = now_ms();
    size_t raw_size = MAX_HEADER + MAX_BODY + 1;
    char *raw = malloc(raw_size);
    Request req = {0};
    Response res = {0};
    snprintf(req.ip, sizeof req.ip, "%s", c->ip);

    int rc = raw ? http_read_request(c->fd, &req, raw, raw_size) : 503;
    if (rc >= 0) {  /* rc < 0: istemci hicbir sey gondermeden kapatti */
        store_lock();
        if (rc == 0) {
            route_request(&req, &res);
        } else {
            render_error(&req, &res, rc, NULL);
            res.label = "(hatali istek)";
        }
        store_unlock();

        size_t sent = http_send(c->fd, &req, &res);
        double ms = now_ms() - t0;

        store_lock();
        stats_record(res.label ? res.label : "(diger)", req.ip,
                     req.method[0] ? req.method : "-", req.path[0] ? req.path : "-",
                     res.status, sent, ms);
        store_unlock();
        log_request(&req, res.status, sent, ms);
    }

    buf_free(&res.body);
    free(raw);
    close(c->fd);
    free(c);
    conn_leave();
    return NULL;
}

static void reject_busy(int fd)
{
    static const char msg[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 31\r\n"
        "Retry-After: 2\r\n"
        "Connection: close\r\n\r\n"
        "Sunucu mesgul, tekrar deneyin.\n";
    ssize_t n = send(fd, msg, sizeof msg - 1, 0);
    (void)n;
    close(fd);
}

static int open_listener(int port)
{
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return -1;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        close(srv);
        return -1;
    }
    if (listen(srv, 128) < 0) {
        perror("listen");
        close(srv);
        return -1;
    }
    return srv;
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Kullanim: %s [port] [defter-dosyasi]\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);  /* kopan istemci sunucuyu oldurmesin */
    struct sigaction sa = {0};
    sa.sa_handler = on_signal; /* SA_RESTART yok: accept() kesilsin */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int srv = open_listener(port);
    if (srv < 0)
        return 1;

    store_init(argc > 2 ? argv[2] : NULL);
    printf("C Sitesi calisiyor: http://localhost:%d\n"
           "Sayfalar: / /blog /defter /oyun /araclar /istatistik /hakkinda\n"
           "API:      /api/durum /api/mesajlar\n"
           "Durdurmak icin Ctrl+C\n\n", port);
    fflush(stdout);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* Sinyaller sadece ana thread'e gelsin: isci thread'ler bunlari engellenmis
     * olarak devralir. */
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);

    while (running) {
        struct sockaddr_in client;
        socklen_t clen = sizeof client;
        int fd = accept(srv, (struct sockaddr *)&client, &clen);
        if (fd < 0) {
            if (errno != EINTR)
                perror("accept");
            continue;
        }

        struct timeval tv = {IO_TIMEOUT_SEC, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        if (conn_active() >= MAX_CONNECTIONS) {
            reject_busy(fd);
            continue;
        }

        Conn *c = malloc(sizeof *c);
        if (!c) {
            close(fd);
            continue;
        }
        c->fd = fd;
        inet_ntop(AF_INET, &client.sin_addr, c->ip, sizeof c->ip);

        conn_enter();
        pthread_t tid;
        pthread_sigmask(SIG_BLOCK, &block, &old);
        int err = pthread_create(&tid, &attr, handle_connection, c);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
        if (err) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            conn_leave();
            reject_busy(fd);
            free(c);
        }
    }

    printf("\nKapatiliyor, acik baglantilar bekleniyor...\n");
    close(srv);
    for (int i = 0; i < 50 && conn_active() > 0; i++)
        usleep(100 * 1000);
    pthread_attr_destroy(&attr);
    printf("Sunucu durduruldu.\n");
    return 0;
}
