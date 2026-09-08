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

Bounds3 computeModelBounds(const WireframeModel& model) {
    const auto& vertices = model.vertices();
    if (vertices.empty()) return Bounds3{};
    Bounds3 bounds{vertices.front(), vertices.front()};
    for (std::size_t i = 1; i < vertices.size(); ++i) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertices[i].x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertices[i].y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertices[i].z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertices[i].x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertices[i].y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertices[i].z);
    }
    return bounds;
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

bool contains2D(const Bounds3& outer, const Bounds3& inner) noexcept {
    return outer.minimum.x <= inner.minimum.x && outer.minimum.y <= inner.minimum.y &&
           outer.maximum.x >= inner.maximum.x && outer.maximum.y >= inner.maximum.y;
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
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::Add;
    op.index = models_.size();
    op.afterModels.push_back(model);
    recordUndoOp(std::move(op));
    models_.push_back(std::move(model));
    const std::size_t index = models_.size() - 1;
    if (modelBounds_.size() == index) {
        modelBounds_.push_back(computeModelBounds(models_.back()));
        if (!effectiveCacheDirty_ && effectiveCache_.size() == index)
            effectiveCache_.push_back(resolveEffectiveProperties(index));
        else if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    } else {
        modelBounds_.resize(models_.size());
        effectiveCacheDirty_ = true;
    }
    markPending(index);
    spatialIndexDirty_ = true;
    documentBounds_.reset();
}
void Document::addLine(const Vec3& from, const Vec3& to) { addModel(WireframeModel::line(from, to)); }
void Document::reserveModels(std::size_t count) { models_.reserve(count); }

void Document::moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::Move;
    op.indices = indices;
    op.displacement = displacement;
    recordUndoOp(std::move(op));
    for (const auto index : indices) {
        if (index >= models_.size()) continue;
        models_[index].translate(displacement);
        // Profilli katinin kendi eksen koordinatlari da tasinir — yoksa
        // rotasyon/degistirme islemleri katiyi ESKI konumda yeniden uretir.
        {
            auto props = models_[index].properties();
            if (!props.profileName.empty()) {
                props.axisFromX += displacement.x;
                props.axisFromY += displacement.y;
                props.axisFromZ += displacement.z;
                props.axisToX += displacement.x;
                props.axisToY += displacement.y;
                props.axisToZ += displacement.z;
                models_[index].setProperties(std::move(props));
            }
        }
        if (modelBounds_.size() == models_.size())
            modelBounds_[index] = computeModelBounds(models_[index]);
        markPending(index);
    }
    spatialIndexDirty_ = true;
    documentBounds_.reset();
}

void Document::copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    ++revision_;
    std::vector<WireframeModel> copies;
    copies.reserve(indices.size());
    for (const auto index : indices) {
        if (index < models_.size()) {
            copies.push_back(models_[index]);
            copies.back().translate(displacement);
            {
                auto props = copies.back().properties();
                if (!props.profileName.empty()) {
                    props.axisFromX += displacement.x;
                    props.axisFromY += displacement.y;
                    props.axisFromZ += displacement.z;
                    props.axisToX += displacement.x;
                    props.axisToY += displacement.y;
                    props.axisToZ += displacement.z;
                    copies.back().setProperties(std::move(props));
                }
            }
            UndoOp op;
            op.kind = UndoOp::Kind::Add;
            op.index = models_.size() + copies.size() - 1;
            op.afterModels.push_back(copies.back());
            recordUndoOp(std::move(op));
        }
    }
    const std::size_t firstCopy = models_.size();
    models_.insert(models_.end(), copies.begin(), copies.end());
    if (modelBounds_.size() == firstCopy && effectiveCache_.size() == firstCopy &&
        !effectiveCacheDirty_) {
        for (std::size_t k = 0; k < copies.size(); ++k) {
            modelBounds_.push_back(computeModelBounds(models_[firstCopy + k]));
            effectiveCache_.push_back(resolveEffectiveProperties(firstCopy + k));
            markPending(firstCopy + k);
        }
    } else {
        modelBounds_.resize(models_.size());
        effectiveCacheDirty_ = true;
        for (std::size_t k = 0; k < copies.size(); ++k)
            markPending(firstCopy + k);
    }
    spatialIndexDirty_ = true;
    documentBounds_.reset();
}

std::size_t Document::setModelLayer(const std::vector<std::size_t>& indices, const std::string& layer) {
    ++revision_;
    if (!layers_.contains(layer)) return 0;
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.layer = layer;
        models_[index].setProperties(std::move(properties));
        ++changed;
    }
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    else {
        for (const auto index : uniqueIndices(indices)) {
            if (index < models_.size()) effectiveCache_[index] = resolveEffectiveProperties(index);
        }
    }
    return changed;
}

std::size_t Document::setModelColor(const std::vector<std::size_t>& indices,
                                    std::optional<std::uint32_t> color) {
    ++revision_;
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
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    else {
        for (const auto index : uniqueIndices(indices)) {
            if (index < models_.size()) effectiveCache_[index] = resolveEffectiveProperties(index);
        }
    }
    return changed;
}

std::size_t Document::setModelProfile(const std::vector<std::size_t>& indices,
                                      const std::string& profileName) {
    ++revision_;
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.profileName = profileName;
        models_[index].setProperties(properties);
        ++changed;
    }
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    else {
        for (const auto index : uniqueIndices(indices)) {
            if (index < models_.size()) effectiveCache_[index] = resolveEffectiveProperties(index);
        }
    }
    return changed;
}

std::size_t Document::setModelMaterial(const std::vector<std::size_t>& indices,
                                       const std::string& material) {
    ++revision_;
    std::size_t changed{};
    for (const auto index : uniqueIndices(indices)) {
        if (!modelIsEditable(index)) continue;
        auto properties = models_[index].properties();
        properties.material = material;
        models_[index].setProperties(std::move(properties));
        ++changed;
    }
    return changed;
}

std::size_t Document::setModelLineType(const std::vector<std::size_t>& indices,
                                       const std::string& lineType) {
    ++revision_;
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
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    else {
        for (const auto index : uniqueIndices(indices)) {
            if (index < models_.size()) effectiveCache_[index] = resolveEffectiveProperties(index);
        }
    }
    return changed;
}

void Document::deleteModels(const std::vector<std::size_t>& indices) {
    ++revision_;
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
    for (const auto index : valid) {
        models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
        if (modelBounds_.size() == models_.size() + 1)
            modelBounds_.erase(modelBounds_.begin() + static_cast<std::ptrdiff_t>(index));
        if (effectiveCache_.size() == models_.size() + 1 && !effectiveCacheDirty_)
            effectiveCache_.erase(effectiveCache_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    if (modelBounds_.size() != models_.size()) modelBounds_.resize(models_.size());
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    // Ortadan silme sonraki tüm ağaç girdilerini kaydırır — delta güvensiz,
    // yeniden kuruluma zorla (nadir işlem).
    spatialOrder_.clear();
    spatialNodes_.clear();
    pendingIndexRebuild_.clear();
    pendingMask_.clear();
    spatialIndexDirty_ = true;
    documentBounds_.reset();
    if (!valid.empty()) invalidateDerivedState();
}

void Document::replaceModel(std::size_t index, std::vector<WireframeModel> replacements) {
    ++revision_;
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
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(replacements.size()) - 1;
    if (modelBounds_.size() == models_.size() - delta) {
        modelBounds_.erase(modelBounds_.begin() + static_cast<std::ptrdiff_t>(index));
        for (std::size_t k = 0; k < replacements.size(); ++k)
            modelBounds_.insert(modelBounds_.begin() + static_cast<std::ptrdiff_t>(index + k),
                                computeModelBounds(models_[index + k]));
    } else {
        modelBounds_.resize(models_.size());
    }
    if (!effectiveCacheDirty_ && effectiveCache_.size() == models_.size() - delta) {
        effectiveCache_.erase(effectiveCache_.begin() + static_cast<std::ptrdiff_t>(index));
        for (std::size_t k = 0; k < replacements.size(); ++k)
            effectiveCache_.insert(effectiveCache_.begin() + static_cast<std::ptrdiff_t>(index + k),
                                   resolveEffectiveProperties(index + k));
    } else {
        effectiveCacheDirty_ = true;
    }
    if (delta != 0) {
        // Boyut değişimi sonraki ağaç girdilerini kaydırır — yeniden kur (nadir).
        spatialOrder_.clear();
        spatialNodes_.clear();
        pendingIndexRebuild_.clear();
    pendingMask_.clear();
    } else {
        markPending(index);
    }
    spatialIndexDirty_ = true;
    documentBounds_.reset();
}

void Document::clear() noexcept {
    ++revision_;
    models_.clear();
    layers_.clear();
    undoStack_.clear();
    redoStack_.clear();
    modelBounds_.clear();
    effectiveCache_.clear();
    pendingIndexRebuild_.clear();
    pendingMask_.clear();
    spatialOrder_.clear();
    spatialNodes_.clear();
    documentBounds_.reset();
    spatialIndexDirty_ = true;
    effectiveCacheDirty_ = false;
    nodeConstraints_.clear();
    grids_.clear();
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
    undoStack_.back().ops.push_back(std::move(op));
}

void Document::recordLayerState() {
    if (undoStack_.empty()) return;
    undoStack_.back().layersAfter = layers_;
}

void Document::pushSnapshot() {
    // Yeni bir geri-alma kaydı açar; sonraki mutasyonlar bu kayda delta olarak eklenir.
    UndoRecord record;
    record.layersBefore = layers_;
    record.layersAfter = layers_;
    undoStack_.push_back(std::move(record));
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
    case UndoOp::Kind::Replace: {
        if (forward) {
            if (op.index < models_.size())
                models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index));
            for (std::size_t k = 0; k < op.afterModels.size(); ++k)
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index + k),
                               op.afterModels[k]);
        } else {
            const std::size_t available = op.index < models_.size()
                ? models_.size() - op.index : 0;
            const std::size_t count = std::min(op.afterModels.size(), available);
            for (std::size_t k = count; k-- > 0;)
                models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index + k));
            if (!op.beforeModels.empty())
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index),
                               op.beforeModels.front());
        }
        break;
    }
    case UndoOp::Kind::Move:
        for (const auto index : op.indices)
            if (index < models_.size()) {
                const Vec3 d = forward ? op.displacement
                                       : Vec3{-op.displacement.x, -op.displacement.y,
                                              -op.displacement.z};
                models_[index].translate(d);
                auto props = models_[index].properties();
                if (!props.profileName.empty()) {
                    props.axisFromX += d.x;
                    props.axisFromY += d.y;
                    props.axisFromZ += d.z;
                    props.axisToX += d.x;
                    props.axisToY += d.y;
                    props.axisToZ += d.z;
                    models_[index].setProperties(std::move(props));
                }
            }
        break;
    }
}

bool Document::undo() {
    ++revision_;
    while (!undoStack_.empty() && undoStack_.back().ops.empty() &&
           undoStack_.back().layersBefore == undoStack_.back().layersAfter)
        undoStack_.pop_back();
    if (undoStack_.empty()) return false;
    UndoRecord record = std::move(undoStack_.back());
    undoStack_.pop_back();
    for (auto it = record.ops.rbegin(); it != record.ops.rend(); ++it)
        applyUndoOp(*it, false);
    layers_ = record.layersBefore;
    redoStack_.push_back(std::move(record));
    rebuildDerivedState();
    return true;
}

bool Document::redo() {
    ++revision_;
    if (redoStack_.empty()) return false;
    UndoRecord record = std::move(redoStack_.back());
    redoStack_.pop_back();
    for (const auto& op : record.ops)
        applyUndoOp(op, true);
    layers_ = record.layersAfter;
    undoStack_.push_back(std::move(record));
    if (undoStack_.size() > kMaxUndoEntries)
        undoStack_.pop_front();
    rebuildDerivedState();
    return true;
}

bool Document::canUndo() const noexcept {
    for (auto it = undoStack_.rbegin(); it != undoStack_.rend(); ++it)
        if (!it->ops.empty() || it->layersBefore != it->layersAfter) return true;
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
    recordLayerState();
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
    recordLayerState();
    effectiveCacheDirty_ = true;
    return true;
}

bool Document::deleteLayer(const std::string& name) {
    if (name.empty() || name == "0" || !layers_.contains(name)) return false;
    if (std::any_of(models_.begin(), models_.end(), [&](const WireframeModel& model) {
            return model.properties().layer == name;
        })) return false;
    const bool erased = layers_.erase(name) == 1;
    if (erased) recordLayerState();
    effectiveCacheDirty_ = true;
    return erased;
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
    recordLayerState();
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

void Document::rebuildDerivedState() {
    modelBounds_.resize(models_.size());
    for (std::size_t i = 0; i < models_.size(); ++i)
        modelBounds_[i] = computeModelBounds(models_[i]);
    pendingIndexRebuild_.clear();
    pendingMask_.clear();
    spatialOrder_.clear();
    spatialNodes_.clear();
    documentBounds_.reset();
    effectiveCacheDirty_ = true;
    spatialIndexDirty_ = true;
}

void Document::markPending(std::size_t index) {
    // Pending uyelerini agac emisyonundan dislamak icin O(1) maske.
    // Benzersiz uyelik: ayni indeks pending'de yalnizca BIR kez bulunur.
    // move/replace agacta zaten var olan indeksi de isaretler; agac gezintisi
    // onu atlar, pending taramasi taze sinirlarla TEK kopya uretir.
    if (index >= pendingMask_.size()) pendingMask_.resize(models_.size(), 0);
    if (pendingMask_[index]) return;
    pendingMask_[index] = 1;
    pendingIndexRebuild_.push_back(index);
}

void Document::invalidateDerivedState() noexcept {
    spatialIndexDirty_ = true;
    effectiveCacheDirty_ = true;
}

void Document::ensureSpatialIndex() const {
    if (!spatialIndexDirty_) return;
    // Amortize: küçük delta setleri için bayat ağaç + taze delta sorguları
    // yeterli; ağaç yalnızca delta büyüyünce / yapı bozulunca kurulur.
    const bool pendingLarge = pendingIndexRebuild_.size() > kMaxPendingIndexEntries;
    const bool shrunkALot = spatialOrder_.size() > models_.size() + models_.size() / 4 + 4'096;
    const bool boundsStale = modelBounds_.size() != models_.size();
    const bool treeMissing = spatialNodes_.empty() && !models_.empty();
    if (!pendingLarge && !shrunkALot && !boundsStale && !treeMissing) {
        spatialIndexDirty_ = false;
        return;
    }
    if (boundsStale) {
        modelBounds_.resize(models_.size());
        for (std::size_t i = 0; i < models_.size(); ++i)
            modelBounds_[i] = computeModelBounds(models_[i]);
    }
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
    pendingIndexRebuild_.clear();
    pendingMask_.clear();
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
        // Yaprak tamamen sorgu alanı içindeyse tüm modelleri kapsar — model
        // başına sınır testi gerekmez; tek seferde reserve + aralık kopyası.
        if (contains2D(area, node.bounds)) {
            for (std::size_t i = node.begin; i < node.end; ++i) {
                const auto modelIndex = spatialOrder_[i];
                if (modelIndex >= models_.size()) continue; // silinmis bayat girdi
                if (modelIndex < pendingMask_.size() && pendingMask_[modelIndex]) continue; // pending uyesi
                result.push_back(modelIndex);
            }
            return;
        }
        for (std::size_t i = node.begin; i < node.end; ++i) {
            const auto modelIndex = spatialOrder_[i];
            if (modelIndex >= models_.size()) continue; // silinmiş bayat girdi
            if (modelIndex < pendingMask_.size() && pendingMask_[modelIndex]) continue; // pending uyesi
            if (intersects2D(modelBounds_[modelIndex], area)) result.push_back(modelIndex);
        }
        return;
    }
    querySpatialNode(node.left, area, result);
    querySpatialNode(node.right, area, result);
}

std::optional<Bounds3> Document::bounds() const {
    ensureSpatialIndex();
    if (!documentBounds_) {
        for (std::size_t i = 0; i < models_.size(); ++i) {
            if (models_[i].vertices().empty()) continue;
            documentBounds_ = documentBounds_ ? mergeBounds(*documentBounds_, modelBounds_[i])
                                              : modelBounds_[i];
        }
        // Yapi gridleri de bounds'a girer — bos belgede zoom extents gridi
        // ekrana siginir (eskiden grid disarida kaliyordu).
        for (const auto& grid : grids_) {
            if (!grid.visible) continue;
            for (const auto& axis : grid.axes) {
                for (const Vec3& p : {axis.from, axis.to}) {
                    const Bounds3 one{p, p};
                    documentBounds_ = documentBounds_ ? mergeBounds(*documentBounds_, one)
                                                      : one;
                }
            }
        }
    }
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
                if (modelIndex >= models_.size()) continue; // silinmiş bayat girdi
                if (intersects(modelBounds_[modelIndex])) result.push_back(modelIndex);
            }
        } else {
            pending.push_back(node.right);
            pending.push_back(node.left);
        }
    }
    for (const auto index : pendingIndexRebuild_) {
        if (index >= models_.size()) continue;
        if (intersects(modelBounds_[index])) result.push_back(index);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::size_t> Document::query2D(Vec3 minimum, Vec3 maximum) const {
    ensureSpatialIndex();
    Bounds3 area{{std::min(minimum.x, maximum.x), std::min(minimum.y, maximum.y),
                  std::min(minimum.z, maximum.z)},
                 {std::max(minimum.x, maximum.x), std::max(minimum.y, maximum.y),
                  std::max(minimum.z, maximum.z)}};
    std::vector<std::size_t> result;
    // Ağaç yaprakları her modeli tam olarak bir kez üretir; pending listesi
    // ağaçta olmayan modelleri kapsar — kümeler ayrık. sort+unique gereksizdi
    // ve büyük pencerelerde sorgunun maliyetinin büyük kısmıydı.
    if (!spatialNodes_.empty()) querySpatialNode(0, area, result);
    if (!pendingIndexRebuild_.empty())
        result.reserve(result.size() + pendingIndexRebuild_.size());
    for (const auto index : pendingIndexRebuild_) {
        if (index >= models_.size()) continue;
        if (intersects2D(modelBounds_[index], area)) result.push_back(index);
    }
    return result;
}

void Document::save(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Could not open file for writing");

    output.precision(17);
    // MMW3: geometri + TUM varlik ozellikleri (katman/profil/rotasyon/
    // eksen/renk/cizgi tipi/malzeme...) — eskiden yalniz geometri
    // yaziliyordu; kayit sonrasi katilar stilini kaybediyordu.
    // MMW5: geometri + props + katman + GRID. (MMW4 = katman; MMW3 = props;
    // MMW1/2 = yalniz geometri.) Yeni dosyalar daima MMW5 yazar.
    output << "MMW5\n" << models_.size() << '\n';
    // Katman tanimlari: isim + (trueColor veya ACI) + linetype + visible.
    // Model yalniz katman ADI tasir; katman rengi/sifati bu haritada —
    // kaydedilmezse dosya acilinca BYLAYER nesneler rengini kaybeder.
    output << 'L' << ' ' << layers_.size() << '\n';
    for (const auto& [name, layer] : layers_) {
        const auto putL = [&output](const std::string& text) {
            output << text.size() << ':';
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
        };
        putL(name); output << ' ';
        output << layer.colorIndex << ' ';
        if (layer.trueColor) output << "1 " << *layer.trueColor << ' ';
        else output << "0 0 ";
        output << layer.visible << ' ' << layer.frozen << ' ' << layer.locked << '\n';
    }

    // Uzunluk-sonralikli string yazici (P ve G bloklari ortak kullanir).
    const auto putS = [&output](const std::string& text) {
        output << text.size() << ':';
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
    };
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
        // Ozellikler: uzunluk-sonralikli string'ler (bosluk/bosa sorun yok).
        const auto& props = model.properties();
        output << "P ";
        putS(props.layer); output << ' ';
        putS(props.profileName); output << ' ';
        output << props.profileRotation << ' '
               << props.profileSourceLine << ' '
               << props.axisFromX << ' ' << props.axisFromY << ' ' << props.axisFromZ << ' '
               << props.axisToX << ' ' << props.axisToY << ' ' << props.axisToZ << ' ';
        putS(props.lineType); output << ' ';
        putS(props.material); output << ' ';
        output << props.colorIndex << ' ';
        if (props.trueColor) output << "1 " << *props.trueColor << ' ';
        else output << "0 0 ";
        output << props.lineWeight << ' ' << props.thickness << ' '
               << props.lineTypeScale << ' ' << props.transparency << ' '
               << props.visible << ' ' << props.frozen << ' ' << props.locked << ' '
               << props.plottable << ' ';
        putS(props.description);
        output << '\n';
    }
    // Yapi gridleri: 'G' <count> sonra her girdi icin props
    output << 'G' << ' ' << grids_.size() << '\n';
    for (const auto& grid : grids_) {
        putS(grid.name); output << ' ' << grid.visible << ' ' << grid.axes.size() << '\n';
        for (const auto& axis : grid.axes) {
            output << axis.from.x << ' ' << axis.from.y << ' ' << axis.from.z << ' '
                   << axis.to.x << ' ' << axis.to.y << ' ' << axis.to.z << ' '
                   << axis.horizontal << ' ';
            putS(axis.label); output << '\n';
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
        (signature != "MMW1" && signature != "MMW2" && signature != "MMW3" &&
         signature != "MMW4" && signature != "MMW5") ||
        modelCount > 100000) {
        throw std::runtime_error("Invalid Model Maker file");
    }
    const bool version2 = signature == "MMW2" || signature == "MMW3" || signature == "MMW4" || signature == "MMW5";
    const bool version3 = signature == "MMW3" || signature == "MMW4" || signature == "MMW5";
    const bool version4 = signature == "MMW4" || signature == "MMW5";
    const bool version5 = signature == "MMW5";
    // uzunluk-sonralikli string okuyucu (katman blogundan once tanimli)
    const auto readS = [&input]() {
        std::size_t length{};
        char colon{};
        if (!(input >> length >> colon) || colon != ':') throw std::runtime_error("Invalid property string");
        if (length > 100000) throw std::runtime_error("Invalid property string length");
        std::string text(length, '\0');
        if (length) input.read(text.data(), static_cast<std::streamsize>(length));
        return text;
    };
    // Katman tablosu MMW4'te zorunlu; MMW3'te OPSIYONEL — ara build'ler
    // (938af2e) MMW3'e blok yazdi, oncekiler yazmadi. 'L' ile basliyorsa
    // oku (blok yoksa model verisi sayilarla baslar, peek bunu ayirir).
    if (version3 || version4) {
        input >> std::ws;
        if (input.peek() == 'L') {
            std::string layerTag;
            std::size_t layerCount{};
            if (!(input >> layerTag >> layerCount) || layerTag != "L" || layerCount > 1000)
                throw std::runtime_error("Invalid layer block");
            for (std::size_t li = 0; li < layerCount; ++li) {
                std::string layerName = readS();
                EntityProperties layer;
                int hasLayerColor{};
                std::uint32_t layerColorValue{};
                if (!(input >> layer.colorIndex >> hasLayerColor >> layerColorValue
                           >> layer.visible >> layer.frozen >> layer.locked))
                    throw std::runtime_error("Invalid layer data");
                layer.trueColor = hasLayerColor ? std::optional<std::uint32_t>(layerColorValue)
                                                : std::nullopt;
                if (layer.trueColor) layer.effectiveColor = *layer.trueColor;
                else layer.effectiveColor = layer.colorIndex; // ACI 0-255 tek kanal fallback
                layers_[layerName] = std::move(layer);
            }
        }
    }


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
        WireframeModel built = [&]() {
            if (pointEntity && vertices.size() == 1)
                return WireframeModel::point(vertices.front());
            if (face3D && vertices.size() == 4)
                return WireframeModel::face3D(
                    {vertices[0], vertices[1], vertices[2], vertices[3]});
            return WireframeModel(std::move(vertices), std::move(edges), std::move(faces));
        }();
        if (version3) {
            // Ozellikler (kayit tarafindaki sirayla):
            // P layer profil rot src fx fy fz tx ty tz linetyp material
            //   color [trueColor] lw thick lts trans vis frozen lock plot desc
            std::string tag;
            if (!(input >> tag) || tag != "P") throw std::runtime_error("Invalid property block");
            EntityProperties props = built.properties();
            props.layer = readS();
            props.profileName = readS();
            if (!(input >> props.profileRotation >> props.profileSourceLine
                       >> props.axisFromX >> props.axisFromY >> props.axisFromZ
                       >> props.axisToX >> props.axisToY >> props.axisToZ))
                throw std::runtime_error("Invalid property data");
            props.lineType = readS();
            props.material = readS();
            int hasTrueColor{};
            std::uint32_t trueColorValue{};
            if (!(input >> props.colorIndex >> hasTrueColor >> trueColorValue
                       >> props.lineWeight >> props.thickness >> props.lineTypeScale
                       >> props.transparency >> props.visible >> props.frozen
                       >> props.locked >> props.plottable))
                throw std::runtime_error("Invalid property data");
            props.trueColor = hasTrueColor ? std::optional<std::uint32_t>(trueColorValue)
                                           : std::nullopt;
            // effectiveColor turet: render onu kullanir; yukleme onu
            // yeniden hesaplamazsa varsayilan mavi kalir ("renkler
            // alinmamis"). trueColor varsa ondan; yoksa katman (BYLAYER)
            // veya ACI paletinden resolve'e birakilir.
            if (props.trueColor) props.effectiveColor = *props.trueColor;
            props.description = readS();
            built.setProperties(std::move(props));
        }
        loaded.push_back(std::move(built));
    }
    models_ = std::move(loaded);
    // Yapi gridleri (MMW5): modellerden sonra 'G' blogu.
    grids_.clear();
    if (version5) {
        std::string gridTag;
        std::size_t gridCount{};
        if (!(input >> gridTag >> gridCount) || gridTag != "G" || gridCount > 1000)
            throw std::runtime_error("Invalid grid block");
        for (std::size_t gi = 0; gi < gridCount; ++gi) {
            GridDefinition grid;
            grid.name = readS();
            std::size_t axisCount{};
            if (!(input >> grid.visible >> axisCount) || axisCount > 10000)
                throw std::runtime_error("Invalid grid data");
            grid.axes.reserve(axisCount);
            for (std::size_t ai = 0; ai < axisCount; ++ai) {
                GridAxisLine axis;
                int horizontal{};
                if (!(input >> axis.from.x >> axis.from.y >> axis.from.z
                           >> axis.to.x >> axis.to.y >> axis.to.z
                           >> horizontal))
                    throw std::runtime_error("Invalid grid axis");
                axis.horizontal = horizontal != 0;
                axis.label = readS();
                grid.axes.push_back(std::move(axis));
            }
            grids_.push_back(std::move(grid));
        }
    }
    invalidateDerivedState();
}

// ── Yapi gridleri ────────────────────────────────────────────────
std::size_t Document::addGrid(GridDefinition grid) {
    grids_.push_back(std::move(grid));
    ++revision_;
    return grids_.size() - 1;
}
void Document::clearGrids() noexcept {
    grids_.clear();
    ++revision_;
}
GridDefinition* Document::mutableGrid(std::size_t index) noexcept {
    if (index >= grids_.size()) return nullptr;
    return &grids_[index];
}
bool Document::setGridVisible(std::size_t index, bool visible) {
    if (index >= grids_.size()) return false;
    grids_[index].visible = visible;
    ++revision_;
    return true;
}
void Document::removeGrid(std::size_t index) {
    if (index >= grids_.size()) return;
    grids_.erase(grids_.begin() + static_cast<std::ptrdiff_t>(index));
    ++revision_;
}

} // namespace mm
