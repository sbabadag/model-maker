#pragma once

#include "model_maker/geometry.hpp"

#include <filesystem>
#include <vector>

namespace mm {

class Document {
public:
    void addModel(WireframeModel model);
    void addLine(const Vec3& from, const Vec3& to);
    void clear() noexcept;

    const std::vector<WireframeModel>& models() const noexcept;

    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);

private:
    std::vector<WireframeModel> models_;
};

} // namespace mm
