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

Durum: BİTTİ (kurulum) — cron işi `gece-mm` kuruldu (40dk, --repeat 14,
workdir C:\work\mm-hermes, kilit dosyası C:\work\mm-hermes-gece.lock
kuralı prompt'a gömülü, 09:00 sonrası kendini siler; ilk çalışma
2026-11-08 01:03). Aktif oturum kilit dosyasını 5 dk'da bir tazeliyor
(arka plan döngüsü); oturum bitince döngü ölür ve gece oturumları başlar.

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

Kayıp önleme (bilgisayar kapanabilir/uyuyabilir):
- Her hata kapanınca (H-xx ya da 0–6 maddesi yeşil) HEMEN push et;
  biriktirme. Push öncesi kapı: ilgili hedefler derlendi + o hatanın
  testi/senaryosu + tam ctest yeşil. (Tam ctest bu makinede ~6 dk.)
- Yarım iş: en geç her 20 dakikada bir `wip/hermes` dalına commit + push
  (kapıdan geçmesi gerekmez; mesajda "WIP"), STATE.md'ye "nerede kaldım"
  satırı ile.
- Her push'ta INBOX Durum satırı ve STATE.md güncel olsun; yeni oturum
  kapanmadan önceki yerden devam edebilsin.
- Oturum açılışında: wip/hermes, hermes/work'ten ileride mi? İleride ise
  önce oradan devam et (git log ile kontrol).

Commit kimliği (planlayıcı notu, 02:00): Bu makinedeki commit'lerin yazarı
kullanıcının git kimliği görünüyor; bu yüzden 893e770'nin "kullanıcının kendi
commit'i" olduğu sonucu güvenilir değil (Hermes'in önceki oturumu da aynı
kimlikle commit atıyor). Kural: C:\work\mm-hermes worktree'sinde yerel
`git config user.name "Hermes (Asus)"` ve `user.email "hermes-asus@local"`;
ZBook'ta "Hermes (ZBook)". Yalnız bu worktree, global ayara dokunma.

Uyanık kalma ve dayanıklılık (planlayıcı notu, 2026-11-08 sabah):
- KÖK NEDEN (gece 1): Asus "Modern Standby" (S0 Low Power Idle) kullanıyor.
  01:44'teki cron çalışması ekran kapanınca dondu, 10:04'te uyanınca
  APIConnectionError ile düştü; 01:44–10:05 arası hiç tur yok.
- Gece modu oturumu başlarken Windows'a "uyanık kal" isteği ver ve oturum
  boyunca tut: arka planda bir PowerShell süreci
  SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED)
  (0x80000003) çağırıp beklesin; oturum bitince süreci kapat. Kullanıcı
  ayrıca powercfg ile AC'de ekran/uyku zaman aşımını 0 yaptı.
- Ağ/API hatası (APIConnectionError, timeout): oturumu düşürme; 1, 2, 4
  dakika arayla 3 kez yeniden dene; yine olmazsa STATE.md'ye yaz, çık
  (bir sonraki cron turu devralır).
- Görüntü analizi: vision yardımcı modeli artık openai-codex · gpt-6.1-sol
  (low). Ekran testlerinde PNG + vision kullan; UIA metin dökümü ek kanıt.
  Kota hatası alırsan vision'sız (UIA) devam et ve Durum'a yaz.

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

Durum: BİTTİ — max_turns=150, gateway_timeout=3600 (config doğrulandı
2026-11-08; oturumda 60/60 limiti YOK; anahtar adları doğru).

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

Durum: BİTTİ — (1) deepseek-v4-pro gerçek pencere 1.048.576 token
(DeepSeek API docs + sağlayıcı kataloğu, iki kaynak). Sabahki 68
sıkıştırmanın nedeni: context_length 65.536'ya pin'liydi (agent.log
02:09). Pin 900.000 → 1048576'ya çıkarıldı (`hermes config set`; log'un
önerdiği gibi kaldırılmadı, kullanıcı "doğru değere çıkar" dedi —
varsayım: %90 yerine tam pencere). (2) Boş oturum tabanı: 16.012 → 11.972
(2026-11-07) → 11.989 (2026-11-08 ölçüldü). (3) image_generate/tts/video/
connections/kanban/x_search/yuanbao/a2a CLI toolset'inde KAPALI;
computer_use + vision AÇIK; cron scheduler (gateway) ÇALIŞIYOR — gece işi
kuruldu (üstte). Arşivlenen skill'ler (silinmedi): 83 skills-archived/
(apple, creative, email, media, mlops, note-taking, productivity,
research, social-media, yuanbao, hermes-desktop-plugins, hermes-themes,
companion-clis, devops, runpod*, smart-home, supabase-migrations,
data-science, deployed-web-change-verification, live-website-section-
change vb.) + 20 skills/.archive/ (eski model-maker/gpu sağlık vb.) —
~51 yüklenebilir skill kaldı. (4) Taban token değişmediği doğrulandı:
11.972 → 11.989 (pin değişimi tabanı etkilemez).

### 0c. GUI'yi arka planda başlat
Ön planda açılan `model-maker.exe` terminali pencere kapanana kadar
kilitler. Kural (skill'e yazılı olmalı): `./model-maker.exe &` ya da process
aracı, PID not al, Computer Use ile çalış, bitince PID ile kapat. Her
her uzun komutta `timeout` kullan.

Durum: BİTTİ — kural model-maker-build-verify skill'ine yazıldı (v1.1.0,
"GUI Launch Rule" + ekran-test mekanikleri: minimize/foreground/ALT-hack,
UIA fiziksel koordinat, InstantPopup press-hold, vision yoksa pencere
enumeration ile doğrulama); repo + yerel kopya senkron.

---

## 1. B1 — Haunch (40) katalogda/aramada yok — yamayı doğrula
Yama `wip/hermes` (e1ca5f0, ribbon_widget.cpp: Dropdown → menü düğmesi).
- Derle, ctest, ekranda: Çelik → Katalog tıkla → menüde yerleşik 4 tip
  (End Plate, Fin Plate, Haunch (40), 146) + yüklenen paketler.
- **Ayrıca hızlı arama:** kullanıcı "Haunch (40)" ve "haunch" yazınca HİÇ
  sonuç görmedi. Katalog düzelse bile arama ayrı sorun olabilir: arama
  dizini component registry dolmadan mı kuruluyor? Ekranda doğrula.
- Yeşilse wip'teki commit'i `hermes/work`'e al, push; `wip/hermes`'i sıfırla.

Durum: BİTTİ — kök neden (tek cümle): `setHost()` `setCatalog()`'tan SONRA
çağrıldığı için Katalog dinamik menüsü host null iken BOŞ kuruluyordu ve bir
daha dolmuyordu; hızlı arama da yalnız komut kaydını indeksliyordu. Düzeltme:
(1) qt_main_window.cpp sıra değişti, (2) ribbon_widget.cpp dinamik menü
aboutToShow'da host'tan yeniden dolar + runSearch/searchSuggestions/completer
bileşen katalogunu da arar (dynamicMenu fallback) + etiket tekilleştirme.
Test: model_maker_ribbon_widget_test 4 yeni kontrol (önce KIRMIZI, sonra
YEŞİL); tam ctest 153/153. Ekran doğrulaması (gece, kullanıcı yokken):
Katalog menüsü AÇILDI ve 4 yerleşik bileşen listelendi (Beam to Column End
Plate (144), Haunch (40), Shear Plate Simple (146), Fin Plate (Shear Tab));
quickSearch "haunch" → tamamlayıcı "Haunch (40)" → Enter → komut başladı
(StatusBar: "Haunch (40) - parça seçin... sağ tık uygular"). Kanıt:
C:\work\mm-hermes-shots\2026-11-08\ (b1-kanit.txt + png). Commit'ler
hermes/work'te; wip/hermes sıfırlandı.

## H-1 — "Yüzeye Göre Görünüş" (view.toSurface) bozuk  [kullanıcı, 2026-11-08 sabah]
Kullanıcı: "yüzeye göre yeni görüntü alma bozulmuş". Komut: Görünüm →
Yüzeye Göre Görünüş (`view.toSurface` → `Application::startViewToSurfaceCommand`),
"parça yüzüne tıklayarak o yüze dik görünüş aç".
Belirti (kullanıcı): yüz tarama/vurgulama çalışıyor (imleç altındaki yüz
işaretleniyor) ama tıklayınca sonunda YENİ GÖRÜNÜŞ AÇILMIYOR. Yani seçim
aşaması sağlam; komutun son adımı (görünüş oluşturma / pencere açma /
tıklamanın komuta teslimi) kopuk. Önce şuna bak: tıklama komuta ulaşıyor mu
(log), ulaşıyorsa görünüş belgeye ekleniyor ama pencere mi açılmıyor
(UI-2'de pencere/görünüş açma çağrısı registry'ye taşınırken kaybolmuş
olabilir; views.* komutları da aynı yolu kullanıyor mu?). Yeniden üret:
- HEB240 kolon + IPE300 kiriş; komutu şeritten başlat; kirişin üst başlık
  yüzüne, gövde yüzüne ve levha yüzüne tıkla → her biri için o yüze dik
  yeni görünüş açılmalı (görünüş adı, yön, çalışma düzlemi doğru).
- Şüpheliler: UI-2 komut yönlendirici (tuval tıklaması komuta mı gidiyor?),
  B5 view cube tıklama tüketimi, B3 gumball, OCCT 7.9.2 indirgemesi (yüz
  seçimi OCCT ile mi?). 9e548f3 (UI-2 öncesi) ile karşılaştır; gerekirse
  ikiye bölme (bisect) — her adımda yalnız model-maker hedefini derle.
- Kırmızı test/senaryo → düzeltme → ekranda (vision) doğrula → push.
Öncelik: B6'dan ÖNCE (kullanıcının günlük kullandığı komut).

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

Durum: YANIT — 893e770 kullanıcının KENDİ commit'i (Selahattin Babadağ,
2026-11-07 20:02). Çoklu seçimde özellik düzenleme: applySelectedEntityBatch
ad/yüzey işlemi/rotasyon/kolon kotlarını tek UndoOp::PropertyReplace ile
uygular (tek geri alma adımı = tek snapshot); seçim tektip değilse panel boş
gösterilir. Testi var: model_maker_document_property_edit_test (ctest 150.).
Ait olduğu madde: T-6 (çoklu seçimde yalnız değişen alan yazılır) / UI-3.
İptal gerekmiyor.
