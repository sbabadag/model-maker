# Model Maker Bridge — Tekla Structures Direct Component Transfer (P1)

Tekla Structures içindeki seçili bir bileşeni (Connection / Component / Detail /
CustomPart) salt-okunur Open API ile okuyup, sürümlü bir pakete dönüştüren ve Model
Maker'ın bu paketi açıp semantik nesneleri önizleyebildiği köprü.

İki parça + ortak veri sözleşmesi:

1. **Extractor** (`tekla-extractor/`, C# net48) — Tekla açıkken seçili bileşeni okuyup
   `.mmbridge` paketi yazar. Tekla'ya bağımlı; yalnızca Windows'ta çalışır.
2. **Importer** (C++, `src/bridge_package.cpp` + `bridge_component_mapper.cpp` + `bridge_occ_adapter.cpp`)
   — paketi okur, doğrular, nesneleri semantik türlere eşler, önizleme üretir
   (OCC-off); V2'de plaka kati + bulon delikleri gerçek OCCT ile Document'a ATOMIK
   işler (OCC-on). Linux'ta test edilir.
3. **Veri sözleşmesi** (`model-maker-bridge.schema.json`) — ikisinin konuştuğu
   sürümlü JSON biçimi.

## Dürüstlük kuralları (bağlayıcı)

- `GetConnectionStatus()` false ise extractor **sahte veri üretmez**, açık hata döner.
- Parametreler yalnızca bilinen ham Tekla attribute adlarıyla `GetAttribute` ile okunur;
  bilinmeyen parametrelerin otomatik keşfi **varsayılmaz** (`--attributes` verilmezse
  `parameters.json` boş kalır).
- Tekla GUID'leri yalnızca kaynak referansıdır; Model Maker EntityId'si importer atar.
- İmporter yalnızca kernel'in gerçekten desteklediği türleri "supported" işaretler
  (Plate, BoltGroup). Weld / Fitting / BooleanPart → Unsupported/ReferenceGeometry.
- Paket köküne göre göreli `geometryFile` yolları güvenlik denetiminden geçer
  (mutlak yol / `..` / sürücü yasak); 32 MB/dosya, 256 MB/paket sınırı.

## Dizin

```
tools/model-maker-bridge/
  model-maker-bridge.schema.json    # paket şeması (draft-07)
  scripts/validate_bridge_package.py  # stdlib yapısal doğrulayıcı
  fixtures/fixture-146-shear-plate.mmbridge/     # FIXTURE V1 (elle, gerçek Tekla DEĞİL)
  fixtures/fixture-146-shear-plate-v2.mmbridge/  # FIXTURE V2 (doğrulanmış 146 değerleri)
  tekla-extractor/
    ModelMaker.TeklaBridge.sln
    Directory.Build.props           # TeklaApiVersion / DisablePatchCodeBase (Linux)
    kur.ps1                         # Windows derleme + publish betiği
    src/ModelMaker.TeklaBridge.Core/     # netstandard2.0, Tekla'sız (POCO + yazıcı + doğrulayıcı)
    src/ModelMaker.TeklaBridge.Extractor/ # net48, Tekla okuyucu (console)
    tests/ModelMaker.TeklaBridge.Core.Tests/  # net8 xUnit
```

## C# — derle + test (Linux'ta doğrulanır)

```bash
export PATH="$HOME/.dotnet:$PATH"; export DOTNET_ROOT="$HOME/.dotnet"; export DOTNET_gcServer=0
cd tools/model-maker-bridge/tekla-extractor
dotnet build ModelMaker.TeklaBridge.sln -c Release -m:1
dotnet test tests/ModelMaker.TeklaBridge.Core.Tests -c Release --no-build
```

Extractor net48'de **Tekla.Structures.Model 2021.0.0** referans assembly'sine karşı
derlenir (0 hata = API çağrıları doğru). Çalıştırmak Tekla'nın olduğu Windows'ta:

```powershell
# Tekla açık + model yüklü + bileşen SEÇİLİ iken:
cd tools\model-maker-bridge\tekla-extractor
powershell -ExecutionPolicy Bypass -File .\kur.ps1
dist\TeklaBridge.Extractor.exe --output C:\dizin\paket --attributes e1,e2,t,boltSize
```

## C++ importer — derle + test (Linux'ta doğrulanır)

```bash
cd /home/ubuntu/model-maker-overnight
cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON
cmake --build build-hermes --target model_maker_bridge_package_reader_test
./build-hermes/model_maker_bridge_package_reader_test
```

Doğrulayıcı (Python, paket çıktısı için):

```bash
python3 tools/model-maker-bridge/scripts/validate_bridge_package.py \
  tools/model-maker-bridge/fixtures/fixture-146-shear-plate-v2.mmbridge
```

OCC-on import testi (gerçek OCCT ile plaka kati + 6 M20 delik + Document + undo/redo):

```bash
cd /home/ubuntu/model-maker-overnight
g++ -std=gnu++20 -O1 -DMM_HAS_OCC \
    -DMM_BRIDGE_FIXTURE_DIR="\"$(pwd)/tools/model-maker-bridge/fixtures\"" \
    -I include -I third_party/nlohmann -I /usr/include/opencascade \
    tests/occ_bridge_146_import_test.cpp \
    src/bridge_package.cpp src/bridge_component_mapper.cpp src/bridge_occ_adapter.cpp \
    src/component_materializer.cpp src/component_framework.cpp src/component_engine.cpp \
    src/component_dialog.cpp src/document.cpp src/mmw8_codec.cpp src/end_plate.cpp \
    src/bolt.cpp src/cut_modifier.cpp src/geometry.cpp src/profile_database.cpp \
    src/reference_geometry.cpp src/constraint_system.cpp src/contact_resolver.cpp \
    src/geometry_reference_resolver.cpp src/magnetic_frame.cpp src/component_recompute.cpp \
    src/component.cpp src/assembly.cpp src/camera.cpp src/occ_contact_resolver.cpp \
    src/occ_solid_store.cpp src/occ_brep_serialize.cpp src/occ_geometry.cpp \
    src/bolt_geometry.cpp src/end_plate_geometry.cpp src/sha256.cpp src/grid.cpp \
    src/occ_store_null.cpp src/component_glyph.cpp \
    -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
    -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase -lTKDESTEP \
    -o /tmp/occ_bridge_146_import_test && /tmp/occ_bridge_146_import_test
```

## Vertical slice durumu (P1 + OCC import dilimi)

Yapıldı ve doğrulandı:
- Sürümlü paket şeması (V1 + V2) + Python doğrulayıcı (fixture'lar GECERLI).
- C# Core (POCO + yazıcı + doğrulayıcı): 12/12 xUnit test.
- C# Extractor (net48): Tekla API'sine karşı 0 hata derlenir.
- C++ importer (okuyucu + doğrulayıcı + eşleyici + önizleme): OCC-off test geçer;
  tam ctest 78/78 (regresyon yok).
- **V2 OCC-on import**: plaka kati (bbox -> 10x170x200, hacim 340000), 6 M20 delik
  (eksen +X inferred, cap 22 standard), Document'a atomik işleme + undo/redo +
  rollback. 51 OCC-on kabul kontrolü geçer.
- 146 fixture paketleri (V1 + V2, açıkça FIXTURE işaretli).

Yapılmadı (sonraki dilim, dürüstçe):
- **Gerçek Tekla API çalışma zamanı testi** — Windows'ta Tekla 2025 SP6 ile (Linux'ta
  çalıştırılamaz; mock/static test gerçek test SAYILMAZ).
- **Gerçek ASUS V2 JSON'u** — bu dilim doğrulanmış değerlerle elle yazılmış fixture
  kullanır; tam extractor çıktısı Windows'tan istendiğinde beslenir.
- **Codex reviewer** — standalone Codex CLI auth yok (401); erişilebilir olduğunda
  kritik OCC + Document değişiklikleri review edilecek.
- **Qt arayüzü** (File > Import > Tekla Component Bridge) — Windows/Qt-only, Linux'ta
  derlenmez. Giriş noktası `importBridgeComponent()` fonksiyonudur; UI bunu çağıracak.
- **Kaynak geometrisi** (weld fillet) ve **ikincil uye govdesi** (HEA300 BEAM)
  yeniden üretilmez — yalnız metadata/referans.

## Veri akışı

```
Tekla (seçili bileşen)
   -> Extractor (GetSelectedObjects -> GetChildren -> GetSolid bbox -> GetAttribute)
   -> .mmbridge paketi (manifest/component/parameters/objects/geometry/diagnostics)
   -> C++ readBridgePackage (oku + doğrula; V2: boltGroup/weld/topology/role)
   -> mapBridgePackage (semantik eşleme + önizleme)
   -> [OCC-on V2] importBridgeComponent (plaka kati + delikler + Document + undo/redo)
```
