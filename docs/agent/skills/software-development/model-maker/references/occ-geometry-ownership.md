# B-Rep sahipligi + surumlu geometri undo/redo (Phase 3.1 / 3.2A / 3.2B)

Dal: `occ-hybrid-geometry`. Commit zinciri: `b37b1b2` (ownership) -> `4524f2b`
(versioned store) -> Phase 3.2B (transactional undo).

## 1. Sahiplik modeli (TEK dogruluk kaynagi)

- B-Rep'lerin tek sahibi **Document**'tir: `std::unique_ptr<OccStore> occStore_`.
  `OccStore` (`include/model_maker/occ_store.hpp`) SOYUT ve **OCCT'siz** — imzalarinda
  OCCT tipi YOK, yalniz `EntityId`/`VersionId`/`std::size_t`.
  `document.hpp` yalnizca ileri bildirim gorur; `TopoDS_Shape` GENIS BASLIKLARA SIZMAZ.
- Somut magaza `OccSolidStore` (`occ_solid_store.hpp/.cpp`) YALNIZ OCC TU'larinda
  derlenir; `Application` `asSolidStore(&document_.occStore())` ile erisir.
- **Saglayici secimi LINK SIRASINA BAGLI DEGILDIR** (weak symbol YOK):
  `makeNullOccStore()` model_maker_core'da (no-op), `makeOccSolidStore()`
  model_maker_occ'de. `Application` OCC-acik derlemede Document'a ENJEKTE eder
  (`document_.setOccStore(makeOccSolidStore())`) — constructor injection.
  Startup'ta bir kez; **`clear()`/`load()` enjekte edileni EZMEZ** (yalniz icerigi bosaltir).
- Application'da AYRI harita **YOKTUR**; `occShapes_` KALDIRILDI. Yeni OCC yolu
  eklerken `grep occShapes_` -> sifir olmali.
- `Document` artik KOPYALANAMAZ (unique_ptr): `~Document()`/move out-of-line;
  `Document doc = DxfFile::read(...)` tasima ile calisir.

### Ekleme kurali (off-by-one tuzagi — KANITLI hata)
Kutu/silindir/boolean: sekil, `addStyledModel(...)` CAGRISINDAN **SONRA**,
YENI varligin kimligiyle anahtarlanir:

    addStyledModel(std::move(model));            // pushUndoSnapshot + addModel (kimlik URETIR)
    putOccSolid(document_.models().size() - 1, shape);   // DOGRU (sonrasinda)

Eskiden `emplace(models().size()-1, ...)` ekleme ONCESI cagriliyordu -> bir ONCEKI
varliga baglaniyordu (ilk eklemede `size_t(-1)`). Harness'te kaynak-sekil kapisi +
`lastLiveSolids` bu regresyonu yakalar.

## 2. Surum modeli (OccSolidStore)

    EntityId -> Slot { versions:[(VersionId, TopoDS_Shape)], active:VersionId }

- Her `set` YENI bir VersionId uretir ve ETKIN yapar; **eski surumler KORUNUR**.
- `find`/`lastLive` ETKIN surumu verir (Phase 3.1 davranisi korunur).
- `activate(id,v)` belirli surumu etkinlestirir; gecersiz surumu REDDEDER.
- **`restoreActive(id,v)`** ayri yoldur: `kInvalidVersionId`'yi KABUL eder
  ("etkin geometri YOK" — olusturma-undo'nun donmesi gereken hal). `activate`
  bunu reddeder; ikisini karistirma.
- VersionId sayaci **TUKENME (exhaustion)** semantigi: `kMax` son gecerli id,
  sonra `kInvalidVersionId` (0). **KELEPCELENMEZ** — kelepce ayni id'yi tekrar
  uretir ve `shapeOfVersion` ILK esleseni dondurdugu icin **yanlis (eski) geometri** verir.
  (EntityId sayaci ise kelepcelenir; orada yanlis geometri donmez. Ikisi BILEREK farkli.)
- IMMUTABILITE: `set` cagirana verilen seklin HANDLE'ini saklar — YERINDE DEGISTIRMEZ.
  Tum ureticiler YENI sekil dondurur; deep-copy (`BRepBuilderAPI_Copy`) BILEREK yok.
  Yeni bir yol mevcut sekli yerinde degistirirse kural bozulur (bugun boyle bir yol yok).

## 3. Undo/redo (Phase 3.2B) — transaction + atomiklik

`UndoRecord` (document.hpp, OCCT'siz) iki alan kazandi:

    std::vector<GeometrySlotRef> geometryBefore;  // islem ONCESI etkin surumler
    std::vector<GeometrySlotRef> geometryAfter;   // islem SONRASI etkin surumler
    // GeometrySlotRef { EntityId entity; VersionId version; }  (entity_id.hpp)

- `Document::recordGeometryDelta(id, before, after)`: acik kayit YOKSA **kendi kaydini acar**
  (`if (undoStack_.empty()) pushSnapshot();`). Bu, `recordUndoOp`'un sessiz-dusme
  davranisindan BILINCLI farktir — geometri degisikligi SESSIZCE undo disinda kalamaz.
- Ayni EntityId birden fazla yazilirsa: **ILK before + SON after** (dedup). Ara surumler
  bagimsiz undo adimina DONUSMEZ.
- `Application::putOccSolid` on-goruntuyu alir:
  `before = store->activeVersion(id); after = store->set(id, shape); recordGeometryDelta(...)`.
- **Iki fazli undo/redo:** FAZ 1 tum `geometryBefore/After` icin `findVersionExists`
  (hicbir sey degistirmeden); gecmezse `undo()` **false** doner ve Document/store/
  yiginlar SABIT kalir (kayit yigindan TUKETILMEZ). FAZ 2 uygular (restoreActive + applyUndoOp).
- **Undo gorunurlugu:** `recordHasEffect(rec)` = ops VEYA layers farki VEYA
  `geometryBefore` bos degil. `canUndo/canRedo/undo/redo` bunu kullanir; yalniz geometri
  deltasi olan kayit da undo edilebilir.

### Kapsanan uretim yollari (her biri putOccSolid -> delta)
addCube, addCylinder, Boolean Fuse/Common/Cut, toplu profil rotasyonu, kolon kotu,
trim, profil atama, grip re-extrude. Cogu `addStyledModel` uzerinden snapshot acar.
**Tuzak (bulundu/duzeltildi):** `assignProfileToSelection`'in 1:1 (`existingSolid`)
dali `addStyledModel` CAGRMAZ -> hic undo kaydi acilmiyordu; `pushUndoSnapshot()`
eklendi. Benzer dal eklerken kontrol et: replaceModel/putOccSolid'den once kayit ACIK mi?

### Davranis (mevcut semantik, degistirilmedi)
- Silme store'a DOKUNMAZ (kayit undo icin korunur). Silme != geometri silme.
- Boolean operandlari TUKETILMEZ (sonuc yeni varlik).
- Budama/GC **3.2C'ye** birakildi: `clear()`/`load()` disinda surum birikir.

## 4. Test altyapisi

- `tests/occ_store_test.cpp` — OCC'SIZ (core). `VersionedSpyStore` ile Document'in
  undo/redo SOZLESMESI: delta kayit acma, restoreActive cagrisi, eksik surum reddi,
  `kInvalidVersionId` gecerliligi. Gercek OCCT magaza davranisi BURADA sinanmaz.
- `tests/run_occ_solid_ids.py` — GERCEK OCCT (Linux sistem OCCT dogrudan-link; Windows 7.9.3).
  162 kontrol: Test 11 replace->undo->redo (hacim+bbox), 12 ayni-id coklu yazma,
  13 coklu-EntityId atomik, 14 olustur->undo->redo, 15 boolean->undo->redo,
  16 sil->undo->redo, 17 eksik surum reddi, 18 delta-only kayit, 19 snapshot'siz yazim,
  20 redo temizligi. Govdeler canli kaynaktan MARKER ile cikarilir (fail-loud);
  `assert_document_shape` document.cpp sozlesmesini de kilitler.
- `tests/mutate_occ_solid_ids.py` — 13 negatif kontrol (NC1-13), dosyalar sha256 ile geri yazilir.

## 5. Tuzaklar (hepsi yasandi)
- Negatif kontrol mutasyonu **derlenmezse** "KACIRILDI" gorunur ve harness cikisi BOS olur:
  yorum satiri ile `if (cond)` govdesini bosaltmak sozdizimi bozar. Mutasyonu tam ifadeyle degistir.
- Harness'i boruya yonlendirince `printf` tam tamponlanir; cokmede satirlar kaybolur ->
  `setvbuf(stdout, nullptr, _IOLBF, 0)`.
- `--no-shape-check` olmadan mutasyon kosucusu kaynak-sekil kapisinda erken duser ve
  RUNTIME testi hic kosmaz.
- Yeni `OccStore` sanal metodu eklerken TUM uygulayicilari guncelle:
  `NullOccStore`, `OccSolidStore`, testteki spy'lar — yoksa "abstract class" derleme hatasi.
- **Budama + auto-open tuzagi (Phase 3.2C, KANITLI):** `recordGeometryDelta`'nin acik
  kayit yokken actigi kaydi `pushSnapshot()` ile ACMA. pushSnapshot budama tetikler; o anda
  `before` surumu (set() sonrasi artik etkin degil, HENUZ kaydedilmedi) budanip KAYBOLUR.
  Kaydi dogrudan ac (`UndoRecord` push + `redoStack_.clear()`), budamayi yalniz komut
  sinirinda (pushSnapshot) ve gecmis temizliginde (clearHistory/redo) kos.

## 6. Surum budama (Phase 3.2C)

Amac: kullanilmayan OCC geometri surumlerini guvenli budamak (bellek birikimi kontrolu).

### Canli referans kumesi (Document::pruneDeadVersions, document.cpp)
Budanmayacak `(varlik, surum)` ciftleri UC kaynaktan gelir:
1. CANLI varliklarin (modelIds_) ETKIN surumu — guncel geometri.
2. undo/redo op'larinda KIMLIKLE referanslanan varliklarin (beforeIds/afterIds) ETKIN
   surumu — sil/ekle/deyistir geri alinirken geri yuklenecek geometri. **GEREKLI**: silme
   store'a DOKUNMAZ, bu yuzden silinen varligin geometrisi yalniz Delete op'unun kimlik
   referansiyla korunur (3. kural kapsamaz).
3. undo/redo kayitlarindaki ACIK geometryBefore/geometryAfter ref'leri.

### Magaza tarafi (OccSolidStore::prune)
`keep(entity, version)` false donen surumleri siler; bosalan slot'u KALDIRIR; budanan surumu
isaret eden `active` alanini kInvalidVersionId'ye dusurur. VersionId SABIT kalir (yeniden
numaralandirma YOK), sayac sifirlanmaz. `totalVersionCount()` toplam surum sayisini verir
(olcme). Soyut `OccStore::prune(const std::function<bool(EntityId,VersionId)>&)` OCCT'sizdir.

### Tetik noktalari
- pushSnapshot (komut basi; redo temizlenir + 100 kayit siniri asildiysa pop) -> budar.
- redo() (100 kayit siniri asildiysa) -> budar.
- clearHistory() -> budar (yalniz etkin surumler kalir).
- clear()/load() -> store tamamen sifirlanir (budama gerekmez).
- Ara surum (ayni varliga coklu yazim) ve terk edilen redo surumleri bir SONRAKI
  pushSnapshot'ta budanir (bir komutluk gecikme, sinirli).

### Test
- OCC-kapali: occ_store_test Test 9-11 (clearHistory/100-siniri tetikler; canli etkin
  surum korunur; olu surum budanir). PruningSpyStore (varlik,surum) modeller.
- OCC-acik harness: Test 20 (redo terk edilince budanir) + Test 21-26 (sil-undo-redo,
  150+ sinirli birikim, coklu-varlik, budanan surum reddi, clearHistory, rollback zinciri).
- Negatif: NC14 (etkin surum erken budama), NC15 (undo ref'i erken budama), NC16
  (redo temizlenmezse terk edilen surum budanmaz).
