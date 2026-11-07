# INBOX — planlayıcıdan (Claude) Hermes'e görevler

**Kural:** Her oturum başında `git pull` → `docs/agent/STATE.md` → bu dosya.
Bu dosyayı yalnız planlayıcı yazar; sen yalnız her maddenin altındaki
`Durum:` satırını güncellersin (commit + push ile). Biten maddeyi silme,
`Durum: BİTTİ (<hash>)` yap. Sıra yukarıdan aşağıya. Bir madde belirsizse
makul varsayılanı seç, `Durum:` satırına "varsayım: …" yaz, devam et.
Kullanıcı ayrıca bir şey söylerse kullanıcı önceliklidir.

Ortak kurallar (her maddede geçerli):
- Kullanıcıya görünen her düzeltme: önce hatayı GÖSTEREN test/senaryo
  (kırmızı), sonra düzeltme, sonra yeşil. Kök neden bir cümle.
- Push kapısı: Windows tam derleme (`build-hermes`, exe link dahil) + tam
  `ctest` yeşil (+ varsa `ctest -L gui`). Tek kırmızıda push yok.
  Push: `git push origin hermes/work:feature/component-contract`.
- Bitince/beklerken/hata ile dururken bip (skill'de).
- Rapor kısa: commit listesi, test özeti, açık sorular.

---

## GECE MODU (2026-11-08 gecesi, sabaha kadar) — kullanıcı uyuyor

Kullanıcı yok; kimseyi bekleme. Bip gerekmez. Amaç: Model Maker'ı bir Tekla
modelleme kullanıcısı gibi canlı test etmek ve bulunan hataları düzeltmek.

Kurulum (ilk tur, bir kez):
- Hermes cron'u ile her 40 dakikada bir yeni oturum başlatan bir iş kur:
  "cd C:\work\mm-hermes; git pull --ff-only origin feature/component-contract;
  docs/agent/STATE.md + docs/agent/INBOX.md oku; GECE MODU kurallarıyla bir
  sonraki açık maddeyi yap". Aynı anda iki oturum çalışmasın (kilit dosyası:
  C:\work\mm-hermes-gece.lock; varsa ve 60 dk'dan yeniyse çık).
- Sabah 09:00'da cron işini durdur (kendi kendini silsin).

Döngü (her oturum):
1. pull; INBOX'u oku (planlayıcı gece boyunca yeni madde ekleyebilir).
2. Sıradaki açık madde: önce 0–6, sonra "8. Tekla test planı" senaryoları.
3. Senaryoyu ekranda canlı uygula (Computer Use; exe arka planda;
   ekran görüntüleri C:\work\mm-hermes-shots\<tarih>\).
4. Hata bulursan: INBOX sonuna "## H-<no> — <başlık>" ekle (adımlar,
   beklenen, görülen, ekran görüntüsü yolu), sonra düzelt (kırmızı test →
   düzeltme → yeşil → ekranda tekrar), ayrı commit.
5. Push kapısı yeşilse push. Kırmızı kalıyorsa yarım işi wip/hermes'e
   push et, Durum'a "takıldı: <neden>" yaz, sonraki maddeye geç.
6. Oturum sonunda STATE.md + INBOX Durum satırları güncel, push.

Gece YASAK: main'e merge; pacman/MSYS2 paket değişikliği; IgnorePkg
değişikliği; C:\work\model-maker-p2d ve kullanıcı dosyalarına dokunma;
Studio reposu; test gevşetme / golden'ı test geçsin diye yeniden üretme;
force push. Bunlardan biri gerekirse Durum'a yaz ve geç.
Geri alınamaz ya da ürün kararı gerektiren konu: varsayım yapma, "karar
bekliyor" diye yaz, geç.

---

## 0. Süreç düzeltmeleri (ÖNCE)

### 0a. Etkin limitler
`hermes config get agent.max_turns` (150 olmalı) ve
`agent.gateway_timeout` (3600). Oturumda hâlâ "60/60" görülüyorsa ayar
uygulanmıyor demektir: doğru anahtarı bul, gateway'i yeniden başlat,
doğrula. Sonucu Durum'a yaz.

### 0b. Bağlam optimizasyonu (önce ölç → değiştir → tekrar ölç)
Bir oturumda 68 sıkıştırma görüldü; kabul edilemez.
1. deepseek-v4-pro'nun gerçek bağlam penceresini doğrula. 65.5K "pinned"
   ise neden (config.yaml / context_length_cache.yaml)? Gerçek sınır
   büyükse güvenli değere (~%90) ayarla.
2. Boş oturum taban token maliyetini ölç (sistem + araç tanımları +
   skill indeksi).
3. Bu proje için gereksiz araçları kapat: image_generate, text_to_speech,
   video, connections. **Computer Use, vision ve cron AÇIK kalsın** (gece
   modu cron'la çalışır).
   İlgisiz skill'leri (sağlık, e-posta, akıllı ev, medya, sosyal, web
   sitesi vb.) arşivle — silme; liste ver.
4. Önce/sonra sayıları Durum'a yaz.

### 0c. GUI'yi arka planda başlat
Ön planda açılan `model-maker.exe` terminali pencere kapanana kadar
kilitler. Kural (skill'e yazılı olmalı): `./model-maker.exe &` ya da process
aracı, PID not al, Computer Use ile çalış, bitince PID ile kapat. Her
uzun komutta `timeout` kullan.

Durum: —

---

## 1. B1 — Haunch (40) katalogda/aramada yok — yamayı doğrula
Yama `wip/hermes` (e1ca5f0, ribbon_widget.cpp: Dropdown → menü düğmesi).
- Derle, ctest, ekranda: Çelik → Katalog tıkla → menüde yerleşik 4 tip
  (End Plate, Fin Plate, Haunch (40), 146) + yüklenen paketler.
- **Ayrıca hızlı arama:** kullanıcı "Haunch (40)" ve "haunch" yazınca HİÇ
  sonuç görmedi. Katalog düzelse bile arama ayrı sorun olabilir: arama
  dizini component registry dolmadan mı kuruluyor? Ekranda doğrula.
- Yeşilse wip'teki commit'i `hermes/work`'e al, push; `wip/hermes`'i sıfırla.

Durum: —

## 2. B6 — Çizgi komutu profil çiziyor
Çizgi, Polyline, Dikdörtgen, Daire, 3DFACE panelde aktif profili (ör.
HEB240) YOK SAYSIN; yalnız profilsiz yardımcı obje üretsin. Profilli üye
yalnız Kolon/Kiriş'ten. (Yardımcı obje görünümü/migration UI-3'te.)
Senaryo: aktif profil HEB240 → Çizgi → profilsiz; Kiriş → HEB240.

Durum: —

## 3. B2/B7 — Çizgi komut bitince görünmüyor (GPU'da da)
Kullanıcı 68c5879'da (temiz derleme) çizgi çizince ekranda görmüyor.
B2 "düzeltildi" denmişti; demek ki gerçek akışı kapsamıyor. GDI ve GPU
(OpenGL) backend'lerinde ayrı ayrı: iki tık + Esc / tıklama ile bitiş →
çizgi görünür çizim listesinde ve ekranda. Birim test neden yakalamadı,
bir cümle.

Durum: —

## 4. B8 — Kopyalanan kesme düzlemi çizilmiyor, ama seçilebiliyor
Plane Cut kopyalanınca kopya görünmüyor; o konuma tıklayınca seçiliyor.
Kural: kopyalanan her nesne (kesim, bulon, kaynak, komponent) aslı gibi
görünür, seçilir, etkisini uygular (kesim → hedef üyeyi keser); geri al →
kopya ve etkisi kalkar.

Durum: —

## 5. B5 — view cube (doğrula)
a) view cube'a tıklamak seçimi sıfırlamasın; komut sırasında tıklama
   komutu bozmasın. b) Bazı yüzler kayboluyordu (winding/eleme). AWS'de
   "yapıldı" denmişti; Windows'ta ekranda doğrula (GDI + GPU).

Durum: —

## 6. GT-1 — GUI test modu (tekrarlanabilir GUI testleri)
`model-maker.exe --test-script <senaryo.json> --out <klasör>`:
- Adımlar: komut id (registry), model aç (repo örnek modeli kopyası),
  tuval koordinatına tık/sürükle, tuş, metin yaz, bekle.
- Her adımda: PNG + durum JSON (seçim sayısı/tipi, aktif komut, nesne
  sayıları ve tipleri, görünür çizim listesi, hızlı arama sonuçları,
  katalog menüsü öğeleri, kamera).
- Bitince kendiliğinden kapanır; hata/zaman aşımında çıkış kodu ≠ 0.
- Render GDI ve GPU için ayrı alınabilir.
- ctest'e `gui` etiketiyle; varsayılan koşuda değil (`ctest -L gui`).
  Masaüstünde pencere açar: yalnız kullanıcı izin verince ya da DURAK'ta.
- İlk senaryolar: 1–5'teki hataların her biri (önce kırmızı, düzeltmeden
  sonra yeşil). PNG'leri vision ile incele.
- Kural: bundan sonra kullanıcıya görünen her düzeltme bir GT-1
  senaryosuyla kanıtlanır.
(1–5'i yaparken GT-1 yoksa Computer Use ile ekrandan doğrula; GT-1
bitince o hataların senaryolarını ekle.)

Durum: —

## 7. Sonra
- Windows'ta kayıtlı olmayan 24 test (Linux 177 / Windows 153): listele,
  koşabilecekleri aç.
- Linux CI (GitHub Actions; Qt6 + OCCT; offscreen; tam ctest).
- `docs/agent/ROADMAP-UI.md` sırası: UI-3 (+K1, K2, profilsiz çizgi
  kuralı) → **DURAK 1** → UI-4 → UI-5 → **DURAK 2** → UI-6b.
- İleride (backlog): K4 çizgiyi profile dönüştür, bağımsız levha komutu,
  146 canvas dönüşümü.

Durum: —


## 8. Tekla test planı (canlı, ekrandan; her senaryo ayrı)

Her senaryoda Tekla'daki beklenen davranış referans. Sonuç satırı:
`T-<no>: GEÇTİ | HATA H-<no> | ATLANDI (<neden>)` — bu bölümün altına yaz.

T-1 Aks: 6×5 aks (A–F, 1–5, 6000 mm), kotlar 0/7000/8500. Etiketler,
    kesişim ve orta nokta snap'i (B4), aks seçimi/silme/geri al.
T-2 Kolon: aks kesişimlerine HEB240 kolon (G kısayolu ve şerit); profil
    adı elle "heb240" → HEB240; olmayan profil → kırmızı + uygulanmaz.
    Dönüş 0/90/180/270; kaydet-aç sonrası aynı (A kuralı).
T-3 Kiriş: kolon tepeleri arası IPE300 kiriş; uç ofsetleri; eğik mertek
    (iki farklı kot); Kiriş komutu profilsiz çalışmasın.
T-4 Yardımcı çizgi: Çizgi/Polyline/Daire yalnız yardımcı obje (B6);
    GDI ve GPU'da görünür (B2/B7); Model Ağacı "Profiller"e girmez.
T-5 Seçim: tek/çoklu/kutu seçim, Parça/Montaj modu, view cube tıklaması
    seçimi bozmaz (B5), gimbal ekran-sabit (B3), Ctrl+Tab görünüm geçişi.
T-6 Özellikler paneli: renk/açı değiştir → UYGULA → seçim değiştir:
    başka nesne değişmez (A); çoklu seçimde yalnız değişen alan yazılır.
T-7 Düzenle: Taşı, Kopyala, Taşı-Δ/Kopyala-Δ, Doğrusal/Dairesel dizi,
    Ayna, Sil; her biri tek geri alma adımı; diyalog açıkken ana pencere
    kullanılabiliyor mu (K1 — henüz yoksa "ATLANDI: UI-3").
T-8 Kesimler: Plane Cut, Line Cut, Polygon Cut, Part Cut, Fitting; kesim
    kopyala (B8); kesim + geri al; OCCT çökmesi yok.
T-9 Bağlantılar: End Plate (kolon başlığına kiriş; HEB260+IPE300),
    Fin Plate, Haunch (40) (kolon + eğik mertek), 146. Katalogdan ve hızlı
    aramadan erişim (B1); diyalog mock view; parametre değiştir → güncelle;
    ihlal → kırmızı check; levhalar gri; geri al.
T-10 Bulon/kaynak: bulon grubu ve kaynak ekle/sil/kopyala; görünürlük.
T-11 Görünümler: plan görünüşü, 3B, yüzeye göre görünüş, çalışma düzlemi
    (3 nokta, sıfırla), görsel stiller (Alt+1..4), GPU/GDI geçişi (F6).
T-12 Kayıt: hepsini içeren modeli kaydet → kapat → aç; geometri, bağlantı,
    renk, dönüş birebir; eski bir model dosyası (repo örneği) açılıyor.
T-13 Kısayollar: G L Q M K O I T E D Del Esc, Ctrl+S/Z/Y, F3/F7/F8/F9,
    Ctrl+Tab; Türkçe klavye; metin alanı odaktayken tek harf tetiklemez.
T-14 Çizim: Paper Çizim açılır, temel görünüm üretilir (varsa).

Sonuçlar:

## Açık soru (yanıtla, Durum'a yaz)
- 893e770 "feat(document): çoklu seçim özellik düzenleme + tek geri alma
  adımı" istenmemişti. Ne yapıyor, hangi maddeye ait, testi var mı?
  (Geri alma yok, yalnız açıklama.)
