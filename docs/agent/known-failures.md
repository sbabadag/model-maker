# Bilinen dış hatalar (Windows)

Bu liste ctest etiketi ya da WILL_FAIL DEĞİLDİR. Testler gevşetilmez; kod
olduğu gibi derlenir ve çalıştırılır. Aşağıdaki testler MSYS2 ortamındaki
DIŞ hatalar yüzünden kırmızı kalabilir; ortam düzelince bu listeden
çıkarılır (test kodu değişmez).

## 1. model_maker_occ_brep_serialize_test (#78)

- Belirti: `BRepAlgoAPI_Cut` sırasında SIGSEGV (exit 139).
- Yığın: `BRepAlgoAPI_Cut` → `BRepClass3d_SClassifier::Perform` →
  `Extrema_ExtCC`.
- Kök neden: MSYS2 OCCT 7.9.3-3 + GCC 16.x `-O3` yanlış derlemesi.
- Referanslar: OCCT #1637, GCC #127917.
- Doğrulama: OCCT 7.9.2-2 (pacman) altında standalone 3/3 geçer.

## 2. model_maker_component_plate_color_test (#96)

- Belirti: OCCT 7.9.3 altında aynı SClassifier SIGSEGV (exit 139).
  OCCT 7.9.2-2 altında ise libTKService'in beklediği FFmpeg 62-ABI DLL
  seti (avcodec-62, avformat-62, avutil-60, swresample-6, swscale-9,
  libSvtAv1Enc-3) sistemdeki FFmpeg 8.x'te bulunmadığı için DLL yükleme
  hatası.
- Kök neden: aynı OCCT/GCC hatası + MSYS2 FFmpeg 8.x ABI değişikliği.
- Referanslar: OCCT #1637, GCC #127917.
- Kural: DLL elle kopyalanmaz / paketten parça ayıklanmaz (DUR). Çözüm
  pacman-yönetimli olmalı; OCCT sürüm kararıyla birlikte yeniden
  değerlendirilir.

## 3. model_maker_entity_id_test (#60) — ÇÖZÜLDÜ

- Eski belirti: paralel ctest altında `atomic replace failed (Win32 error
  1175)` (ReplaceFileW hedefi kilitli).
- Çözüm: her çalıştırmada benzersiz geçici dosya adı (commit 87c1cfb).
  Test içeriği değişmedi.
