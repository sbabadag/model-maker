#pragma once

#include "model_maker/geometry.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

struct Bounds3 {
    Vec3 minimum{};
    Vec3 maximum{};
};

class Document {
public:
    void addModel(WireframeModel model);
    void addLine(const Vec3& from, const Vec3& to);
    void reserveModels(std::size_t count);
    void moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    void copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement);
    void deleteModels(const std::vector<std::size_t>& indices);
    void replaceModel(std::size_t index, std::vector<WireframeModel> replacements);
    void clear() noexcept;

    const std::vector<WireframeModel>& models() const noexcept;
    void setLayerProperties(EntityProperties properties);
    const std::unordered_map<std::string, EntityProperties>& layers() const noexcept;
    std::optional<Bounds3> bounds() const;
    const std::vector<Bounds3>& modelBounds() const;
    std::vector<std::size_t> queryBounds(const std::function<bool(const Bounds3&)>& intersects) const;
    std::vector<std::size_t> query2D(Vec3 minimum, Vec3 maximum) const;

    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);

private:
    struct SpatialNode {
        Bounds3 bounds{};
        std::size_t begin{};
        std::size_t end{};
        std::size_t left{static_cast<std::size_t>(-1)};
        std::size_t right{static_cast<std::size_t>(-1)};
    };

    void invalidateSpatialIndex() noexcept;
    void ensureSpatialIndex() const;
    std::size_t buildSpatialNode(std::size_t begin, std::size_t end) const;
    void querySpatialNode(std::size_t node, const Bounds3& area,
                          std::vector<std::size_t>& result) const;

    std::vector<WireframeModel> models_;
    std::unordered_map<std::string, EntityProperties> layers_;
    mutable bool spatialIndexDirty_{true};
    mutable std::vector<Bounds3> modelBounds_;
    mutable std::vector<std::size_t> spatialOrder_;
    mutable std::vector<SpatialNode> spatialNodes_;
    mutable std::optional<Bounds3> documentBounds_;
};

} // namespace mm
