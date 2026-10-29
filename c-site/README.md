# Saf C ile Web Sitesi

Hiçbir framework, kütüphane ya da hazır sunucu kullanmadan, yalnızca **standart C
kütüphanesi** ve **POSIX sistem çağrıları** (soketler, pthread) ile yazılmış bir web sitesi.
TCP soketini açmaktan HTTP isteğini ayrıştırmaya, HTML/CSS/JSON üretmekten çerezleri
yönetmeye kadar her şey elle yazıldı. Sayfalarda tek satır JavaScript yok.

## Çalıştırma

```sh
cd c-site
make                      # derle
./server 8080             # http://localhost:8080
./server 8080 defter.txt  # isteğe bağlı: defter dosyasının yolu

make debug                # AddressSanitizer + UBSan ile derler (server-debug)
```

Linux ve macOS'ta çalışır; gereken tek şey gcc ya da clang. Durdurmak için `Ctrl+C`
(açık bağlantılar bitirilip düzgünce kapanır).

## Sayfalar

| Yol                | İçerik                                                                 |
|--------------------|------------------------------------------------------------------------|
| `/`                | Tanıtım, canlı sayaçlar, son yazılar ve defter mesajları               |
| `/blog`            | Sunucunun nasıl yazıldığını anlatan 5 yazı, etiket filtresi (`?etiket=c`) |
| `/blog/<yazı>`     | Yazı sayfası: okuma süresi, etiketler, önceki/sonraki yazı             |
| `/defter`          | Ziyaretçi defteri: form, sayfalama, satır sonu desteği, spam koruması  |
| `/oyun`            | Sayı tahmin oyunu: çerez + sunucu tarafı oturum, rekor                 |
| `/araclar`         | Taban dönüştürücü (GET) ve UTF-8 farkında metin analizi (POST)         |
| `/istatistik`      | İstek sayıları, durum kodları, rota grafiği, son 25 istek              |
| `/hakkinda`        | Mimari, isteğin yolculuğu, güvenlik önlemleri, API belgeleri           |
| `/api/durum`       | Sunucu durumu (JSON)                                                   |
| `/api/mesajlar`    | Defter mesajları (JSON, `?limit=1..100`)                               |
| `/tema?t=koyu`     | Açık / koyu / otomatik tema (çerezle hatırlanır)                       |

## Kaynak kodu

```
c-site/
├── Makefile
└── src/
    ├── main.c    soket, accept döngüsü, thread başına bağlantı, erişim günlüğü, kapanış
    ├── http.c/h  istek ayrıştırma, yanıt gönderme, URL/form çözme, çerez okuma
    ├── buf.c/h   büyüyen metin tamponu, HTML ve JSON kaçırma
    ├── store.c/h defter (dosyaya kayıt), istatistikler, oyun oturumları, mutex
    ├── pages.c/h yönlendirici, sayfa şablonu, gömülü CSS ve tüm sayfalar
    └── blog.c/h  programa gömülü blog yazıları
```

## Teknik ayrıntılar

- **Eşzamanlılık:** Her bağlantı ayrı bir thread'de işlenir (en fazla 64; fazlası `503`).
  Ağ okuma/yazma paralel yürür; paylaşılan veriye dokunan sayfa üretimi tek bir
  `pthread_mutex` altında çalışır, böylece veri yarışı olmaz.
- **HTTP:** `GET`, `HEAD`, `POST`; `Content-Length` ile gövde; diğer yöntemlere `405` ve
  `Allow` başlığı. Chunked gövde `501` ile reddedilir.
- **Sınırlar:** başlıklar 16 KB (`431`), gövde 16 KB (`413`), yol 512 bayt (`414`),
  10 saniyelik okuma/yazma zaman aşımı (`408`).
- **Güvenlik:** her kullanıcı metni HTML'e kaçırılarak yazılır; sıkı
  `Content-Security-Policy`, `X-Frame-Options`, `X-Content-Type-Options`; `HttpOnly` ve
  `SameSite=Lax` çerezler; oturum kimlikleri `/dev/urandom`'dan; defterde honeypot alanı ve
  IP başına 15 saniye bekleme (`429`); tema yönlendirmesinde açık yönlendirme koruması.
- **UTF-8:** karakter sınırları bayt değil karakter sayısıyla uygulanır; metin analizi
  kod noktalarını çözer ve Türkçe büyük/küçük harf kurallarını (I→ı, İ→i) uygular.
- **Kalıcılık:** defter mesajları `defter.txt` dosyasına satır satır yazılır (satır
  sonları `\n` olarak kaçırılır) ve açılışta geri yüklenir. Diğer veriler bellektedir.

Öğrenme amaçlı bir projedir; internete açık üretim kullanımı için önünde bir ters vekil
sunucu (reverse proxy) ve TLS önerilir.
