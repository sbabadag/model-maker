#pragma once

#include "model_maker/document.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <stop_token>

namespace mm {

class DxfImportCancelled : public std::runtime_error {
public:
    DxfImportCancelled() : std::runtime_error("DXF import cancelled") {}
};

class DxfFile {
public:
    using ProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;
    static Document read(const std::filesystem::path& path);
    static Document read(const std::filesystem::path& path, std::stop_token stopToken,
                         ProgressCallback progress);
    static void write(const Document& document, const std::filesystem::path& path);
};

} // namespace mm
