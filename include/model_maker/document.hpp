#pragma once

#include "model_maker/geometry.hpp"

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

struct DocumentSnapshot {
    std::vector<WireframeModel> models;
    std::unordered_map<std::string, EntityProperties> layers;
};

class Document {
public:
    Document();
    void addModel(WireframeModel model);
    void addLine(const Vec3& from, const Vec3& to);
    void reserveModels(std::size_t count);
    void moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    void copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    std::size_t setModelLayer(const std::vector<std::size_t>& indices, const std::string& layer);
    std::size_t setModelColor(const std::vector<std::size_t>& indices,
                              std::optional<std::uint32_t> color);
    std::size_t setModelProfile(const std::vector<std::size_t>& indices,
                                const std::string& profileName);
    std::size_t setModelLineType(const std::vector<std::size_t>& indices, const std::string& lineType);
    void deleteModels(const std::vector<std::size_t>& indices);
    void replaceModel(std::size_t index, std::vector<WireframeModel> replacements);
    void clear() noexcept;
    void pushSnapshot();
    bool undo();
    bool redo();
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    void clearHistory() noexcept;

    std::vector<WireframeModel>& mutableModels() noexcept;
    const std::vector<WireframeModel>& models() const noexcept;
    void setLayerProperties(EntityProperties properties);
    const std::unordered_map<std::string, EntityProperties>& layers() const noexcept;
    bool createLayer(std::string name);
    bool deleteLayer(const std::string& name);
    bool renameLayer(const std::string& oldName, std::string newName);
    std::vector<std::string> layerNames(std::string filter = {}) const;
    EntityProperties effectiveProperties(const WireframeModel& model) const;
    const EntityProperties& effectiveProperties(std::size_t index) const;
    bool modelIsEditable(std::size_t index) const;
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

    void setBeamLoad(std::size_t modelIndex, BeamLoad load);
    std::optional<BeamLoad> getBeamLoad(std::size_t modelIndex) const;
    const std::unordered_map<std::size_t, BeamLoad>& beamLoads() const noexcept;
    void clearBeamLoads();

private:
    static constexpr std::size_t kMaxUndoEntries = 100;

    struct SpatialNode {
        Bounds3 bounds{};
        std::size_t begin{};
        std::size_t end{};
        std::size_t left{static_cast<std::size_t>(-1)};
        std::size_t right{static_cast<std::size_t>(-1)};
    };

    void invalidateDerivedState() noexcept;
    void ensureSpatialIndex() const;
    void ensureEffectiveCache() const;
    std::size_t buildSpatialNode(std::size_t begin, std::size_t end) const;
    void querySpatialNode(std::size_t node, const Bounds3& area,
                          std::vector<std::size_t>& result) const;
    void restoreSnapshot(const DocumentSnapshot& snapshot);

    std::vector<WireframeModel> models_;
    std::unordered_map<std::string, EntityProperties> layers_;
    std::deque<DocumentSnapshot> undoStack_;
    std::deque<DocumentSnapshot> redoStack_;
    EntityProperties resolveEffectiveProperties(std::size_t index) const;
    mutable bool spatialIndexDirty_{true};
    mutable bool effectiveCacheDirty_{true};
    mutable std::vector<EntityProperties> effectiveCache_;
    mutable std::vector<Bounds3> modelBounds_;
    mutable std::vector<std::size_t> spatialOrder_;
    mutable std::vector<SpatialNode> spatialNodes_;
    mutable std::optional<Bounds3> documentBounds_;
    std::unordered_map<std::string, NodeConstraint> nodeConstraints_;
    std::unordered_map<std::size_t, BeamLoad> beamLoads_;
};

} // namespace mm
