---
name: model-maker-component-studio
description: "Use when working on the mmcs Component Studio CAD repo."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux, windows]
metadata:
  hermes:
    tags: [C++, Qt6, OpenCASCADE, CAD, parametric-component]
    related_skills: [model-maker, model-maker-component-development, windows-cpp-cross-verification]
prerequisites:
  commands: [git, g++, cmake, ninja]
---

# model-maker-component-studio (mmcs)

`sbabadag/model-maker-component-studio` — **model-maker'dan AYRI** proje: Tekla-tarzı parametrik
çelik bileşen tasarlama stüdyosu. C++20, Qt6 Widgets, OCCT B-Rep, namespace `mmcs`.
Model Maker'daki `application.cpp`/Win32/GDI/MMW8 dünyası burada YOK; ikisini karıştırma.

## When to Use

- Bu repoda herhangi bir iş: klonlama, derleme, test, CAD aracı (Move/Copy/Draw), snap, gizmo,
  constraint veya MMCOMP işleri.
- Kullanıcı "Component Studio" / "component-studio" / "mmcs" / "Studio'yu aç" dediğinde —
  `sbabadag/model-maker` (model-maker) ile karıştırma, o ayrı bir repo ve ayrı bir skill.
- Repo private olduğu için erişim sorunu yaşandığında: kimlik bilgisi tahmin etme, aşağıdaki
  deploy anahtarı kuralını uygula.

- Yerel klon: `/home/ubuntu/model-maker-component-studio` (varsayılan dal `main`).
- Feature dalı çalışma kopyası: `/home/ubuntu/mmcs-cad` (`feature/cad-direct-drawing-20261031`).
- Git erişimi: `~/.ssh/componentstudio_deploy` (repo deploy anahtarı, Allow write access).
  Deploy anahtarları REPO BAŞINA tektir; aynı key'i başka repoya eklemek GitHub'da
  `Key is already in use` verir — bu repoya ait anahtarı kullan, yenisini üretmeye kalkma.
  `~/.ssh/config` yazımı onay istemine takılabilir; config'e dokunmadan:
  `GIT_SSH_COMMAND="ssh -i ~/.ssh/componentstudio_deploy -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new" git clone git@github.com:sbabadag/model-maker-component-studio.git`

## Doğrulama döngüsü (bu host'ta ÖLÇÜLDÜ — üçü de geçiyor)

**1. Core (Qt/OCCT yok, en hızlı):**

```bash
cmake -S . -B build-core -G Ninja -DMMCS_BUILD_GUI=OFF -DMMCS_ENABLE_OCCT=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core -j 2
ctest --test-dir build-core --output-on-failure
```

**2. OCCT + Qt6 GUI (gerçek AIS viewport):**

```bash
cmake -S . -B build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release -DMMCS_BUILD_GUI=ON -DMMCS_ENABLE_OCCT=ON -DOCC_ROOT=/usr
cmake --build build-occ -j 2
ctest --test-dir build-occ --output-on-failure
```

**3. Xvfb smoke (gerçek OCCT penceresi açar; exit 0 şart):**

```bash
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x960x24" ./build-occ/component-studio --smoke
```

- `xvfb` kurulu değilse: `sudo apt-get install -y --no-install-recommends xvfb` — yoksa `xvfb-run`
  "failed to execute process" verir ve smoke hiç koşmamış olur.
- Qt6 geliştirme paketleri bu host'ta kurulu; `find_package(Qt6)` burada patlamaz.
  `-DMMCS_ENABLE_OCCT=OFF` iken `main.cpp` "OCCT not enabled" etiketli boş pencere açar.
- **`-DOCC_ROOT=/usr` burada ÇALIŞIR** (model-maker'da `find_package(OpenCASCADE)` VTK hedefleri
  yüzünden patlıyordu; bu repoda elle OCC_ROOT yolu temiz geçiyor — OCC'siz hızlı döngü için
  `-DMMCS_ENABLE_OCCT=OFF` yeterli).
- Bellek kısıtlı: `-j 2` kullan; daha yüksek paralellik OOM/SIGBUS'a gider.
- `build*` dizinleri `.gitignore`'da; derleme sonrası `git status` temiz kalmalı.

## Canlı GUI çalıştırma + ekran görüntüsü (headless)

Kullanıcı "çalıştıralım" deyip arayüzü GÖRMEK istediğinde (`--smoke` değil, açık kalan pencere):
Xvfb'de uygulamayı ayakta bırakıp ffmpeg `x11grab` ile tek kare al.

```bash
Xvfb :99 -screen 0 1600x1000x24      # background=true ile başlat
DISPLAY=:99 ./build-occ/component-studio   # background=true; --smoke YOK, açık kalsın
sleep 5
/home/ubuntu/.hermes/tools/ffmpeg-9.0.1-linux-x64/bin/ffmpeg -y -hide_banner -loglevel error \
  -f x11grab -video_size 1600x1000 -i :99 -frames:v 1 ekran.png
```

- ffmpeg `/home/ubuntu/.hermes/tools/ffmpeg-9.0.1-linux-x64/bin/ffmpeg` altında, `x11grab`
  destekli; `import`/`scrot`/`xwd`/`xdpyinfo`/`xset` KURULU DEĞİL — kareyi ffmpeg ile al.
- Terminal aracı `&` arka planlamayı reddeder: Xvfb ve uygulamayı ayrı ayrı `background=true`
  çağrısıyla başlat, ardından `sleep` + ffmpeg'i foreground çağrıda koş.
- Boş oturumla açılır (nesne listesi/Properties boş); bu beklenen durumdur. Bitince
  `kill <pid>` + `pkill -f 'Xvfb :99'` ile belleği bırak (host RAM kısıtlı).

## Mimari sınırlar

| Katman | Sorumluluk |
|---|---|
| `domain/` | `Entity`/`Profile`/`Plate`/`ReferencePlane`/`MagneticPlane`, `Pose3D`, constraint sınıfları, `Command`/`CommandManager`, `DependencyGraph` |
| `geometry/` | `IGeometryBackend` (UI'siz arayüz), `OcctGeometryBackend` (OCCT'yi içeren TEK katman), `GeometryCache`, `SnapEngine` |
| `studio/` | `StudioSession` (Qt'siz facade), `OcctViewport` (Qt + V3d/AIS), `StudioWindow` (toolbar/panel) |

- **OCCT tipleri `geometry/occt_backend.*` dışına sızmaz.** Yeni geometri işi `GeometrySpec`
  üzerinden yapılır; GUI kodu `TopoDS_*` görmez.
- **Tek yazıcı:** düzenleme yalnız `CommandManager` üzerinden; bir batch = bir Undo adımı;
  başarısız batch hiçbir durumu değiştirmez.
- Geometri invalidation `lastGeometryChanged()` ile yapılır; metadata-only düzenleme B-Rep'i
  yeniden üretmemeli (`GeometryCache` anahtarı = geometryRevision + GeometrySpec).
- `docs/MMCOMP-v1.md` yalnız TASARIM TASLAĞI — loader/compiler YOK. Dokümanda yazan bir şeyi
  "uygulanmış" diye raporlama.

## Çalışma akışı

1. `git branch -a` + `git log --oneline origin/<dal>` ile **uçuştaki dalı** bul; yalnız `main`'e bakıp
   "özellik yok" demek yanıltır — CAD özellikleri feature dalında duruyor olabilir.
2. Özellik/teşhis işini ayrı worktree'de yap (`git worktree add /home/ubuntu/mmcs-cad origin/<dal>`),
   ana ağacı `main` üzerinde temiz bırak.
3. Derle + ctest + Xvfb smoke; koşamadığın adımı raporda AÇIKÇA yaz.
4. **Dokümantasyonu kodla aynı işte güncelle:** `README.md` + `docs/<konu>-v0.1.md`.
5. Push/merge yalnız kullanıcı açıkça söylediğinde. Kullanıcıya komut verirken her komut kendi
   satırında, tek satır olsun (kullanıcı blok halinde kopyalamıyor).

## Tuzaklar

- **Yeşil test ≠ bitmiş iş.** Dal derlenip tüm testleri geçse bile README/docs o dalda
  güncellenmemiş olabilir; `git log -1 -- README.md` ile dal başını karşılaştır. Kod sağlam +
  doküman eski durumu "tamamlandı" diye raporlama; eksiği söyle.
- **CI 4 işten oluşur** (`.github/workflows/core-ci.yml`): core, occt (GUI'siz), studio-gui (Xvfb +
  `--smoke`), windows-ucrt64. Linux'ta yerel yeşil, Windows UCRT64 işini KANITLAMAZ.
- `--smoke` 1 sn sonra kapanır ve çoğu zaman stdout üretmez; başarısızlık sinyali exit kodudur
  (`populateSmokeScene()` fail → 2), stdout değil.
- `feature/cad-direct-drawing-20261031` (iki noktalı Move/Copy, Draw Profile/Draw Plate,
  workplane Z, snap'li imleç önizlemesi, `tests/cad_commands_test.cpp`) `main`'e merge edildi.
  Ama README/docs hâlâ eski (move/rotate'i "next stage" diye anlatıyor) — merge edildi,
  dokümantasyon güncellemesi YAPILMADI, o iş açık.
