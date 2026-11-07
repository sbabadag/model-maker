# Bilinen dış hatalar (Windows)

Bu liste ctest etiketi ya da WILL_FAIL DEĞİLDİR. Testler gevşetilmez; kod
olduğu gibi derlenir ve çalıştırılır. Aşağıdaki testler MSYS2 ortamındaki
DIŞ hatalar yüzünden kırmızı kalabilir; ortam düzelince bu listeden
çıkarılır (test kodu değişmez).

## 1. model_maker_occ_brep_serialize_test (#78) — ÇÖZÜLDÜ (2026-11-07)

- Eski belirti: `BRepAlgoAPI_Cut` sırasında SIGSEGV (exit 139).
- Yığın: `BRepAlgoAPI_Cut` → `BRepClass3d_SClassifier::Perform` →
  `Extrema_ExtCC`.
- Kök neden: MSYS2 OCCT 7.9.3-3 + GCC 16.x `-O3` yanlış derlemesi.
  Referanslar: OCCT #1637, GCC #127917.
- Çözüm: pacman-yönetimli `opencascade 7.9.2-2` + IgnorePkg sabitleme.
  Detay: `docs/agent/msys2-paket-sabitleme.md`.

## 2. model_maker_component_plate_color_test (#96) — ÇÖZÜLDÜ (2026-11-07)

- Eski belirti: OCCT 7.9.3 altında aynı SClassifier SIGSEGV (exit 139);
  OCCT 7.9.2-2 altında ise libTKService'in beklediği FFmpeg 62-ABI DLL
  seti (avcodec-62, avformat-62, avutil-60, swresample-6, swscale-9,
  libSvtAv1Enc-3, libx265-215, libbluray-3) sistemdeki FFmpeg 9.x'te
  bulunmadığı için DLL yükleme hatası (exit 127).
- Kök neden: aynı OCCT/GCC hatası + MSYS2 FFmpeg 9.x ABI değişikliği.
- Çözüm: pacman-yönetimli ffmpeg 8.0-5 (8.0-3, gettext/libxml2'den
  DllMain importu yüzünden 0xc0000139 veriyordu) + svt-av1 3.0.2-2 +
  x265 4.1-2 + libbluray 1.4.1-1 + IgnorePkg sabitleme.
  Detay: `docs/agent/msys2-paket-sabitleme.md`.

## 3. model_maker_entity_id_test (#60) — ÇÖZÜLDÜ

- Eski belirti: paralel ctest altında `atomic replace failed (Win32 error
  1175)` (ReplaceFileW hedefi kilitli).
- Çözüm: her çalıştırmada benzersiz geçici dosya adı (commit 87c1cfb).
  Test içeriği değişmedi.
