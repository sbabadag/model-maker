# STATE — devir notu

- Aktif makine: ZBook (iş makinesi; model deepseek-v4-flash, deneme). Asus (ev)
  pasif. Senkron yalnız GitHub üzerinden; aynı anda tek makinede çalışılır.
- Son güncelleme: 2026-11-10
- **P-1 turu (2026-11-10, ZBook, gece gözetimsiz)** — kaynak:
  `docs/agent/P-1-STUDIO-PAKETLERI.md`. "Bugün olan" listesi kodda doğrulandı:
  - MM-5 yükleyici (`loadComponentPackagesFrom`, `%APPDATA%\ModelMaker\components`
    + `<proje>\components`, fail-closed): **VAR**.
  - Paket → registry + diyalog kaydı (`loadComponentPackages`): **VAR**;
    tipin paketten geldiği artık `ComponentRegistry::isPackageType()` ile
    işaretli (`registerType(..., fromPackage)`).
  - İçe aktarma komutu (`component.importPackage`): **YOKTU → eklendi** (madde 1).
  - Yeniden yükleme komutu + geliştirme klasörü + `QFileSystemWatcher`: **YOK**
    (madde 2).
  - `schemaVersion` yükseltmesi + modeldeki örneklerin yeniden üretimi: **YOK**
    (madde 3; kurulum katmanındaki sürüm kuralı eklendi, yeniden üretim sırada).
  - Katalog: `pickCount` (1 ve 2) + `primaryMemberType`/`secondaryMemberType`
    Türkçe seçim istemi: **VAR** (paket manifest `catalog` bloğu okunuyor).
  - Diyalog (`dialog.json`, `autoSchematic`, `preview.dimensions`, String/Enum,
    `documentedOnly` salt okunur, `check` hataları TR): **VAR**.
  - `.mmw` bileşen kaydında `typeId` + `schemaVersion` + parametreler: **VAR**;
    paket **checksum**'ı: **YOK** (madde 6).
  - Studio paketleri golden fixture olarak (`tests/fixtures/studio_pkgs/`):
    **YOK** (madde 7).
  - GT-1 paket senaryosu: **YOKTU → `t15_package_import` eklendi** (madde 1).
  - Kabul paketi + `NELER-YENI.txt`: **YOK** (madde 9).
- **P-1/1 (içe aktarma) — BİTTİ (2026-11-10 21:15).**
  - `mm::installComponentPackage(sourceDir, targetRoot, registry, dialogs, opts)`
    (`src/component_package.cpp`): doğrula → geçici klasöre kopyala → KOPYAYI
    yeniden doğrula (checksums bayt üzerinden) → `rename` ile atomik yerine koy.
    Hata halinde hedefteki paket DOKUNULMAZ, `.tmp`/`.old` artığı silinir.
  - Sürüm kuralı: yerleşik typeId → RED; paketten gelen tip + **yüksek**
    `schemaVersion` → KABUL; aynı/düşük sürüm → RED (mevcut kayıt korunur).
  - `ComponentRegistry::registerType(..., fromPackage)` + `isPackageType()`;
    `ComponentDialogRegistry::unregisterDialog()` (yükseltmede eski şema düşer).
  - `Application::importComponentPackage(sourceDir, embed, userRootOverride={})`
    — kullanıcı klasörü + isteğe bağlı `<proje>\components` kopyası; sonuç
    `lastComponentPackageMessage()` (GT/teşhis). `userRootOverride` boş değilse
    oraya kurulur (GT izolasyonu).
  - Komut `component.importPackage` (Komponent sekmesi + menü + hızlı arama) →
    `QtMainWindow::importComponentPackage()` (klasör seçimi + "Projeye göm").
  - `defaultUserComponentPackagesDir()`: `%APPDATA%` yoksa
    `%USERPROFILE%\AppData\Roaming` yedeği; ikisi de yoksa AÇIK mesaj
    ("hedef klasor bos (APPDATA/USERPROFILE yok)") — sessiz yutma yok.
  - Testler: `model_maker_component_package_install_test` (**51/51 kontrol**),
    GT `tests/gt1/t15_package_import` (**18/18 adım**, `plateGap 0.00 mm`,
    `componentDigest p1 = de1c60be6ea84f32`, kaydet→aç aynı digest), yeni runner
    adımları `appImportPackage` (+`userRoot` izolasyonu) ve
    `componentTypeRegistered`; fixture `tests/fixtures/p1_pkg/` (`mm_p1_gt_pkg`).
  - Kapı: tam derleme **exit=0** + tam ctest **161/161 exit=0** (21:13, `-j 4`).
  - **DİKKAT (GT tuzağı):** MSYS `timeout` ile GUI testi koşulursa süreç
    ORTAMSIZ başlıyor (`APPDATA`/`USERPROFILE` yok) → paket yolu boş. GT'yi
    `timeout` olmadan koş. (Aynı tuzağa düşersen belirti: "hedef klasor bos".)
- **P-1/2 (yeniden yükleme + geliştirme klasörü) — BİTTİ (2026-11-10 21:50).**
  - `Application::reloadComponentPackages()`: (1) PAKET kaynaklı tipler kaldırılır
    (`isPackageType`), YERLEŞİK tipler korunur; (2) kökler **GELİŞTİRME →
    kullanıcı → proje** sırasıyla taranır — Studio'nun canlı klasörü aynı
    `typeId`'yi GÖLGELER; (3) etkilenen örnekler yeni recipe ile yeniden
    üretilir, başarısız örnek ESKİ hâliyle kalır (fail-closed).
  - `setComponentDevPackageDirs()` + `setComponentUserPackageRootOverride()`
    (GT izolasyonu: reload, içe aktarmanın kurduğu kökü tarar).
  - UI: komutlar `component.reloadPackages` ("Paketleri Yeniden Yükle") ve
    `component.addDevFolder` ("Geliştirme Paket Klasörü Ekle..."), Komponent
    sekmesi "Katalog" grubu + menü + hızlı arama. İçe aktarma sonrası
    "geliştirme klasörü olarak izle?" sorusu.
  - `QFileSystemWatcher` + 1 sn debounce (`QtMainWindow::setupComponentPackageWatcher`);
    ayar `component/devPackageDirs` (QSettings). Studio atomik yazdığı için
    yarım klasör okunmaz (yükleyici checksums ile reddeder).
  - Testler: `model_maker_component_package_reload_test` (**19/19 kontrol**:
    geliştirme kökü v2 kazandırır, yerleşik tipler korunur, bozuk dev paketi
    atlanır, v1'e dönüş), GT `tests/gt1/t16_package_reload` (**21/21 adım**:
    v1 → v2 (dev) → v1, `componentDigest p2 = de1c60be6ea84f32` üç durumda da
    AYNI); fixture `tests/fixtures/p1_dev/mm_p1_gt_pkg.mmcpkg` (schemaVersion 2).
  - Kapı: tam derleme **exit=0** + tam ctest **162/162 exit=0** (21:47, `-j 4`).
  - **varsayım:** GT'de `QFileSystemWatcher` olayı tetiklenmiyor (dosya sistemi
    olayı zamanlaması deterministik değil); izleyici yolu `appReloadPackages`
    adımıyla AYNI kodu çağırdığı için GT, izleyicinin çağırdığı yolu ölçüyor.
    İzleyicinin kendisi elle GUI turunda doğrulanmalı (aşağıdaki adım).
- **P-1/3 (sürüm yükseltmesi + parametre eşleme) — BİTTİ (2026-11-10 22:15).**
  - `mm::remapParamsToSpecs(specs, values, &report)` + `ComponentParamRemap`
    (`component_engine.hpp/cpp`): eşleme ADA GÖRE — aynı ad → değer korunur
    (aralık/tip dışı `validateParamValue` ile KELEPÇELENİR), yeni ad →
    varsayılan, semadan KALKAN ad → ATILIR. Rapor: kept/clamped/added/dropped
    + `addedNames`/`droppedNames`. (`alignParamsToSpecs` bilinmeyenleri KORUR;
    paket yükseltmesinde bu yanıltıcı olduğu için ayrı fonksiyon.)
  - `reloadComponentPackages()`: eşleme her etkilenen kayda YAZILIR
    (`params` + `schemaVersion`), sonra geometri yeniden üretilir. Durum
    çubuğu: "Paketler yeniden yüklendi: N tip, M örnek (parametre: X korundu,
    Y yeni varsayılan, Z atıldı[, W kelepçelendi])".
  - **DÜZELTİLEN YANLIŞ POZİTİF:** yeniden üretimden önce `updated.dirty=true`
    set ediliyordu; materializer bu bayrağı temizlemediği için BAŞARILI
    yeniden üretim "1 örnek ESKİ hâlinde kaldı" diye raporlanıyordu. Bayrak
    kaldırıldı; başarısızlık artık `atomicRecompute`'nun dönüş değerinden
    (hazırlama/commit başarısız → HİÇBİR örnek değişmez) okunur. Teşhis için
    `solidStatusMessage_` gerekçesi startup loguna yazılır.
  - Testler: `model_maker_component_param_remap_test` (**18/18 kontrol**:
    korunur/eklenir/atılır/kelepçelenir, sıra, boş şema), GT
    `tests/gt1/t17_package_version_upgrade` (**23/23 adım**): v1 üzerinde
    `plateThickness=16` → v3 (schemaVersion 3; `plateFinish` KALKTI,
    `plateCorner` EKLENDİ, `plateExtra` üst sınırı 20→10) → reload →
    26 korundu / 1 yeni / 1 atıldı / 1 kelepçelendi; digest
    `4af221b652d301da` → `daeab7ec32c15c4a` (yeniden üretim GERÇEKTEN koştu),
    kaydet→aç sonrası AYNI digest. Fixture `tests/fixtures/p1_dev3/`.
  - Runner'a `expect.packageMessage {contains, excludes}` eklendi (Türkçe
    rapor metni GT'de denetlenir).
  - **varsayım:** yeniden üretim başarısızlığı GT'de zorlanamadı (geçerli
    paketle üretim hep başarılı); yol `atomicRecompute` sözleşmesiyle
    (all-or-nothing) ve startup log kanıtıyla doğrulandı, ayrı bir "bozuk
    üretim" senaryosu YOK.
- Son güncelleme (önceki): 2026-11-09
- Son tur: 2026-11-09 (Asus) — H-2 (ters seçim: End Plate/Fin Plate/Haunch rol
  çözümü) — BİTTİ — Kök neden: plan rol anahtarları `inputRoleFor` ile konumsal
  atanıyordu; ters seçimde digest değişiyor + End Plate kiriş ucu kesimi kolonu
  kesiyordu. Düzeltme: generator'larda rol anahtarları geometriden SABİT
  ("main"=kolon, "secondary_0"=kiriş); yeni `componentRolesFromGeometry`
  (dikeylik swap + belirsizde fail-closed red) `applyComponentFromParts`'te
  cap-gap/kayıttan ÖNCE çalışıyor. Test: `component_role_resolution_test`
  (kırmızı→yeşil) + tam ctest 160/160. GUI doğrulaması bekliyor.
  Sıradaki: B8 → B5 → T-planı.
- Son tur: 2026-11-09 (ZBook) — H-9 (temas etmeyen üyede bağlantı) — BİTTİ —
  Kullanıcı: "yine haunch sorunu var" (`CUMALI KOC.mmw`). Ölçüm (yeni
  `componentPlateGap` / `modelBox` beklentileri): mertek Y=0'da, kolonlar
  Y=-250'de → alın levhası kolondan **155 mm havada**; haunch matematiği doğru,
  kusur üyelerin 3B'de ayrık olması (mertek ön görünüşte, derinlik bilgisi
  olmadan çizilmiş). (1) engine: `componentContactCheck` (fail-closed) — kiriş
  uç kesiti kolonun kesit+eksen kapsamıyla üç eksende örtüşmüyorsa bağlantı
  KURULMAZ, mesaj "kiriş kolona değmiyor: N mm boşluk (eksen)". Yalnız YENİ
  kurulum (`Application::applyComponentFromParts`); dosyadan gelen kayıtların
  regeneration'ı etkilenmez. (2) Testler: birim testi
  `model_maker_component_contact_test` (değen / kısmi örtüşen / ayrık / ters
  seçim / eksik girdi) + GT-1 `t13_haunch_offset_failclosed` (red + "175.0 mm");
  t9/t11'e `componentPlateGap` kalkanı (reanchor sonrası dahil). (3) Doğrulama:
  t9/t11/t13 yeşil, tam ctest **158/158**, ZBook kurulum derlemesi 665/665.
  Kullanıcı modeli için tamir: mertekleri seç → Taşı (M) → ΔY = -250 → haunch'ı
  yeniden kur (kod artık hizasız kurulumu reddediyor).
- Ek (2026-11-09, ZBook): **cap levhası–kolon üstü boşluğu parametresi** —
  kullanıcı: "haunch'ta cap plakası ile kolon üstü için bir parametre olmalı".
  Yeni `topPlateGap` (varsayılan **0** = eski davranış). Diyalog "Kolon Cap"
  sekmesinde `t cap` + `gap cap`; 3B önizlemede `gap cap` alanı. Diğer levhalar
  (alın levhası) kaymaz. (Aşağıdaki H-10 bu parametrenin nihai davranışını
  belirler.)
- H-10 (aynı gün, devam): **cap boşluğu KOLON BOYUNU değiştirir** — kullanıcı:
  "kolon cap yeri değişince kolon üstüne göre uzayıp kısalmalı". Karar (kullanıcı
  onayı): levha havada kalmaz; kolonun üst ucu gap kadar kayar, cap levhası yeni
  tepeye oturur; parametre 0'a dönünce kolon eski boyuna döner.
  - Motor (`generateHaunch40`) artık gap'i levha konumunda KULLANMAZ (levha =
    kolon üstü + t/2). Aralık ±500 mm (negatif = kolon kısalır).
  - `Application::shiftColumnTopNoUndo` (yeni): dikey elemanın ust ucunu kaydırır,
    katı profil/rotasyon/stille yeniden üretir, kesme zincirini tazeler; undo
    kaydı AÇMAZ.
  - `applyComponentFromParts` + `setSelectedComponentParams`: undo kaydı kolon
    kaydırmadan ÖNCE açılır, `commit/recompute(openOwnRecord=false)` aynı kayda
    yazar → tek geri-alma adımı; başarısızlıkta rollback. Düzenlemede **delta**
    (yeni−eski) uygulanır: birikme yok.
  - Önizleme: `applyCapPlateGapToColumn` (motor yardımcısı) — canlı 3B önizleme
    ve 2B şema da kolonun uzadığını gösterir (taban geometri her recompute'ta
    yeniden kurulur).
  - Test: `haunch_cap_gap_test` 33 kontrol (motor gap'i yok sayar + yardımcı
    ±gap + no-op), yeni GT senaryosu `tests/gt1/t14_haunch_cap_gap_column.json`
    (7000→7025→7050→7000→6970→7000, alın levhası teması korunur) ve yeni runner
    adımı `appComponentParams`.
- Önceki tur: 2026-11-09 — B-picker regresyonu + H-5 — BİTTİ —
  (1) Kullanıcı: "yeni profil ve props düzeltmesinden sonra kiriş komutu profil
  çizmiyor" — picker UYGULA/TAMAM yolu panele yazıyor ama pendingProfileName_'i
  güncellemiyordu; Kiriş kapısı reddediyordu. profileApplied bağlantısı artık pending'i
  günceller; komut kapısı KİRLİ panel değerini benimser (katalog dışıysa engeller), kirli
  değilse bekleyen profil geçerli (bos panel ezmez — B6/H-4). Kalkan: t12_beam_picker_regression
  (21 adım). (2) H-5: yüzeye göre görünüş yatay yüzlerde "Plan Görünüşü" sanılıyordu —
  ViewDefinition.fromFace bayrağı eklendi; başlık "yüzey görünüşü, derinlik …" olur.
  h1 senaryosu ayrımlı: kiriş Z=3000, üst yüz (viewTitle+viewOriginZ=3000) + yan yüz
  (viewNormalZ). Runner: faceClick/clickWorld/fit + viewTitle/viewOriginZ/viewNormalZ.
  Doğrulama: 13/13 GT-1, ctest 157/157, python props OK. Commit'ler fa8e853 (B-picker)
  + 9261441 (H-5). Sıradaki: H-2 → B8 → B5 → T-planı.
- Daha önceki tur: 2026-11-09 — H-8 (Haunch kolon/mertek ayrıklığı) — BİTTİ — panel kot
  uygulaması artık bağlı üye uçlarını yeni kota sabitliyor (reanchorSecondaryMembers,
  diğer ucu sabit — Tekla davranışı) + End Plate/Haunch aynı undo kaydında yeniden
  üretiliyor; başarısızlıkta tam rollback. Aynı turda assignProfileToSelection da
  uç bileşenlerini yeniden üretmeye başladı (profil değişimi de aynı sınıf açıktı).
  GT-1 kalkanı: t9_haunch40_e2e (34 adım) + t9_endplate_e2e (35 adım — profil
  değişim kalkanı eklendi), ikisi yeşil; ctest 155/155; python props testi OK.
  H-7 INBOX'ta BİTTİ (15a41fb). Sıradaki: H-6 →
  T-planı (INBOX Sıra).

## Git düzeni

- Bu klon: `C:\work\mm-hermes` (git worktree; yerel dal `hermes/work`, uzak:
  `origin/feature/component-contract`).
- Pull: `git pull --ff-only origin feature/component-contract`
- Push: `git push origin hermes/work:feature/component-contract`
- Yarım kalmış iş YALNIZ `wip/hermes` dalına commit edilir; `hermes/work`'e asla.
- Detay: `docs/agent/skills/model-maker/model-maker-hermes-git/`

## Derleme (Windows — Asus/ZBook)

- Derleme dizini: `C:\work\mm-hermes\build-hermes` (Release, Ninja, UCRT64 g++).
- Kabuk: Git Bash; her derleme/ctest komutundan önce:

      export PATH=/c/msys64/ucrt64/bin:$PATH
      export QT_PLUGIN_PATH=/c/msys64/ucrt64/share/qt6/plugins
      export TMP='C:\work\mm-tmp' TEMP='C:\work\mm-tmp' TMPDIR='C:\work\mm-tmp'
      mkdir -p /c/work/mm-tmp

- **ZBook (zorunlu):** üç `TMP/TEMP/TMPDIR` değişkeni olmadan hiçbir hedef
  derlenmez (g++ `Cannot create temporary file in C:\Windows\`) — ayrıntı
  `docs/agent/known-failures.md` §4. Asus'ta görülmedi.

- İlk configure:

      cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
        -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 \
        -DMM_OCC_LINK_DIRECT=ON

- Paralellik: `-j 4` (bellek daralırsa `-j 2`). Aynı anda iki büyük derleme
  başlatma (Claude Code da aynı makinede derleyebilir).
- Kullanıcı kabul klasörü: `C:\work\model-maker-p2d` (kabul derlemesi
  `build-occ`, PowerShell ile) — DOKUNMA.

## Oturum kuralı

- Oturum başı: pull + STATE.md oku + `docs/agent/skills/`'i yerel skill
  klasörüne kopyala.
- Oturum sonu ("devir" ya da iş bitince): STATE.md güncelle + commit + push.

## Push kapısı

- Windows: tam derleme (model-maker.exe dahil, link dahil) + tam ctest.
- Linux: GitHub Actions CI (ubuntu, Qt6 + OCCT, tam ctest, offscreen).

## Bilinen dış hatalar (Windows)

- Testler gevşetilmez; ctest etiketi/WILL_FAIL kullanılmaz.
- Ayrı liste: `docs/agent/known-failures.md` — #78 (occ_brep_serialize),
  #96 (component_plate_color), #60 (entity_id) → üçü ÇÖZÜLDÜ.
- Çözüm (2026-11-07): pacman-yönetimli paket sabitleme —
  `docs/agent/msys2-paket-sabitleme.md` (opencascade 7.9.2-2 + ffmpeg
  8.0-5 zinciri + IgnorePkg). ZBook kurulumunda aynısı uygulanacak.

## Durum (2026-11-07)

- [x] İçe aktarma (skill + hafıza, Linux yolları Windows'a çevrildi)
- [x] `hermes skills trust C:/work/mm-hermes`
- [x] docs/agent/ kurulumu: AGENTS.md, STATE.md, ROADMAP-UI.md, skills/
- [x] Windows temiz derleme (build-hermes) + tam ctest: 153/153
- [x] Bağlam optimizasyonu: context_length 1048576 (deepseek-v4-pro 1M),
      boş oturum tabanı 16.012 → 11.989 token; gereksiz araçlar kapalı,
      83+20 skill arşivli (INBOX 0a/0b/0c)
- [x] Gece modu cron'u: `gece-mm` (40dk, kilit dosyalı, 09:00 self-delete)
      → 2026-11-08 SÜREKLİ MOD'a geçildi: `gece-mm` silindi; `surekli-mm`
      cron kuruldu (30dk, süresiz, catch-up kapalı, kilit
      `C:\work\mm-hermes-surekli.lock`, workdir `C:\work\mm-hermes`)
- [x] H-4 (tutulan snap mesafe kuralı): tutma süreye değil yalnız
      histerezise bağlandı (tutma süresi yalnız görsel marker); GT-1
      h4_held_snap_release_distance kırmızı→yeşil; ctest -L gui 6/6 +
      tam ctest 160/160; kabul paketi yayınlandı (cb9506f)
- [x] H-6 (soluk snap izi): holdTimeMs yeniden çalışıyor — histerezis
      dışında snap hemen geçersiz ama marker holdTimeMs boyunca soluk
      çiziliyor (yalnız görsel; tıklama/hover değişmez); GT-1 h6 senaryosu
      kırmızı→yeşil + captureLive görsel kanıt; ctest -L gui 7/7 + tam
      ctest 161/161; kabul paketi yayınlandı (133fe71)
- [x] H-7 (3B GL Solid çizgi görünmezliği): kök neden GL Solid'de
      lineBatch kapısı + kontur üreticisinin yüzü boş modelleri atlaması;
      freeLineBatch_ eklendi; b2b7 senaryosu GDI+GL ayrıldı + lineVisible
      piksel kanıtı (GL önce kırmızı 0/101, sonra yeşil); ctest -L gui 8/8
      + tam ctest 162/162; kabul paketi yayınlandı (15a41fb)
- [x] B1 (Katalog + hızlı arama): kök neden setHost→setCatalog sırası +
      arama indeksi; düzeltildi, ribbon testi kırmızı→yeşil, ekranda
      doğrulandı (INBOX 1. madde)
- [x] H-1 (Yüzeye Göre Görünüş): HEAD'de üretilemedi (tik yolu 9e548f3'ten
      beri değişmemiş; ekranda gerçek tıkla 3 komut doğrulandı); tanı logu +
      sözleşme testi eklendi (82be6d7) — kullanıcı build'i yenileyip
      deneyecek, tekrarlarsa trim log kök nedeni gösterecek
- [ ] INBOX 2-6: B6, B2/B7, B8, B5, GT-1
- [ ] Linux CI iş akışı (GitHub Actions)
- [x] UI-3 + UI-4 (UI-34): Özellikler paneli iskeleti (29cc028) + profil
      seçim penceresi + GT-1 t10 (bu commit); ctest 156/156. Uç ofseti ve
      Konum düzlemde/derinlik bölümleri, levha/bulon/kaynak bölümleri ve
      K1/K2 ayrı kesit olarak kaldı.

## Notlar

- Kullanıcının uzun talimatları dosyada: `C:\work\HERMES-YEREL-ILK.md`
  (ilk mesaj + Sıra), `C:\work\HERMES-MESAJ-19.md` (UI-3/4/5/6b tarifi;
  repodaki kopyası `docs/agent/ROADMAP-UI.md`).
