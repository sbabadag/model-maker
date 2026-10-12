#include "model_maker/occ_store.hpp"

namespace mm {

namespace {

// OCC kapaliyken Document'a enjekte edilen BOS magaza. Hicbir sekil tutmaz;
// clear/size no-op'tur. TopoDS_Shape gerektiren API YOKTUR (OCC'siz derlenir).
class NullOccStore final : public OccStore {
public:
    void clear() noexcept override {}
    std::size_t size() const noexcept override { return 0; }
};

}  // namespace

std::unique_ptr<OccStore> makeNullOccStore() {
    return std::make_unique<NullOccStore>();
}

}  // namespace mm
