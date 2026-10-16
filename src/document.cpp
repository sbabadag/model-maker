#include "model_maker/document.hpp"

#include "model_maker/occ_store.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cctype>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // windows.h min/max makrolarinin std::min/std::max'i bozmamasi icin
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // ReplaceFileW / MoveFileExW / GetLastError (atomik kayit)
#endif

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

Document::Document() : occStore_(makeNullOccStore()) {
    auto layer = defaultLayerProperties();
    layers_.emplace(layer.layer, std::move(layer));
}

// PIMPL: yikici ve tasima islemleri OUT-OF-LINE. std::unique_ptr<OccStore>
// yok edilirken TAM TIP gerekir; tam tip bu TU'da (occ_store.hpp) mevcuttur.
// Boylece document.hpp OCCT'siz kalir ve Document baska TU'larda da kullanilir.
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

void Document::setOccStore(std::unique_ptr<OccStore> store) noexcept {
    // OCC-acik Application gercek magazayi enjekte eder. nullptr verilirse
    // no-op magazaya dusulur (guvenli varsayilan).
    occStore_ = store ? std::move(store) : makeNullOccStore();
}

void Document::addModel(WireframeModel model) {
    ++revision_;
    opaqueBrepsStale_ = true;  // geometri degisti: opak B-Rep (OCC-kapali) bayat
    // Tükenme durumunda firlatir (KONTROLLU basarisizlik) — kayit/ekleme
    // ONCESI, yani kismi model veya bozuk undo kaydi BIRAKILMAZ.
    const EntityId id = allocateEntityIdRequired();
    UndoOp op;
    op.kind = UndoOp::Kind::Add;
    op.index = models_.size();
    op.afterModels.push_back(model);
    op.afterIds.push_back(id);
    recordUndoOp(std::move(op));
    models_.push_back(std::move(model));
    modelIds_.push_back(id);
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

std::size_t Document::addConstructionPoint(const Vec3& position) {
    const std::size_t index = models_.size();
    WireframeModel model = WireframeModel::point(position);
    model.setConstructionKind(ConstructionKind::Point);
    addModel(std::move(model));
    return index;
}

std::size_t Document::addConstructionLine(const Vec3& from, const Vec3& to) {
    const std::size_t index = models_.size();
    WireframeModel model = WireframeModel::line(from, to);
    model.setConstructionKind(ConstructionKind::Line);
    addModel(std::move(model));
    return index;
}

std::size_t Document::addConstructionPlane(const WorkPlane& plane, double size) {
    const std::size_t index = models_.size();
    const double h = std::max(1e-6, size * 0.5);
    // Dikdortgen + yuz (is duzleminde): kenarlari/koseleri snap'lenir, yuzu
    // "duzlem" olarak secilebilir. Sonsuz-duzlem semantigi ayri faz (gorsel
    // isaret sonlu dikdortgenle temsil edilir).
    WireframeModel model{
        {plane.fromPlane({-h, -h}), plane.fromPlane({h, -h}),
         plane.fromPlane({h, h}), plane.fromPlane({-h, h})},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}},
        {{0, 1, 2, 3}}};
    model.setConstructionKind(ConstructionKind::Plane);
    addModel(std::move(model));
    return index;
}

std::vector<std::size_t> Document::constructionObjects() const {
    std::vector<std::size_t> result;
    result.reserve(models_.size() / 16 + 1);
    for (std::size_t i = 0; i < models_.size(); ++i)
        if (models_[i].constructionKind() != ConstructionKind::None) result.push_back(i);
    return result;
}

bool Document::isConstruction(std::size_t index) const {
    return index < models_.size() && models_[index].constructionKind() != ConstructionKind::None;
}

bool Document::setConstructionVisible(bool visible) {
    ++revision_;
    bool changed = false;
    for (std::size_t i = 0; i < models_.size(); ++i) {
        if (models_[i].constructionKind() == ConstructionKind::None) continue;
        EntityProperties p = models_[i].properties();
        if (p.visible != visible) {
            p.visible = visible;
            models_[i].setProperties(std::move(p));
            changed = true;
        }
    }
    if (changed) invalidateDerivedState();
    return changed;
}

void Document::reserveModels(std::size_t count) { models_.reserve(count); modelIds_.reserve(count); }

void Document::moveModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    ++revision_;
    opaqueBrepsStale_ = true;
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

static Vec3 applyLinear3(const std::array<double, 9>& m, const Vec3& v) {
    return Vec3{m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
}

// DUZLEM normali donusumu: n' = normalize(transpose(M^-1) * n). Dogru kural;
// rotasyonda transpose(inv) = M ile ayni sonucu verir, non-uniform olcekte de
// (gumball yerel olcek) baslik duzleminin gercek normalini verir. Kati donusup
// referans normal sabit kalsaydi gumball cercevesi katidan kayardi.
static Vec3 transformSectionNormal3(const std::array<double, 9>& linearInverse, const Vec3& n) {
    const Vec3 r{linearInverse[0] * n.x + linearInverse[3] * n.y + linearInverse[6] * n.z,
                 linearInverse[1] * n.x + linearInverse[4] * n.y + linearInverse[7] * n.z,
                 linearInverse[2] * n.x + linearInverse[5] * n.y + linearInverse[8] * n.z};
    const double len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (!(len > 1e-12)) return Vec3{0.0, 0.0, 1.0};
    return Vec3{r.x / len, r.y / len, r.z / len};
}

void Document::transformModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                               const std::array<double, 9>& linear,
                               const std::array<double, 9>& linearInverse) {
    if (indices.empty()) return;
    ++revision_;
    opaqueBrepsStale_ = true;
    UndoOp op;
    op.kind = UndoOp::Kind::Transform;
    op.indices = indices;
    op.pivot = pivot;
    op.linear = linear;
    op.linearInverse = linearInverse;
    recordUndoOp(std::move(op));
    for (const auto index : indices) {
        if (index >= models_.size()) continue;
        models_[index].affineAbout(pivot, linear);
        auto props = models_[index].properties();
        if (!props.profileName.empty()) {
            const Vec3 from = pivot + applyLinear3(
                linear, Vec3{props.axisFromX, props.axisFromY, props.axisFromZ} - pivot);
            const Vec3 to = pivot + applyLinear3(
                linear, Vec3{props.axisToX, props.axisToY, props.axisToZ} - pivot);
            props.axisFromX = from.x; props.axisFromY = from.y; props.axisFromZ = from.z;
            props.axisToX = to.x; props.axisToY = to.y; props.axisToZ = to.z;
            const Vec3 n = transformSectionNormal3(
                linearInverse, Vec3{props.sectionNormalX, props.sectionNormalY,
                                    props.sectionNormalZ});
            props.sectionNormalX = n.x; props.sectionNormalY = n.y; props.sectionNormalZ = n.z;
            models_[index].setProperties(std::move(props));
        }
        if (modelBounds_.size() == models_.size())
            modelBounds_[index] = computeModelBounds(models_[index]);
        markPending(index);
    }
    spatialIndexDirty_ = true;
    documentBounds_.reset();
}

void Document::rotateModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                            const Vec3& axis, double radians) {
    if (indices.empty() || radians == 0.0) return;
    const double len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len < 1e-12) return;
    const double ux = axis.x / len, uy = axis.y / len, uz = axis.z / len;
    const double c = std::cos(radians), s = std::sin(radians), t = 1.0 - c;
    // Rodrigues; inverse = transpoze (mi).
    const std::array<double, 9> m{
        t * ux * ux + c,    t * ux * uy - s * uz, t * ux * uz + s * uy,
        t * ux * uy + s * uz, t * uy * uy + c,    t * uy * uz - s * ux,
        t * ux * uz - s * uy, t * uy * uz + s * ux, t * uz * uz + c};
    const std::array<double, 9> mi{
        t * ux * ux + c,    t * ux * uy + s * uz, t * ux * uz - s * uy,
        t * ux * uy - s * uz, t * uy * uy + c,    t * uy * uz + s * ux,
        t * ux * uz + s * uy, t * uy * uz - s * ux, t * uz * uz + c};
    transformModels(indices, pivot, m, mi);
}

void Document::scaleModels(const std::vector<std::size_t>& indices, const Vec3& pivot,
                           const Vec3& factors) {
    if (indices.empty()) return;
    const auto safe = [](double f) { return std::abs(f) < 1e-9 ? (f < 0.0 ? -1e-9 : 1e-9) : f; };
    const double fx = safe(factors.x), fy = safe(factors.y), fz = safe(factors.z);
    const std::array<double, 9> m{fx, 0, 0, 0, fy, 0, 0, 0, fz};
    const std::array<double, 9> mi{1.0 / fx, 0, 0, 0, 1.0 / fy, 0, 0, 0, 1.0 / fz};
    transformModels(indices, pivot, m, mi);
}

void Document::copyModels(const std::vector<std::size_t>& indices, const Vec3& displacement) {
    ++revision_;
    opaqueBrepsStale_ = true;
    std::vector<WireframeModel> copies;
    copies.reserve(indices.size());
    std::vector<EntityId> copyIds; // kopyalara atanan YENI kimlikler (models_ ile hizali)
    copyIds.reserve(indices.size());
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
            // Kopya YENI bir varliktir: YENI kimlik alir (orijinalin kimligini
            // tasimaz). Kimlik, op.index ile ayni konuma denk gelecek sirada
            // uretilir; boylece redo ayni sirada ayni kimligi geri koyar.
            const EntityId id = allocateEntityId();
            UndoOp op;
            op.kind = UndoOp::Kind::Add;
            op.index = models_.size() + copies.size() - 1;
            op.afterModels.push_back(copies.back());
            op.afterIds.push_back(id);
            recordUndoOp(std::move(op));
            copyIds.push_back(id);
        }
    }
    const std::size_t firstCopy = models_.size();
    models_.insert(models_.end(), copies.begin(), copies.end());
    modelIds_.insert(modelIds_.end(), copyIds.begin(), copyIds.end());
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

std::vector<EntityId> Document::deleteModels(const std::vector<std::size_t>& indices) {
    ++revision_;
    opaqueBrepsStale_ = true;
    std::vector<std::size_t> valid;
    valid.reserve(indices.size());
    for (const auto index : indices)
        if (index < models_.size()) valid.push_back(index);
    std::sort(valid.begin(), valid.end(), std::greater<>{});
    valid.erase(std::unique(valid.begin(), valid.end()), valid.end());
    // Silinen kimlikler (modeller ERASE edilmeden ONCE toplanmali).
    std::unordered_set<EntityId> deletedIds;
    deletedIds.reserve(valid.size() * 2);
    for (const auto index : valid) deletedIds.insert(modelIds_[index]);
    UndoOp op;
    op.kind = UndoOp::Kind::Delete;
    op.indices = valid;
    for (const auto index : valid) {
        op.beforeModels.push_back(models_[index]);
        op.beforeIds.push_back(modelIds_[index]); // silinen indekslerin KIMLIKLERI (ayni azalan sirada)
    }
    recordUndoOp(std::move(op));
    // KESME KASKADI (P6): silinen varligi kaynak (source), hedef (target) veya
    // arac (BooleanCut tool) olarak kullanan modifier'lar da kaldirilir. En
    // yuksek indeksten asagi silinir (kayma guvenli). Kalan (silinmemis)
    // hedefler donulur — cagiran bunlari yeniden hesaplar.
    std::vector<EntityId> affectedTargets;
    for (std::size_t ci = cutModifiers_.size(); ci-- > 0;) {
        const auto& mod = cutModifiers_[ci];
        const bool touches = deletedIds.count(mod.source) || deletedIds.count(mod.target) ||
                             (mod.kind == CutKind::BooleanCut && deletedIds.count(mod.tool));
        if (!touches) continue;
        if (mod.target != kInvalidEntityId && !deletedIds.count(mod.target))
            affectedTargets.push_back(mod.target);
        removeCutModifier(ci);
    }
    for (const auto index : valid) {
        models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
        modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(index));
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
    // ALIN LEVHASI KASKADI (Phase C1): kullanici parcalarindan (main/secondary)
    // HERHANGI biri silinen baglanti kaldirilir; alt nesnelerinden (levha veya
    // bulon montaji) biri silinen baglanti de kaldirilir. removeEndPlateConnection
    // levhayi + bulon grubunu da sildigi icin bu kaskad BULON KASKADINDAN ONCE
    // calisir (cift silmeyi onler). Yuksekten dusuge.
    for (std::size_t ei = endPlateConnections_.size(); ei-- > 0;) {
        const auto& conn = endPlateConnections_[ei];
        bool touches = false;
        if (deletedIds.count(conn.mainPart) || deletedIds.count(conn.secondaryPart)) touches = true;
        if (!touches && deletedIds.count(conn.plateEntityId)) touches = true;
        if (!touches && deletedIds.count(conn.bolts.displayEntityId)) touches = true;
        if (!touches) continue;
        if (conn.mainPart != kInvalidEntityId) affectedTargets.push_back(conn.mainPart);
        if (conn.secondaryPart != kInvalidEntityId) affectedTargets.push_back(conn.secondaryPart);
        removeEndPlateConnection(ei);
    }
    // BULON KASKADI (Phase B1): (a) bagli parcalarindan HERHANGI biri silinen
    // bulon grubu kaldirilir; (b) DISPLAY modeli silinen bulon grubu da
    // kaldirilir (bolton silinince profil ETKILENMEZ — delikler kalkar). Her
    // iki durumda deliklerin HEDEFLERI (bagli parcalar) yeniden hesaplanmasi
    // icin affectedTargets'a eklenir. Yuksekten dusuge — removeBoltGroup kaydi
    // sildigi icin indeks kaymasi guvenli.
    for (std::size_t bi = boltGroups_.size(); bi-- > 0;) {
        bool touches = false;
        for (const EntityId part : boltGroups_[bi].parts)
            if (deletedIds.count(part)) { touches = true; break; }
        if (!touches && deletedIds.count(boltGroups_[bi].displayEntityId)) touches = true;
        if (!touches) continue;
        for (const EntityId part : boltGroups_[bi].parts) affectedTargets.push_back(part);
        removeBoltGroup(bi);
    }
    return affectedTargets;
}

void Document::replaceModel(std::size_t index, std::vector<WireframeModel> replacements) {
    ++revision_;
    opaqueBrepsStale_ = true;
    if (index >= models_.size()) return;
    const EntityId oldId = modelIds_[index];
    // 1:1 degistirme (or. kati yeniden uretim) AYNI varliktir → kimlik KORUNUR.
    // 1:N (bolme/patlatma) YENI varliklar uretir → her birine YENI kimlik.
    // Kimlikler YEREL vektorde tutulur: op kayittan SONRA tasindigi icin
    // op.afterIds'e guvenmek yanlis olur (tasima sonrasi bosalir).
    std::vector<EntityId> newIds;
    if (replacements.size() == 1) {
        newIds.push_back(oldId);
    } else {
        for (std::size_t k = 0; k < replacements.size(); ++k)
            newIds.push_back(allocateEntityIdRequired());
    }
    UndoOp op;
    op.kind = UndoOp::Kind::Replace;
    op.index = index;
    op.beforeModels.push_back(models_[index]);
    op.beforeIds.push_back(oldId);
    for (const auto& replacement : replacements) op.afterModels.push_back(replacement);
    op.afterIds = newIds;
    recordUndoOp(std::move(op));
    const auto position = models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
    models_.insert(position, std::make_move_iterator(replacements.begin()),
                   std::make_move_iterator(replacements.end()));
    modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(index));
    modelIds_.insert(modelIds_.begin() + static_cast<std::ptrdiff_t>(index),
                     newIds.begin(), newIds.end());
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
    modelIds_.clear();
    // Yeni belge: Document-SAHIPLI B-Rep magazasi da SIFIRLANIR. Boylece
    // Application'da AYRI bir temizleme cagrisi GEREKMEZ (iki yerde elle
    // esleme riski yapisal olarak ORTADAN KALKAR).
    occStore_->clear();
    // Yeni belge: kimlik sayaci da sifirlanir. undo/redo yiginlari temizlendigi
    // icin (asagida) eski kimliklerle catisma riski YOKTUR; boylece her yeni
    // belgede kimlikler deterministik olarak 1'den baslar.
    nextEntityId_ = 1;
    layers_.clear();
    // MMW8 opak B-Rep / metadata / bilinmeyen bloklar da sifirlanir.
    opaqueBreps_.clear();
    opaqueBrepsStale_ = false;
    mmw8Metadata_ = Mmw8Metadata{};
    mmw8UnknownBlocks_.clear();
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
    beamLoads_.clear(); // yeni belge: kiris yukleri de sifirlanir
    grids_.clear();
    cutModifiers_.clear();
    boltGroups_.clear();
    endPlateConnections_.clear();
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

void Document::setBeamLoad(EntityId entityId, BeamLoad load) {
    // Gecersiz kimlik (0) kaydedilmez — sentinel degere yuk baglanmaz.
    if (entityId == kInvalidEntityId) return;
    beamLoads_[entityId] = load;
}

std::optional<BeamLoad> Document::getBeamLoad(EntityId entityId) const {
    auto it = beamLoads_.find(entityId);
    if (it != beamLoads_.end()) return it->second;
    return std::nullopt;
}

const std::unordered_map<EntityId, BeamLoad>& Document::beamLoads() const noexcept {
    return beamLoads_;
}

void Document::clearBeamLoads() {
    beamLoads_.clear();
}
void Document::recordUndoOp(UndoOp op) {
    if (undoStack_.empty()) return; // kayıt açık değil (toplu yükleme vb.) — geri alınmaz
    undoStack_.back().ops.push_back(std::move(op));
}

void Document::recordGeometryDelta(EntityId entity, VersionId before, VersionId after) {
    if (entity == kInvalidEntityId) return; // geçersiz varlığa delta bağlanmaz
    // Açık kayıt YOKSA kendi kaydını aç — geometri değişikliği SESSİZCE undo
    // dışında kalmaz (recordUndoOp'un sessiz-düşme davranışından bilinçli fark).
    // DIKKAT (Phase 3.2C): burada pushSnapshot() KULLANILMAZ — pushSnapshot
    // budama tetikler ve `before` surumu (set() sonrasi artik etkin DEGIL,
    // HENUZ kaydedilmedi) budanip KAYBOLURDU. Bu yuzden kayit dogrudan acilir;
    // budama yalniz komut sinirinda (pushSnapshot) ve gecmis temizliginde
    // (clearHistory/redo) kosar — delta kaydedildikten SONRA `before` zaten
    // korunur.
    if (undoStack_.empty()) {
        UndoRecord record;
        record.layersBefore = layers_;
        record.layersAfter = layers_;
        undoStack_.push_back(std::move(record));
        redoStack_.clear(); // yeni islem: terk edilen redo gecersiz
    }
    UndoRecord& rec = undoStack_.back();
    for (std::size_t i = 0; i < rec.geometryBefore.size(); ++i) {
        if (rec.geometryBefore[i].entity == entity) {
            // AYNI varlık: İLK öncesi korunur, SON sonrası güncellenir.
            rec.geometryAfter[i].version = after;
            return;
        }
    }
    rec.geometryBefore.push_back(GeometrySlotRef{entity, before});
    rec.geometryAfter.push_back(GeometrySlotRef{entity, after});
}

// Bir undo kaydı GÖRÜNÜR bir etki taşıyor mu? (undo/redo bunu atlamaz.)
// Geometri deltası olan kayıt — op/layer değişmese bile — ETKİLİDİR.
static bool recordHasEffect(const UndoRecord& rec) {
    return !rec.ops.empty() || rec.layersBefore != rec.layersAfter ||
           !rec.geometryBefore.empty();
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
    // Onceki komutlarda terk edilen (redo temizlenen / 100 kayit siniri asan /
    // ara surum) geometriyi buda — bellek birikimi kontrol altinda.
    pruneDeadVersions();
}

void Document::applyUndoOp(const UndoOp& op, bool forward) {
    opaqueBrepsStale_ = true;  // undo/redo geometriyi degistirir
    switch (op.kind) {
    case UndoOp::Kind::Add:
        if (forward) {
            if (!op.afterModels.empty()) {
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index),
                               op.afterModels.front());
                // AyNI varlik geri konur → AYNI kimlik (afterIds[0]). afterIds
                // her zaman dolu; guvenlik icin bos ise yeni kimlik uretilir
                // (modelIds_ ile models_ hizasi her durumda korunur).
                const EntityId id = !op.afterIds.empty() ? op.afterIds.front()
                                                         : allocateEntityIdRequired();
                modelIds_.insert(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index), id);
            }
        } else if (op.index < models_.size()) {
            models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index));
            if (op.index < modelIds_.size())
                modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index));
        }
        break;
    case UndoOp::Kind::Delete:
        if (forward) {
            // indices azalan sırada saklı — bu sırayla silme güvenli
            for (const auto index : op.indices)
                if (index < models_.size()) {
                    models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
                    if (index < modelIds_.size())
                        modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(index));
                }
        } else {
            // indices azalan sırada; sondan başa (artan) geri ekle
            for (std::size_t k = op.indices.size(); k-- > 0;) {
                const std::size_t index = op.indices[k];
                if (k < op.beforeModels.size() && index <= models_.size()) {
                    models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(index),
                                   op.beforeModels[k]);
                    // Silinen varlik AYNI kimligiyle geri gelir.
                    const EntityId id = k < op.beforeIds.size() ? op.beforeIds[k]
                                                                : allocateEntityIdRequired();
                    modelIds_.insert(modelIds_.begin() + static_cast<std::ptrdiff_t>(index), id);
                }
            }
        }
        break;
    case UndoOp::Kind::Replace: {
        if (forward) {
            if (op.index < models_.size())
                models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index));
            if (op.index < modelIds_.size())
                modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index));
            for (std::size_t k = 0; k < op.afterModels.size(); ++k) {
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index + k),
                               op.afterModels[k]);
                const EntityId id = k < op.afterIds.size() ? op.afterIds[k]
                                                           : allocateEntityIdRequired();
                modelIds_.insert(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index + k), id);
            }
        } else {
            const std::size_t available = op.index < models_.size()
                ? models_.size() - op.index : 0;
            const std::size_t count = std::min(op.afterModels.size(), available);
            for (std::size_t k = count; k-- > 0;) {
                models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(op.index + k));
                if (op.index + k < modelIds_.size())
                    modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index + k));
            }
            if (!op.beforeModels.empty()) {
                models_.insert(models_.begin() + static_cast<std::ptrdiff_t>(op.index),
                               op.beforeModels.front());
                // Eski varlik AYNI kimligiyle geri gelir.
                const EntityId id = !op.beforeIds.empty() ? op.beforeIds.front()
                                                          : allocateEntityIdRequired();
                modelIds_.insert(modelIds_.begin() + static_cast<std::ptrdiff_t>(op.index), id);
            }
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
    case UndoOp::Kind::Transform: {
        const auto& m = forward ? op.linear : op.linearInverse;
        // Referans normal ters matrisle doner (forward: linearInverse, geri:
        // linear) — gumball cercevesi katidan kaymasin.
        const auto& mInverse = forward ? op.linearInverse : op.linear;
        for (const auto index : op.indices)
            if (index < models_.size()) {
                models_[index].affineAbout(op.pivot, m);
                auto props = models_[index].properties();
                if (!props.profileName.empty()) {
                    const Vec3 from = op.pivot + applyLinear3(
                        m, Vec3{props.axisFromX, props.axisFromY, props.axisFromZ} - op.pivot);
                    const Vec3 to = op.pivot + applyLinear3(
                        m, Vec3{props.axisToX, props.axisToY, props.axisToZ} - op.pivot);
                    props.axisFromX = from.x; props.axisFromY = from.y; props.axisFromZ = from.z;
                    props.axisToX = to.x; props.axisToY = to.y; props.axisToZ = to.z;
                    const Vec3 n = transformSectionNormal3(
                        mInverse, Vec3{props.sectionNormalX, props.sectionNormalY,
                                       props.sectionNormalZ});
                    props.sectionNormalX = n.x;
                    props.sectionNormalY = n.y;
                    props.sectionNormalZ = n.z;
                    models_[index].setProperties(std::move(props));
                }
            }
        break;
    }
    case UndoOp::Kind::GridAdd:
        if (forward) {
            if (op.gridIndex <= grids_.size())
                grids_.insert(grids_.begin() + static_cast<std::ptrdiff_t>(op.gridIndex),
                              op.gridAfter);
        } else if (op.gridIndex < grids_.size()) {
            grids_.erase(grids_.begin() + static_cast<std::ptrdiff_t>(op.gridIndex));
        }
        break;
    case UndoOp::Kind::GridRemove:
        if (forward) {
            if (op.gridIndex < grids_.size())
                grids_.erase(grids_.begin() + static_cast<std::ptrdiff_t>(op.gridIndex));
        } else if (op.gridIndex <= grids_.size()) {
            grids_.insert(grids_.begin() + static_cast<std::ptrdiff_t>(op.gridIndex),
                          op.gridBefore);
        }
        break;
    case UndoOp::Kind::GridReplace:
        if (op.gridIndex < grids_.size())
            grids_[op.gridIndex] = forward ? op.gridAfter : op.gridBefore;
        break;
    case UndoOp::Kind::CutAdd:
        if (forward) {
            if (op.cutIndex <= cutModifiers_.size())
                cutModifiers_.insert(cutModifiers_.begin() + static_cast<std::ptrdiff_t>(op.cutIndex),
                                     op.cutAfter);
        } else if (op.cutIndex < cutModifiers_.size()) {
            cutModifiers_.erase(cutModifiers_.begin() + static_cast<std::ptrdiff_t>(op.cutIndex));
        }
        break;
    case UndoOp::Kind::CutRemove:
        if (forward) {
            if (op.cutIndex < cutModifiers_.size())
                cutModifiers_.erase(cutModifiers_.begin() + static_cast<std::ptrdiff_t>(op.cutIndex));
        } else if (op.cutIndex <= cutModifiers_.size()) {
            cutModifiers_.insert(cutModifiers_.begin() + static_cast<std::ptrdiff_t>(op.cutIndex),
                                 op.cutBefore);
        }
        break;
    case UndoOp::Kind::CutReplace:
        if (op.cutIndex < cutModifiers_.size())
            cutModifiers_[op.cutIndex] = forward ? op.cutAfter : op.cutBefore;
        break;
    case UndoOp::Kind::BoltAdd:
        if (forward) {
            if (op.boltIndex <= boltGroups_.size())
                boltGroups_.insert(boltGroups_.begin() + static_cast<std::ptrdiff_t>(op.boltIndex),
                                   op.boltAfter);
        } else if (op.boltIndex < boltGroups_.size()) {
            boltGroups_.erase(boltGroups_.begin() + static_cast<std::ptrdiff_t>(op.boltIndex));
        }
        break;
    case UndoOp::Kind::BoltRemove:
        if (forward) {
            if (op.boltIndex < boltGroups_.size())
                boltGroups_.erase(boltGroups_.begin() + static_cast<std::ptrdiff_t>(op.boltIndex));
        } else if (op.boltIndex <= boltGroups_.size()) {
            boltGroups_.insert(boltGroups_.begin() + static_cast<std::ptrdiff_t>(op.boltIndex),
                               op.boltBefore);
        }
        break;
    case UndoOp::Kind::BoltReplace:
        if (op.boltIndex < boltGroups_.size())
            boltGroups_[op.boltIndex] = forward ? op.boltAfter : op.boltBefore;
        break;
    case UndoOp::Kind::EndPlateAdd:
        if (forward) {
            if (op.endPlateIndex <= endPlateConnections_.size())
                endPlateConnections_.insert(
                    endPlateConnections_.begin() + static_cast<std::ptrdiff_t>(op.endPlateIndex),
                    op.endPlateAfter);
        } else if (op.endPlateIndex < endPlateConnections_.size()) {
            endPlateConnections_.erase(
                endPlateConnections_.begin() + static_cast<std::ptrdiff_t>(op.endPlateIndex));
        }
        break;
    case UndoOp::Kind::EndPlateRemove:
        if (forward) {
            if (op.endPlateIndex < endPlateConnections_.size())
                endPlateConnections_.erase(
                    endPlateConnections_.begin() + static_cast<std::ptrdiff_t>(op.endPlateIndex));
        } else if (op.endPlateIndex <= endPlateConnections_.size()) {
            endPlateConnections_.insert(
                endPlateConnections_.begin() + static_cast<std::ptrdiff_t>(op.endPlateIndex),
                op.endPlateBefore);
        }
        break;
    case UndoOp::Kind::EndPlateReplace:
        if (op.endPlateIndex < endPlateConnections_.size())
            endPlateConnections_[op.endPlateIndex] = forward ? op.endPlateAfter : op.endPlateBefore;
        break;
    }
}

bool Document::undo() {
    while (!undoStack_.empty() && !recordHasEffect(undoStack_.back()))
        undoStack_.pop_back();
    if (undoStack_.empty()) return false;
    // FAZ 1 — DOGRULA (hicbir sey degistirme). Geometri surumlerinin hepsi
    // mevcut degilse islem BASLAMAZ: Document, store ve yiginlar sabit kalir.
    for (const auto& ref : undoStack_.back().geometryBefore)
        if (!occStore_->findVersionExists(ref.entity, ref.version)) return false;
    // FAZ 2 — UYGULA (dogrulama gectiginden restoreActive basarisiz olmaz).
    ++revision_;
    UndoRecord record = std::move(undoStack_.back());
    undoStack_.pop_back();
    for (auto it = record.ops.rbegin(); it != record.ops.rend(); ++it)
        applyUndoOp(*it, false);
    for (const auto& ref : record.geometryBefore)
        occStore_->restoreActive(ref.entity, ref.version);
    layers_ = record.layersBefore;
    redoStack_.push_back(std::move(record));
    rebuildDerivedState();
    return true;
}

bool Document::redo() {
    while (!redoStack_.empty() && !recordHasEffect(redoStack_.back()))
        redoStack_.pop_back();
    if (redoStack_.empty()) return false;
    // FAZ 1 — DOGRULA.
    for (const auto& ref : redoStack_.back().geometryAfter)
        if (!occStore_->findVersionExists(ref.entity, ref.version)) return false;
    // FAZ 2 — UYGULA.
    ++revision_;
    UndoRecord record = std::move(redoStack_.back());
    redoStack_.pop_back();
    for (const auto& op : record.ops)
        applyUndoOp(op, true);
    for (const auto& ref : record.geometryAfter)
        occStore_->restoreActive(ref.entity, ref.version);
    layers_ = record.layersAfter;
    undoStack_.push_back(std::move(record));
    if (undoStack_.size() > kMaxUndoEntries)
        undoStack_.pop_front();
    rebuildDerivedState();
    // Redo sonrasi kayit undo yigini ile korunur; 100 kayit siniri asildiysa
    // en eski kaydin referanslari birakilmistir — buda (Phase 3.2C).
    pruneDeadVersions();
    return true;
}

bool Document::canUndo() const noexcept {
    for (auto it = undoStack_.rbegin(); it != undoStack_.rend(); ++it)
        if (recordHasEffect(*it)) return true;
    return false;
}

bool Document::canRedo() const noexcept {
    for (auto it = redoStack_.rbegin(); it != redoStack_.rend(); ++it)
        if (recordHasEffect(*it)) return true;
    return false;
}

void Document::clearHistory() noexcept {
    undoStack_.clear();
    redoStack_.clear();
    // Gecmis bosaldiginda yalnizca canli varliklarin etkin surumleri korunur;
    // silinen/terk edilen varliklarin geometrisi budanir (Phase 3.2C).
    pruneDeadVersions();
}

// SURUM BUDAMA (Phase 3.2C): yalnizca HICBIR canli referans tarafindan
// kullanilmayan (varlik, surum) ciftlerini siler. Canli referans kumesi uc
// kaynaktan gelir:
//   (1) CANLI varliklarin (modelIds_) ETKIN surumu — guncel geometri;
//   (2) undo/redo op'larinda KIMLIKLE referanslanan varliklarin (beforeIds/
//       afterIds) ETKIN surumu — sil/ekle/deyistir geri alinirken geri
//       yuklenecek geometri (silme store'a dokunmadigindan silinen varligin
//       etkin surumu burada korunur);
//   (3) undo/redo kayitlarindaki ACIK geometryBefore/geometryAfter ref'leri.
// Tamamlanmamis bir transaction'in geri donus geometrisi (1) veya (3) ile
// korunur; budama HICBIR ZAMAN acik kaydin ihtiyacini silmez.
void Document::pruneDeadVersions() {
    std::set<std::pair<EntityId, VersionId>> keep;

    const auto protectActive = [&](EntityId e) {
        if (e == kInvalidEntityId) return;
        const VersionId v = occStore_->activeVersion(e);
        if (v != kInvalidVersionId) keep.emplace(e, v);
    };

    // (1b) KESME TABAN SURUMU (Phase 4.2): bir varligin taban (pre-cut) surumu
    // kesme modifier'larinin yeniden uygulanacagi kaynaktir; budamada ASLA
    // silinmemelidir (yoksa "orijinal B-Rep kaybolmaz" sozlesmesi kirilir).
    const auto protectBase = [&](EntityId e) {
        if (e == kInvalidEntityId) return;
        const VersionId v = occStore_->baseVersion(e);
        if (v != kInvalidVersionId) keep.emplace(e, v);
    };

    // (1) canli varliklar
    for (const EntityId e : modelIds_) { protectActive(e); protectBase(e); }

    // (2) + (3): undo/redo kayitlari
    const auto scan = [&](const UndoRecord& rec) {
        for (const UndoOp& op : rec.ops) {
            for (const EntityId e : op.beforeIds) { protectActive(e); protectBase(e); }
            for (const EntityId e : op.afterIds) { protectActive(e); protectBase(e); }
        }
        for (const GeometrySlotRef& ref : rec.geometryBefore)
            if (ref.version != kInvalidVersionId) keep.emplace(ref.entity, ref.version);
        for (const GeometrySlotRef& ref : rec.geometryAfter)
            if (ref.version != kInvalidVersionId) keep.emplace(ref.entity, ref.version);
    };
    for (const UndoRecord& rec : undoStack_) scan(rec);
    for (const UndoRecord& rec : redoStack_) scan(rec);

    occStore_->prune([&](EntityId e, VersionId v) { return keep.count({e, v}) != 0; });
}
std::vector<WireframeModel>& Document::mutableModels() noexcept { return models_; }
const std::vector<WireframeModel>& Document::models() const noexcept { return models_; }

EntityId Document::allocateEntityId() noexcept {
    // TUKENME (bkz. entity_id.hpp): id uzayi dolduysa GUVENLI REDDET — ayni
    // id ASLA ikinci kez uretilmez (eski kelepceleme YINELENEN kimlik
    // olusturuyordu). Sayac DEGISMEZ; cagiran kInvalidEntityId gorur.
    if (nextEntityId_ == kExhaustedEntityId) return kInvalidEntityId;
    const EntityId id = nextEntityId_;
    nextEntityId_ = advanceEntityIdCounter(nextEntityId_);
    return id;
}

EntityId Document::allocateEntityIdRequired() {
    const EntityId id = allocateEntityId();
    if (id == kInvalidEntityId)
        throw std::runtime_error("entity ID space exhausted");
    return id;
}

EntityId Document::modelId(std::size_t index) const noexcept {
    return index < modelIds_.size() ? modelIds_[index] : kInvalidEntityId;
}

std::optional<std::size_t> Document::indexOfEntity(EntityId id) const noexcept {
    if (id == kInvalidEntityId) return std::nullopt;
    for (std::size_t i = 0; i < modelIds_.size(); ++i)
        if (modelIds_[i] == id) return i;
    return std::nullopt;
}
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

bool Document::modelIsPickable(std::size_t index) const {
    if (!modelIsEditable(index)) return false;
    if (!pickFilter_) return true;
    ensureSpatialIndex();
    if (index >= modelBounds_.size()) return false;
    return pickFilter_(modelBounds_[index]);
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

void Document::buildMmw8(Mmw8Document& out) const {
    out.nextEntityId = nextEntityId_;
    out.metadata.binToolsFormatVersion = occStore_->bRepFormatVersion();
    out.metadata.occtVersion = occStore_->occVersionString();
    out.models = models_;
    out.entityIds = modelIds_;
    out.layers = layers_;
    out.grids = grids_;
    // B-Rep bloblari: YALNIZ canli modellerin ETKIN B-Rep'i kalicilastirilir.
    // OCC-acik: store'dan serializeActive; OCC-kapali: opak koruma (opaqueBreps_).
    out.blobs.clear();
    for (const EntityId e : modelIds_) {
        std::string blob = occStore_->serializeActive(e);
        if (blob.empty()) {
            const auto it = opaqueBreps_.find(e);
            if (it != opaqueBreps_.end()) blob = it->second;
        }
        if (!blob.empty()) out.blobs.push_back(Mmw8Blob{e, std::move(blob), ""});
    }
    out.unknownBlocks = mmw8UnknownBlocks_;
    out.cutModifiers = cutModifiers_;
    out.boltGroups = boltGroups_;
    out.endPlateConnections = endPlateConnections_;
    // Konstruksiyon turleri (K blogu): kind != None olan modellerin
    // EntityId -> kind eslesmesi (model kaydi ConstructionKind tasimaz).
    out.constructionKinds.clear();
    for (std::size_t i = 0; i < models_.size(); ++i) {
        const ConstructionKind kind = models_[i].constructionKind();
        if (kind != ConstructionKind::None)
            out.constructionKinds.push_back(Mmw8ConstructionKind{modelIds_[i], kind});
    }
    // Taban B-Rep blob'lari (V blogu): kesme tabani (pre-cut) surumu olan
    // varliklar. Boylece "orijinal B-Rep kaybolmaz" kayit/ac sonrasinda da
    // korunur ve kesim sonradan duzenlenebilir.
    out.baseBlobs.clear();
    for (const EntityId e : modelIds_) {
        std::string blob = occStore_->serializeBase(e);
        if (!blob.empty()) out.baseBlobs.push_back(Mmw8Blob{e, std::move(blob), ""});
    }
}

void Document::save(const std::filesystem::path& path) const {
    // OCC-kapali guvenlik: opak B-Rep varken geometri DEGISMISSE (eski B-Rep
    // yeni Wireframe ile uyumsuz) kaydetmeyi REDDET — sessiz tutarsiz dosya
    // asla yazilmaz.
    if (!opaqueBreps_.empty() && opaqueBrepsStale_)
        throw std::runtime_error(
            "B-Rep data is stale (geometry modified in OCC-disabled build); "
            "refusing to save");

    Mmw8Document mdoc;
    buildMmw8(mdoc);

    // ATOMIK kayit: once gecici dosyaya yaz; basarili yazimdan sonra hedefi
    // atomik olarak degistir. Hedef yalniz tam basarili yazimda DEGISIR.
    const auto tmp = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) throw std::runtime_error("Could not open file for writing");
        writeMmw8(out, mdoc);
        out.flush();
        if (!out) throw std::runtime_error("Could not write document");
    }
#ifdef _WIN32
    // Windows: ReplaceFileW atomik degistirir (mevcut hedef var oldugunda;
    // basarisizlikta hedef KORUNUR — veri kaybi yok). Hedef henuz yoksa
    // MoveFileExW ile tasinir. Hata kodlari GetLastError ile raporlanir.
    const std::wstring srcW = tmp.wstring();
    const std::wstring dstW = path.wstring();
    const bool targetExists =
        ::GetFileAttributesW(dstW.c_str()) != INVALID_FILE_ATTRIBUTES;
    const BOOL ok = targetExists
                        ? ::ReplaceFileW(dstW.c_str(), srcW.c_str(), nullptr,
                                         REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
                        : ::MoveFileExW(srcW.c_str(), dstW.c_str(),
                                        MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        const DWORD werr = ::GetLastError();
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);  // gecici dosya guvenli temizlik
        const std::error_code wec(static_cast<int>(werr), std::system_category());
        throw std::runtime_error("atomic replace failed (Win32 error " +
                                 std::to_string(werr) + ": " + wec.message() + ")");
    }
#else
    // POSIX: std::filesystem::rename ayni dosya sistemi icinde ATOMIK degistirir.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        throw std::runtime_error("Could not replace document: " + ec.message());
    }
#endif
}

void Document::commitFromMmw8(Mmw8Document&& mdoc) {
    // B-Rep bloblari TAZE bir store'a (ayni tur, cloneEmpty) geri yukle.
    // Basarili olursa store devralinir; basarisizlikta (or. uyumsuz OCCT
    // surumu) fresh atilir ve *this DEGISMEMIS kalir — ATOMIK yukleme.
    auto fresh = occStore_->cloneEmpty();
    std::unordered_map<EntityId, std::string> opaque;
    for (const auto& blob : mdoc.blobs) {
        std::string err;
        if (!fresh->restoreActiveSolid(blob.entityId, blob.bytes, &err)) {
            // OCC-kapali (fresh restore edemiyor): blob'u OPAK koru.
            opaque[blob.entityId] = blob.bytes;
        }
    }
    // Taban B-Rep blob'lari (V blogu): kesme tabani (pre-cut) geri yukle.
    // OCC-kapali derlemede restore edilemez -> taban geometrisi kaybolur
    // (wireframe-only; kesme recompute'u zaten OCC gerektirir). Kesme
    // modifier'lari ayri korunur; yalniz taban geometrisi eksik kalir.
    for (const auto& blob : mdoc.baseBlobs) {
        std::string err;
        fresh->restoreBaseSolid(blob.entityId, blob.bytes, &err);
    }

    // Basari: tum durumu devral.
    occStore_ = std::move(fresh);
    models_ = std::move(mdoc.models);
    modelIds_ = std::move(mdoc.entityIds);
    nextEntityId_ = mdoc.nextEntityId;
    layers_ = std::move(mdoc.layers);
    grids_ = std::move(mdoc.grids);
    cutModifiers_ = std::move(mdoc.cutModifiers);
    boltGroups_ = std::move(mdoc.boltGroups);
    endPlateConnections_ = std::move(mdoc.endPlateConnections);
    // Grid `axes` TUREV bir alandir (spec'ten cizim geometrisi); codec yalniz
    // spec'i tasir. Yuklemede yeniden TURET.
    for (auto& grid : grids_) rebuildGridAxes(grid);
    // Konstruksiyon turleri (K blogu): EntityId -> kind eslemesini modellere
    // geri yukle (model kaydi ConstructionKind tasimaz).
    for (const auto& ck : mdoc.constructionKinds) {
        if (auto idx = indexOfEntity(ck.entityId); idx.has_value())
            models_[*idx].setConstructionKind(ck.kind);
    }
    opaqueBreps_ = std::move(opaque);
    opaqueBrepsStale_ = false;
    mmw8Metadata_ = mdoc.metadata;
    mmw8UnknownBlocks_ = std::move(mdoc.unknownBlocks);

    // Undo/redo gecmisi dosyaya YAZILMADI; basarili yukleme eski gecmisi
    // TEMIZLER (kullanici karari). Kiris yukleri/dugum sinirlari kimlik
    // anahtarli oldugu icin yeni belgede gecersizdir — sifirlanir.
    undoStack_.clear();
    redoStack_.clear();
    pruneDeadVersions();
    beamLoads_.clear();
    nodeConstraints_.clear();
    invalidateDerivedState();
    ++revision_;
}

void Document::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open file for reading");

    std::string signature;
    if (!(input >> signature)) throw std::runtime_error("Invalid Model Maker file");

    // MMW8 (Phase 3.4C): ikili B-Rep + EntityId + metadata tasiyan yeni format.
    // readMmw8 her dogrulama hatasinda (checksum, kisaltma, yinelenen kimlik...)
    // FIRLATIR ve commitFromMmw8 ATOMIK isler — basarisizlikta *this degismez.
    if (signature == "MMW8") {
        input.clear();
        input.seekg(0, std::ios::beg);
        Mmw8Document mdoc;
        readMmw8(input, mdoc);
        commitFromMmw8(std::move(mdoc));
        return;
    }

    // Legacy MMW1-7: mevcut metin formati (geriye donuk uyumluluk korunur).
    input.clear();
    input.seekg(0, std::ios::beg);
    std::size_t modelCount{};
    if (!(input >> signature >> modelCount) ||
        (signature != "MMW1" && signature != "MMW2" && signature != "MMW3" &&
         signature != "MMW4" && signature != "MMW5" && signature != "MMW6" &&
         signature != "MMW7") ||
        modelCount > 100000) {
        throw std::runtime_error("Invalid Model Maker file");
    }
    const bool version2 = signature == "MMW2" || signature == "MMW3" || signature == "MMW4" ||
                          signature == "MMW5" || signature == "MMW6" || signature == "MMW7";
    const bool version3 = signature == "MMW3" || signature == "MMW4" || signature == "MMW5" ||
                          signature == "MMW6" || signature == "MMW7";
    const bool version4 = signature == "MMW4" || signature == "MMW5" || signature == "MMW6" ||
                          signature == "MMW7";
    const bool version5 = signature == "MMW5" || signature == "MMW6" || signature == "MMW7";
    // MMW6: kesit referans normali (sectionNormal) P blogunda; MMW5 ve oncesi
    // okunurken varsayilan (0,0,1) kalir → eski dosyalar AYNI davranis.
    const bool version6 = signature == "MMW6" || signature == "MMW7";
    // MMW7: grid blogu TEKLA spesifikasyonu tasir (MMW5/6: yalniz-axes).
    const bool version7 = signature == "MMW7";
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
            if (version6 &&
                !(input >> props.sectionNormalX >> props.sectionNormalY >>
                  props.sectionNormalZ))
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
    // Yukleme belge icerigini TAMAMEN degistirir: Document-SAHIPLI B-Rep
    // magazasi da SIFIRLANIR (eski belgenin sekilleri gecersizdir ve yeni
    // varlik kimlikleriyle YANLIS ESLESMEMELI). MMW1-7 B-Rep tasimaz →
    // yuklenen katilar yalnizca tel kafes olarak gelir (mevcut davranis).
    occStore_->clear();
    // KARARLI KIMLIK: MMW1-7 dosyalari kimlik TASIMAZ (format degistirilmedi).
    // Yuklenen her modele sirayla kimlik atanir. Sayac SIFIRLANMAZ (monoton
    // kalir): boylece dosya acildiktan sonra hala yiginda duran eski undo
    // op'lariyla (load gecmisi temizlemez — onceden var olan davranis) kimlik
    // catismasi OLUSMAZ. Taze bir belgede (sayac=1) yukleme deterministik
    // olarak 1..N verir.
    modelIds_.clear();
    modelIds_.reserve(models_.size());
    for (std::size_t i = 0; i < models_.size(); ++i)
        modelIds_.push_back(allocateEntityId());
    // Yukleme belge icerigini TAMAMEN degistirir: onceki belgenin kiris
    // yukleri (kimlik anahtarli) artik gecerli bir varliga karsilik gelmez —
    // sizmamalari icin SIFIRLANIR.
    beamLoads_.clear();
    // Yapi gridleri (MMW5+): modellerden sonra 'G' blogu.
    grids_.clear();
    cutModifiers_.clear(); // kesme modifier'leri henuz MMW8'de kalici degil
    if (version5) {
        std::string gridTag;
        std::size_t gridCount{};
        if (!(input >> gridTag >> gridCount) || gridTag != "G" || gridCount > 1000)
            throw std::runtime_error("Invalid grid block");
        for (std::size_t gi = 0; gi < gridCount; ++gi) {
            GridDefinition grid;
            if (version7) {
                // MMW7: TEKLA SPESIFIKASYONU. Cizim geometrisi TUREtilir.
                grid.name = readS();
                int visible = 1, magnetic = 0, kind = 0;
                if (!(input >> visible >> magnetic >> kind
                           >> grid.origin.x >> grid.origin.y >> grid.origin.z
                           >> grid.extendLeft >> grid.extendRight
                           >> grid.extendBelow >> grid.extendAbove
                           >> grid.color >> grid.labelColor >> grid.labelFontSize))
                    throw std::runtime_error("Invalid grid spec");
                grid.visible = visible != 0;
                grid.magnetic = magnetic != 0;
                grid.kind = static_cast<GridKind>(kind);
                const auto readNumRow = [&input]() {
                    std::vector<double> out;
                    std::size_t n{};
                    if (!(input >> n) || n > 4096) throw std::runtime_error("Invalid grid row");
                    out.resize(n);
                    for (std::size_t k = 0; k < n; ++k)
                        if (!(input >> out[k])) throw std::runtime_error("Invalid grid coordinate");
                    return out;
                };
                const auto readLabelRow = [&input](const std::function<std::string()>& readStr) {
                    std::vector<std::string> out;
                    std::size_t n{};
                    if (!(input >> n) || n > 4096) throw std::runtime_error("Invalid grid label row");
                    out.reserve(n);
                    for (std::size_t k = 0; k < n; ++k) out.push_back(readStr());
                    return out;
                };
                grid.xCoords = readNumRow();
                grid.yCoords = readNumRow();
                grid.zCoords = readNumRow();
                grid.xLabels = readLabelRow(readS);
                grid.yLabels = readLabelRow(readS);
                grid.zLabels = readLabelRow(readS);
                rebuildGridAxes(grid);
            } else {
                // MMW5/6: yalniz turetilmis cizim geometrisi (spec yok).
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
            }
            grids_.push_back(std::move(grid));
        }
    }
    invalidateDerivedState();
}

// ── Yapi gridleri ────────────────────────────────────────────────
// NOT: model mutasyonlari gibi grid mutasyonlari da KENDI undo op'unu
// kaydeder; cagiran yalnizca pushSnapshot() ile kayit acmalidir. Aksi halde
// grid ekleme/silme geri alinamaz (eski davranis buydu).
std::size_t Document::addGrid(GridDefinition grid) {
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::GridAdd;
    op.gridIndex = grids_.size();
    op.gridAfter = grid;
    recordUndoOp(std::move(op));
    grids_.push_back(std::move(grid));
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
bool Document::setGrid(std::size_t index, GridDefinition grid) {
    if (index >= grids_.size()) return false;
    // Cizim geometrisi her zaman spesifikasyondan TUREtilir.
    rebuildGridAxes(grid);
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::GridReplace;
    op.gridIndex = index;
    op.gridBefore = grids_[index];
    op.gridAfter = grid;
    recordUndoOp(std::move(op));
    grids_[index] = std::move(grid);
    return true;
}
void Document::removeGrid(std::size_t index) {
    if (index >= grids_.size()) return;
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::GridRemove;
    op.gridIndex = index;
    op.gridBefore = grids_[index];
    recordUndoOp(std::move(op));
    grids_.erase(grids_.begin() + static_cast<std::ptrdiff_t>(index));
}

std::size_t Document::addCutModifier(CutModifier modifier) {
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::CutAdd;
    op.cutIndex = cutModifiers_.size();
    op.cutAfter = modifier;
    recordUndoOp(std::move(op));
    cutModifiers_.push_back(std::move(modifier));
    return cutModifiers_.size() - 1;
}

void Document::removeCutModifier(std::size_t index) {
    if (index >= cutModifiers_.size()) return;
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::CutRemove;
    op.cutIndex = index;
    op.cutBefore = cutModifiers_[index];
    recordUndoOp(std::move(op));
    cutModifiers_.erase(cutModifiers_.begin() + static_cast<std::ptrdiff_t>(index));
}

bool Document::setCutModifier(std::size_t index, CutModifier modifier) {
    if (index >= cutModifiers_.size()) return false;
    ++revision_;
    UndoOp op;
    op.kind = UndoOp::Kind::CutReplace;
    op.cutIndex = index;
    op.cutBefore = cutModifiers_[index];
    op.cutAfter = modifier;
    recordUndoOp(std::move(op));
    cutModifiers_[index] = std::move(modifier);
    return true;
}

std::vector<std::size_t> Document::cutModifiersForTarget(EntityId target) const {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < cutModifiers_.size(); ++i)
        if (cutModifiers_[i].target == target) result.push_back(i);
    return result;
}

std::vector<std::size_t> Document::cutModifiersUsingTool(EntityId tool) const {
    // Ters sorgu (canli Part Cut): kesici parca tasininca bu parçayi ARAÇ olarak
    // kullanan BooleanCut modifier'larinin hedefleri yeniden hesaplanir.
    std::vector<std::size_t> result;
    if (tool == kInvalidEntityId) return result;
    for (std::size_t i = 0; i < cutModifiers_.size(); ++i)
        if (cutModifiers_[i].kind == CutKind::BooleanCut && cutModifiers_[i].tool == tool)
            result.push_back(i);
    return result;
}

std::vector<std::size_t> Document::cutModifiersForSource(EntityId source) const {
    // Ters sorgu (P6): kaynak (konstrüksiyon duzlemi/cizgi/3DFACE) tasininca
    // onu kullanan modifier'larin hedefleri yeniden hesaplanir.
    std::vector<std::size_t> result;
    if (source == kInvalidEntityId) return result;
    for (std::size_t i = 0; i < cutModifiers_.size(); ++i)
        if (cutModifiers_[i].source == source) result.push_back(i);
    return result;
}

// --- BULON GRUPLARI (Phase B1) ----------------------------------------------

// Tek bir modeli (yaprak — kesme kaskadi olmadan) siler ve Delete op kaydeder.
// Kitap isleri deleteModels'in tek-indeks yolunun aynisidir.
void Document::eraseModelLeaf(std::size_t index) {
    if (index >= models_.size()) return;
    UndoOp op;
    op.kind = UndoOp::Kind::Delete;
    op.indices = {index};
    op.beforeModels.push_back(models_[index]);
    op.beforeIds.push_back(modelIds_[index]);
    recordUndoOp(std::move(op));
    models_.erase(models_.begin() + static_cast<std::ptrdiff_t>(index));
    modelIds_.erase(modelIds_.begin() + static_cast<std::ptrdiff_t>(index));
    if (modelBounds_.size() == models_.size() + 1)
        modelBounds_.erase(modelBounds_.begin() + static_cast<std::ptrdiff_t>(index));
    if (effectiveCache_.size() == models_.size() + 1 && !effectiveCacheDirty_)
        effectiveCache_.erase(effectiveCache_.begin() + static_cast<std::ptrdiff_t>(index));
    if (modelBounds_.size() != models_.size()) modelBounds_.resize(models_.size());
    if (effectiveCache_.size() != models_.size()) effectiveCacheDirty_ = true;
    spatialOrder_.clear();
    spatialNodes_.clear();
    pendingIndexRebuild_.clear();
    pendingMask_.clear();
    spatialIndexDirty_ = true;
    documentBounds_.reset();
    invalidateDerivedState();
}

std::size_t Document::addBoltGroup(BoltGroup bolt, WireframeModel displayModel) {
    ++revision_;
    opaqueBrepsStale_ = true;
    if (bolt.id == kInvalidEntityId) bolt.id = allocateEntityIdRequired();
    // 1) Display modelini ekle (Add op) — kimligi yakala.
    addModel(std::move(displayModel));
    const std::size_t di = models_.size() - 1;
    bolt.displayEntityId = modelId(di);
    // 2) Delikleri ekle (CutAdd op) — owner = bolt.id.
    for (auto mod : boltGroupHoleModifiers(bolt)) {
        mod.owner = bolt.id;
        addCutModifier(std::move(mod));
    }
    // 3) Bulon kaydini ekle (BoltAdd op).
    UndoOp op;
    op.kind = UndoOp::Kind::BoltAdd;
    op.boltIndex = boltGroups_.size();
    op.boltAfter = bolt;
    recordUndoOp(std::move(op));
    boltGroups_.push_back(std::move(bolt));
    return boltGroups_.size() - 1;
}

void Document::removeBoltGroup(std::size_t index) {
    if (index >= boltGroups_.size()) return;
    ++revision_;
    opaqueBrepsStale_ = true;
    const BoltGroup bolt = boltGroups_[index];
    // 1) Bulon kaydini SIL (BoltRemove op) — kaskad yeniden tetiklenmesin.
    UndoOp op;
    op.kind = UndoOp::Kind::BoltRemove;
    op.boltIndex = index;
    op.boltBefore = bolt;
    recordUndoOp(std::move(op));
    boltGroups_.erase(boltGroups_.begin() + static_cast<std::ptrdiff_t>(index));
    // 2) Delikleri sil (owner == bolt.id) — yuksekten dusuge (kayma guvenli).
    for (std::size_t ci = cutModifiers_.size(); ci-- > 0;) {
        if (cutModifiers_[ci].owner == bolt.id) removeCutModifier(ci);
    }
    // 3) Display modelini sil (yaprak — kesme/bulon kaskadi yok).
    if (bolt.displayEntityId != kInvalidEntityId) {
        if (auto di = indexOfEntity(bolt.displayEntityId))
            eraseModelLeaf(*di);
    }
}

bool Document::setBoltGroup(std::size_t index, BoltGroup bolt) {
    if (index >= boltGroups_.size()) return false;
    ++revision_;
    opaqueBrepsStale_ = true;
    const EntityId id = boltGroups_[index].id;
    bolt.id = id;  // kimlik korunur (girdideki id yok sayilir)
    bolt.displayEntityId = boltGroups_[index].displayEntityId;  // display kimligi sabit
    // 1) Bulon kaydini degistir (BoltReplace op).
    UndoOp op;
    op.kind = UndoOp::Kind::BoltReplace;
    op.boltIndex = index;
    op.boltBefore = boltGroups_[index];
    op.boltAfter = bolt;
    recordUndoOp(std::move(op));
    boltGroups_[index] = bolt;
    // 2) Eski delikleri sil (owner == id), yenilerini ekle.
    for (std::size_t ci = cutModifiers_.size(); ci-- > 0;) {
        if (cutModifiers_[ci].owner == id) removeCutModifier(ci);
    }
    for (auto mod : boltGroupHoleModifiers(bolt)) {
        mod.owner = id;
        addCutModifier(std::move(mod));
    }
    return true;
}

std::optional<std::size_t> Document::indexOfBoltGroup(EntityId id) const {
    if (id == kInvalidEntityId) return std::nullopt;
    for (std::size_t i = 0; i < boltGroups_.size(); ++i)
        if (boltGroups_[i].id == id) return i;
    return std::nullopt;
}

std::vector<std::size_t> Document::boltGroupHoles(EntityId boltId) const {
    std::vector<std::size_t> result;
    if (boltId == kInvalidEntityId) return result;
    for (std::size_t i = 0; i < cutModifiers_.size(); ++i)
        if (cutModifiers_[i].owner == boltId) result.push_back(i);
    return result;
}

double Document::boltGripLength(const BoltGroup& bolt) const {
    const double len =
        std::sqrt(bolt.axis.x * bolt.axis.x + bolt.axis.y * bolt.axis.y +
                  bolt.axis.z * bolt.axis.z);
    if (len < 1e-12) return 0.0;
    const double ax = bolt.axis.x / len, ay = bolt.axis.y / len, az = bolt.axis.z / len;
    double mn = +1e30, mx = -1e30;
    bool any = false;
    for (const EntityId part : bolt.parts) {
        const auto idx = indexOfEntity(part);
        if (!idx) continue;
        const Bounds3& b = modelBounds()[*idx];
        for (int i = 0; i < 8; ++i) {
            const double px = (i & 1) ? b.maximum.x : b.minimum.x;
            const double py = (i & 2) ? b.maximum.y : b.minimum.y;
            const double pz = (i & 4) ? b.maximum.z : b.minimum.z;
            const double t = px * ax + py * ay + pz * az;
            if (t < mn) mn = t;
            if (t > mx) mx = t;
            any = true;
        }
    }
    if (!any) return 0.0;
    return mx - mn;
}

std::size_t Document::addEndPlateConnection(EndPlateConnection conn, WireframeModel plateModel,
                                            WireframeModel boltDisplayModel) {
    ++revision_;
    opaqueBrepsStale_ = true;
    if (conn.id == kInvalidEntityId) conn.id = allocateEntityIdRequired();
    if (conn.bolts.id == kInvalidEntityId) conn.bolts.id = allocateEntityIdRequired();
    // 1) Levha modelini ekle (Add op) — kimligi yakala.
    addModel(std::move(plateModel));
    conn.plateEntityId = modelId(models_.size() - 1);
    // 2) Bulon grubunu kur: parcalar = {levha, mainPart(kolon)}. Kiriş (secondary)
    //    delinmez — levha kirişe kaynaklı, bulonlar levha->kolon bağlantısı kurar.
    BoltGroup bolt = conn.bolts;
    bolt.parts.clear();
    if (conn.plateEntityId != kInvalidEntityId) bolt.parts.push_back(conn.plateEntityId);
    if (conn.mainPart != kInvalidEntityId) bolt.parts.push_back(conn.mainPart);
    // 2a) Montaj display modelini ekle (Add op) — kimligi yakala.
    addModel(std::move(boltDisplayModel));
    bolt.displayEntityId = modelId(models_.size() - 1);
    // 2b) Delikleri ekle (CutAdd op) — owner = bolt.id.
    for (auto mod : boltGroupHoleModifiers(bolt)) {
        mod.owner = bolt.id;
        addCutModifier(std::move(mod));
    }
    // 2c) Bulon kaydini ekle (BoltAdd op).
    UndoOp bop;
    bop.kind = UndoOp::Kind::BoltAdd;
    bop.boltIndex = boltGroups_.size();
    bop.boltAfter = bolt;
    recordUndoOp(std::move(bop));
    boltGroups_.push_back(std::move(bolt));
    conn.bolts = boltGroups_.back();
    // 3) Baglanti kaydini ekle (EndPlateAdd op).
    UndoOp op;
    op.kind = UndoOp::Kind::EndPlateAdd;
    op.endPlateIndex = endPlateConnections_.size();
    op.endPlateAfter = conn;
    recordUndoOp(std::move(op));
    endPlateConnections_.push_back(std::move(conn));
    return endPlateConnections_.size() - 1;
}

void Document::removeEndPlateConnection(std::size_t index) {
    if (index >= endPlateConnections_.size()) return;
    ++revision_;
    opaqueBrepsStale_ = true;
    const EndPlateConnection conn = endPlateConnections_[index];
    // 1) Baglanti kaydini sil (EndPlateRemove op) — kaskad yeniden tetiklenmesin.
    UndoOp op;
    op.kind = UndoOp::Kind::EndPlateRemove;
    op.endPlateIndex = index;
    op.endPlateBefore = conn;
    recordUndoOp(std::move(op));
    endPlateConnections_.erase(endPlateConnections_.begin() + static_cast<std::ptrdiff_t>(index));
    // 2) Bulon grubunu sil (delikler + montaj).
    if (auto bi = indexOfBoltGroup(conn.bolts.id)) removeBoltGroup(*bi);
    // 3) Levha modelini sil (yaprak — kesme/bulon kaskadi yok).
    if (conn.plateEntityId != kInvalidEntityId) {
        if (auto pi = indexOfEntity(conn.plateEntityId)) eraseModelLeaf(*pi);
    }
}

bool Document::setEndPlateConnection(std::size_t index, EndPlateConnection conn,
                                     WireframeModel plateModel, WireframeModel boltDisplayModel) {
    if (index >= endPlateConnections_.size()) return false;
    ++revision_;
    opaqueBrepsStale_ = true;
    const EndPlateConnection old = endPlateConnections_[index];
    conn.id = old.id;                                    // kimlik korunur
    conn.bolts.id = old.bolts.id;                        // bulon kimligi korunur
    conn.plateEntityId = old.plateEntityId;              // levha kimligi sabit
    conn.bolts.displayEntityId = old.bolts.displayEntityId; // montaj kimligi sabit
    // 1) Baglanti kaydini degistir (EndPlateReplace op).
    UndoOp op;
    op.kind = UndoOp::Kind::EndPlateReplace;
    op.endPlateIndex = index;
    op.endPlateBefore = old;
    op.endPlateAfter = conn;
    recordUndoOp(std::move(op));
    endPlateConnections_[index] = conn;
    // 2) Levha modelini yeniden uret (replaceModel — kimlik korunur).
    if (conn.plateEntityId != kInvalidEntityId) {
        if (auto pi = indexOfEntity(conn.plateEntityId))
            replaceModel(*pi, std::vector<WireframeModel>{std::move(plateModel)});
    }
    // 3) Bulon grubunu guncelle: parcalar yeniden kurulur, delikler yeniden uretilir.
    BoltGroup bolt = conn.bolts;
    bolt.parts.clear();
    if (conn.plateEntityId != kInvalidEntityId) bolt.parts.push_back(conn.plateEntityId);
    if (conn.mainPart != kInvalidEntityId) bolt.parts.push_back(conn.mainPart);
    if (auto bi = indexOfBoltGroup(bolt.id)) setBoltGroup(*bi, bolt);
    // 4) Montaj display modelini yeniden uret.
    if (bolt.displayEntityId != kInvalidEntityId) {
        if (auto di = indexOfEntity(bolt.displayEntityId))
            replaceModel(*di, std::vector<WireframeModel>{std::move(boltDisplayModel)});
    }
    return true;
}

std::optional<std::size_t> Document::indexOfEndPlateConnection(EntityId id) const {
    if (id == kInvalidEntityId) return std::nullopt;
    for (std::size_t i = 0; i < endPlateConnections_.size(); ++i)
        if (endPlateConnections_[i].id == id) return i;
    return std::nullopt;
}

CutModifier resolveCutModifierSource(const CutModifier& mod, const Document& document) {
    if (mod.source == kInvalidEntityId) return mod;
    const auto idxOpt = document.indexOfEntity(mod.source);
    if (!idxOpt) return mod;  // kaynak yok (kaskad kaldirmis olmali) — sabit geometri
    const auto& src = document.models()[*idxOpt];
    CutModifier out = mod;
    switch (mod.kind) {
    case CutKind::PlaneCut:
    case CutKind::Fitting:
        if (const auto frame = constructionPlaneFrame(src)) {
            out.planeOrigin = frame->origin;
            out.planeNormal = frame->normal;
        }
        break;
    case CutKind::LineCut:
        if (src.vertices().size() >= 2)
            out.polygon = {src.vertices()[0], src.vertices()[1]};
        break;
    case CutKind::PolygonCut:
        out.polygon = src.vertices();
        break;
    case CutKind::BooleanCut:
        break;  // BooleanCut: tool referansiyla cozumlenir, source kullanilmaz
    case CutKind::Hole:
        break;  // Hole: bulon origin/ekseninden uretilir, source kullanilmaz
    }
    return out;
}

} // namespace mm
