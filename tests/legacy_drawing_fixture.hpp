#pragma once
// 2026-10-29: Existing drawing regression fixtures assert the original main/detail
// view counts and paper-space dimensions. Exercise their explicit no-section mode;
// default-on 1:1 sections are covered independently in sections_test.cpp.
#include "model_maker/drawing_set.hpp"

inline mm::drawing::BuildResult legacyBuildSheet(const mm::Document& d, mm::MarkRegistry& r,
                                                  mm::EntityId id, mm::drawing::DrawingOptions o = {}) {
    o.sectionViews = false;
    return mm::drawing::buildSheet(d,r,id,o);
}
inline mm::drawing::DrawingSetResult legacyBuildDrawingSet(
    const mm::Document& d, mm::MarkRegistry& r, mm::EntityId id,
    mm::drawing::DrawingOptions o = {}, mm::drawing::DrawingPreferences p = {},
    const mm::ProgressCallback& progress = {}) {
    o.sectionViews = false;
    return mm::drawing::buildDrawingSet(d,r,id,o,p,progress);
}
