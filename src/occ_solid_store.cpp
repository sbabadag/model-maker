#include "model_maker/occ_solid_store.hpp"

#include <algorithm>

namespace mm {

VersionId OccSolidStore::set(EntityId id, const TopoDS_Shape& shape) {
    // TUKENME (bkz. entity_id.hpp): yeni bir BENZERSIZ id uretemiyorsak ayni
    // id'yi tekrar uretmek yerine GUVENLI REDDET — magaza DEGISMEZ, etkin
    // surum korunur, hicbir sekil eklenmez. (2^64 sonrasi erisilemez bir
    // guvenlik agi; ama sozlesme "ASLA tekrar kullanilmaz" boylece GERCEK olur.)
    if (nextVersionId_ == kInvalidVersionId) return kInvalidVersionId;

    const VersionId vid = nextVersionId_;
    nextVersionId_ = advanceVersionIdCounter(nextVersionId_);

    Slot& slot = slots_[id];
    slot.versions.push_back(Version{vid, shape});
    slot.active = vid;
    return vid;
}

const TopoDS_Shape* OccSolidStore::shapeOfVersion(const Slot& slot, VersionId version) noexcept {
    if (version == kInvalidVersionId) return nullptr;
    for (const Version& v : slot.versions) {
        if (v.id == version) return &v.shape;
    }
    return nullptr;
}

bool OccSolidStore::activate(EntityId id, VersionId version) noexcept {
    if (version == kInvalidVersionId) return false;
    const auto it = slots_.find(id);
    if (it == slots_.end()) return false;
    if (shapeOfVersion(it->second, version) == nullptr) return false;
    it->second.active = version;
    return true;
}

const TopoDS_Shape* OccSolidStore::find(EntityId id) const {
    const auto it = slots_.find(id);
    if (it == slots_.end()) return nullptr;
    return shapeOfVersion(it->second, it->second.active);
}

const TopoDS_Shape* OccSolidStore::findVersion(EntityId id, VersionId version) const {
    const auto it = slots_.find(id);
    if (it == slots_.end()) return nullptr;
    return shapeOfVersion(it->second, version);
}

VersionId OccSolidStore::activeVersion(EntityId id) const noexcept {
    const auto it = slots_.find(id);
    return it == slots_.end() ? kInvalidVersionId : it->second.active;
}

bool OccSolidStore::findVersionExists(EntityId id, VersionId version) const {
    // "Geometri yok" her zaman temsil edilebilir bir durumdur → gecerli.
    if (version == kInvalidVersionId) return true;
    const auto it = slots_.find(id);
    if (it == slots_.end()) return false;
    return shapeOfVersion(it->second, version) != nullptr;
}

bool OccSolidStore::restoreActive(EntityId id, VersionId version) {
    if (version == kInvalidVersionId) {
        // Etkin geometri YOK. Var olan slot varsa etkinligi dusur; slot
        // yoksa hicbir sey yapma (zaten "geometri yok" durumu).
        const auto it = slots_.find(id);
        if (it != slots_.end()) it->second.active = kInvalidVersionId;
        return true;
    }
    const auto it = slots_.find(id);
    if (it == slots_.end()) return false;
    if (shapeOfVersion(it->second, version) == nullptr) return false;
    it->second.active = version;
    return true;
}

std::size_t OccSolidStore::versionCount(EntityId id) const noexcept {
    const auto it = slots_.find(id);
    return it == slots_.end() ? 0 : it->second.versions.size();
}

std::size_t OccSolidStore::totalVersionCount() const noexcept {
    std::size_t total = 0;
    for (const auto& entry : slots_) total += entry.second.versions.size();
    return total;
}

std::size_t OccSolidStore::prune(const std::function<bool(EntityId, VersionId)>& keep) {
    std::size_t pruned = 0;
    for (auto it = slots_.begin(); it != slots_.end();) {
        Slot& slot = it->second;
        std::vector<Version>& versions = slot.versions;
        const std::size_t before = versions.size();
        // `keep` false donen her surumu sil. (VersionId'ler SABIT kalir;
        // kalanlar yeniden numaralandirilmaz.)
        versions.erase(std::remove_if(versions.begin(), versions.end(),
                                      [&](const Version& v) { return !keep(it->first, v.id); }),
                       versions.end());
        pruned += before - versions.size();
        // Budanan surumu isaret eden etkin-alani "geometri yok"a dusur.
        if (slot.active != kInvalidVersionId && shapeOfVersion(slot, slot.active) == nullptr)
            slot.active = kInvalidVersionId;
        if (versions.empty()) {
            it = slots_.erase(it);  // tamamen bosalan slot kaldirilir
        } else {
            ++it;
        }
    }
    return pruned;
}

std::vector<VersionId> OccSolidStore::versionIds(EntityId id) const {
    std::vector<VersionId> out;
    const auto it = slots_.find(id);
    if (it == slots_.end()) return out;
    out.reserve(it->second.versions.size());
    for (const Version& v : it->second.versions) out.push_back(v.id);
    return out;
}

std::vector<TopoDS_Shape> OccSolidStore::lastLive(std::size_t count,
    const std::function<bool(EntityId)>& isLive) const {
    std::vector<TopoDS_Shape> out;
    out.reserve(count);
    for (auto it = slots_.rbegin(); it != slots_.rend() && out.size() < count; ++it) {
        if (!isLive(it->first)) continue;
        // ETKIN surum (Phase 3.1 davranisi).
        if (const TopoDS_Shape* shape = shapeOfVersion(it->second, it->second.active)) {
            out.push_back(*shape);
        }
    }
    return out;
}

void OccSolidStore::clear() noexcept {
    // nextVersionId_ BILEREK sifirlanmaz: surum kimlikleri tekrar kullanilmaz.
    slots_.clear();
}

std::size_t OccSolidStore::size() const noexcept { return slots_.size(); }

OccSolidStore* asSolidStore(OccStore* store) noexcept {
    return dynamic_cast<OccSolidStore*>(store);
}

const OccSolidStore* asSolidStore(const OccStore* store) noexcept {
    return dynamic_cast<const OccSolidStore*>(store);
}

std::unique_ptr<OccStore> makeOccSolidStore() {
    return std::make_unique<OccSolidStore>();
}

}  // namespace mm
