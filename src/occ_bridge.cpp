#ifdef MM_HAS_OCC
#include "model_maker/occ_bridge_api.h"
#include "model_maker/occ_geometry.hpp"

#include <cstdlib>

namespace {
struct Packed {
    float* vertices;
    int vertexCount;
    unsigned int* edges;
    int edgeCount;
};

Packed pack(const mm::WireframeModel& model) {
    Packed result{};
    result.vertexCount = static_cast<int>(model.vertices().size());
    result.edgeCount = static_cast<int>(model.edges().size());
    result.vertices = static_cast<float*>(
        std::malloc(static_cast<std::size_t>(result.vertexCount) * 3 * sizeof(float)));
    result.edges = static_cast<unsigned int*>(
        std::malloc(static_cast<std::size_t>(result.edgeCount) * 2 * sizeof(unsigned int)));
    if (result.vertices) {
        for (int i = 0; i < result.vertexCount; ++i) {
            result.vertices[i * 3 + 0] = static_cast<float>(model.vertices()[static_cast<std::size_t>(i)].x);
            result.vertices[i * 3 + 1] = static_cast<float>(model.vertices()[static_cast<std::size_t>(i)].y);
            result.vertices[i * 3 + 2] = static_cast<float>(model.vertices()[static_cast<std::size_t>(i)].z);
        }
    }
    if (result.edges) {
        for (int i = 0; i < result.edgeCount; ++i) {
            result.edges[i * 2 + 0] = static_cast<unsigned int>(model.edges()[static_cast<std::size_t>(i)].from);
            result.edges[i * 2 + 1] = static_cast<unsigned int>(model.edges()[static_cast<std::size_t>(i)].to);
        }
    }
    return result;
}
} // namespace

extern "C" const char* mm_occ_version(void) { return "7.9.0"; }

extern "C" int mm_occ_solid_box(double dx, double dy, double dz,
                                float** out_vertices, int* out_vertex_count,
                                unsigned int** out_edges, int* out_edge_count) {
    const Packed packed = pack(mm::solidBoxWireframe(dx, dy, dz));
    *out_vertices = packed.vertices;
    *out_vertex_count = packed.vertexCount;
    *out_edges = packed.edges;
    *out_edge_count = packed.edgeCount;
    return 0;
}

extern "C" int mm_occ_solid_cylinder(double radius, double height, int segments,
                                     float** out_vertices, int* out_vertex_count,
                                     unsigned int** out_edges, int* out_edge_count) {
    const Packed packed = pack(mm::solidCylinderWireframe(radius, height, segments));
    *out_vertices = packed.vertices;
    *out_vertex_count = packed.vertexCount;
    *out_edges = packed.edges;
    *out_edge_count = packed.edgeCount;
    return 0;
}

extern "C" void mm_occ_free(void* ptr) { std::free(ptr); }
#endif // MM_HAS_OCC
