#include "model_maker/document.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace mm {

void Document::addModel(WireframeModel model) { models_.push_back(std::move(model)); }
void Document::addLine(const Vec3& from, const Vec3& to) { addModel(WireframeModel::line(from, to)); }
void Document::clear() noexcept { models_.clear(); }
const std::vector<WireframeModel>& Document::models() const noexcept { return models_; }

void Document::save(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Could not open file for writing");

    output.precision(17);
    output << "MMW1\n" << models_.size() << '\n';
    for (const auto& model : models_) {
        output << model.vertices().size() << ' ' << model.edges().size() << '\n';
        for (const auto& vertex : model.vertices()) output << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
        for (const auto& edge : model.edges()) output << edge.from << ' ' << edge.to << '\n';
    }
    if (!output) throw std::runtime_error("Could not write document");
}

void Document::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Could not open file for reading");

    std::string signature;
    std::size_t modelCount{};
    if (!(input >> signature >> modelCount) || signature != "MMW1" || modelCount > 100000) {
        throw std::runtime_error("Invalid Model Maker file");
    }

    std::vector<WireframeModel> loaded;
    loaded.reserve(modelCount);
    for (std::size_t i = 0; i < modelCount; ++i) {
        std::size_t vertexCount{}, edgeCount{};
        if (!(input >> vertexCount >> edgeCount) || vertexCount > 1000000 || edgeCount > 2000000) {
            throw std::runtime_error("Invalid model data");
        }
        std::vector<Vec3> vertices(vertexCount);
        std::vector<Edge> edges(edgeCount);
        for (auto& vertex : vertices) {
            if (!(input >> vertex.x >> vertex.y >> vertex.z)) throw std::runtime_error("Invalid vertex data");
        }
        for (auto& edge : edges) {
            if (!(input >> edge.from >> edge.to)) throw std::runtime_error("Invalid edge data");
        }
        loaded.emplace_back(std::move(vertices), std::move(edges));
    }
    models_ = std::move(loaded);
}

} // namespace mm
