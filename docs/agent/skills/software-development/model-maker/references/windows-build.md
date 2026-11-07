# Windows'ta derleme/calistirma — makineye ozel komutlar

`SKILL.md`'in "Windows'ta derleme/calistirma — HANGI MAKINE?" bolumunun makine bloklarini tasir.
Tetikleyici sozcukler ("ev"/"is") ve "hangi makineyi sormadan varsayma" kurali SKILL.md'de kalir;
buradaki bloklar o tabloyla eslesir.

## ZBook = IS makinesi (bilgisayar adi SEBA, kullanici `sbaba`)

Repo: `C:\Users\sbaba\OneDrive\Belgeler\Projeler\APPS\model-maker` (OneDrive senkron).
Qt `C:\Qt\6.9.3\mingw_64` VE Qt'in KENDI MinGW'i VAR
(`C:\Qt\Tools\mingw1310_64\bin\g++.exe`, GNU 13.1.0 — Qt surumuyle birebir, tercih edilen).
MSYS2/UCRT64 + OpenCASCADE 7.9.3 KURULDU (pacman: `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools,opencascade}`;
`C:\msys64\ucrt64\bin` PATH'e). OCC'li tam derleme `C:\mm-occ-build`
(`-DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON`), exe UCRT64
kabugundan. OCC'siz sade derleme `C:\mm-build` (Qt MinGW).

PGP/keyring hatasi → `pacman-key --init && pacman-key --populate msys2 && pacman -Sy`.
`cmake` = C:\Program Files, `ninja` = scoop; PATH'te ayrica scoop `g++` (13) var -> CMake
yanlisini secebilir, derleyiciyi ACIKCA ver. OneDrive esitlemesi build'i karistirir ->
derleme dizini OneDrive DISINDA (`C:\mm-build`). OneDrive'dan gelen `build-release`/
`build-vcpkg` eski CMakeCache tasir; kullanma.

```powershell
$qt='C:\Qt\6.9.3\mingw_64'; $ming='C:\Qt\Tools\mingw1310_64\bin'
$env:PATH="$qt\bin;$ming;"+$env:PATH
cmake -S 'C:\Users\sbaba\OneDrive\Belgeler\Projeler\APPS\model-maker' -B C:\mm-build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$qt -DCMAKE_CXX_COMPILER="$ming\g++.exe"
cmake --build C:\mm-build --target model-maker
C:\mm-build\model-maker.exe            # Qt6 DLL'leri PATH'te olsun (ayni pencere)
```

## Asus = EV makinesi (host DESKTOP-BFEQ0VF, kullanici `Asus`)

Repo: `C:\Users\Asus\Documents\MEGA\APPS\model-maker` (MEGA senkron).
Qt `C:\Qt\6.9.3\mingw_64` ama Qt'in kendi MinGW'i YOK → derleyici (GNU 16.1.0) WinGet
WinLibs paketinden; **cmake ve ninja da ayni paketin `mingw64\bin` icinde** (ayri sistem
CMake/scoop ninja yok — `cmake not recognized` = bu bin PATH'te degil).
MSYS2 UCRT64 + OpenCASCADE de kurulu → OCC'li `build-occ`, OCC'siz `build-release`.

**Tuzak (kanıtlanmış):** build dizinleri (`build-release`, `build-occ`) `.gitignore`'da → MEGA
bunları SENKRONLAMAZ. Taze pull'dan sonra `cmake --build build-release --target ...` "not a CMake
build directory (missing CMakeCache.txt)" verir — önce `cmake -S . -B build-release -G Ninja
-DCMAKE_BUILD_TYPE=Release` (configure) çalıştır. Yani Asus'ta her derlemede önce configure, sonra
build; configure'ı atlama.

OCC'li (tam ozellik) — MSYS2 UCRT64 kabugunda:
```bash
cd /c/Users/Asus/Documents/MEGA/APPS/model-maker
cmake -S . -B build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON
cmake --build build-occ --target model-maker
./build-occ/model-maker.exe          # UCRT64 kabugundan calistir (OCC DLL'leri PATH'te)
```

OCC'siz sade — PowerShell. Her komutu AYRI kopyala, hepsi AYNI pencerede
(`$mingw`/`$env:PATH`/`$deploy` sonraki satira tasinir):

```powershell
$mingw='C:\Users\Asus\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin'
```

```powershell
$env:PATH="$mingw;C:\Qt\6.9.3\mingw_64\bin;"+$env:PATH
```

```powershell
cd 'C:\Users\Asus\Documents\MEGA\APPS\model-maker'
```

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```powershell
cmake --build build-release --target model-maker
```

```powershell
$deploy="$PWD\build-release"
```

```powershell
C:\Qt\6.9.3\mingw_64\bin\windeployqt.exe --release "$deploy\model-maker.exe"
```

```powershell
Copy-Item "$mingw\libstdc++-6.dll","$mingw\libgcc_s_seh-1.dll","$mingw\libwinpthread-1.dll" $deploy -Force
```

```powershell
& "$deploy\model-maker.exe"
```

ABI notu: Qt 6.9.3 resmi MinGW 13.1.0 ile derlenmistir; Asus'ta WinGet GCC 16.1.0 kullanilir —
genelde calisir ama acilis crash'inde ilk supheli budur (ZBook'ta Qt MinGW'ini kullanip bu riski
kaldirdik).

## Windows derleme/test tuzaklari (ZBook, UCRT64, kanitlanmis)

- `far`/`near` degisken adi: `windef.h` bunlari bos makro yapar → `TopoDS_Shape far = ...` 'expected unqualified-id'. Testlerde `farShape`/`ptsClose` gibi adlar kullan. `tests/occ_weld_geometry_test.cpp` satir 134/135/140 etkilenir; derlemeyi `cmake --build <dir> -- -k 0` ile surdur.
- Testte sabit `/tmp/...` yolu (or. `bridge_native_bind_test`) native Windows exe icin `C:\tmp` demektir; klasor yoksa `std::runtime_error` → terminate → cikis kodu `0xc0000409`. Gecici cozum `mkdir -p /c/tmp`; kalici cozum `std::filesystem::temp_directory_path()`.
- Beklenen kalici Windows hatasi: `occ_brep_serialize` SEGFAULT (ayri sorun, maskeleme).
- Haunch 40 dalinda `component_dialog_test` sekme sayisi beklentisi eskiydi (7); ust/alt bulon sekmeleri ayrilinca 9 olmali.
- ZBook'ta `C:\work\model-maker-bridge-v31` YOKTUR (dal notlarindaki yol baska makineye ait); klonu `C:\work\model-maker-haunch40`, derleme dizini `/c/mm-occ-build-haunch40`. Haunch test kumesi: `ctest -R "^model_maker_(component_engine|component_dialog|component_catalog|occ_haunch40_native_e2e)_test$"`.
- Kullaniciya verilen komutlarda `\` ve `\/` yapistirmada KAYBOLUR (`$env:USERPROFILE\.ssh` → `.ssh` oldu; `sed s/a\/b/` bozuldu). Yollari tirnakla ver, `sed`'de `|` ayraci kullan, metinde `/` olmasin.
- Anahtar: ZBook'tan scp icin `C:\Users\sbaba\Desktop\hp_hermes.pem`; hedef klasor onceden `New-Item -ItemType Directory -Force C:\work` ile olusturulmali.

## Bundle ile commit aktarimi (tek branch) — FETCH_HEAD tuzagi

`git fetch <bundle> overnight-features-...` YALNIZ FETCH_HEAD'i gunceller; yerel branch
ve calisma agaci DEGISMEZ. Sonra `git checkout <branch>` "Already on" der ve eski commit
kalir (derleme eski kaynakla yapilir — belirti: test eski metni basar). Aktarimdan sonra:

```
git merge --ff-only FETCH_HEAD
git log --oneline -1      # yeni hash gorulmeli
```

Yerel degisiklik yoksa `git reset --hard FETCH_HEAD` de olur (calisma agacini siler).

## OCC'li configure — PowerShell'de DEGIL, UCRT64 kabugunda

ZBook'ta OCC'li build `-DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64
-DMM_OCC_LINK_DIRECT=ON` ister. Bunu **PowerShell'de** calistirinca iki tuzak cikar:

1. Automoc `moc.exe` `0xc0000135` (DLL not found) verir — MSYS2 moc'u ucrt64 DLL'lerini
   PowerShell PATH'inde bulamaz.
2. CMake derleyici olarak Qt'nin MinGW'sini (C:/Qt/Tools/mingw1310_64) secer; Qt/OCC ise
   MSYS2'den gelir -> karisik toolchain.

Cozum: **MSYS2 UCRT64 kabugunda** calistir ve derleyiciyi acikca ver:

```
cmake -S . -B /c/mm-occ-build-finplate -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
```

Yarim kalmis (Qt MinGW cache'li) build dizinini once `rm -rf` ile sil, yoksa eski
derleyici cache'te kalir. Test exe'sini de UCRT64 kabugundan calistir (OCC DLL'leri PATH'te).

## Makine yollari (kanitlanmis, 2026-11 — GUNCEL, kullanici teyidi)

| Makine | Repo | Build dizini | GitHub remote adi |
|---|---|---|---|
| EV (Asus) | `C:\work\model-maker-p2d` | `build-occ` | `github` |
| IS (ZBook) | `C:\work\model-maker-p2d` | `build-occ` | `origin` |

Studio (component studio AI MVP): `C:\work\model-maker-component-studio-ai-command-mvp` (iki makinede de).

Her iki makinede de tek gecerli recete: MSYS2 UCRT64 (Release, Ninja) + `-DQT_ROOT=C:/msys64/ucrt64
-DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`.

### PowerShell komutlari (kullanici kurali, 2026-11)

Git pull icin (kullanici PowerShell'de calisiyor; remote adi makineye gore DEGISIR — Asus `github`,
ZBook `origin`):

```powershell
git pull --ff-only github feature/component-contract
```

```powershell
git log --oneline -1
```

Derleme — PowerShell'de, MSYS2 kabugu SART DEGIL. Once env, sonra build; **build-occ zaten
varsa configure VERME** (bayat cache tuzağı); yalnız `cmake --build`:

```powershell
$env:Path="C:\msys64\ucrt64\bin;$env:Path"
```

```powershell
$env:QT_PLUGIN_PATH="C:\msys64\ucrt64\share\qt6\plugins"
```

```powershell
cd C:\work\model-maker-p2d
```

```powershell
cmake --build build-occ --target model-maker -j 2
```

```powershell
.\build-occ\model-maker.exe
```

`build-occ` yoksa (taze klon) tek seferlik configure (MSYS2 UCRT64 kabugunda):
`cmake -S . -B build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON`.

## ZBook ilk kurulum — yanlis toolchain kombinasyonlari (kanitlanmis, 2026-11)

Yeni makinede (veya temiz kopyada) derlerken su uc kombinasyon KESIN OLARAK patlar; vakit kaybetmeden
UCRT64 recetesine gec:

| Belirti | Kok neden | Yapma |
|---|---|---|
| `undefined reference to std::codecvt<wchar_t,char,_Mbstatet>::codecvt(unsigned long long)` | MSYS2 **mingw64 GCC 16.2** `libstdc++.a` std::codecvt tanimlarini tasimiyor (`nm ... \| grep -c _ZNSt7codecvtIwc9_MbstatetEC` = 0); ucrt64'te 16 adet var | mingw64 g++ ile `-static` link |
| ayni codecvt hatasi, Qt 13.1 derleyicisiyle | `C:/Qt/Tools/mingw1310_64/lib/libstdc++.a` **hic yok** (yalniz DLL) | Qt'nin MinGW'sini MSYS2 Qt/OCC ile karistirma |
| exe acilir ama ilk komutta segfault | UCRT exe + MSVCRT Qt 6.9.3 DLL'leri ayni surecte | `-DQT_ROOT=C:/Qt/6.9.3/mingw_64` + MSYS2 derleyici |
| eski exe yeni Qt DLL'iyle `Segmentation fault` | build dizini yeniden configure edilmemis (`ninja: no work to do`) | configure'suz `cmake --build` |
| `uic.exe -h` -> `0xc0000139` (STATUS_ENTRYPOINT_NOT_FOUND) | PATH'te onceki Qt bin/Qt 6.9.3 duruyor; uic yanlis Qt6Core.dll yukluyor | ayni kabukta export kalintilariyla configure |
| configure: "Check for working CXX compiler: C:/msys64/mingw64/bin/c++.exe" ama QT_ROOT ucrt64 | PATH'te /mingw64 once | PATH'i duzeltmeden configure |

Kalici dersler:
- Ilk komut `which c++ g++` + configure ciktisindeki derleyici yolu; **prefix'ler ayni olmali**
  (ucrt64 Qt + ucrt64 g++ + ucrt64 OCC).
- uic/moc'un calistigi tek dogrulama: `PATH=/ucrt64/bin:/usr/bin:/c/Windows/system32 /c/msys64/ucrt64/share/qt6/bin/uic.exe -h`.
- QUIET Qt (`find_package(Qt6 QUIET)`) yine de AUTOUIC testini calistirir; repo'da `.ui` yok, bu yuzden
  CMakeLists AUTOUIC'i yalniz `.ui` varsa aciyor (b6b37c1). Bozuk uic artik configure'u kilitlemez;
  "CMake Generate step failed" alirsan once bunun uygulandigini (git log) dogrula.
- Kanitli calisan ZBook komutu: `cmake -S . -B build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DQT_ROOT=C:/msys64/ucrt64
  -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON`
