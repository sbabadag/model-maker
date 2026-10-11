#pragma once

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/grid.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

struct Bounds3 {
    Vec3 minimum{};
    Vec3 maximum{};
};

struct UndoOp {
    enum class Kind { Add, Delete, Replace, Move, Transform, GridAdd, GridRemove, GridReplace };
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
};
struct UndoRecord {
    std::vector<UndoOp> ops;
    std::unordered_map<std::string, EntityProperties> layersBefore;
    std::unordered_map<std::string, EntityProperties> layersAfter;
};

// GridDefinition / GridAxisLine ve Tekla koordinat dili `model_maker/grid.hpp`
// icinde tanimlidir (portatif cekirdek). Aks cizgisi geometrisi `axes` alaninda
// TUREtilmis olarak durur; kaynak olan spesifikasyondur (xCoords/yCoords/...).

class Document {
public:
    Document();
    void addModel(WireframeModel model);
    void addLine(const Vec3& from, const Vec3& to);
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
    bool undo();
    bool redo();
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    void clearHistory() noexcept;
    // Her mutasyonda artan icerik surumu — GPU batch yenileme etiketi
    // (boyut degismeyen pozisyon degisikliklerini de yakalar).
    std::uint64_t revision() const noexcept { return revision_; }

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
    void markPending(std::size_t index);
    void applyUndoOp(const UndoOp& op, bool forward);
    // Yeni, tekil, monoton bir varlik kimligi uretir (0 uretmez).
    EntityId allocateEntityId() noexcept;

    std::vector<WireframeModel> models_;
    // models_ ile BIREBIR hizali kimlik vektoru (modelIds_.size() == models_.size()
    // belirtsizdir). Silme kaydirmasi/undo-redo burada da ayni sekilde uygulanir.
    std::vector<EntityId> modelIds_;
    EntityId nextEntityId_{1};
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
};

} // namespace mm
