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
    VersionId baseVersion(EntityId) const noexcept override { return kInvalidVersionId; }
    bool setBaseVersion(EntityId, VersionId) override { return false; }  // gercek surum yok
    std::size_t prune(const std::function<bool(EntityId, VersionId)>&) override {
        return 0;  // hicbir surum tutulmaz
    }
    // MMW8 B-Rep serilestirme: OCC kapali, hicbir B-Rep yok.
    std::string serializeActive(EntityId) const override { return {}; }
    bool restoreActiveSolid(EntityId, const std::string&, std::string* error) override {
        if (error) *error = "no OCC store available (OCC-disabled build)";
        return false;
    }
    std::string serializeBase(EntityId) const override { return {}; }
    bool restoreBaseSolid(EntityId, const std::string&, std::string* error) override {
        if (error) *error = "no OCC store available (OCC-disabled build)";
        return false;
    }
    std::string occVersionString() const noexcept override { return {}; }
    int bRepFormatVersion() const noexcept override { return 0; }
    std::unique_ptr<OccStore> cloneEmpty() const override { return makeNullOccStore(); }
};

}  // namespace

std::unique_ptr<OccStore> makeNullOccStore() {
    return std::make_unique<NullOccStore>();
}

}  // namespace mm
