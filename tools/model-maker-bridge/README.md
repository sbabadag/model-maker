# Model Maker Bridge — Tekla Structures Direct Component Transfer (P1)

Tekla Structures içindeki seçili bir bileşeni (Connection / Component / Detail /
CustomPart) salt-okunur Open API ile okuyup, sürümlü bir pakete dönüştüren ve Model
Maker'ın bu paketi açıp semantik nesneleri önizleyebildiği köprü.

İki parça + ortak veri sözleşmesi:

1. **Extractor** (`tekla-extractor/`, C# net48) — Tekla açıkken seçili bileşeni okuyup
   `.mmbridge` paketi yazar. Tekla'ya bağımlı; yalnızca Windows'ta çalışır.
2. **Importer** (C++ OCC-off, `src/bridge_package.cpp` + `bridge_component_mapper.cpp`)
   — paketi okur, doğrular, nesneleri semantik türlere eşler, önizleme üretir.
   Linux'ta test edilir.
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
  fixtures/fixture-146-shear-plate.mmbridge/  # FIXTURE (elle yazılmış, gerçek Tekla DEĞİL)
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
  tools/model-maker-bridge/fixtures/fixture-146-shear-plate.mmbridge
```

## Vertical slice durumu (P1)

Yapıldı ve doğrulandı:
- Sürümlü paket şeması + Python doğrulayıcı (fixture GECERLI).
- C# Core (POCO + yazıcı + doğrulayıcı): 12/12 xUnit test.
- C# Extractor (net48): Tekla API'sine karşı 0 hata derlenir.
- C++ importer (okuyucu + doğrulayıcı + eşleyici + önizleme): OCC-off test geçer;
  tam ctest 78/78 (regresyon yok).
- 146 fixture paketi (açıkça FIXTURE işaretli) + roundtrip.

Yapılmadı (sonraki dilim, dürüstçe):
- **Gerçek Tekla API çalışma zamanı testi** — Windows'ta Tekla 2025 ile (Linux'ta
  çalıştırılamaz; mock/static test gerçek test SAYILMAZ).
- **Document/OCCT materializasyonu** — paketteki Plate/BoltGroup'ları gerçek
  `Document`/`OccSolidStore` nesnelerine çevirme (member geometry + ComponentMaterializer
  yolu). Şu an importer yalnızca önizleme + semantik kayıt üretir.
- **Qt arayüzü** (File > Import > Tekla Component Bridge) — Windows/Qt-only, Linux'ta
  derlenmez. Giriş noktası `mapBridgePackage()` fonksiyonudur; UI bunu çağıracak.
- **Parametrik ComponentBlueprint üretimi** (parametreler -> GenerationPlan) ve
  SOURCE B varyasyon karşılaştırması.

## Veri akışı

```
Tekla (seçili bileşen)
   -> Extractor (GetSelectedObjects -> GetChildren -> GetSolid bbox -> GetAttribute)
   -> .mmbridge paketi (manifest/component/parameters/objects/geometry/diagnostics)
   -> C++ readBridgePackage (oku + doğrula)
   -> mapBridgePackage (semantik eşleme + önizleme)
   -> [sonraki dilim] Document/OCCT materializasyonu
```
