# STATE — devir notu

- Aktif makine: Asus (ev). ZBook (iş) pasif. Senkron yalnız GitHub üzerinden;
  aynı anda tek makinede çalışılır.
- Son güncelleme: 2026-11-09
- Son tur: 2026-11-09 — B-picker regresyonu + H-5 — BİTTİ —
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
- Önceki tur: 2026-11-09 — H-8 (Haunch kolon/mertek ayrıklığı) — BİTTİ — panel kot
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

## Derleme (Windows, Asus)

- Derleme dizini: `C:\work\mm-hermes\build-hermes` (Release, Ninja, UCRT64 g++).
- Kabuk: Git Bash; her derleme komutundan önce:

      export PATH=/c/msys64/ucrt64/bin:$PATH
      export QT_PLUGIN_PATH=/c/msys64/ucrt64/share/qt6/plugins

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
