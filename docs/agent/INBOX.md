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

## ÇALIŞMA MODU: MANUEL (2026-11-08 23:12 — kullanıcı kararı; SÜREKLİ MOD askıda)

Cron ile otomatik başlatma YOK (`surekli-mm` duraklatılır/silinir). Kullanıcı
Hermes'e "başla" der; Hermes:
1. pull → STATE.md + INBOX.md oku.
2. Sıradaki açık maddeden başlayıp maddeleri SIRAYLA yapar (aşağıdaki
   "Sıra" satırı geçerli). Her madde bitince commit + push + kabul paketi
   + INBOX Durum + STATE "Son tur" + bip, sonra bir sonraki maddeye geçer.
3. Durur: kullanıcı "dur" derse, KARAR BEKLİYOR çıkarsa, kapı kırmızı
   kalırsa ya da oturum çok uzarsa (bu durumda STATE yaz, "yeni oturum
   öneriyorum" de).
Diğer bütün kurallar (45 dk / 20 dk WIP, kayıp önleme, git yasakları,
kabul paketi, GUI yalnız kullanıcı boştayken, regresyon senaryoları) aynen.
Planlayıcı (Claude) görevleri yalnız bu dosyaya yazar.

## KABUL PAKETİ (kalıcı kural, 2026-11-08) — kullanıcı artık kendisi derlemez

Kullanıcıya görünen bir düzeltme/özellik push kapısından geçince (Windows
tam derleme + tam ctest yeşil + kullanıcı boştaysa `ctest -L gui` yeşil),
Hermes kullanıcının deneyeceği çalışır exe'yi hazırlar:
- Klasör: `C:\work\mm-kabul\<YYYYMMDD-HHMM>-<kısa hash>\` içine
  build-hermes'ten `model-maker.exe` + exe'nin yanında gereken her şey
  (profiles/ vb. — build'in exe dizinine kopyaladıkları).
- Aynı klasöre `calistir.cmd`: PATH'e `C:\msys64\ucrt64\bin` ve
  `QT_PLUGIN_PATH=C:\msys64\ucrt64\share\qt6\plugins` ekleyip exe'yi
  başlatır (çalışma dizini = paket klasörü).
- Aynı klasöre `NELER-YENI.txt` (Türkçe, kısa): hangi commit'ler, kullanıcının
  ekranda kontrol etmesi gereken maddeler (adım adım: ne yap → ne görmelisin),
  bilinen eksikler.
- Masaüstüne tek kısayol: `Model Maker (son kabul).lnk` → en yeni paketin
  `calistir.cmd`'si (her yeni pakette güncellenir).
- Kalıcı bir yol daha: `C:\work\mm-kabul\son\` = en yeni paketin kopyası
  (ya da junction).
- Paketi oluşturduktan sonra `calistir.cmd`'yi bir kez başlatıp pencerenin
  açıldığını doğrula (kullanıcı boştaysa), sonra kapat.
- En fazla son 5 paketi tut; eskileri silmek yerine `C:\work\mm-kabul\_eski\`
  altına taşı (silme yetkisi istemez).
- Kullanıcının kabul klasörü `C:\work\model-maker-p2d` ve build-occ'ye
  DOKUNMA.
- STATE.md "Son tur" satırına paket adını ekle: `kabul: <klasör>`.
Paket yalnız kapı yeşilken; kırmızıysa paket yok.

## SÜREKLİ MOD (ASKIDA — yerine ÇALIŞMA MODU: MANUEL)

Amaç: kullanıcı beklemeden kesintisiz geliştirme. Döngü:
Hermes (cron) iş yapar → push → planlayıcı (Claude) düzenli aralıklarla
repoyu okur, inceler, INBOX'a yeni iş/hata/öncelik yazar → Hermes'in bir
sonraki turu onu alır. Kullanıcı yalnız hata bildirir ve son görsel
kontrolü yapar.

Tur süresi ve öncelik (planlayıcı, 2026-11-08 18:32):
- Bir tur EN FAZLA 45 dakika. 45. dakikada nerede olursan ol: kapıdan
  geçmiş iş → push; geçmemiş iş → wip/hermes'e commit + push; STATE "Son tur"
  satırına "DEVAM" yaz ve turu bitir. Sonraki tur devralır.
- 20 dakikalık WIP push zorunlu (17:27 turu 60+ dk push'suz kaldı).
- Kullanıcı boşta değilse GUI/ekran testini BEKLEME; GUI'siz kısmı yap,
  Durum'a "GUI doğrulaması bekliyor" yaz, turu bitir.
- Sıra (bu satır INBOX'taki madde sırasını EZER): H-8 → H-7 → UI-34 → H-6 → H-5 →
  H-2 → B8 → B5 → T-planı. Yarım kalan madde varsa önce onu bitir.

Hermes kurulumu (bir kez; eski `gece-mm` işini sil, yenisini kur):
- cron `surekli-mm`: her 30 dakikada bir, süresiz (repeat yok), workdir
  C:\work\mm-hermes, YENİ oturum. Prompt: "git pull --ff-only origin
  feature/component-contract; docs/agent/STATE.md + INBOX.md oku; SÜREKLİ
  MOD kurallarıyla sıradaki açık maddeyi yap."
- Kilit dosyası C:\work\mm-hermes-surekli.lock (60 dk'dan yeniyse çık;
  kullanıcının elle açtığı oturum da kilidi tutar).
- Gece modu kuralları (uyanık kal, API yeniden deneme, kayıp önleme,
  yasaklar, git yasakları) aynen geçerli.

Tur kuralları:
- Bir turda EN FAZLA bir INBOX maddesi (ya da onun bir parçası); bitince
  Durum satırı + STATE.md + push. Uzun madde: parçala, her parça push.
- GUI/ekran testi yalnız kullanıcı 2 dk boşta iken (GetLastInputInfo);
  değilse GUI'siz kısımları yap (kod, birim test), GUI doğrulamasını
  Durum'a "GUI doğrulaması bekliyor" diye yaz, sonraki turda dene.
- Açık madde kalmadıysa: "8. Tekla test planı"ndan sıradaki senaryoyu
  (GUI test modu ile, kullanıcı boştaysa) çalıştır; hata bulursan H-xx aç.
- Planlayıcıya sinyal: her tur sonunda STATE.md başına
  `Son tur: <tarih saat> — <madde> — <BİTTİ|DEVAM|TAKILDI> — <hash>`
  satırını güncelle. Planlayıcı bu satırı okuyarak ilerlemeyi izler.
- Karar gerektiren konu: Durum'a "KARAR BEKLİYOR: <soru>" yaz, sonraki
  maddeye geç. Planlayıcı INBOX'ta cevaplar.

## GECE MODU (arşiv — SÜREKLİ MOD ile değiştirildi) (2026-11-08 gecesi, sabaha kadar) — kullanıcı uyuyor

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

GIT YASAKLARI (kalıcı, kullanıcı kuralı 2026-11-08 — skill'de de yazılı):
uzak dal silme (`git push origin :dal`) ve force push HİÇ yapılmaz (onay
ister, gece modu kilitlenir). wip/hermes SIFIRLANMAZ: yeni yarım iş
üstüne normal commit olarak eklenir; feature dalına alınmış eski WIP
commit'leri orada kalır, zararı yok.

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


Öncelik notu (planlayıcı, 2026-11-08 öğle): Ekran otomasyonu (Computer Use /
ön plan / gerçek tık) Windows ön plan kilidi yüzünden çok adım yiyor (H-1'de
150 adımın çoğu). GT-1 (uygulama içi --test-script modu) B6'dan hemen sonra
yapılacak; ondan sonra ekran testleri yalnız son kontrol. Sıra: H-3 → B6 (wip'te
yarım, 7ca487e) → GT-1 → H-2 → B2/B7 → B8 → B5 → T-planı.

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

Durum: BİTTİ (inceleme + kanıt + tanı; kod hatası HEAD'de üretilemedi) —
Kök neden tek cümle: tıklama yolu (onLeftButtonDown ViewToSurface dalı,
pickSceneFaceAt, twoPointViewCallback_, openTwoPointView) 9e548f3'ten beri
DEĞİŞMEMİŞ ve ekranda hem sentez hem GERÇEK fare tıklamasıyla çalıştığı
kanıtlandı; kullanıcı belirtisi bu derlemede üretilemedi (varsayım: kullanıcı
exe'si bayat ya da başka yapı — p2d kaynağı 83c686f'de aynı kod). Yapılan:
(1) TANI: startViewToSurfaceCommand + tıklama dalına model-maker-trim.log'a
yazan VIEW-TO-SURFACE-START/CLICK/FACE/NO-FACE/NO-CALLBACK satırları — sorun
kullanıcı makinesinde tekrarlarsa log kök nedeni gösterecek. (2) SÖZLEŞME
TESTİ: tests/run_view_to_surface.py + CMake'te model_maker_view_to_surface
(Windows'ta da koşar; 18 kontrol + 4 mutasyon — callback silinirse KIRMIZI).
(3) EKRAN (gerçek tık, ön plan doğrulaması ile): kolon yüzü → "Görünüş 1 —
yüzey görünüşü, derinlik 100/100 mm" açıldı, log normal=0,1,0 (yön doğru);
Plan Görünüşü → "Plan Görünüşü 2 — plan, Z = 0 mm" (aynı callback yolu);
2 Noktalı Görünüş → "Görünüş 3" açıldı. Kanıt:
C:\work\mm-hermes-shots\2026-11-08\h1\ (h1-yuzey-gorunus-1.png,
h1-plan-gorunus-2.png, h1-plan-diyalog.png, h1-trim-kanit.txt).
SONRAKİ: kullanıcı kendi build'ini yenileyip deneyecek; hâlâ bozuksa
model-maker-trim.log'daki VIEW-TO-SURFACE satırlarıyla devam.

## H-3 — Görünüm sekmesini sadeleştir: iki açılır menü  [kullanıcı kararı, 2026-11-08]
Kullanıcı: şerit çok karışık. Kural (Tekla'daki gibi):
- **"Görünüş Oluştur"** — TEK açılır düğme; yeni görünüş açan HER komut
  bunun içinde: Yüzey Görünüşü (`view.toSurface`), 2 Noktalı, Plan, 3 Noktalı,
  Çalışma Düzleminde, parça düzlemleri (ön/üst/uç/arka/alt), varsayılan parça
  görünüşleri, parça 3B, aks görünüşleri. Başka yerde (Görünüm grubu,
  "Görünümler" grubu) bu komutlar YOK.
- **"Çalışma Düzlemi"** — TEK açılır düğme; tüm UCS / çalışma düzlemi
  komutları içinde: Yüz Aracı, XYZ+D, 1/2/3 Nokta, Görünüşe ∥, UCS Dik,
  Düzlem Sıfırla, Hayalet, Adlandır / Git / Sil. Ayrı küçük düğmeler YOK.
- Görünüm sekmesinde geriye: Görünüm (2B/3B, Extents, Zoom Pencere, Sıfırla),
  Yön, Stil, Gezin, Teşhis + bu iki açılır. Grup/düğme sayısını azalt.
- ≡ menüde aynı iki alt menü (Görünüş Oluştur, Çalışma Düzlemi).
- Kısayollar aynen çalışır (F7 Hayalet vb.).
- command_catalog_test: view-oluşturan tüm komutlar yalnız "Görünüş Oluştur"
  altında; wp.* yalnız "Çalışma Düzlemi" altında.
- Ekranda doğrula (vision): Görünüm sekmesinin ekran görüntüsü.
- Küçük iş: B6'dan önce, ayrı commit, push.

Durum: BİTTİ (H-3 kodu tamam; commit ctest sonrası) — Görünüm sekmesi:
Görünüm grubu 4 büyük düğme (2B/3B, Extents, Pencere, Sıfırla; toSurface
çıktı) + "Görünüş Oluştur" TEK açılır (toSurface, 2N, Plan, 3N, çalışma
düzleminde, parça ön/üst/uç/arka/alt, varsayılan, parça 3B, aks görünüşleri)
+ "Çalışma Düzlemi" TEK açılır (Yüz Aracı, XYZ∥, 1/2/3 Nokta, Görünüşe∥,
UCS Dik, Sıfırla, Hayalet, Adlandır/Git/Sil — ayrı küçük düğme YOK). ≡
menüde aynı iki alt menü; eski "Çalışma Düzlemi" ve "Görünüşler" menü
sekmeleri kaldırıldı. Kısayollar aynen (F7 Hayalet vb.). Test:
command_catalog_test'e H-3 bölümü eklendi (her görünüş/wp komutu seritte
ve menüde TAM bir kez; Görünüm grubu 4 düğme; açılır kimlikleri
views.create/wp.all) — YEŞİL. Ekran (vision): sekme + iki açılırın içeriği
doğrulandı. Kanıt: C:\work\mm-hermes-shots\2026-11-08\h3\
(h3-gorunum-sekme.png, h3-gorunus-olustur-menu.png,
h3-calisma-duzlemi-menu.png).

## H-4 — Tutulan snap tıklamayı uzak noktaya kaydırmasın (B6'nın devamı)  [planlayıcı, 2026-11-08 16:55]
09d277e yalnız araç değişiminde `heldSnap_`'i sıfırlıyor. Aynı hata tek komut
içinde de olur: imleç uç noktadan hızla uzaklaşıp 300 ms içinde tıklanırsa
tıklama eski noktaya kayar. Kural: tutulan snap, imleç tutulan noktadan
histerezis/yakalama yarıçapından uzaktaysa süreye bakmaksızın geçersizdir;
tıklama her zaman imlecin o anki konumuna göre çözülür. Tutma süresi yalnız
görsel marker içindir. GT-1 senaryosu: aynı komut içinde uç noktadan 200 px
uzaklaş, 100 ms içinde tıkla → tıklama imlecin olduğu yerde (önce kırmızı).

Durum: BİTTİ (cb9506f) — Kök neden: heldSnap_ holdTimeMs (300 ms) boyunca
süreye bağlı korunuyordu; imleç tutulan noktadan 200 px hızla uzaklaşıp
300 ms içinde tıklarsa tıklama eski snap noktasına çözülüyordu (kirişin iki
tıklaması da çizgi ucuna kaçtı, kiriş hiç üretilmedi). Düzeltme: tutma
yalnız histerezis yarıçapına bağlı; uzaksa süreye bakmaksızın geçersiz
(tutma süresi yalnız görsel marker). GT-1 h4_held_snap_release_distance
(moveClick + axisScreen adımları koşucuya eklendi): önce KIRMIZI
(profiled=0), sonra YEŞİL (14 adım; kiriş ekseni tıklanan (650,300)'de);
vision: kiriş çizgi ucundan 200 px uzakta başlıyor. ctest -L gui 6/6,
tam ctest 160/160. SÜREKLİ MOD kuruldu (surekli-mm cron, 30dk; gece-mm
silindi) — bu tur ilk turdu.

## H-8 — Haunch (40) yine bozuk görünüyor: kolon mertekten ayrık  [kullanıcı, 2026-11-08 23:08 — ÖNCELİK 1]
Kullanıcı ekran görüntüsü (kabul paketi 20261108-2224-29cc028): mertek ucu
haunch levhalarıyla duruyor, KOLON ise belirgin aşağıda ve ayrık (kolon
tepesi mertek ucunun çok altında). Kullanıcı: "bir yeri yaparken diğer yeri
bozuyoruz". Kullanıcı: model YENİ kuruldu (eski dosya değil) ve kolonun ALT ve ÜST
kotunu PANELDEN girdi ("Kolon Alt/Üst (mm)" alanları). Şüphe güçlendi:
29cc028 panelinde kolon kotlarının uygulanması (ENTER ile doğrudan
uygulama, hangi nesneye yazıldığı, birim/bağıl-mutlak yorumu, uygulama
sonrası bağlı Haunch'ın yeniden üretilmesi). Senaryo: kolon kur → panelden
Alt=0, Üst=7000 gir (ENTER ve UYGULA ayrı ayrı) → mertek kur → Haunch (40).
Ayrıca: Haunch kurulduktan SONRA kolon kotu panelden değiştirilirse
Haunch yeniden üretilmeli ve kolon/mertek bağlantısı korunmalı (Tekla gibi).
Şüpheliler (kontrat golden'ları yeşil → büyük olasılıkla UYGULAMA katmanı):
- 29cc028 yeni PropertiesPanel: "Kolon Üst/Alt (mm)" ENTER ile doğrudan
  uygulanıyor; seçim değişince / çoklu seçimde / mertek seçiliyken kolon
  kotları yanlış nesneye yazılıyor olabilir (A sınıfı veri hatası).
- 893e770 çoklu seçim özellik düzenleme (document.cpp) — istenmemiş commit.
- Haunch otomatik yeniden üretimi (recompute) kolonun eski/yeni geometrisini
  karıştırıyor olabilir.
İstenen:
1. Kök nedeni bisect ile bul (15a41fb ↔ 29cc028 arası ve 893e770).
2. **Regresyon kalkanı (kalıcı):** GT-1 `t9_haunch40_e2e`: kolon (HEB240,
   0→7000) + eğik mertek (IPE300) kur → Haunch (40) uygula → expect: kolon
   tepesi = 7000, mertek ucu kolon başlığında, haunch plan digest'i
   kontrat golden'ıyla aynı; sonra panelden mertekte profil değiştir +
   UYGULA → kolon kotları DEĞİŞMEMİŞ, haunch yeniden üretilmiş ve tutarlı;
   kaydet-aç → aynı. Aynısı End Plate için (`t9_endplate_e2e`).
3. Bu iki senaryo bundan sonra HER push kapısında koşar (kullanıcı boşta
   değilse de: `--test-script` gizli/arka plan modunda koşabiliyorsa öyle;
   koşamıyorsa push'u bir sonraki boş ana ertele, wip'e it).
4. Kök neden + hangi commit bozdu + neden testler yakalamadı (bir cümle).

Durum: TAMAM (2026-11-09). Kök neden: panel kot uygulaması (setSelectedEntityColumnLevels)
yalnız kolon katısını yeniden üretiyordu; kolon tepesine oturan ikincil üye uçları eski
kotta kalıyor (mertek/kolon ayrıklığı) ve bağlı bileşenler (End Plate/Haunch) yeniden
hesaplanmıyordu. Regresyon değil, eksik zincir (15a41fb'de de aynıydı). Düzeltme:
reanchorSecondaryMembers (bağlı üye ucunu yeni kota sabitler, diğer ucu sabit — Tekla
davranışı) + recomputeEndPlateConnectionsFor + recomputeComponentInstancesFor tek undo
kaydında; başarısızlıkta tam rollback + false. Testlerin yakalamama nedeni: kolon yeniden
üretimini doğrulayan tek test (run_props_column_levels.py) bağlı üye/bileşen zincirini
ölçmüyordu. Kalkan: GT-1 t9_haunch40_e2e (34 adım) + t9_endplate_e2e (28 adım) eklendi,
ikisi yeşil; ctest 155/155; python props testi OK.

DEVAM (2026-11-09, ikinci bulgu — kullanıcı: "haunch lar bozulmuş"): yeni kurulumda
(panel kotu değişmeden) haunch LEVHALARI kirişin İÇİNDE ve kolon tepesinin üstünde
çıkıyordu; varsayılan parametrelerle (240/25) üretim "iki bulon grubu alin levhasina
sigmiyor" ile RED ediliyordu. Kök neden: Top-ankraj (varsayılan) kesitte
memberGeometryFromProfile kanonikleştirme refNormal'i çevirince sectionCenterOffset'ı
da işaret çeviriyor; resolveShearTabPlacement + haunchFrameAnalysis ise ankraj kaymasını
YENİDEN kurulan p.sec (lenDir + worldUp) çerçevesinde uyguluyordu → kayma ters yöne gitti
(yukarı), haunch gövdesi kiriş gövdesine girdi, alın levhası kırpılınca 263 mm'ye küçüldü.
Düzeltme: kayma yönü üyenin KENDİ kanonik çerçevesinden (memberSectionFrame(beam).height;
end plate yolunun zaten kullandığı) alınır. Kalkan: tests/haunch_topanchor_placement_test
(20 kontrol, varsayılan parametrelerle plan OK + levha/gövde/kesim konumları) + GT-1
t11_haunch_default_visual (22 adım, front/iso/side PNG — vision ile doğrulandı). ctest
157/157; t9'lar ve t10 yeşil. Kullanıcının "farkli ve gercek kolon/kiris kesitleri"
hatası ayrı bir dal: aynı parçayı iki kez seçme veya katalogda olmayan profil seçimi —
kullanıcı modeliyle doğrulanacak (model dosyası isteniyor).

## H-7 — 3B görünümde çizgi çizilmiyor / görünmüyor (B2/B7 HÂLÂ AÇIK)  [kullanıcı, 2026-11-08 17:30, kabul paketi 20261108-1716-cb9506f]
Kullanıcı kabul paketinde: 3B görünümde Çizgi komutu → çizgi yok/görünmüyor.
GT-1 `b2b7_line_visible` YEŞİL ama yanlış koşulu ölçüyor: varsayılan
(2B/plan) görünümde ve GDI'da koşuyor olmalı. Senaryoyu düzelt (önce KIRMIZI):
- `view.toggle3d` (ya da İzometrik) → 3B görünüm; backend hem `gdi` hem `gl`
  (durum çubuğu "GPU: GL" — kullanıcı GL'de).
- Çizgi: iki tık + Esc. Expect: model/yardımcı obje sayısı +1 VE görünür
  çizim listesinde çizgi VE capture PNG'de çizgi (vision).
Ayrıca ayır: (a) çizgi hiç oluşmuyor mu (3B'de tıklama çalışma düzlemine
izdüşmüyor / nokta alınmıyor), (b) oluşuyor ama çizilmiyor mu (3B/GL
renderer yardımcı objeleri — B6 sonrası "profilsiz = yardımcı" — atlıyor
olabilir), (c) çiziliyor ama kamera/kırpma dışında mı. Kök nedeni bir
cümleyle yaz. Birim test ve önceki senaryo neden yakalamadı, bir cümle.
ÖNCELİK: en üst (kullanıcının temel çizim akışı). H-5'ten önce.

Durum: BİTTİ (15a41fb) — Kök neden: GL çizici Solid stilde (faceAlpha==255)
lineBatch'i hiç çizmiyor ve kontur üreticisi yüzü boş modelleri atlıyordu
→ Çizgi/Polyline/Dikdörtgen/Daire hiçbir GL pass'inde yoktu (GDI'da sorun
yok; kullanıcı GL'de olduğu için görüyordu). Model listesine ekleniyordu,
yalnız RENDER eksikti — (b) sınıfı. Düzeltme: Solid için yüzü boş modelleri
taşıyan ayrı freeLineBatch_ (konturun üstüne derinliksiz; HiddenLine'da
lineBatch zaten taşır). Senaryo düzeltildi (önce KIRMIZI): b2b7_line_visible
(gdi) + b2b7_line_visible_gl (gpu), ikisi de kesin 3B (view.dir.iso) + Solid;
koşucuya expect.lineVisible piksel kanıtı eklendi — GL düzeltme öncesi
KIRMIZI (0/101), sonra YEŞİL. Vision: step_008 çizgi yok → step_009 çizgi
var (B6 sonrası "profilsiz = yardımcı" atlaması DEĞİL; B6'dan bağımsız,
eski GL Solid tasarım açığı). Birim test/senaryo neden yakalamadı: eski
senaryo backend'i gdi'ye zorluyordu ve yalnız model SAYISI ölçüyordu
(visibleCount = görünürlük bayrağı, piksel değil). Kapı: ctest -L gui 8/8,
tam ctest 162/162; kabul paketi yayınlandı.

## UI-34 — Özellikler paneli (Tekla gibi) + "..." profil seçim penceresi  [kullanıcı isteği, 2026-11-08 17:33 — H-7'den hemen sonra]
Tam tarif: `docs/agent/ROADMAP-UI.md` → UI-3 ve UI-4 (+ K1, K2, profilsiz
çizgi kuralı). Bu turda şu kesit, sırayla, her parça ayrı commit + push +
kabul paketi:
1. **UI-3a — panel iskeleti:** sağ dock'ta "<tip> (N seçili)" başlıklı,
   daraltılabilir bölümler. Kiriş/kolon için Genel (Ad, **Profil [metin +
   "..."]**, **Malzeme [metin + "..."]**, Sınıf, Yüzey işlemi), Konum
   (düzlemde / dönüş / derinlik — mevcut alanlarla eşleşen neyse), Uç ofseti
   (başlangıç/bitiş Dx Dy Dz). Alt çubukta Uygula/Değiştir (A kuralı: yalnız
   Uygula yazar; çoklu seçimde yalnız değişen alan). Profil elle yazılır,
   büyük/küçük harf duyarsız (`heb240` → `HEB240`), otomatik tamamlama;
   katalogda yoksa kırmızı + uygulanmaz. Eski "Profil (aktif)" açılır
   listesi ve "Profilsiz (düz çizgi)" seçeneği KALKAR. Aktif profil
   (yeni Kolon/Kiriş için) aynı alandan.
2. **UI-4 — profil seçim penceresi ("..."):** solda aileye göre ağaç (HEA,
   HEB, HEM, IPE, UPN, kutu, boru… katalogda ne varsa), üstte Filtre (`*`
   joker, ör. `HE*3*`), profil adı alanı (ağaçla senkron); sağda tip/alt
   tip, kesit alanı, **kesit resmi kendi şematik motorumuzla** (ölçü harfleri
   h b tw tf r), özellik tablosu (Özellik / Sembol / Değer / Birim);
   altta Tamam / Uygula / İptal (Uygula alana yazar, pencere açık kalır).
   Malzeme "..." için aynı yapıda basit liste. Tekla ikon/resim/marka YOK.
3. GT-1 senaryoları: (a) profil alanına `heb240` yaz → HEB240, Uygula →
   seçili kirişin profili değişir, tek geri alma; (b) "..." → filtre `IPE3*`
   → IPE300 seç → Tamam → alan IPE300; (c) geçersiz ad → kırmızı, uygulanmaz.
   PNG'ler vision ile; panelin ve pencerenin ekran görüntüsü NELER-YENI'de.
Kalan UI-3 parçaları (levha/bulon/kaynak/komponent bölümleri, K1 modsuz
pencereler, K2 Temizle, yardımcı obje görünümü + migration) bundan sonra.

Planlayıcı incelemesi (2026-11-08 23:00, 29cc028): iskelet onaylandı (Ad,
Profil+"…", Malzeme+"…", Sınıf, Yüzey işlemi, Dönüş, kirli bayrak, mutasyon
koşucuları 9/9-10/10). Eksik kalan UI-3a alanları — UI-4'ten SONRA ayrı
kesit: (a) **Uç ofseti** bölümü (başlangıç/bitiş Dx Dy Dz) — modelde alan
yoksa önce rapor et (Durum'a "modelde yok" yaz, uydurma); (b) Konum:
düzlemde (Orta/Sol/Sağ + mm) ve derinlik (Önde/Ortada/Arkada + mm) — aynı
kural. Başlık "<tip> (N seçili)" seçim değişince doğru mu, GT-1 ile kanıtla.

Durum: KESIT 1 BİTTİ (29cc028) — UI-3a panel iskeleti: PropertiesPanel sınıfı
(sağ dock; Genel: Ad/Profil/Malzeme/Sınıf/Yüzey işlemi, Konum: Dönüş,
Görünüm: Katman/Çizgi Tipi, Kolon: Üst/Alt kot+Uzunluk; UYGULA+KOLON
YERLESTİR). Ribbon profil combo KALKTI; tek profil alanı paneldeki QLineEdit
(ENTER doğrudan uygular, katalog 1 kez, harf duyarsız, katalog dışı ad
kırmızı+uygulanmaz). A kuralı: yalnız UYGULA yazar (dirty flags + tek undo).
3 Python testi yeni sözleşmeye uyarlandı (mutasyon 9/9 + 10/10 yakalıyor);
C++ offscreen properties_panel_test eklendi. Windows kapısı: tam derleme +
ctest 155/155. GUI doğrulaması YAPILDI (kullanıcı boşta): pencere açıldı,
panel görünür (Genel/Konum/Görünüm/Kolon, profil HEB240, kolon üst 8500),
ribbon'da eski combo yok, hata yok. Kabul paketi:
C:\work\mm-kabul\20261108-2224-29cc028. Kalan: UI-4 ("..." penceresi) + GT-1
senaryoları.

Durum: KESIT 2 BİTTİ (bu commit) — UI-4 profil seçim penceresi: solda aile
ağacı (katalog aileleri), üstte Filtre (`*` joker), profil adı alanı (ağaçla
senkron, çift tık seçer); sağda tip/alt tip, kesit alanı, kendi şematik
motorumuzla kesit resmi (h b tw tf r ölçü harfleri), Özellik/Sembol/Değer/
Birim tablosu; altta Tamam/Uygula/İptal (Uygula alana yazar, pencere açık).
Paneldeki Profil ve Malzeme "..." butonları etkinleştirildi (eskiden pasif).
Refresh timer dirty koruması: kullanıcı panelde yazarken 500 ms refresh
alanı EZMEZ (isProfileDirty kapısı; GT-1 t10'da yakalandı). GT-1
t10_panel_profile_picker: heb240 yaz → HEB240, Uygula → model HEB240, tek
geri alma → IPE360; "..." → filtre IPE3* → IPE300 → Tamam → alan IPE300
(25/25). Offscreen C++ test profile_picker_dialog_test eklendi (filtre,
showAll, senkron, kanonik ad). ctest 156/156; t9 ikisi de yeşil; 3 Python
props testi yeşil. Kalan (ayrı kesit): Uç ofseti (Dx Dy Dz) ve Konum
düzlemde/derinlik bölümleri — modelde alan yoksa rapor edilir; levha/bulon/
kaynak/komponent bölümleri, K1 modsuz pencereler, K2 Temizle.

## H-6 — holdTimeMs artık hiçbir şey yapmıyor (H-4 yan etkisi)  [planlayıcı incelemesi, cb9506f]
H-4 doğru: tıklama artık süreye bağlı değil. Ama `snapSettings_.holdTimeMs`
kodda hiç okunmuyor (yalnız ayar dosyası). Kullanıcının istediği "marker
snap noktasında bir süre dursun" davranışı kayboldu. İstenen: imleç
histerezis dışına çıkınca son marker holdTimeMs boyunca YALNIZ GÖRSEL
olarak (soluk) çizilmeye devam etsin; tıklama/hover sonucu değişmez
(H-4 kuralı korunur). GT-1: uzaklaşınca 100 ms'de marker görünür ama
tıklama imleç konumunda; 400 ms'de marker yok.

Durum: BİTTİ (133fe71) — histerezis dışına çıkınca heldSnap_ hemen reset
(H-4 korunur); marker fadingSnap_ olarak holdTimeMs boyunca YALNIZ GÖRSEL
soluk çizilir (timer 8 + draftView; tıklama/hover sonucu DEĞİŞMEZ). GT-1
h6_held_snap_visual_hold: held→fading→gone zamanlama + tıklama çözünürlüğü
(vertexScreen 650,300 — tıklama eski snap noktasına kaçmıyor) + captureLive
görsel kanıt (soluk kare eski snap noktasında @~452,302 var; 600 ms sonra
yok). ctest -L gui 7/7, tam ctest 161/161. Not (bulgu): düz çizgilerde
axisFrom doldurulmuyor (h4'te beam profil ataması dolduruyordu) → GT-1'e
vertexScreen beklentisi eklendi.

## H-5 — H-1 senaryosu doğru şeyi mi ölçüyor? (planlayıcı incelemesi)
GT-1 raporu: kirişin üst yüzüne tıklayınca "Plan Görünüşü 1 — plan, Z=0"
açıldı. Şüpheli: yüzey görünüşü yüzün kendi düzleminde olmalı (kiriş üst
yüzü Z = üst başlık kotu, 0 değil) ve başlık "yüzey görünüşü, derinlik …"
olmalıydı (H-1'in ekran testinde öyleydi). Kontrol et: senaryo gerçekten
`view.toSurface` mı çalıştırıyor, yoksa Plan Görünüşü mü açılıyor? Z=0 doğru
mu (kiriş Z=0'da mı modellenmiş)? Senaryoya beklenen başlık ve kot için
expect ekle; kiriş kotunu 0'dan farklı yap (ör. 3000) ki ayırt edici olsun.
Ayrıca gövde (yan) yüzüne tıklama adımı ekle → dikey düzlemde görünüş.

Durum: TAMAM (2026-11-09). Senaryo gerçekten view.toSurface çalıştırıyordu; sorun
BAŞLIKTAYDI: isPlanView (normal ±Z) üst yüzü plan sanıp "Plan Görünüşü — plan, Z=0"
yazıyordu (kiriş Z=0'da, Top ankraj → üst yüz de 0'da). Düzeltme: ViewDefinition.fromFace
bayrağı — yüzey görünüşleri (yatay yüz dahil) her zaman "yüzey görünüşü, derinlik …"
başlığını ve "Görünüş N" adını alır. Senaryo güncellendi: kiriş Z=3000'de (assignLine),
üst yüz → viewTitle (yüzey görünüşü, Plan DEĞİL) + viewOriginZ=3000 bekleri; ön görünüşe
geçilip yan yüz → viewNormalZ (|nz|<0.1) beki. Runner'a faceClick/clickWorld/fit op'ları
ve viewTitle/viewOriginZ/viewNormalZ bekleri eklendi. 13 GT-1 + ctest 157/157 yeşil.
Commit 9261441.

Not (GUI testleri): MM_GUI_TESTS varsayılan OFF kalabilir, ama her push
kapısında kullanıcı 2 dk boştaysa `ctest -L gui` de koşulur ve rapora
yazılır; boşta değilse "GUI koşulmadı" diye belirtilir.

## H-2 — End Plate ters rol sırasında levhayı uzağa koyuyor  [Hermes bulgusu, H-1 sırasında]
Kullanıcı ana/ikincil üyeyi ters seçerse (önce kiriş, sonra kolon) End
Plate levhayı bağlantıdan uzağa yerleştiriyor. Kabul edilemez: sessizce
yanlış geometri. Kural (fail-closed): roller geometriyle tutarsızsa
(ikincilin ucu ana üyeye temas etmiyor / ana üye kirişin ucunda değil)
açık mesajla reddet ("önce kolonu (ana), sonra kirişi (ikincil) seçin")
ya da rolleri geometriden kesin olarak belirleyebiliyorsan düzelt —
hangisini seçtiğini gerekçele. Haunch (40) ve Fin Plate için de aynı
kontrol. Kırmızı test (ters sıra) → düzeltme → yeşil; ekranda kanıt.

Durum: —

## 2. B6 — Çizgi komutu profil çiziyor
Çizgi, Polyline, Dikdörtgen, Daire, 3DFACE panelde aktif profili (ör.
HEB240) YOK SAYSIN; yalnız profilsiz yardımcı obje üretsin. Profilli üye
yalnız Kolon/Kiriş'ten. (Yardımcı obje görünümü/migration UI-3'te.)
Senaryo: aktif profil HEB240 → Çizgi → profilsiz; Kiriş → HEB240.

Durum: BİTTİ (wip 7ca487e'den alındı; commit ctest sonrası) — Kök neden:
commitPoint'in Line dalı pendingProfileName_ boşsa düz çizgi, doluysa
profilli üye üretiyordu; Kiriş de Line aracını kullanıyordu. Düzeltme:
(1) Line dalı HER ZAMAN düz çizgi üretir (profil yalnız Kolon/Kiriş'te),
(2) yeni DrawTool::Beam aracı (profil zorunlu, profilsiz kullanım uyarır,
geometri üretmez), (3) draw.beam handler Beam aracını seçer. Test:
tests/run_line_beam_tool.py (gerçek commitPoint gövdesi; 14 kontrol:
Çizgi/Polyline/Dikdörtgen/Daire profili yok sayar, Kiriş uygular,
Kiriş profilsiz üretmez, Kolon akışı korunur) — Windows'ta elle YEŞİL;
mutasyonla doğrulandı (eski Line dalına dönüşte KIRMIZI: 1 hata). Ekran:
aktif HEB240 → Çizgi → "Profil: atanmamış" + Model Ağacı "Çizgiler →
Çizgi 0"; Kiriş → "Profil: HEB240, A=10600mm²" + "Profiller → HEB240".
Kanıt: C:\work\mm-hermes-shots\2026-11-08\b6\
(b6-cizgi-profilsiz-kiris-heb240.png). Not: test CMake'te UNIX bloğunda
(harness deseni); Windows'ta elle koşuldu, Linux CI'da kayıtlı.

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

Durum: BİTTİ (09d277e) — ctest "gui" etiketi 5 senaryo (smoke_line,
b6_line_ignores_profile, b2b7_line_visible, b1_catalog_search,
h1_view_to_surface) Windows'ta yeşil (ctest -L gui 5/5, ~11 sn).
Koşucuya search/clickModel/capture(target=window)/expect quickSearch/
catalog/status adımları eklendi. B6 düzeltmesi: selectTool yeni aracta
heldSnap_ temizler (eski komutun tutulan snap'i sonraki ilk tiklamayi
kacirmiyordu). PNG'ler vision ile incelendi: B1 arama kutusunda
"Haunch (40)" gorunuyor; H-1 "Plan Görünüşü 1 — plan, Z=0" penceresi
aciliyor (kiriş üst yüzü Z-normal). Build MM_GUI_TESTS=OFF'a döndürüldü.

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
