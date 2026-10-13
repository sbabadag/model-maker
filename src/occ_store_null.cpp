#include "model_maker/occ_store.hpp"

namespace mm {

namespace {

// OCC kapaliyken Document'a enjekte edilen BOS magaza. Hicbir sekil tutmaz;
// clear/size no-op'tur. TopoDS_Shape gerektiren API YOKTUR (OCC'siz derlenir).
class NullOccStore final : public OccStore {
public:
    void clear() noexcept override {}
    std::size_t size() const noexcept override { return 0; }

    // OCC kapali: hicbir geometri tutulmaz. Tum varliklar icin etkin surum
    // "yok"tur; bu GECERLI bir durumdur (undo dogrulamasi gecmeli ki
    // OCC-kapali derlemede de undo/redo calissin).
    VersionId activeVersion(EntityId) const noexcept override { return kInvalidVersionId; }
    bool findVersionExists(EntityId, VersionId version) const override {
        return version == kInvalidVersionId;  // yalnizca "geometri yok" gecerli
    }
    bool restoreActive(EntityId, VersionId version) override {
        return version == kInvalidVersionId;  // no-op; gercek surum yok
    }
};

}  // namespace

std::unique_ptr<OccStore> makeNullOccStore() {
    return std::make_unique<NullOccStore>();
}

}  // namespace mm
