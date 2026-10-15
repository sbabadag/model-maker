#pragma once

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/grid.hpp"
#include "model_maker/mmw8_codec.hpp"
#include "model_maker/cut_modifier.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// OPAK B-Rep magazasi: TANIMI bu baslikta degil (occ_store.hpp). Document
// yalnizca soyut arayuz uzerinden sahiplenir; OCCT tipi bu basliga SIZMAZ.
class OccStore;

struct Bounds3 {
    Vec3 minimum{};
    Vec3 maximum{};
};

struct UndoOp {
    enum class Kind { Add, Delete, Replace, Move, Transform, GridAdd, GridRemove, GridReplace,
                      CutAdd, CutRemove, CutReplace };
    Kind kind{Kind::Add};
    std::size_t index{};
    std::vector<WireframeModel> beforeModels; // delete: silinenler; replace: eski model
    std::vector<WireframeModel> afterModels;  // add: eklenen; replace: yeni modeller
    // KARARLI KIMLIK: before/after modelleriyle BIREBIR hizali kimlikler.
    // Geri al/yinele sirasinda DOGRU varlik kimligi geri getirilir (yeni
    // varlik yaratildiginda yeni kimlik atanirken ureten taraf kaydeder).
    std::vector<EntityId> beforeIds;          // delete/replace: silinen/eski varliklarin kimligi
    std::vector<EntityId> afterIds;           // add/replace: eklenen/yeni varliklarin kimligi
    std::vector<std::size_t> indices;         // delete (azalan) / move: etkilenen indeksler
    Vec3 displacement{};                      // move: uygulanan yer değiştirme
    // Transform (rotate/scale gumball): v' = pivot + linear*(v - pivot).
    // Undo/redo linearInverse ile uygulanir (tam tersinir).
    Vec3 pivot{};
    std::array<double, 9> linear{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<double, 9> linearInverse{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Grid (GridAdd/GridRemove/GridReplace): yapi gridi ekle/sil/degistir.
    // GridAdd: gridAfter eklendi (gridIndex'e). GridRemove: gridBefore silindi.
    // GridReplace: gridBefore -> gridAfter (ayni indeks).
    std::size_t gridIndex{};
    GridDefinition gridBefore;
    GridDefinition gridAfter;
    // Cut (CutAdd/CutRemove/CutReplace): kesme modifier ekle/sil/degistir.
    std::size_t cutIndex{};
    CutModifier cutBefore;
    CutModifier cutAfter;
};
struct UndoRecord {
    std::vector<UndoOp> ops;
    std::unordered_map<std::string, EntityProperties> layersBefore;
    std::unordered_map<std::string, EntityProperties> layersAfter;
    // GEOMETRI SURUM REFERANSLARI (OCCT tipi YOK — yalniz EntityId+VersionId).
    // Bir komutta DEGISEN tum EntityId'leri kapsar. Ayni EntityId birden fazla
    // yazilirsa: ILK oncesi (geometryBefore) ve SON sonrasi (geometryAfter)
    // saklanir — ARA surumler bagimsiz undo adimlarina donusmez. `version ==
    // kInvalidVersionId` "o varlik icin etkin geometri YOK" demektir (gecerli).
    std::vector<GeometrySlotRef> geometryBefore;  // islem ONCESI etkin surumler
    std::vector<GeometrySlotRef> geometryAfter;   // islem SONRASI etkin surumler
};

// GridDefinition / GridAxisLine ve Tekla koordinat dili `model_maker/grid.hpp`
// icinde tanimlidir (portatif cekirdek). Aks cizgisi geometrisi `axes` alaninda
// TUREtilmis olarak durur; kaynak olan spesifikasyondur (xCoords/yCoords/...).

class Document {
public:
    Document();
    // OPAK B-Rep magazasi (PIMPL): OCCT tipi bu baslikta GORUNMEZ. Yikici ve
    // tasima islemleri OUT-OF-LINE tanimlidir — std::unique_ptr<OccStore>
    // yok edilirken tam tip gerekir (document.cpp occ_store.hpp'yi include eder).
    // Document TASINABILIR (DxfFile::read + optional<Document>), KOPYALANAMAZ
    // (magaza tek sahipli).
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Belge-sahipli B-Rep magazasi. Varsayilan: no-op store (OCC-kapali guvenli).
    // OCC-acik Application baslangicta gercek OCCT store'unu ENJEKTE eder
    // (makeOccSolidStore) — saglayici secimi LINK SIRASINA bagli degildir.
    OccStore& occStore() noexcept { return *occStore_; }
    const OccStore& occStore() const noexcept { return *occStore_; }
    void setOccStore(std::unique_ptr<OccStore> store) noexcept;

    void addModel(WireframeModel model);
    void addLine(const Vec3& from, const Vec3& to);
    // KONSTRUKSIYON NESNELERI (P2): Point/Line/Plane. Standart WireframeModel
    // olarak saklanir — snap/secim/gumball/EntityId/undo zaten model yollarinda
    // calisir; ConstructionKind etiketiyle ayristirilir. Eklenen modelin
    // indeksini dondurur.
    std::size_t addConstructionPoint(const Vec3& position);
    std::size_t addConstructionLine(const Vec3& from, const Vec3& to);
    std::size_t addConstructionPlane(const WorkPlane& plane, double size);
    // ConstructionKind != None olan model indeksleri (belge sirasinda).
    std::vector<std::size_t> constructionObjects() const;
    bool isConstruction(std::size_t index) const;
    // Tum konstruksiyon nesnelerinin gorunurlugunu tek bayrakla degistirir.
    bool setConstructionVisible(bool visible);
    void reserveModels(std::size_t count);
    void moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    void copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    // Gumball rotate/scale: pivot etrafinda afin donusum (tek transform yolu).
    // linear row-major 3x3; linearInverse tam tersi (undo icin).
    void transformModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                         const std::array<double, 9>& linear,
                         const std::array<double, 9>& linearInverse);
    // Kolaylik: pivot etrafinda axis (birim) ekseninde aci (radyan) kadar dondur.
    void rotateModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                      const Vec3& axis, double radians);
    // Kolaylik: pivot etrafinda eksen-bazli olcekleme (factors.x/y/z).
    void scaleModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                     const Vec3& factors);
    std::size_t setModelLayer(const std::vector<std::size_t>& indices, const std::string& layer);
    std::size_t setModelColor(const std::vector<std::size_t>& indices,
                              std::optional<std::uint32_t> color);
    std::size_t setModelProfile(const std::vector<std::size_t>& indices,
                                const std::string& profileName);
    std::size_t setModelLineType(const std::vector<std::size_t>& indices, const std::string& lineType);
    std::size_t setModelMaterial(const std::vector<std::size_t>& indices, const std::string& material);
    void deleteModels(const std::vector<std::size_t>& indices);
    void replaceModel(std::size_t index, std::vector<WireframeModel> replacements);
    void clear() noexcept;
    void pushSnapshot();
    // ACIK (kapanmamis) bir undo kaydi var mi? (pushSnapshot() sonrasi true.)
    bool hasOpenUndoRecord() const noexcept { return !undoStack_.empty(); }
    // GEOMETRI SURUM DELTASI. Bir varligin etkin B-Rep surumu `before`dan
    // `after`a degistiginde cagrilir. Acik kayit YOKSA GUVENLI bicimde kendi
    // kaydini ACAR — boylece geometri degisikligi SESSIZCE undo disinda
    // KALMAZ (mevcut `recordUndoOp` davranisindan bilincli FARKI budur).
    // Ayni EntityId icin ILK `before` ve SON `after` korunur (ara surumler
    // bagimsiz undo adimina donusmez).
    void recordGeometryDelta(EntityId entity, VersionId before, VersionId after);
    bool undo();
    bool redo();
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    void clearHistory() noexcept;
    // Her mutasyonda artan icerik surumu — GPU batch yenileme etiketi
    // (boyut degismeyen pozisyon degisikliklerini de yakalar).
    std::uint64_t revision() const noexcept { return revision_; }

    // Bir sonraki tahsis edilecek EntityId (MMW8 kaliciligi/teshis icin).
    // kExhaustedEntityId = kimlik uzayi tukendi.
    EntityId nextEntityId() const noexcept { return nextEntityId_; }

    std::vector<WireframeModel>& mutableModels() noexcept;
    const std::vector<WireframeModel>& models() const noexcept;
    // KARARLI KIMLIK: index -> EntityId (gecersiz index icin kInvalidEntityId).
    // Indeks KALICI degildir; kimlik varlik yasam dongusu boyunca sabit kalir.
    EntityId modelId(std::size_t index) const noexcept;
    // EntityId -> guncel indeks (kimlik tektir). Yoksa nullopt.
    std::optional<std::size_t> indexOfEntity(EntityId id) const noexcept;
    const std::vector<EntityId>& modelIds() const noexcept { return modelIds_; }
    void setLayerProperties(EntityProperties properties);
    const std::unordered_map<std::string, EntityProperties>& layers() const noexcept;
    bool createLayer(std::string name);
    bool deleteLayer(const std::string& name);
    bool renameLayer(const std::string& oldName, std::string newName);
    std::vector<std::string> layerNames(std::string filter = {}) const;
    EntityProperties effectiveProperties(const WireframeModel& model) const;
    const EntityProperties& effectiveProperties(std::size_t index) const;
    bool modelIsEditable(std::size_t index) const;
    // ETKILESIM FILTRESI (ikincil gorunus pencereleri): ayarliyken isabet,
    // pencere secimi ve snap yalniz filtreyi gecen (or. gorunus derinlik
    // dilimindeki) modelleri gorur. Duzenlenebilirligi DEGISTIRMEZ (secili
    // nesne move/delete edilebilir). Bos fonksiyon = filtre yok (ana gorunus).
    void setPickFilter(std::function<bool(const Bounds3&)> filter) { pickFilter_ = std::move(filter); }
    bool hasPickFilter() const noexcept { return static_cast<bool>(pickFilter_); }
    bool modelIsPickable(std::size_t index) const;
    std::optional<Bounds3> bounds() const;
    const std::vector<Bounds3>& modelBounds() const;
    std::vector<std::size_t> queryBounds(const std::function<bool(const Bounds3&)>& intersects) const;
    std::vector<std::size_t> query2D(Vec3 minimum, Vec3 maximum) const;

    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);

    void setNodeConstraint(const Vec3& position, NodeConstraint constraint);
    std::optional<NodeConstraint> getNodeConstraint(const Vec3& position) const;
    const std::unordered_map<std::string, NodeConstraint>& nodeConstraints() const noexcept;
    void clearNodeConstraints();

    // KIRIS YUKLERI: KARARLI KIMLIK (EntityId) anahtarli.
    // Indeks KALICI degildir; yuk, varlik silinip geri alinsa bile DOGRU
    // varliga bagli kalir (bkz. entity_id.hpp). YASAM DONGUSU POLITIKASI:
    // (a) silinen varligin kaydi KORUNUR — geri alma AYNI kimligi geri
    //     getirir ve yuk yeniden baglanir; kimlikler YENI varliklara asla
    //     yeniden kullanilmadigi icin baska bir varliga baglanamaz;
    // (b) belge temizleme (clear) ve dosya yukleme (load) kayitlari SIFIRLAR.
    void setBeamLoad(EntityId entityId, BeamLoad load);
    std::optional<BeamLoad> getBeamLoad(EntityId entityId) const;
    const std::unordered_map<EntityId, BeamLoad>& beamLoads() const noexcept;
    void clearBeamLoads();
    // Yapi gridleri (Tekla tarzi aks cizgileri)
    std::size_t addGrid(GridDefinition grid);
    void clearGrids() noexcept;
    const std::vector<GridDefinition>& grids() const noexcept { return grids_; }
    GridDefinition* mutableGrid(std::size_t index) noexcept;
    bool setGridVisible(std::size_t index, bool visible);
    // Tekla tarzi: grid SPESIFIKASYONUNU degistirir ve `axes`'i yeniden uretir.
    bool setGrid(std::size_t index, GridDefinition grid);
    void removeGrid(std::size_t index);
    // Kesme modifier'leri (P3): kalici + duzenlenebilir. Indeks-bazli (grid
    // gibi); hedef/arac parcalar EntityId ile referanslanir (parca kimligi
    // korunur). add/remove/set undo/redo'ya girer.
    std::size_t addCutModifier(CutModifier modifier);
    void removeCutModifier(std::size_t index);
    bool setCutModifier(std::size_t index, CutModifier modifier);
    const std::vector<CutModifier>& cutModifiers() const noexcept { return cutModifiers_; }
    // O varligi hedefleyen kesme modifier'larinin INDEKSLERI (belge sirasinda).
    // Recompute zinciri bu sirayla uygulanir (deterministik). Devre disi olanlar
    // da dondurulur; uygulama zinciri onlari atlar.
    std::vector<std::size_t> cutModifiersForTarget(EntityId target) const;

private:
    static constexpr std::size_t kMaxUndoEntries = 100;
    std::uint64_t revision_{0};
    static constexpr std::size_t kMaxPendingIndexEntries = 4'096;

    struct SpatialNode {
        Bounds3 bounds{};
        std::size_t begin{};
        std::size_t end{};
        std::size_t left{static_cast<std::size_t>(-1)};
        std::size_t right{static_cast<std::size_t>(-1)};
    };

    void invalidateDerivedState() noexcept;
    void rebuildDerivedState();
    void ensureSpatialIndex() const;
    void ensureEffectiveCache() const;
    std::size_t buildSpatialNode(std::size_t begin, std::size_t end) const;
    void querySpatialNode(std::size_t node, const Bounds3& area,
                          std::vector<std::size_t>& result) const;
    void recordUndoOp(UndoOp op);
    void recordLayerState();
    // SURUM BUDAMA (Phase 3.2C): canli referans kumesini (etkin surumler +
    // undo/redo kayitlarindaki geometryBefore/geometryAfter + kimlikle
    // referanslanan varliklar) hesaplar ve magazadan geri kalanini budar.
    void pruneDeadVersions();
    // MMW8 (Phase 3.4C): guncel durumdan Mmw8Document kurar (save icin) ve
    // ayristirilmis Mmw8Document'i Document durumuna isler (load icin).
    void buildMmw8(Mmw8Document& out) const;
    void commitFromMmw8(Mmw8Document&& mdoc);
    void markPending(std::size_t index);
    void applyUndoOp(const UndoOp& op, bool forward);
    // Yeni, tekil, monoton bir varlik kimligi uretir (0 uretmez). Id uzayi
    // TUKENDIYSE (nextEntityId_ == kExhaustedEntityId) kInvalidEntityId doner
    // ve sayac DEGISMEZ — ayni id ASLA ikinci kez uretilmez.
    EntityId allocateEntityId() noexcept;
    // allocateEntityId'in FIRLATAN sarmalayicisi: tukenme durumunda
    // std::runtime_error firlatir. Yeni varlik ekleyen tum yollar bunu kullanir —
    // KONTROLLU basarisizlik, kismi ekleme veya kimlik-0 model birakilmaz.
    EntityId allocateEntityIdRequired();

    std::vector<WireframeModel> models_;
    // models_ ile BIREBIR hizali kimlik vektoru (modelIds_.size() == models_.size()
    // belirtsizdir). Silme kaydirmasi/undo-redo burada da ayni sekilde uygulanir.
    std::vector<EntityId> modelIds_;
    EntityId nextEntityId_{1};
    // Document-SAHIPLI B-Rep magazasi (tek dogruluk kaynagi). Application'da
    // AYRI bir harita YOKTUR; tum kati yollari buradan gecer.
    std::unique_ptr<OccStore> occStore_;
    // MMW8 (Phase 3.4C): OCC-kapali derlemede B-Rep'i OPAK korur. OCC-acik
    // derlemede B-Rep gercek store'dadir ve opaqueBreps_ BOS kalir.
    std::unordered_map<EntityId, std::string> opaqueBreps_;
    bool opaqueBrepsStale_{false};
    Mmw8Metadata mmw8Metadata_;
    std::vector<Mmw8UnknownBlock> mmw8UnknownBlocks_;
    std::unordered_map<std::string, EntityProperties> layers_;
    std::deque<UndoRecord> undoStack_;
    std::deque<UndoRecord> redoStack_;
    EntityProperties resolveEffectiveProperties(std::size_t index) const;
    mutable bool spatialIndexDirty_{true};
    mutable bool effectiveCacheDirty_{true};
    mutable std::vector<EntityProperties> effectiveCache_;
    mutable std::vector<Bounds3> modelBounds_;
    std::function<bool(const Bounds3&)> pickFilter_;
    mutable std::vector<std::size_t> spatialOrder_;
    mutable std::vector<std::size_t> pendingIndexRebuild_;
    mutable std::vector<unsigned char> pendingMask_; // pending uyelik maskesi (O(1))
    mutable std::vector<SpatialNode> spatialNodes_;
    mutable std::optional<Bounds3> documentBounds_;
    std::unordered_map<std::string, NodeConstraint> nodeConstraints_;
    std::unordered_map<EntityId, BeamLoad> beamLoads_;
    std::vector<GridDefinition> grids_;
    std::vector<CutModifier> cutModifiers_;
};

} // namespace mm
