#include "model_maker/occ_solid_store.hpp"

namespace mm {

void OccSolidStore::set(EntityId id, const TopoDS_Shape& shape) {
    shapes_[id] = shape;
}

const TopoDS_Shape* OccSolidStore::find(EntityId id) const {
    const auto it = shapes_.find(id);
    return it == shapes_.end() ? nullptr : &it->second;
}

std::vector<TopoDS_Shape> OccSolidStore::lastLive(std::size_t count,
    const std::function<bool(EntityId)>& isLive) const {
    std::vector<TopoDS_Shape> out;
    out.reserve(count);
    for (auto it = shapes_.rbegin(); it != shapes_.rend() && out.size() < count; ++it) {
        if (!isLive(it->first)) continue;
        out.push_back(it->second);
    }
    return out;
}

void OccSolidStore::clear() noexcept { shapes_.clear(); }

std::size_t OccSolidStore::size() const noexcept { return shapes_.size(); }

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
