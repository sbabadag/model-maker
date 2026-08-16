#include "model_maker/document.hpp"

#include <fstream>
#include <algorithm>
#include <iterator>
#include <cctype>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace mm {
namespace {
EntityProperties defaultLayerProperties() {
    EntityProperties layer;
    layer.layer = "0";
    layer.lineType = layer.effectiveLineType = "CONTINUOUS";
    layer.colorIndex = 7;
    layer.trueColor = layer.effectiveColor = 0xFFFFFFu;
    layer.lineWeight = layer.effectiveLineWeight = 0;
    return layer;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

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

std::vector<std::size_t> uniqueIndices(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> result = indices;
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}

Document::Document() {
    auto layer = defaultLayerProperties();
    layers_.emplace(layer.layer, std::move(layer));
}

void Document::addModel(WireframeModel model) {
    UndoOp op;
    op.kind = UndoOp::Kind::Add;
    op.index = models_.size();
    op.afterModels.push_back(model);
    recordUndoOp(std::move(op));
    models_.push_back(std::move(model));
    invalidateDerivedState();
}
void Document::addLine(const Vec3& from, const Vec3& to) { addModel(WireframeModel::line(from, to)); }
void Document::reserveModels(std::size_t count) { models_.reserve(count); }

void Document::moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    UndoOp op;
    op.kind = UndoOp::Kind::Move;
    op.indices = indices;
    op.displacement = displacement;
    recordUndoOp(std::move(op));
    for (const auto index : indices) {
        if (index < models_.size()) models_[index].translate(displacement);
    }
    invalidateDerivedState();
}

void Document::copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    std::vector<WireframeModel> copies;
    copies.reserve(indices.size());
    for (const auto index : indices) {
        if (index < models_.size()) {
            copies.push_back(models_[index]);
            copies.back().translate(displacement);
            UndoOp op;
            op.kind = UndoOp::Kind::Add;
            op.index = models_.size() + copies.size() - 1;
            op.afterModels.push_back(copies.back());
            recordUndoOp(std::move(op));
        }
    }
    models_.insert(models_.end(), copies.begin(), copies.end());
    invalidateDerivedState();
}

std::size_t Document::setModelLayer(const std::vector<std::size_t>& indices, const std::string& layer) {
    if (!layers_.contains(layer)) return 0;
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.layer = layer;
        models_[index].setProperties(std::move(properties));
        ++changed;
    }
    effectiveCacheDirty_ = true;
    return changed;
}

std::size_t Document::setModelColor(const std::vector<std::size_t>& indices,
                                    std::optional<std::uint32_t> color) {
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.trueColor = color;
        properties.colorIndex = 256;
        if (color) properties.effectiveColor = *color;
        models_[index].setProperties(std::move(properties));
        ++changed;
    }
    effectiveCacheDirty_ = true;
    return changed;
}

std::size_t Document::setModelProfile(const std::vector<std::size_t>& indices,
                                      const std::string& profileName) {
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.profileName = profileName;
        models_[index].setProperties(properties);
        ++changed;
    }
    effectiveCacheDirty_ = true;
    return changed;
}

std::size_t Document::setModelLineType(const std::vector<std::size_t>& indices,
                                       const std::string& lineType) {
    if (lineType.empty()) return 0;
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.lineType = lineType;
        if (lineType != "BYLAYER") properties.effectiveLineType = lineType;
        models_[index].setProperties(std::move(properties));
        ++changed;
    }
    effectiveCacheDirty_ = true;
    return changed;
}

void Document::deleteModels(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> valid;
    valid.reserve(indices.size());
    for (const auto index : indices)
        if (index < models_.size()) valid.push_back(index);
    std::sort(valid.begin(), valid.end(), std::greater<>{});
    valid.erase(std::unique(valid.begin(), valid.end()), valid.end());
    UndoOp op;
    op.kind = UndoOp::Kind::Delete;
    op.indices = valid;
    for (const auto index : valid) op.beforeModels.push_back(models_[index]);
    recordUndoOp(std::move(op));
    for (const auto index : valid)
        models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!valid.empty()) invalidateDerivedState();
}

void Document::replaceModel(std::size_t index, std::vector<WireframeModel> replacements) {
    if (index >= models_.size()) return;
    UndoOp op;
    op.kind = UndoOp::Kind::Replace;
    op.index = index;
    op.beforeModels.push_back(models_[index]);
    for (const auto& replacement : replacements) op.afterModels.push_back(replacement);
    recordUndoOp(std::move(op));
    const auto position = models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
    models_.insert(position, std::make_move_iterator(replacements.begin()),
                   std::make_move_iterator(replacements.end()));
    invalidateDerivedState();
}

void Document::clear() noexcept {
    models_.clear();
    layers_.clear();
    undoStack_.clear();
    redoStack_.clear();
    nodeConstraints_.clear();
    invalidateDerivedState();
    documentBounds_.reset();
}

static std::string jointKey(const Vec3& pt) {
    auto s = [](double v) { return std::to_string(v); };
    return s(pt.x) + "," + s(pt.y) + "," + s(pt.z);
}

void Document::setNodeConstraint(const Vec3& position, NodeConstraint constraint) {
    nodeConstraints_[jointKey(position)] = constraint;
}

std::optional<NodeConstraint> Document::getNodeConstraint(const Vec3& position) const {
    auto it = nodeConstraints_.find(jointKey(position));
    if (it != nodeConstraints_.end()) return it->second;
    return std::nullopt;
}

const std::unordered_map<std::string, NodeConstraint>& Document::nodeConstraints() const noexcept {
    return nodeConstraints_;
}

void Document::clearNodeConstraints() {
    nodeConstraints_.clear();
}

void Document::setBeamLoad(std::size_t modelIndex, BeamLoad load) {
    beamLoads_[modelIndex] = load;
}

std::optional<BeamLoad> Document::getBeamLoad(std::size_t modelIndex) const {
    auto it = beamLoads_.find(modelIndex);
    if (it != beamLoads_.end()) return it->second;
    return std::nullopt;
}

const std::unordered_map<std::size_t, BeamLoad>& Document::beamLoads() const noexcept {
    return beamLoads_;
}

void Document::clearBeamLoads() {
    beamLoads_.clear();
}
void Document::recordUndoOp(UndoOp op) {
    if (undoStack_.empty()) return; // kayıt açık değil (toplu yükleme vb.) — geri alınmaz
    undoStack_.back().push_back(std::move(op));
}

void Document::pushSnapshot() {
    // Yeni bir geri-alma kaydı açar; sonraki mutasyonlar bu kayda delta olarak eklenir.
    undoStack_.push_back(UndoRecord{});
    if (undoStack_.size() > kMaxUndoEntries)
        undoStack_.pop_front();
    redoStack_.clear();
}

void Document::applyUndoOp(const UndoOp& op, bool forward) {
    switch (op.kind) {
    case UndoOp::Kind::Add:
        if (forward) {
            if (!op.afterModels.empty())
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index),
                               op.afterModels.front());
        } else if (op.index < models_.size()) {
            models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index));
        }
        break;
    case UndoOp::Kind::Delete:
        if (forward) {
            // indices azalan sırada saklı — bu sırayla silme güvenli
            for (const auto index : op.indices)
                if (index < models_.size())
                    models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            // indices azalan sırada; sondan başa (artan) geri ekle
            for (std::size_t k = op.indices.size(); k-- > 0;) {
                const std::size_t index = op.indices[k];
                if (k < op.beforeModels.size() && index <= models_.size())
                    models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(index),
                                   op.beforeModels[k]);
            }
        }
        break;
    case UndoOp::Kind::Replace:
        if (op.index < models_.size()) {
            if (forward && !op.afterModels.empty()) models_[op.index] = op.afterModels.front();
            else if (!forward && !op.beforeModels.empty()) models_[op.index] = op.beforeModels.front();
        }
        break;
    case UndoOp::Kind::Move:
        for (const auto index : op.indices)
            if (index < models_.size())
                models_[index].translate(forward ? op.displacement
                                                 : Vec3{-op.displacement.x, -op.displacement.y,
                                                        -op.displacement.z});
        break;
    }
    invalidateDerivedState();
}

bool Document::undo() {
    while (!undoStack_.empty() && undoStack_.back().empty())
        undoStack_.pop_back();
    if (undoStack_.empty()) return false;
    UndoRecord record = std::move(undoStack_.back());
    undoStack_.pop_back();
    for (auto it = record.rbegin(); it != record.rend(); ++it)
        applyUndoOp(*it, false);
    redoStack_.push_back(std::move(record));
    return true;
}

bool Document::redo() {
    if (redoStack_.empty()) return false;
    UndoRecord record = std::move(redoStack_.back());
    redoStack_.pop_back();
    for (const auto& op : record)
        applyUndoOp(op, true);
    undoStack_.push_back(std::move(record));
    if (undoStack_.size() > kMaxUndoEntries)
        undoStack_.pop_front();
    return true;
}

bool Document::canUndo() const noexcept {
    for (auto it = undoStack_.rbegin(); it != undoStack_.rend(); ++it)
        if (!it->empty()) return true;
    return false;
}

bool Document::canRedo() const noexcept {
    return !redoStack_.empty();
}

void Document::clearHistory() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}
std::vector<WireframeModel>& Document::mutableModels() noexcept { return models_; }
const std::vector<WireframeModel>& Document::models() const noexcept { return models_; }
void Document::setLayerProperties(EntityProperties properties) {
    if (properties.layer.empty()) return;
    layers_[properties.layer] = std::move(properties);
    effectiveCacheDirty_ = true;
}
const std::unordered_map<std::string, EntityProperties>& Document::layers() const noexcept { return layers_; }

bool Document::createLayer(std::string name) {
    if (name.empty() || layers_.contains(name)) return false;
    EntityProperties layer;
    layer.layer = std::move(name);
    layer.lineType = layer.effectiveLineType = "CONTINUOUS";
    layer.colorIndex = 7;
    layer.trueColor = layer.effectiveColor = 0xFFFFFFu;
    layer.lineWeight = layer.effectiveLineWeight = 0;
    layers_.emplace(layer.layer, std::move(layer));
    effectiveCacheDirty_ = true;
    return true;
}

bool Document::deleteLayer(const std::string& name) {
    if (name.empty() || name == "0" || !layers_.contains(name)) return false;
    if (std::any_of(models_.begin(), models_.end(), [&](const WireframeModel& model) {
            return model.properties().layer == name;
        })) return false;
    effectiveCacheDirty_ = true;
    return layers_.erase(name) == 1;
}

bool Document::renameLayer(const std::string& oldName, std::string newName) {
    if (oldName.empty() || oldName == "0" || newName.empty() || layers_.contains(newName)) return false;
    const auto found = layers_.find(oldName);
    if (found == layers_.end()) return false;
    EntityProperties layer = found->second;
    layers_.erase(found);
    layer.layer = newName;
    layers_.emplace(newName, std::move(layer));
    for (auto& model : models_) {
        if (model.properties().layer != oldName) continue;
        EntityProperties properties = model.properties();
        properties.layer = newName;
        model.setProperties(std::move(properties));
    }
    effectiveCacheDirty_ = true;
    return true;
}

std::vector<std::string> Document::layerNames(std::string filter) const {
    const std::string loweredFilter = lowerAscii(std::move(filter));
    std::vector<std::string> result;
    result.reserve(layers_.size());
    for (const auto& [name, properties] : layers_) {
        (void)properties;
        if (loweredFilter.empty() || lowerAscii(name).find(loweredFilter) != std::string::npos)
            result.push_back(name);
    }
    std::sort(result.begin(), result.end(), [](const std::string& left, const std::string& right) {
        if (left == "0") return right != "0";
        if (right == "0") return false;
        return lowerAscii(left) < lowerAscii(right);
    });
    return result;
}

EntityProperties Document::resolveEffectiveProperties(std::size_t index) const {
    const WireframeModel& model = models_[index];
    EntityProperties result = model.properties();
    const auto found = layers_.find(result.layer);
    if (found == layers_.end()) return result;
    const EntityProperties& layer = found->second;
    result.visible = result.visible && layer.visible && !layer.frozen;
    result.frozen = layer.frozen;
    result.locked = layer.locked;
    result.plottable = layer.plottable;
    result.description = layer.description;
    if (!result.trueColor && (result.colorIndex <= 0 || result.colorIndex >= 256))
        result.effectiveColor = layer.effectiveColor;
    if (result.lineType.empty() || result.lineType == "BYLAYER" || result.lineType == "BYBLOCK")
        result.effectiveLineType = layer.effectiveLineType;
    if (result.lineWeight < 0) result.effectiveLineWeight = layer.effectiveLineWeight;
    if (result.transparency == 0) result.transparency = layer.transparency;
    return result;
}

EntityProperties Document::effectiveProperties(const WireframeModel& model) const {
    const std::size_t index = static_cast<std::size_t>(&model - models_.data());
    return effectiveProperties(index);
}

const EntityProperties& Document::effectiveProperties(std::size_t index) const {
    ensureEffectiveCache();
    return effectiveCache_[index];
}

void Document::ensureEffectiveCache() const {
    if (!effectiveCacheDirty_) return;
    effectiveCache_.resize(models_.size());
    for (std::size_t i = 0; i < models_.size(); ++i)
        effectiveCache_[i] = resolveEffectiveProperties(i);
    effectiveCacheDirty_ = false;
}

bool Document::modelIsEditable(std::size_t index) const {
    if (index >= models_.size()) return false;
    const auto& properties = effectiveProperties(index);
    return properties.visible && !properties.locked;
}

void Document::invalidateDerivedState() noexcept {
    spatialIndexDirty_ = true;
    effectiveCacheDirty_ = true;
}

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
    output << "MMW2\n" << models_.size() << '\n';
    for (const auto& model : models_) {
        output << model.vertices().size() << ' ' << model.edges().size() << ' '
               << model.faces().size() << ' ' << model.isPointEntity() << ' '
               << model.isFace3D() << '\n';
        for (const auto& vertex : model.vertices()) output << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
        for (const auto& edge : model.edges()) output << edge.from << ' ' << edge.to << '\n';
        for (const auto& face : model.faces()) {
            output << face.size();
            for (const auto index : face) output << ' ' << index;
            output << '\n';
        }
    }
    if (!output) throw std::runtime_error("Could not write document");
}

void Document::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Could not open file for reading");

    std::string signature;
    std::size_t modelCount{};
    if (!(input >> signature >> modelCount) ||
        (signature != "MMW1" && signature != "MMW2") || modelCount > 100000) {
        throw std::runtime_error("Invalid Model Maker file");
    }
    const bool version2 = signature == "MMW2";

    std::vector<WireframeModel> loaded;
    loaded.reserve(modelCount);
    for (std::size_t i = 0; i < modelCount; ++i) {
        std::size_t vertexCount{}, edgeCount{}, faceCount{};
        bool pointEntity{}, face3D{};
        if (!(input >> vertexCount >> edgeCount) ||
            (version2 && !(input >> faceCount >> pointEntity >> face3D)) ||
            vertexCount > 1000000 || edgeCount > 2000000 || faceCount > 1000000) {
            throw std::runtime_error("Invalid model data");
        }
        std::vector<Vec3> vertices(vertexCount);
        std::vector<Edge> edges(edgeCount);
        std::vector<Face> faces;
        faces.reserve(faceCount);
        for (auto& vertex : vertices) {
            if (!(input >> vertex.x >> vertex.y >> vertex.z)) throw std::runtime_error("Invalid vertex data");
        }
        for (auto& edge : edges) {
            if (!(input >> edge.from >> edge.to)) throw std::runtime_error("Invalid edge data");
        }
        for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
            std::size_t cornerCount{};
            if (!(input >> cornerCount) || cornerCount < 3 || cornerCount > vertexCount)
                throw std::runtime_error("Invalid face data");
            Face face(cornerCount);
            for (auto& index : face)
                if (!(input >> index) || index >= vertexCount) throw std::runtime_error("Invalid face data");
            faces.push_back(std::move(face));
        }
        if (pointEntity && vertices.size() == 1) {
            loaded.push_back(WireframeModel::point(vertices.front()));
        } else if (face3D && vertices.size() == 4) {
            loaded.push_back(WireframeModel::face3D(
                {vertices[0], vertices[1], vertices[2], vertices[3]}));
        } else {
            loaded.emplace_back(std::move(vertices), std::move(edges), std::move(faces));
        }
    }
    models_ = std::move(loaded);
    invalidateDerivedState();
}

} // namespace mm
