# MSYS2 paket sabitleme — opencascade + ffmpeg zinciri

Tarih: 2026-11-07. Makine: Asus (ev). ZBook (iş) kurulumunda AYNI sabitleme
uygulanacak. Elle DLL kopyalama / paketten parça ayıklama YASAK — her şey
pacman-yönetimli.

## Neden

1. MSYS2 `opencascade 7.9.3-3`, GCC 16.x `-O3` ile
   `BRepClass3d_SClassifier::Perform`'u yanlış derliyor → boolean-op yolu
   SIGSEGV (Trim/boolean testleri + uygulama komutları).
   Referanslar: OCCT #1637, GCC #127917. `SetParallelMode(False)` çare değil.
2. Çözüm: `opencascade 7.9.2-2` (GCC 15 sürümü). Ama 7.9.2-2'nin
   `libTKService.dll`'i FFmpeg **62-ABI** setini import ediyor:
   `avcodec-62.dll avformat-62.dll avutil-60.dll swresample-6.dll
   swscale-9.dll`. MSYS2 `ffmpeg 9.x` yalnız 63-setini taşıyor → loader
   hatası (exit 127, DLL adı yazmaz).
3. Çözüm: ffmpeg'i 62-setini taşıyan bir sürüme indirmek + kodlayıcı
   bağımlılıklarını onun beklediği DLL adlarına eşleştirmek.
4. İlk kurulan `8.0-3` YETERSİZDİ: `avcodec-62`/`avformat-62`, gettext
   `libintl-8` / libxml2 `libxml2-16`'dan `DllMain` import ediyordu; bu
   DLL'ler `DllMain` export ETMİYOR → loader 0xc0000139
   (STATUS_ENTRYPOINT_NOT_FOUND, exit 3221225785). 8.0-1/-2/-4 aynı kusurlu;
   `8.0-5` ve `8.0.1-7` temiz. Runtime testte `8.0-5` tam set +
   `libTKService` yüklendi, `8.0.1-7` avcodec bağımlılık eksikliğiyle
   düştü → **8.0-5** sabitlendi (pacman -U + IgnorePkg).

## Sabitlenen paketler (pacman -U ile; --nodeps / -dd kullanılmadı)

| Paket | Sürüm | Önceki | Not |
|---|---|---|---|
| mingw-w64-ucrt-x86_64-opencascade | 7.9.2-2 | 7.9.3-3 | GCC 15 derlemesi |
| mingw-w64-ucrt-x86_64-ffmpeg | 8.0-5 | 9.0.2-1 | 62-ABI; DllMain importsuz |
| mingw-w64-ucrt-x86_64-svt-av1 | 3.0.2-2 | 4.2.0-1 | `libSvtAv1Enc-3.dll` |
| mingw-w64-ucrt-x86_64-x265 | 4.1-2 | 4.3-1 | `libx265-215.dll` |
| mingw-w64-ucrt-x86_64-libbluray | 1.4.1-1 | 1.5.0-1 | `libbluray-3.dll` |

Kurulumdan önce kuru plan (`pacman -U --print` + repo.msys2.org tarball
içerik doğrulaması, objdump import yürüyüşü): yalnız bu 5 paket değişir;
başka paket İNMEZ/DEĞİŞMEZ. `pacman -Qi ffmpeg` "Required By" = yalnız
opencascade; svt-av1/x265/libbluray yalnız ffmpeg'e bağlı. Qt etkilenmez.

## IgnorePkg satırı (C:\msys64\etc\pacman.conf)

```
IgnorePkg   = mingw-w64-ucrt-x86_64-opencascade mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-svt-av1 mingw-w64-ucrt-x86_64-x265 mingw-w64-ucrt-x86_64-libbluray
```

## Bütünlük kontrolü

```
pacman -Qkk <5 paket>   # 0 altered olmalı
```

## Ne zaman geri alınır

- OCCT 7.9.4+ (ya da GCC 16 düzeltmesi) MSYS2'ye gelince: `opencascade`
  satırını IgnorePkg'den çıkar, `pacman -Syu`; ardından ffmpeg zinciri de
  serbest bırakılır ve 63-ABI'ye (9.x) döner. Eski 62-seti DLL'leri pacman
  dosyalarıdır — elle silme, `pacman -Syu` temizler.
- Takip: OCCT #1637, GCC #127917 kapanışı; `pacman -Q opencascade ffmpeg`.

## ZBook kurulumu

- Aynı 5 paket + aynı sürümler + aynı IgnorePkg satırı.
- Tarball'lar `repo.msys2.org/mingw/ucrt64/` adresinden; eski pkgrel'ler
  silinmişse o tarihteki en yakın sürümü seç ve DLL adlarını
  `zstd -dc pkg.tar.zst | tar -tf -` + `objdump -p <dll> | grep 'DLL Name'`
  ile DOĞRULA (sürüm numarası DLL adını garanti etmez: x265 4.2-1
  `libx265-216.dll` taşır, 4.1-2 `libx265-215.dll`).

## Doğrulama (Asus)

- `pacman -Qkk` → 5 paket temiz.
- Standalone: `model_maker_occ_brep_serialize_test` 34/34 + 11/11,
  `model_maker_component_plate_color_test` exit 0.
- Tam ctest: **153/153 (%100)** — 0xc0000139 sayısı 0 (8.0-5 sonrası).
