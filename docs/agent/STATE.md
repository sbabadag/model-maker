# STATE — devir notu

- Aktif makine: Asus (ev). ZBook (iş) pasif. Senkron yalnız GitHub üzerinden;
  aynı anda tek makinede çalışılır.
- Son güncelleme: 2026-11-07 (oturum başlangıç kurulumu)

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
- [x] B1 (Katalog + hızlı arama): kök neden setHost→setCatalog sırası +
      arama indeksi; düzeltildi, ribbon testi kırmızı→yeşil, ekranda
      doğrulandı (INBOX 1. madde)
- [ ] INBOX 2-6: B6, B2/B7, B8, B5, GT-1
- [ ] Linux CI iş akışı (GitHub Actions)
- [ ] UI-3 → DURAK 1 (rapor + Windows doğrulaması beklenir)

## Notlar

- Kullanıcının uzun talimatları dosyada: `C:\work\HERMES-YEREL-ILK.md`
  (ilk mesaj + Sıra), `C:\work\HERMES-MESAJ-19.md` (UI-3/4/5/6b tarifi;
  repodaki kopyası `docs/agent/ROADMAP-UI.md`).
