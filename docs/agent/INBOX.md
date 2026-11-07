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
   video, cron, connections. **Computer Use ve vision AÇIK kalsın.**
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

## Açık soru (yanıtla, Durum'a yaz)
- 893e770 "feat(document): çoklu seçim özellik düzenleme + tek geri alma
  adımı" istenmemişti. Ne yapıyor, hangi maddeye ait, testi var mı?
  (Geri alma yok, yalnız açıklama.)
