# Model-Maker — Windows Kurulum ve Derleme Reçetesi (OCC'li)

Bu belge, model-maker'ı **OCC (OpenCASCADE) entegrasyonu dahil** başka bir yerel
Windows makinede sıfırdan kurup derlemek için gereken her şeyi içerir.
Kod kaynağı **Oracle makinesindeki git deposudur** (GitHub: `sbabadag/model-maker`,
çalışma dalı **`OCC`**). Derleme makinesinin işlemci hızına göre toplam kurulum
30-90 dakika sürer (OCC'nin vcpkg'den derlenmesi en uzun kısımdır).

---

## 1) Kurulacak bileşenler

| Bileşen | Sürüm | Nasıl kurulur |
|---|---|---|
| Git for Windows | güncel | https://git-scm.com/download/win |
| CMake | 3.25+ | Qt kurulumuyla gelir (Qt\Tools\CMake_64) ya da https://cmake.org/download |
| Ninja | güncel | Qt kurulumuyla gelir (Qt\Tools\Ninja) ya da scoop/winget |
| **Qt** | **6.9.3** (mingw_64 + MinGW 13.1.0 64-bit) | https://www.qt.io/download — aşağıdaki adım 2 |
| **vcpkg** | güncel | https://github.com/microsoft/vcpkg — aşağıdaki adım 3 |

> Uygulamanın Qt toolchain'i **mingw1310_64 (GCC 13)** ile derlenir.
> OCC de aynı derleyiciyle, **statik** olarak vcpkg'den kurulur
> (`x64-mingw-static` triplet) — bu, runtime DLL çakışmalarını kökten çözer.
> Makinede scoop GCC gibi başka derleyiciler varsa PATH'ten uzak tutulmalıdır.

---

## 2) Qt kurulumu

1. Qt Online Installer'ı indirip çalıştır.
2. Bir Qt hesabıyla giriş yap (ücretsiz).
3. Bileşen seçiminde şunları işaretle — **yalnız bunlar yeterli**:
   - `Qt 6.9.3` → `MinGW 13.1.0 64-bit`
   - `Qt 6.9.3` → `Additional Libraries` (gerekirse)
   - `Developer and Designer Tools` → `MinGW 13.1.0 64-bit` (araç zinciri)
   - `Developer and Designer Tools` → `CMake` + `Ninja` (kolaylık)
4. Kurulum yolu varsayılan kalsın: **`C:\Qt`** (reçetedeki komutlar bu yolu varsayar;
   farklı yere kurarsan komutlardaki yolları değiştir).

Kurulum sonrası kontrol:

```powershell
Test-Path 'C:\Qt\6.9.3\mingw_64\bin\qmake.exe'   # True olmali
Test-Path 'C:\Qt\Tools\mingw1310_64\bin\g++.exe' # True olmali
```

---

## 3) vcpkg + OpenCASCADE (statik)

```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg.exe install opencascade --triplet x64-mingw-static
```

- Bu adım OCC'yi kaynaktan derler: **30-60 dakika** sürer.
- Çıktı: `C:\vcpkg\installed\x64-mingw-static\...` altında statik TK kütüphaneleri.
- Hata: "vcpkg integrates" gerekmez — proje toolchain dosyasını doğrudan kullanır.
- Boş alan: OCC derlemesi için ~15-20 GB gerekebilir.

---

## 4) Kodun alınması

```powershell
cd C:\
git clone https://github.com/sbabadag/model-maker
cd model-maker
git checkout OCC
git pull origin OCC
```

---

## 5) Derleme

```powershell
cd C:\model-maker
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.9.3\mingw_64\bin;' + $env:PATH

cmake -S . -B build-vcpkg -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static

cmake --build build-vcpkg --target model-maker
```

Başarı ölçütü (cmake çıktısında):

```
-- OpenCASCADE bulundu: C:/vcpkg/installed/x64-mingw-static/share/opencascade
-- Uygulama OCC'yi dogrudan bagliyor (ayni toolchain)
```

ve build'in sonunda:

```
Linking CXX executable model-maker.exe
```

---

## 6) Çalıştırma

```powershell
cd C:\model-maker
.\build-vcpkg\model-maker.exe
```

Test: **B** (BRep kutu), **S** (silindir), **J** (boolean birleşim),
**F2** (Solid/Transparent stil), **V** (3B görünüm), **F6** (GL/GDI),
**F11** (perf katmanı). Durum çubuğunda hacimler görünür.

---

## 7) Sorun giderme — bilinen tuzaklar

1. **PATH sırası:** Qt'nin `mingw1310_64\bin`'i her derlemede EN BAŞTA olmalı.
   Makinede scoop/winget GCC varsa CMake yanlış derleyiciyi seçebilir —
   belirtiler: `M_PI` bulunamadı, AUTOMOC hataları, "g++.exe is broken".
2. **Çalışan exe link'i kilitler:** Derlemeden önce
   `taskkill /IM model-maker.exe /F` çalıştır; "Permission denied"/`ld returned 1`
   bunun belirtisidir.
3. **OneDrive/mtime bayatlığı:** Repo OneDrive altındaysa ninja bazen kaynağı
   yeni saymaz ("no work to do"). Zorla:
   `(Get-Item src\application.cpp).LastWriteTime = Get-Date` sonra yeniden derle.
4. **vcpkg triplet hatası:** `x64-mingw-static` yazımı tam olmalı; yanlış triplet
   OCC'yi yanlış runtime ile kurar ve link/DLL hataları verir.
5. **Profil kataloğu:** Uygulama Tekla profillerini çalışma anında
   `C:\TeklaStructures\<surum>\Environments\default\General\Shared\Profil\*.lis`
   yolundan okur; Tekla yoksa profil seçici boş görünür (uygulama yine çalışır).
6. **Görünen arayüz Qt'dir:** UI ekleyecekseniz `src/qt_main_window.cpp`'yi
   düzenleyin; `application.cpp`'nin Win32 penceresi üretimde gizlidir.
7. **GL sürücüsü:** Çok eski bir GPU'da GL yolu (F6) açılmayabilir; GDI yolu
   her makinede çalışır.

---

## 8) Güncelleme döngüsü (sonraki günler)

```powershell
cd C:\model-maker
git pull origin OCC
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.9.3\mingw_64\bin;' + $env:PATH
taskkill /IM model-maker.exe /F
cmake --build build-vcpkg --target model-maker
.\build-vcpkg\model-maker.exe
```
