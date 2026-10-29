#pragma once
#include "model_maker/drawing.hpp"
#include "model_maker/fabrication.hpp"

namespace mm::drawing {
struct SectionRequest {
    EntityId member{kInvalidEntityId};
    double t{0.0};                     // member start -> section plane, mm
    std::string label;                 // A-A, B-B, ... (assigned after deduplication)
    std::string reason;
    EntityId source{kInvalidEntityId};
};

// All genuine member-axis changes, sorted by member ID then t; points within 1 mm merge.
std::vector<SectionRequest> changePoints(const Document& document,
                                          const std::vector<EntityId>& parts,
                                          const fab::FabricationInventory& inventory);
// Geometric plane intersections of wireframe faces + in-slab wire segments (half-thickness 0.5 mm).
DrawingView generateSectionView(const Document& document, const std::vector<EntityId>& parts,
                                const SectionRequest& request, const fab::MemberDatum& datum);
// Translation-invariant, direction-invariant, deterministic 0.1 mm geometry key.
std::string sectionSignature(const DrawingView& view);
std::string sectionLabel(std::size_t index);
}
