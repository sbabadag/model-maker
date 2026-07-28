#include "model_maker/document.hpp"

#include <fstream>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace mm {
namespace {
Bounds3 mergeBounds(const Bounds3& a, const Bounds3& b) noexcept {
    return {{std::min(a.minimum.x, b.minimum.x), std::min(a.minimum.y, b.minimum.y),
             std::min(a.minimum.z, b.minimum.z)},
            {std::max(a.maximum.x, b.maximum.x), std::max(a.maximum.y, b.maximum.y),
             std::max(a.maximum.z, b.maximum.z)}};
}

bool intersects2D(const Bounds3& a, const Bounds3& b) noexcept {
    return a.maximum.x >= b.minimum.x && a.minimum.x <= b.maximum.x &&
           a.maximum.y >= b.minimum.y && a.minimum.y <= b.maximum.y;
}
}

void Document::addModel(WireframeModel model) {
    models_.push_back(std::move(model));
    invalidateSpatialIndex();
}
void Document::addLine(const Vec3& from, const Vec3& to) { addModel(WireframeModel::line(from, to)); }
void Document::reserveModels(std::size_t count) { models_.reserve(count); }

void Document::moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    for (const auto index : indices) {
        if (index < models_.size()) models_[index].translate(displacement);
    }
    invalidateSpatialIndex();
}

void Document::copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    std::vector<WireframeModel> copies;
    copies.reserve(indices.size());
    for (const auto index : indices) {
        if (index < models_.size()) {
            copies.push_back(models_[index]);
            copies.back().translate(displacement);
        }
    }
    models_.insert(models_.end(), copies.begin(), copies.end());
    invalidateSpatialIndex();
}

void Document::clear() noexcept { models_.clear(); invalidateSpatialIndex(); }
const std::vector<WireframeModel>& Document::models() const noexcept { return models_; }
void Document::setLayerProperties(EntityProperties properties) {
    layers_[properties.layer] = std::move(properties);
}
const std::unordered_map<std::string, EntityProperties>& Document::layers() const noexcept { return layers_; }

void Document::invalidateSpatialIndex() noexcept { spatialIndexDirty_ = true; }

void Document::ensureSpatialIndex() const {
    if (!spatialIndexDirty_) return;
    modelBounds_.clear();
    modelBounds_.resize(models_.size());
    spatialOrder_.clear();
    spatialOrder_.reserve(models_.size());
    documentBounds_.reset();
    for (std::size_t index = 0; index < models_.size(); ++index) {
        const auto& vertices = models_[index].vertices();
        if (vertices.empty()) continue;
        Bounds3 bounds{vertices.front(), vertices.front()};
        for (const auto& vertex : vertices) {
            bounds.minimum.x = std::min(bounds.minimum.x, vertex.x);
            bounds.minimum.y = std::min(bounds.minimum.y, vertex.y);
            bounds.minimum.z = std::min(bounds.minimum.z, vertex.z);
            bounds.maximum.x = std::max(bounds.maximum.x, vertex.x);
            bounds.maximum.y = std::max(bounds.maximum.y, vertex.y);
            bounds.maximum.z = std::max(bounds.maximum.z, vertex.z);
        }
        modelBounds_[index] = bounds;
        spatialOrder_.push_back(index);
        documentBounds_ = documentBounds_ ? mergeBounds(*documentBounds_, bounds) : bounds;
    }
    spatialNodes_.clear();
    spatialNodes_.reserve(spatialOrder_.size() * 2);
    if (!spatialOrder_.empty()) buildSpatialNode(0, spatialOrder_.size());
    spatialIndexDirty_ = false;
}

std::size_t Document::buildSpatialNode(std::size_t begin, std::size_t end) const {
    Bounds3 bounds = modelBounds_[spatialOrder_[begin]];
    for (std::size_t i = begin + 1; i < end; ++i)
        bounds = mergeBounds(bounds, modelBounds_[spatialOrder_[i]]);
    const std::size_t nodeIndex = spatialNodes_.size();
    spatialNodes_.push_back({bounds, begin, end});
    if (end - begin <= 16) return nodeIndex;

    const double xSpan = bounds.maximum.x - bounds.minimum.x;
    const double ySpan = bounds.maximum.y - bounds.minimum.y;
    const bool splitX = xSpan >= ySpan;
    const std::size_t middle = begin + (end - begin) / 2;
    std::nth_element(spatialOrder_.begin() + static_cast<std::ptrdiff_t>(begin),
                     spatialOrder_.begin() + static_cast<std::ptrdiff_t>(middle),
                     spatialOrder_.begin() + static_cast<std::ptrdiff_t>(end),
                     [&](std::size_t a, std::size_t b) {
        const auto center = [&](std::size_t index) {
            const auto& value = modelBounds_[index];
            return splitX ? value.minimum.x + value.maximum.x : value.minimum.y + value.maximum.y;
        };
        return center(a) < center(b);
    });
    const std::size_t left = buildSpatialNode(begin, middle);
    const std::size_t right = buildSpatialNode(middle, end);
    spatialNodes_[nodeIndex].left = left;
    spatialNodes_[nodeIndex].right = right;
    return nodeIndex;
}

void Document::querySpatialNode(std::size_t nodeIndex, const Bounds3& area,
                                std::vector<std::size_t>& result) const {
    const auto& node = spatialNodes_[nodeIndex];
    if (!intersects2D(node.bounds, area)) return;
    constexpr std::size_t noNode = static_cast<std::size_t>(-1);
    if (node.left == noNode) {
        for (std::size_t i = node.begin; i < node.end; ++i) {
            const auto modelIndex = spatialOrder_[i];
            if (intersects2D(modelBounds_[modelIndex], area)) result.push_back(modelIndex);
        }
        return;
    }
    querySpatialNode(node.left, area, result);
    querySpatialNode(node.right, area, result);
}

std::optional<Bounds3> Document::bounds() const {
    ensureSpatialIndex();
    return documentBounds_;
}

const std::vector<Bounds3>& Document::modelBounds() const {
    ensureSpatialIndex();
    return modelBounds_;
}

std::vector<std::size_t> Document::queryBounds(
    const std::function<bool(const Bounds3&)>& intersects) const {
    ensureSpatialIndex();
    std::vector<std::size_t> result;
    if (spatialNodes_.empty()) return result;
    std::vector<std::size_t> pending{0};
    while (!pending.empty()) {
        const std::size_t nodeIndex = pending.back();
        pending.pop_back();
        const auto& node = spatialNodes_[nodeIndex];
        if (!intersects(node.bounds)) continue;
        if (node.left == static_cast<std::size_t>(-1)) {
            for (std::size_t position = node.begin; position < node.end; ++position) {
                const std::size_t modelIndex = spatialOrder_[position];
                if (intersects(modelBounds_[modelIndex])) result.push_back(modelIndex);
            }
        } else {
            pending.push_back(node.right);
            pending.push_back(node.left);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::size_t> Document::query2D(Vec3 minimum, Vec3 maximum) const {
    ensureSpatialIndex();
    Bounds3 area{{std::min(minimum.x, maximum.x), std::min(minimum.y, maximum.y),
                  std::min(minimum.z, maximum.z)},
                 {std::max(minimum.x, maximum.x), std::max(minimum.y, maximum.y),
                  std::max(minimum.z, maximum.z)}};
    std::vector<std::size_t> result;
    if (!spatialNodes_.empty()) querySpatialNode(0, area, result);
    std::sort(result.begin(), result.end());
    return result;
}

void Document::save(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Could not open file for writing");

    output.precision(17);
    output << "MMW1\n" << models_.size() << '\n';
    for (const auto& model : models_) {
        output << model.vertices().size() << ' ' << model.edges().size() << '\n';
        for (const auto& vertex : model.vertices()) output << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
        for (const auto& edge : model.edges()) output << edge.from << ' ' << edge.to << '\n';
    }
    if (!output) throw std::runtime_error("Could not write document");
}

void Document::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Could not open file for reading");

    std::string signature;
    std::size_t modelCount{};
    if (!(input >> signature >> modelCount) || signature != "MMW1" || modelCount > 100000) {
        throw std::runtime_error("Invalid Model Maker file");
    }

    std::vector<WireframeModel> loaded;
    loaded.reserve(modelCount);
    for (std::size_t i = 0; i < modelCount; ++i) {
        std::size_t vertexCount{}, edgeCount{};
        if (!(input >> vertexCount >> edgeCount) || vertexCount > 1000000 || edgeCount > 2000000) {
            throw std::runtime_error("Invalid model data");
        }
        std::vector<Vec3> vertices(vertexCount);
        std::vector<Edge> edges(edgeCount);
        for (auto& vertex : vertices) {
            if (!(input >> vertex.x >> vertex.y >> vertex.z)) throw std::runtime_error("Invalid vertex data");
        }
        for (auto& edge : edges) {
            if (!(input >> edge.from >> edge.to)) throw std::runtime_error("Invalid edge data");
        }
        loaded.emplace_back(std::move(vertices), std::move(edges));
    }
    models_ = std::move(loaded);
    invalidateSpatialIndex();
}

} // namespace mm
