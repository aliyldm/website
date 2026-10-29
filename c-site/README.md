# Saf C ile Web Sitesi

Hiçbir framework ya da harici kütüphane kullanmadan, sadece **standart C kütüphanesi**
ve **POSIX soketleri** ile yazılmış basit bir web sitesi. Her şey `server.c` dosyasında.

## Çalıştırma

```sh
cd c-site
make            # veya: cc -O2 -Wall -o server server.c
./server 8080   # tarayıcıda http://localhost:8080
```

Durdurmak için `Ctrl+C`.

## Sayfalar

| Yol          | Açıklama                                              |
|--------------|-------------------------------------------------------|
| `/`          | Ana sayfa: görüntülenme sayısı, çalışma süresi, saat  |
| `/hakkinda`  | Sunucunun nasıl çalıştığı                             |
| `/defter`    | Ziyaretçi defteri (form ile mesaj bırakma)            |
| `/style.css` | C kodunun içine gömülü CSS                            |

## Elle yazılanlar

- **TCP sunucusu:** `socket`, `bind`, `listen`, `accept`, `recv`, `send`
- **HTTP ayrıştırma:** istek satırı, `Content-Length` başlığı, istek gövdesi
- **Yönlendirme:** yol ve metoda göre (`GET`, `HEAD`, `POST`; diğerleri `405`)
- **HTML üretimi:** büyüyebilen metin tamponu (`Buf`) ve HTML kaçırma (XSS koruması)
- **Form çözme:** `application/x-www-form-urlencoded` (`+` ve `%XX`)
- **Kalıcılık:** defter mesajları `defter.txt` dosyasına yazılır, açılışta okunur
- **POST-Redirect-GET**, 404 sayfası, açık/koyu tema, mobil uyumlu düzen

Sunucu tek iş parçacıklıdır ve istekleri sırayla işler; öğrenme amaçlı bir
projedir, internete açık üretim kullanımı için tasarlanmamıştır.
