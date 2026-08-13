#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_cube.hpp"
#include "model_maker/ribbon_layout.hpp"
#include "model_maker/renderer.hpp"
#include "model_maker/performance.hpp"
#include "model_maker/dxf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_vector_arithmetic() {
    const mm::Vec3 result = mm::Vec3{1.0, 2.0, 3.0} + mm::Vec3{4.0, -2.0, 1.0};
    require(result == mm::Vec3{5.0, 0.0, 4.0}, "Vec3 addition failed");
}

void test_work_plane_axis_glyph_uses_local_xyz_basis() {
    const mm::WorkPlane plane{{1.0, 2.0, 3.0}, {0.0, 1.0, 0.0},
                              {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};
    const auto glyph = mm::workPlaneAxisGlyph(plane, 2.0);
    require(glyph.origin == plane.origin, "Work-plane XYZ glyph must start at the base point");
    require(glyph.x == mm::Vec3{1.0, 4.0, 3.0}, "Glyph X must follow work-plane U");
    require(glyph.y == mm::Vec3{1.0, 2.0, 5.0}, "Glyph Y must follow work-plane V");
    require(glyph.z == mm::Vec3{3.0, 2.0, 3.0}, "Glyph Z must follow work-plane normal");
}

void test_cube_geometry() {
    const auto cube = mm::WireframeModel::cube(2.0);
    require(cube.vertices().size() == 8, "Cube must have 8 vertices");
    require(cube.edges().size() == 12, "Cube must have 12 edges");
    require(cube.faces().size() == 6 &&
                std::all_of(cube.faces().begin(), cube.faces().end(),
                            [](const auto& face) { return face.size() == 4; }),
            "Cube must expose six quad surfaces for Solid and Transparent rendering");
}

void test_pyramid_geometry() {
    const auto pyramid = mm::WireframeModel::pyramid(2.0, 3.0);
    require(pyramid.vertices().size() == 5, "Pyramid must have 5 vertices");
    require(pyramid.edges().size() == 8, "Pyramid must have 8 edges");
    require(pyramid.faces().size() == 5,
            "Pyramid must expose its base and four side surfaces for filled rendering");
}

void test_3dface_geometry_preserves_four_ordered_corners_and_identity() {
    const std::array<mm::Vec3, 4> corners{{{0.0, 0.0, 0.0}, {4.0, 0.0, 1.0},
                                           {4.0, 3.0, 2.0}, {0.0, 3.0, 1.0}}};
    const auto face = mm::WireframeModel::face3D(corners);
    require(face.isFace3D(), "3DFACE factory must preserve entity identity for DXF export");
    require(face.vertices() == std::vector<mm::Vec3>(corners.begin(), corners.end()),
            "3DFACE must preserve all four selected world-space corners in order");
    require(face.edges() == std::vector<mm::Edge>({{0, 1}, {1, 2}, {2, 3}, {3, 0}}),
            "3DFACE must expose its four boundary edges as one selectable model");
    require(face.faces() == std::vector<std::vector<std::size_t>>({{0, 1, 2, 3}}),
            "3DFACE must expose one fillable four-corner surface");
}

void test_visual_styles_define_wireframe_opaque_and_transparent_face_alpha() {
    require(mm::visualStyleFaceAlpha(mm::VisualStyle::Wireframe) == 0,
            "Wireframe mode must not fill object surfaces");
    require(mm::visualStyleFaceAlpha(mm::VisualStyle::Solid) == 255,
            "Solid mode must render opaque object surfaces");
    require(mm::visualStyleFaceAlpha(mm::VisualStyle::Transparent) > 0 &&
                mm::visualStyleFaceAlpha(mm::VisualStyle::Transparent) < 255,
            "Transparent mode must use partial surface opacity");
}

void test_mirror_preserves_3dface_identity_for_later_dxf_export() {
    const auto face = mm::WireframeModel::face3D(
        {{{0.0, 0.0, 0.0}, {2.0, 0.0, 1.0}, {2.0, 2.0, 2.0}, {0.0, 2.0, 1.0}}});
    const auto mirrored = mm::mirrorModel2D(face, {0.0, -1.0, 0.0}, {0.0, 3.0, 0.0});
    require(mirrored.has_value() && mirrored->isFace3D(),
            "Mirroring a 3DFACE must preserve its entity identity for DXF export");
}

void test_camera_projects_origin_to_view_center() {
    mm::Camera camera;
    const auto point = camera.project({0.0, 0.0, 0.0}, 800, 600);
    require(std::abs(point.x - 400.0) < 0.001, "Projected x must be centered");
    require(std::abs(point.y - 300.0) < 0.001, "Projected y must be centered");
}

void test_camera_zoom_scales_the_initial_2d_view() {
    mm::Camera camera;
    const auto before = camera.project2D({2.0, 0.0, 0.0}, 800, 600);
    camera.zoomBy(2.0);
    const auto after = camera.project2D({2.0, 0.0, 0.0}, 800, 600);

    require(std::abs(before.x - 520.0) < 0.001, "Initial 2D projection scale mismatch");
    require(std::abs(after.x - 640.0) < 0.001,
            "Mouse-wheel camera zoom must scale the initial empty 2D view");
}

void test_camera_2d_projection_round_trips_after_zoom() {
    mm::Camera camera;
    camera.zoomBy(1.75);
    const mm::Vec3 expected{-3.25, 4.5, 0.0};
    const auto screen = camera.project2D(expected, 900, 700);
    const auto world = camera.unproject2D(screen, 900, 700);

    require(std::abs(world.x - expected.x) < 1e-9, "Zoomed 2D unprojection x mismatch");
    require(std::abs(world.y - expected.y) < 1e-9, "Zoomed 2D unprojection y mismatch");
}

void test_camera_2d_pan_tracks_middle_button_screen_displacement() {
    mm::Camera camera;
    constexpr int width = 800;
    constexpr int height = 600;
    camera.pan2DByPixels(75.0, -40.0);

    const auto origin = camera.project2D({0.0, 0.0, 0.0}, width, height);
    require(std::abs(origin.x - 475.0) < 1e-9,
            "2D pan must move drawing geometry by the horizontal mouse displacement");
    require(std::abs(origin.y - 260.0) < 1e-9,
            "2D pan must move drawing geometry by the vertical mouse displacement");
    require(camera.unproject2D(origin, width, height) == mm::Vec3{0.0, 0.0, 0.0},
            "Panned 2D projection and unprojection must remain inverse operations");
}

void test_camera_zoom_extents_fits_world_bounds_with_margin() {
    mm::Camera camera;
    constexpr int width = 1000;
    constexpr int height = 700;
    require(camera.fit2D({-4.0, -2.0, 0.0}, {6.0, 3.0, 0.0}, width, height, 50.0),
            "Valid world bounds must be accepted by Zoom Extents");

    const auto minimum = camera.project2D({-4.0, -2.0, 0.0}, width, height);
    const auto maximum = camera.project2D({6.0, 3.0, 0.0}, width, height);
    require(minimum.x >= 49.999 && maximum.x <= width - 49.999,
            "Zoom Extents must fit horizontal bounds inside the requested margin");
    require(maximum.y >= 49.999 && minimum.y <= height - 49.999,
            "Zoom Extents must fit vertical bounds inside the requested margin");
    const auto center = camera.project2D({1.0, 0.5, 0.0}, width, height);
    require(std::abs(center.x - width * 0.5) < 1e-9 && std::abs(center.y - height * 0.5) < 1e-9,
            "Zoom Extents must center the selected world bounds");
}

void test_camera_fit3d_centers_large_nonplanar_bounds() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Isometric);
    constexpr int width = 1200;
    constexpr int height = 800;
    const mm::Vec3 minimum{10000.0, -5000.0, -2000.0};
    const mm::Vec3 maximum{14000.0, 1000.0, 7000.0};
    require(camera.fit3D(minimum, maximum, width, height, 60.0),
            "Valid 3D bounds must be accepted by 3D Zoom Extents");
    for (double x : {minimum.x, maximum.x}) {
        for (double y : {minimum.y, maximum.y}) {
            for (double z : {minimum.z, maximum.z}) {
                const auto screen = camera.project({x, y, z}, width, height);
                require(screen.x >= 59.9 && screen.x <= width - 59.9 &&
                        screen.y >= 59.9 && screen.y <= height - 59.9,
                        "3D Zoom Extents must keep every bounds corner visible");
            }
        }
    }
    const mm::Vec3 center{12000.0, -2000.0, 2500.0};
    const auto projectedCenter = camera.project(center, width, height);
    require(std::abs(projectedCenter.x - width * 0.5) < 1e-6 &&
            std::abs(projectedCenter.y - height * 0.5) < 1e-6,
            "3D Zoom Extents must center drawings far from the origin");
}

void test_camera_uses_parallel_projection_at_every_depth() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Top);

    const auto nearA = camera.project({0.0, 0.0, -3.0}, 800, 600);
    const auto nearB = camera.project({2.0, 0.0, -3.0}, 800, 600);
    const auto farA = camera.project({0.0, 0.0, 3.0}, 800, 600);
    const auto farB = camera.project({2.0, 0.0, 3.0}, 800, 600);

    const double nearLength = std::hypot(nearB.x - nearA.x, nearB.y - nearA.y);
    const double farLength = std::hypot(farB.x - farA.x, farB.y - farA.y);
    require(std::abs(nearLength - farLength) < 1e-9,
            "Parallel projection must keep equal lengths equal at every depth");
}

void test_camera_standard_view_presets() {
    mm::Camera camera;

    camera.setView(mm::StandardView::Top);
    require(std::abs(camera.yaw()) < 0.001, "Top view yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Top view pitch mismatch");

    camera.setView(mm::StandardView::Bottom);
    require(std::abs(camera.yaw() - 3.141592653589793) < 0.001, "Bottom view yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Bottom view pitch mismatch");

    camera.setView(mm::StandardView::Front);
    require(std::abs(camera.yaw()) < 0.001, "Front view yaw mismatch");
    require(std::abs(camera.pitch() - 1.5707963267948966) < 0.001, "Front view pitch mismatch");

    camera.setView(mm::StandardView::Back);
    require(std::abs(camera.yaw()) < 0.001, "Back view yaw mismatch");
    require(std::abs(camera.pitch() + 1.5707963267948966) < 0.001, "Back view pitch mismatch");

    camera.setView(mm::StandardView::Right);
    require(std::abs(camera.yaw() + 1.5707963267948966) < 0.001, "Right view yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Right view pitch mismatch");

    camera.setView(mm::StandardView::Isometric);
    require(std::abs(camera.yaw()) < 0.001, "Isometric yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Isometric pitch mismatch");
}

void test_view_cube_hit_testing() {
    constexpr int viewportWidth = 1200;
    mm::Camera camera;
    const auto layout = mm::ViewCube::layout(viewportWidth, camera);
    for (const auto& face : layout.faces) {
        if (!face.visible) continue;
        int x = 0;
        int y = 0;
        for (const auto& point : face.points) { x += point.x; y += point.y; }
        x /= static_cast<int>(face.points.size());
        y /= static_cast<int>(face.points.size());
        require(mm::ViewCube::hitTest(x, y, viewportWidth, camera) == face.view,
                "Each visible global ViewCube face must select its matching standard view");
    }
    const int homeX = (layout.homeControl.left + layout.homeControl.right) / 2;
    const int homeY = (layout.homeControl.top + layout.homeControl.bottom) / 2;
    require(mm::ViewCube::hitTest(homeX, homeY, viewportWidth, camera) == mm::StandardView::Isometric,
            "View cube home control must select isometric view");
    require(!mm::ViewCube::hitTest(800, 400, viewportWidth, camera).has_value(),
            "Canvas outside the view cube must not select a view");
}

void test_view_cube_rotates_with_camera_and_global_axes() {
    constexpr int viewportWidth = 1200;
    mm::Camera camera;
    const auto before = mm::ViewCube::layout(viewportWidth, camera);
    camera.rotate(0.42, -0.27);
    const auto after = mm::ViewCube::layout(viewportWidth, camera);

    require(before.corners != after.corners,
            "ViewCube geometry must rotate with the camera instead of remaining a static icon");
    require(before.xAxis != after.xAxis || before.yAxis != after.yAxis || before.zAxis != after.zAxis,
            "The ViewCube XYZ triad must track the global coordinate system while the view rotates");
    require(mm::ViewCube::containsWidget(after.centerX, after.centerY, viewportWidth),
            "The rendered cube body must be available as a manipulation target");
}

void test_view_cube_host_stays_anchored_inside_canvas() {
    const auto large = mm::ViewCube::hostBounds(1200, 800);
    require(large.left == 996 && large.top == 12 && large.right == 1188 && large.bottom == 250,
            "ViewCube host must stay twelve pixels from the canvas top-right corner");

    const auto narrow = mm::ViewCube::hostBounds(240, 160);
    require(narrow.left == 36 && narrow.top == 0 && narrow.right == 228 && narrow.bottom == 160,
            "ViewCube host must remain fully inside a small canvas without resizing horizontally");
}

void test_ribbon_groups_commands_into_function_tabs() {
    const auto file = mm::RibbonLayout::commands(mm::RibbonTab::File);
    const auto drawing = mm::RibbonLayout::commands(mm::RibbonTab::Drawing);
    const auto modify = mm::RibbonLayout::commands(mm::RibbonTab::Modify);
    const auto view = mm::RibbonLayout::commands(mm::RibbonTab::View);

    require(file == std::vector<int>({100, 101, 102, 103, 104, 610, 611, 710, 711, 712, 713, 714, 750, 751, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809}),
            "File tab must expose native and DXF read/write commands");
    require(drawing == std::vector<int>({200, 201, 202, 203, 204, 205}),
            "Drawing tab must contain the drawing tools followed by the Layer Manager command");
    require(modify == std::vector<int>({509, 500, 501, 502, 503, 504, 505, 506, 507, 508, 510, 511, 512}),
            "Modify tab must start with the neutral arrow followed by every modifier");
    require(view == std::vector<int>({300, 301, 302, 303, 304, 305, 306, 307, 308, 900}),
            "View tab must expose dropdown buttons for visual styles, standard camera views, and UCS");
    require(mm::RibbonLayout::commands(mm::RibbonTab::Aids) == std::vector<int>({400, 401, 402, 403, 404}),
            "Aids tab must expose snap settings and F10 Polar Tracking");
}

void test_modifier_point_cursor_policy_matches_interaction_phase() {
    using mm::TransformCommand;
    using mm::TransformPhase;
    require(!mm::modifierUsesPointCursor(TransformCommand::Move, TransformPhase::Selecting),
            "Entity selection must retain the square pickbox cursor");
    require(mm::modifierUsesPointCursor(TransformCommand::Move, TransformPhase::BasePoint),
            "Move base-point selection must use the crosshair cursor");
    require(mm::modifierUsesPointCursor(TransformCommand::Mirror, TransformPhase::Destination),
            "Mirror axis-point selection must use the crosshair cursor");
    require(!mm::modifierUsesPointCursor(TransformCommand::Offset, TransformPhase::BasePoint,
                                         false, false),
            "Numeric Offset distance entry is not a point-pick phase");
    require(mm::modifierUsesPointCursor(TransformCommand::Offset, TransformPhase::Destination,
                                        false, true),
            "Offset side-point selection must use the crosshair cursor");
    require(!mm::modifierUsesPointCursor(TransformCommand::PolarArray, TransformPhase::BasePoint,
                                         false, false) &&
            mm::modifierUsesPointCursor(TransformCommand::PolarArray, TransformPhase::BasePoint,
                                        true, false),
            "Polar Array must switch to crosshair only after item-count entry");
    require(!mm::modifierUsesPointCursor(TransformCommand::Trim, TransformPhase::Destination),
            "Trim target-segment selection must retain the square pickbox cursor");
}

void test_trim_and_extend_remain_active_for_multiple_targets() {
    require(mm::modifierCompletesAfterCommit(mm::TransformCommand::Move) &&
            mm::modifierCompletesAfterCommit(mm::TransformCommand::Copy),
            "Move and Copy must remain single-shot modifiers");
    require(!mm::modifierCompletesAfterCommit(mm::TransformCommand::Trim) &&
            !mm::modifierCompletesAfterCommit(mm::TransformCommand::Extend),
            "Trim and Extend must remain in target-picking mode for multiple objects");
    require(!mm::modifierCompletesAfterCommit(mm::TransformCommand::None),
            "The idle state is not a completed modifier operation");
}

void test_modify_commands_consume_idle_preselection() {
    using mm::ModifierPreselectionAction;
    using mm::TransformCommand;
    require(mm::modifierPreselectionAction(TransformCommand::Move, 2) ==
                ModifierPreselectionAction::BasePoint &&
            mm::modifierPreselectionAction(TransformCommand::Copy, 2) ==
                ModifierPreselectionAction::BasePoint &&
            mm::modifierPreselectionAction(TransformCommand::Mirror, 2) ==
                ModifierPreselectionAction::BasePoint &&
            mm::modifierPreselectionAction(TransformCommand::LinearArray, 2) ==
                ModifierPreselectionAction::BasePoint &&
            mm::modifierPreselectionAction(TransformCommand::PolarArray, 2) ==
                ModifierPreselectionAction::BasePoint,
            "Point-based modifiers must proceed directly to point input with an idle selection");
    require(mm::modifierPreselectionAction(TransformCommand::Offset, 1) ==
                ModifierPreselectionAction::BasePoint &&
            mm::modifierPreselectionAction(TransformCommand::Offset, 2) ==
                ModifierPreselectionAction::SelectEntities,
            "Offset must reuse exactly one preselected entity");
    require(mm::modifierPreselectionAction(TransformCommand::Delete, 2) ==
                ModifierPreselectionAction::DeleteEntities,
            "Delete must immediately consume an idle selection");
    require(mm::modifierPreselectionAction(TransformCommand::Trim, 2) ==
                ModifierPreselectionAction::PickTargets &&
            mm::modifierPreselectionAction(TransformCommand::Extend, 2) ==
                ModifierPreselectionAction::PickTargets,
            "Trim and Extend must use idle selections as boundaries");
    require(mm::modifierPreselectionAction(TransformCommand::Fillet, 1) ==
                ModifierPreselectionAction::PickSecondFilletEntity,
            "Fillet must reuse one idle-selected line as its first entity");
    require(mm::modifierPreselectionAction(TransformCommand::Move, 0) ==
                ModifierPreselectionAction::SelectEntities,
            "A modifier without preselection must retain the normal selection phase");
}

void test_snap_evaluation_requires_an_active_command() {
    using mm::TransformCommand;
    using mm::TransformPhase;
    require(!mm::commandAllowsSnapping(false, TransformCommand::None, TransformPhase::Selecting),
            "Snap must be inactive while the neutral arrow has no command running");
    require(mm::commandAllowsSnapping(true, TransformCommand::None, TransformPhase::Selecting),
            "An active drawing command must allow snap evaluation");
    require(!mm::commandAllowsSnapping(false, TransformCommand::Move, TransformPhase::Selecting),
            "Modifier entity selection must not evaluate object snaps");
    require(mm::commandAllowsSnapping(false, TransformCommand::Move, TransformPhase::BasePoint),
            "Modifier point picking must allow snap evaluation");
    require(mm::commandShowsSnapFeedback(false, false, TransformCommand::LinearArray,
                                         TransformPhase::BasePoint, true, false),
            "3D Linear Array point picking must render its snap marker and tooltip");
    require(mm::commandShowsSnapFeedback(false, false, TransformCommand::PolarArray,
                                         TransformPhase::BasePoint, true, false),
            "3D Polar Array center picking must render its snap marker and tooltip");
}

void test_every_modifier_preserves_the_current_3d_view() {
    using mm::TransformCommand;
    constexpr std::array commands{
        TransformCommand::Move, TransformCommand::Copy, TransformCommand::Offset,
        TransformCommand::Mirror, TransformCommand::Delete, TransformCommand::LinearArray,
        TransformCommand::PolarArray, TransformCommand::Trim, TransformCommand::Extend};
    for (const auto command : commands)
        require(!mm::modifierRequires2DView(command),
                "No modifier may implicitly switch an active 3D view back to 2D");
}

void test_idle_enter_repeat_never_steals_keyboard_point_input() {
    require(mm::shouldRepeatLastModifierOnEnter(false, false, false),
            "Truly idle Enter must repeat the last modifier");
    require(!mm::shouldRepeatLastModifierOnEnter(true, false, false),
            "Enter must commit keyboard coordinates while a drawing command is active");
    require(!mm::shouldRepeatLastModifierOnEnter(false, true, false),
            "Enter must commit a pending dynamic input instead of repeating a modifier");
    require(!mm::shouldRepeatLastModifierOnEnter(false, false, true),
            "Enter must not repeat a modifier while another modal interaction is active");
}

void test_ribbon_compact_buttons_fit_above_full_width_canvas() {
    constexpr int width = 1280;
    const auto layout = mm::RibbonLayout::layout(mm::RibbonTab::Drawing, width);
    require(layout.ribbonHeight >= 140 && layout.ribbonHeight <= 156,
            "AutoCAD-style ribbon must provide title, tab, command, and group-label bands");
    require(layout.canvas.left == 0 && layout.canvas.top == layout.ribbonHeight,
            "Canvas must use the full window width directly below the ribbon");
    require(layout.canvas.right == width, "Ribbon must not reserve a left sidebar");
    for (const auto& button : layout.commandButtons) {
        require(button.rect.right <= width && button.rect.bottom <= layout.ribbonHeight,
                "Every icon command button must fit inside the top ribbon");
        require(button.rect.right - button.rect.left <= 74,
                "Ribbon command buttons must use compact icon-button sizing");
    }
    require(layout.groups.size() == 2 && layout.groups.front().label == L"Draw" &&
                layout.groups.back().label == L"Layers",
            "Drawing tools and Layer Manager must occupy labeled ribbon panels");
    require(layout.groups.front().rect.bottom == layout.ribbonHeight,
            "Ribbon group captions must occupy the bottom of the command band");
}

void test_document_round_trip() {
    mm::Document original;
    original.addModel(mm::WireframeModel::cube(2.0));
    original.addLine({-1.0, 2.0, 0.0}, {3.0, 4.0, 0.0});
    original.addModel(mm::WireframeModel::face3D(
        {{{0.0, 0.0, 0.0}, {2.0, 0.0, 1.0}, {2.0, 2.0, 2.0}, {0.0, 2.0, 1.0}}}));

    const auto path = std::filesystem::temp_directory_path() / "model-maker-test.mmw";
    original.save(path);

    mm::Document loaded;
    loaded.load(path);
    std::filesystem::remove(path);

    require(loaded.models().size() == 3, "Round trip model count mismatch");
    require(loaded.models()[0].edges().size() == 12, "Round trip cube mismatch");
    require(loaded.models()[0].faces().size() == 6,
            "Native round trip must preserve cube faces for Solid/Transparent rendering");
    require(loaded.models()[1].vertices()[1] == mm::Vec3{3.0, 4.0, 0.0}, "Round trip line mismatch");
    require(loaded.models()[2].isFace3D() && loaded.models()[2].faces().size() == 1,
            "Native round trip must preserve explicit 3DFACE identity and fill surface");
}

void test_document_layer_manager_creates_renames_filters_and_deletes_layers() {
    mm::Document document;
    require(document.layers().contains("0"), "Every document must start with layer 0");
    require(document.createLayer("Walls") && document.createLayer("Annotations"),
            "Layer manager must create named layers");
    require(!document.createLayer("Walls") && !document.createLayer(""),
            "Layer names must be non-empty and unique");

    require(document.renameLayer("Walls", "Exterior Walls"),
            "Layer manager must rename an existing layer");
    require(!document.layers().contains("Walls") && document.layers().contains("Exterior Walls"),
            "Renaming must replace the layer key");
    require(!document.renameLayer("0", "Default") && !document.deleteLayer("0"),
            "Layer 0 must not be renamed or deleted");

    const auto filtered = document.layerNames("wall");
    require(filtered == std::vector<std::string>({"Exterior Walls"}),
            "Layer search must be case-insensitive");
    require(document.deleteLayer("Annotations") && !document.layers().contains("Annotations"),
            "Unused layers must be removable");
}

void test_document_layer_manager_resolves_live_layer_properties() {
    mm::Document document;
    require(document.createLayer("DETAIL"), "Test layer creation failed");
    auto layer = document.layers().at("DETAIL");
    layer.visible = false;
    layer.locked = true;
    layer.effectiveColor = 0x123456;
    layer.effectiveLineType = "DASHED";
    layer.effectiveLineWeight = 50;
    layer.transparency = 35;
    layer.description = "Detail geometry";
    document.setLayerProperties(layer);

    auto model = mm::WireframeModel::line({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    mm::EntityProperties entity;
    entity.layer = "DETAIL";
    entity.colorIndex = 256;
    entity.lineType = "BYLAYER";
    entity.lineWeight = -1;
    model.setProperties(entity);
    document.addModel(model);

    const auto effective = document.effectiveProperties(document.models().front());
    require(!effective.visible && effective.locked && effective.effectiveColor == 0x123456 &&
                effective.effectiveLineType == "DASHED" && effective.effectiveLineWeight == 50 &&
                effective.transparency == 35,
            "Entities must resolve visibility, lock, color, linetype, lineweight, and transparency live from their layer");
    require(!document.deleteLayer("DETAIL"), "A layer used by geometry must not be deleted");

    layer.visible = true;
    layer.frozen = true;
    document.setLayerProperties(layer);
    require(!document.effectiveProperties(document.models().front()).visible,
            "Frozen layers must stay hidden even when switched on");
}

void test_hidden_and_locked_layers_are_not_selectable_or_snappable() {
    mm::Document document;
    document.createLayer("LOCKED");
    auto layer = document.layers().at("LOCKED");
    layer.locked = true;
    document.setLayerProperties(layer);
    auto model = mm::WireframeModel::line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "LOCKED";
    model.setProperties(properties);
    document.addModel(model);

    require(!mm::hitTestModel2D({5.0, 0.0, 0.0}, document, 0.1).has_value(),
            "Locked layer geometry must not be selectable");
    const auto lockedSnap = mm::SnapEngine::snap({0.01, 0.01, 0.0}, document, 0.1, 1.0,
                                                 true, false);
    require(lockedSnap.type == mm::SnapType::None,
            "Locked layer geometry must not provide object snaps");

    layer.locked = false;
    layer.visible = false;
    document.setLayerProperties(layer);
    require(!mm::hitTestModel2D({5.0, 0.0, 0.0}, document, 0.1).has_value(),
            "Hidden layer geometry must not be selectable");
    const auto hiddenSnap = mm::SnapEngine::snap({0.01, 0.01, 0.0}, document, 0.1, 1.0,
                                                 true, false);
    require(hiddenSnap.type == mm::SnapType::None,
            "Hidden layer geometry must not provide object snaps");
}

void test_document_changes_layer_for_selected_models_only() {
    mm::Document document;
    require(document.createLayer("DETAIL"), "Selection-property test layer creation failed");
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
    document.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});

    require(document.setModelLayer({0, 2}, "DETAIL") == 2,
            "Layer dropdown must update every selected editable model");
    require(document.models()[0].properties().layer == "DETAIL" &&
                document.models()[1].properties().layer == "0" &&
                document.models()[2].properties().layer == "DETAIL",
            "Layer changes must not affect unselected models");
    require(document.setModelLayer({0, 1}, "MISSING") == 0,
            "Models must not be assigned to an unknown layer");
}

void test_document_changes_color_for_selected_models_and_supports_bylayer() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});

    require(document.setModelColor({0}, 0xFF4040u) == 1,
            "Color dropdown must update selected models");
    require(document.models()[0].properties().trueColor == 0xFF4040u &&
                document.models()[0].properties().effectiveColor == 0xFF4040u &&
                !document.models()[1].properties().trueColor,
            "Explicit color changes must not affect unselected models");
    require(document.setModelColor({0}, std::nullopt) == 1 &&
                !document.models()[0].properties().trueColor &&
                document.models()[0].properties().colorIndex == 256,
            "ByLayer must remove a selected model's explicit color override");
}

void test_document_changes_linetype_for_selected_models_and_supports_bylayer() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});

    require(document.setModelLineType({0}, "DASHED") == 1,
            "Linetype dropdown must update selected models");
    require(document.models()[0].properties().lineType == "DASHED" &&
                document.models()[0].properties().effectiveLineType == "DASHED" &&
                document.models()[1].properties().lineType == "BYLAYER",
            "Linetype changes must not affect unselected models");
    require(document.setModelLineType({0}, "BYLAYER") == 1 &&
                document.models()[0].properties().lineType == "BYLAYER",
            "ByLayer must remove a selected model's linetype override");
}

void test_document_undo_redo_restores_models_and_layers() {
    mm::Document document;
    document.createLayer("Walls");
    document.pushSnapshot();
    document.addLine({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    document.pushSnapshot();
    document.addLine({0.0, 1.0, 0.0}, {5.0, 1.0, 0.0});
    require(document.models().size() == 2, "Setup must create two models before undo");

    require(document.undo(), "Undo must succeed after two snapshots");
    require(document.models().size() == 1, "Undo must remove the second line");
    require(document.undo(), "Second undo must succeed");
    require(document.models().empty(), "Second undo must remove the first line");
    require(!document.undo(), "Undo beyond history must return false");
    require(document.canRedo() && document.redo(), "Redo must restore the first line");
    require(document.models().size() == 1, "Redo must restore exactly one model");
    require(document.redo(), "Second redo must succeed");
    require(document.models().size() == 2, "Second redo must restore both models");
    require(!document.redo(), "Redo beyond history must return false");

    document.pushSnapshot();
    document.addLine({1.0, 1.0, 0.0}, {2.0, 1.0, 0.0});
    require(!document.canRedo(), "A new mutation after undo must clear the redo stack");
    require(document.undo() && document.models().size() == 2,
            "Undo after a fresh mutation must restore the previous state");
}

void test_document_undo_redo_restores_layer_changes() {
    mm::Document document;
    document.pushSnapshot();
    document.createLayer("Walls");
    require(document.layers().contains("Walls"), "Layer must exist after creation");
    require(document.undo(), "Undo must restore layer state");
    require(!document.layers().contains("Walls"), "Undo must remove the created layer");
    require(document.redo(), "Redo must restore the layer");
    require(document.layers().contains("Walls"), "Redo must recreate the layer");

    document.pushSnapshot();
    auto layer = document.layers().at("Walls");
    layer.visible = false;
    document.setLayerProperties(layer);
    require(!document.layers().at("Walls").visible, "Setup must hide the layer");
    require(document.undo(), "Undo must restore visibility");
    require(document.layers().at("Walls").visible, "Undo must restore the visible flag");
    require(document.redo(), "Redo must reapply visibility");
    require(!document.layers().at("Walls").visible, "Redo must restore the hidden flag");
}

void test_document_undo_history_is_bounded() {
    mm::Document document;
    for (int index = 0; index < 200; ++index) {
        document.pushSnapshot();
        document.addLine({static_cast<double>(index), 0.0, 0.0},
                         {static_cast<double>(index) + 1.0, 0.0, 0.0});
    }
    std::size_t undoCount{};
    while (document.undo()) ++undoCount;
    require(undoCount == 100, "Undo history must be bounded to 100 entries");
    document.redo();
    require(document.models().size() == 101,
            "Redo after bounded undo must step forward to the next state");
}

void test_dxf_round_trip_preserves_supported_entities() {
    mm::Document original;
    original.addLine({1.0, 2.0, 3.0}, {4.0, 5.0, 6.0});
    original.addModel(mm::WireframeModel::circle({8.0, -2.0, 1.5}, 3.25, 32));
    original.addModel(mm::WireframeModel::point({-4.0, 7.0, 2.0}));
    const auto path = std::filesystem::temp_directory_path() / "model-maker-roundtrip.dxf";

    mm::DxfFile::write(original, path);
    const mm::Document loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);

    require(loaded.models().size() == 3, "DXF round trip must preserve the entity count");
    require(loaded.models()[0].vertices() == original.models()[0].vertices(),
            "DXF LINE must preserve 3D endpoints");
    require(loaded.models()[1].analyticCenter() == mm::Vec3{8.0, -2.0, 1.5} &&
            loaded.models()[1].analyticRadius() == 3.25,
            "DXF CIRCLE must preserve analytic center and radius");
    require(loaded.models()[2].isPointEntity() &&
            loaded.models()[2].vertices()[0] == mm::Vec3{-4.0, 7.0, 2.0},
            "DXF POINT must preserve point identity and position");
}

void test_dxf_round_trip_writes_and_reads_a_four_corner_3dface_entity() {
    const std::array<mm::Vec3, 4> corners{{{1.0, 2.0, 3.0}, {5.0, 2.0, 4.0},
                                           {5.0, 6.0, 7.0}, {1.0, 6.0, 5.0}}};
    mm::Document original;
    original.addModel(mm::WireframeModel::face3D(corners));
    const auto path = std::filesystem::temp_directory_path() / "model-maker-3dface-roundtrip.dxf";
    mm::DxfFile::write(original, path);
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const mm::Document loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);

    require(text.find("0\n3DFACE\n") != std::string::npos,
            "DXF export must emit a real 3DFACE entity instead of a generic polyline");
    require(loaded.models().size() == 1 && loaded.models().front().isFace3D(),
            "DXF import must preserve 3DFACE entity identity");
    require(loaded.models().front().vertices() == std::vector<mm::Vec3>(corners.begin(), corners.end()),
            "DXF 3DFACE round trip must preserve all four XYZ corners");
}

void test_dxf_round_trip_preserves_entity_properties() {
    mm::WireframeModel line = mm::WireframeModel::line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "DETAIL";
    properties.lineType = properties.effectiveLineType = "DASHED";
    properties.trueColor = properties.effectiveColor = 0x123456;
    properties.lineWeight = properties.effectiveLineWeight = 70;
    properties.lineTypeScale = 2.0;
    properties.transparency = 0x02000040;
    line.setProperties(properties);
    mm::Document original;
    original.addModel(std::move(line));
    const auto path = std::filesystem::temp_directory_path() / "model-maker-properties-roundtrip.dxf";
    mm::DxfFile::write(original, path);
    const mm::Document loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 1, "Styled DXF round trip must preserve entity count");
    const auto& loadedProperties = loaded.models().front().properties();
    require(loadedProperties.layer == "DETAIL" && loadedProperties.trueColor == 0x123456 &&
            loadedProperties.lineWeight == 70 && loadedProperties.lineType == "DASHED" &&
            std::abs(loadedProperties.lineTypeScale - 2.0) < 1e-9 &&
            loadedProperties.transparency == 0x02000040,
            "DXF export must preserve layer, true color, lineweight, linetype, scale, and transparency");
}

void test_dxf_import_defaults_entity_and_rendered_thickness_to_zero() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-zero-thickness.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nENTITIES\n"
                  "0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const auto loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 1, "Zero-thickness fixture must import one entity");
    require(loaded.models().front().properties().thickness == 0.0,
            "DXF entity thickness must default to zero");
    require(loaded.models().front().properties().effectiveLineWeight == 0,
            "Rendered DXF line thickness must default to zero");
}

void test_dxf_reads_closed_lwpolyline() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-polyline.dxf";
    std::ofstream output(path);
    output << "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n8\nWalls\n90\n3\n70\n1\n"
              "10\n0\n20\n0\n10\n5\n20\n0\n10\n5\n20\n4\n0\nENDSEC\n0\nEOF\n";
    output.close();

    const mm::Document loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 1, "One LWPOLYLINE must import as one selectable entity");
    require(loaded.models()[0].vertices().size() == 3 && loaded.models()[0].edges().size() == 3,
            "Closed LWPOLYLINE must preserve its vertices and closing edge");
    require(loaded.models()[0].edges().back() == mm::Edge{2, 0},
            "Closed LWPOLYLINE must connect its final vertex to its first vertex");
}

void test_dxf_insert_expands_block_with_base_scale_rotation_and_byblock_style() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-block-insert.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
                  "0\nLAYER\n2\nPIPES\n70\n0\n62\n1\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
                  "0\nSECTION\n2\nBLOCKS\n"
                  "0\nBLOCK\n2\nVALVE\n10\n10\n20\n20\n30\n0\n"
                  "0\nLINE\n8\n0\n62\n0\n10\n10\n20\n20\n11\n12\n21\n20\n"
                  "0\nENDBLK\n0\nENDSEC\n"
                  "0\nSECTION\n2\nENTITIES\n"
                  "0\nINSERT\n2\nVALVE\n8\nPIPES\n62\n3\n10\n100\n20\n200\n"
                  "41\n2\n42\n3\n43\n1\n50\n90\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const auto loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 1, "INSERT must expand its referenced block geometry");
    const auto& line = loaded.models().front();
    require(std::abs(line.vertices()[0].x - 100.0) < 1e-9 &&
            std::abs(line.vertices()[0].y - 200.0) < 1e-9 &&
            std::abs(line.vertices()[1].x - 100.0) < 1e-9 &&
            std::abs(line.vertices()[1].y - 204.0) < 1e-9,
            "INSERT must honor block base point, nonuniform scale, rotation, and insertion point");
    require(line.properties().layer == "PIPES" && line.properties().effectiveColor == 0x00FF00,
            "Layer 0 and BYBLOCK color inside a block must inherit from INSERT");
    require(line.properties().thickness == 0.0 && line.properties().effectiveLineWeight == 0,
            "Expanded block geometry must preserve zero default entity and rendered thickness");
}

void test_dxf_insert_expands_nested_blocks() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-nested-block.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nBLOCKS\n"
                  "0\nBLOCK\n2\nLEAF\n10\n0\n20\n0\n"
                  "0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDBLK\n"
                  "0\nBLOCK\n2\nPARENT\n10\n0\n20\n0\n"
                  "0\nINSERT\n2\nLEAF\n10\n2\n20\n0\n50\n90\n0\nENDBLK\n"
                  "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                  "0\nINSERT\n2\nPARENT\n10\n10\n20\n5\n41\n2\n42\n2\n43\n2\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const auto loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 1, "Nested INSERT must recursively expand referenced blocks");
    const auto& vertices = loaded.models().front().vertices();
    require(std::abs(vertices[0].x - 14.0) < 1e-9 && std::abs(vertices[0].y - 5.0) < 1e-9 &&
            std::abs(vertices[1].x - 14.0) < 1e-9 && std::abs(vertices[1].y - 7.0) < 1e-9,
            "Nested block transforms must compose in parent-to-child order");
}

void test_dxf_insert_rejects_undefined_block_reference() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-undefined-block.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nENTITIES\n"
                  "0\nINSERT\n2\nMISSING\n10\n0\n20\n0\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    bool rejected = false;
    try {
        (void)mm::DxfFile::read(path);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("undefined block") != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "INSERT referencing an undefined block must report a parser error");
}

void test_dxf_insert_rejects_recursive_block_references() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-recursive-block.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nBLOCKS\n"
                  "0\nBLOCK\n2\nA\n10\n0\n20\n0\n0\nINSERT\n2\nB\n10\n0\n20\n0\n0\nENDBLK\n"
                  "0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nINSERT\n2\nA\n10\n0\n20\n0\n0\nENDBLK\n"
                  "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                  "0\nINSERT\n2\nA\n10\n0\n20\n0\n0\nENDSEC\n0\nEOF\n";
    }
    bool rejected = false;
    try {
        (void)mm::DxfFile::read(path);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("Cyclic DXF block reference") != std::string::npos;
    }
    std::filesystem::remove(path);
    require(rejected, "Recursive INSERT chains must be rejected instead of expanding forever");
}

void test_dxf_dimension_displays_generated_block_lines_arrows_and_text() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-dimension.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nBLOCKS\n"
                  "0\nBLOCK\n2\n*D1\n10\n0\n20\n0\n"
                  "0\nLINE\n8\nDIM\n10\n2\n20\n3\n11\n12\n21\n3\n"
                  "0\nSOLID\n8\nDIM\n10\n2\n20\n3\n11\n3\n21\n3.5\n12\n3\n22\n2.5\n13\n2\n23\n3\n"
                  "0\nMTEXT\n8\nDIM\n10\n7\n20\n4\n40\n1\n71\n5\n1\n10.0\n"
                  "0\nENDBLK\n0\nENDSEC\n"
                  "0\nSECTION\n2\nENTITIES\n"
                  "0\nDIMENSION\n2\n*D1\n8\nDIM\n10\n100\n20\n200\n11\n7\n21\n4\n70\n0\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const auto loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() >= 3,
            "DIMENSION must expand its generated block line, arrow outline, and text strokes");
    require(loaded.models()[0].vertices()[0] == mm::Vec3{2.0, 3.0, 0.0} &&
            loaded.models()[0].vertices()[1] == mm::Vec3{12.0, 3.0, 0.0},
            "DIMENSION block graphics are already in drawing coordinates and must not move to definition point 10");
    require(std::any_of(loaded.models().begin(), loaded.models().end(), [](const auto& model) {
                return model.edges().size() >= 4 && model.vertices().size() >= 4;
            }), "DIMENSION display must include arrow or text wireframe geometry");
}

void test_dxf_import_preserves_layer_color_lineweight_and_linetype() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-properties.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
                  "0\nLAYER\n2\nPIPES\n70\n0\n62\n1\n6\nDASHED\n370\n50\n"
                  "0\nENDTAB\n0\nENDSEC\n"
                  "0\nSECTION\n2\nENTITIES\n"
                  "0\nLINE\n8\nPIPES\n10\n0\n20\n0\n11\n10\n21\n0\n"
                  "0\nLINE\n8\nDETAIL\n420\n1193046\n370\n100\n39\n3.5\n6\nCENTER\n48\n2.5\n"
                  "10\n0\n20\n2\n11\n10\n21\n2\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const mm::Document loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models().size() == 2, "Property fixture must import both entities");
    const auto& inherited = loaded.models()[0].properties();
    require(inherited.layer == "PIPES" && inherited.effectiveColor == 0xFF0000 &&
            inherited.effectiveLineWeight == 50 && inherited.effectiveLineType == "DASHED",
            "Entity must inherit color, lineweight, and linetype from its DXF layer");
    const auto& explicitStyle = loaded.models()[1].properties();
    require(explicitStyle.layer == "DETAIL" && explicitStyle.trueColor == 0x123456 &&
            explicitStyle.effectiveColor == 0x123456 && explicitStyle.lineWeight == 100 &&
            explicitStyle.effectiveLineType == "CENTER" &&
            std::abs(explicitStyle.lineTypeScale - 2.5) < 1e-9 &&
            std::abs(explicitStyle.thickness - 3.5) < 1e-9,
            "Entity-level true color, lineweight, linetype, and scale must override defaults");
    const auto exported = std::filesystem::temp_directory_path() / "model-maker-layer-roundtrip.dxf";
    mm::DxfFile::write(loaded, exported);
    const auto reloaded = mm::DxfFile::read(exported);
    std::filesystem::remove(exported);
    require(reloaded.models()[0].properties().colorIndex == 256 &&
            reloaded.models()[0].properties().effectiveColor == 0xFF0000 &&
            reloaded.models()[0].properties().effectiveLineWeight == 50 &&
            reloaded.models()[0].properties().effectiveLineType == "DASHED",
            "DXF layer table and BYLAYER semantics must survive export and re-import");
}

void test_dxf_import_resolves_extended_aci_and_hidden_layers() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-aci.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
                  "0\nLAYER\n2\nORANGE\n70\n0\n62\n30\n6\nCONTINUOUS\n"
                  "0\nLAYER\n2\nFROZEN\n70\n1\n62\n3\n6\nCONTINUOUS\n"
                  "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                  "0\nLINE\n8\nORANGE\n10\n0\n20\n0\n11\n1\n21\n0\n"
                  "0\nLINE\n8\nFROZEN\n10\n0\n20\n1\n11\n1\n21\n1\n"
                  "0\nENDSEC\n0\nEOF\n";
    }
    const auto loaded = mm::DxfFile::read(path);
    std::filesystem::remove(path);
    require(loaded.models()[0].properties().effectiveColor == 0xFF8000,
            "Extended AutoCAD Color Index colors must resolve to RGB");
    require(!loaded.effectiveProperties(loaded.models()[1]).visible,
            "Entities on frozen DXF layers must remain hidden");
}

void test_dxf_import_can_be_cancelled_before_large_parse() {
    const auto path = std::filesystem::temp_directory_path() / "model-maker-cancel.dxf";
    {
        std::ofstream output(path);
        output << "0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n0\n20\n0\n11\n1\n21\n1\n0\nENDSEC\n0\nEOF\n";
    }
    std::stop_source stop;
    stop.request_stop();
    bool cancelled = false;
    try {
        (void)mm::DxfFile::read(path, stop.get_token(), {});
    } catch (const mm::DxfImportCancelled&) {
        cancelled = true;
    }
    std::filesystem::remove(path);
    require(cancelled, "DXF reader must honor cancellation instead of blocking the caller");
}

void test_document_spatial_index_limits_large_drawing_queries() {
    mm::Document document;
    constexpr std::size_t entityCount = 50'000;
    document.reserveModels(entityCount);
    for (std::size_t i = 0; i < entityCount; ++i) {
        const double x = static_cast<double>(i);
        document.addLine({x, 0.0, 0.0}, {x + 0.5, 1.0, 0.0});
    }

    const auto bounds = document.bounds();
    require(bounds.has_value() && bounds->minimum == mm::Vec3{0.0, 0.0, 0.0} &&
            bounds->maximum == mm::Vec3{49999.5, 1.0, 0.0},
            "Large document bounds must be cached accurately");
    const auto& modelBounds = document.modelBounds();
    require(modelBounds.size() == entityCount &&
            modelBounds[25000].minimum == mm::Vec3{25000.0, 0.0, 0.0} &&
            modelBounds[25000].maximum == mm::Vec3{25000.5, 1.0, 0.0},
            "The cached per-model bounds must be available for projected viewport filtering");
    const auto filteredBounds = document.queryBounds([](const mm::Bounds3& candidate) {
        return candidate.maximum.x >= 24995.0 && candidate.minimum.x <= 25005.0;
    });
    require(filteredBounds.size() == 11 && filteredBounds.front() == 24995 && filteredBounds.back() == 25005,
            "Generic bounds queries must prune the spatial tree and return exact model indices");
    const auto nearby = document.query2D({24995.0, -1.0, 0.0}, {25005.0, 2.0, 0.0});
    require(nearby.size() == 11 && nearby.front() == 24995 && nearby.back() == 25005,
            "Spatial query must return only nearby entities from a large drawing");
}

void test_document_moves_selected_models_by_displacement() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    document.addModel(mm::WireframeModel::circle({5.0, 5.0, 0.0}, 2.0, 16));

    document.moveModels({0}, {3.0, -2.0, 1.0});

    require(document.models().size() == 2, "Move must not change the entity count");
    require(document.models()[0].vertices()[0] == mm::Vec3{3.0, -2.0, 1.0},
            "Move must translate the selected entity from its original location");
    require(document.models()[0].vertices()[1] == mm::Vec3{5.0, -2.0, 1.0},
            "Move must preserve the selected entity geometry");
    require(document.models()[1].analyticCenter() == mm::Vec3{5.0, 5.0, 0.0},
            "Move must leave unselected entities unchanged");
}

void test_document_copies_selected_models_and_preserves_metadata() {
    mm::Document document;
    document.addModel(mm::WireframeModel::circle({1.0, 2.0, 0.0}, 3.0, 16));

    document.copyModels({0}, {4.0, -1.0, 2.0});

    require(document.models().size() == 2, "Copy must append one entity for each selection");
    require(document.models()[0].analyticCenter() == mm::Vec3{1.0, 2.0, 0.0},
            "Copy must retain the original entity");
    require(document.models()[1].analyticCenter() == mm::Vec3{5.0, 1.0, 2.0},
            "Copy must translate analytic metadata with the cloned entity");
    require(document.models()[1].analyticRadius() == document.models()[0].analyticRadius(),
            "Copy must preserve analytic radius metadata");
}

void test_document_deletes_only_selected_models() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
    document.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});
    document.deleteModels({2, 0, 2, 999});
    require(document.models().size() == 1,
            "Delete must remove each valid selected model once and ignore invalid indices");
    require(document.models().front().vertices().front() == mm::Vec3{0.0, 1.0, 0.0},
            "Delete must retain unselected model geometry");
}

void test_document_replaces_one_model_with_trimmed_segments_in_place() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {10.0, 1.0, 0.0});
    document.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});
    std::vector<mm::WireframeModel> replacements;
    replacements.push_back(mm::WireframeModel::line({0.0, 1.0, 0.0}, {3.0, 1.0, 0.0}));
    replacements.push_back(mm::WireframeModel::line({7.0, 1.0, 0.0}, {10.0, 1.0, 0.0}));
    document.replaceModel(1, std::move(replacements));
    require(document.models().size() == 4,
            "Replacing one target with two trimmed segments must grow the document by one");
    require(document.models()[1].vertices()[1] == mm::Vec3{3.0, 1.0, 0.0} &&
            document.models()[2].vertices()[0] == mm::Vec3{7.0, 1.0, 0.0} &&
            document.models()[3].vertices()[0] == mm::Vec3{0.0, 2.0, 0.0},
            "Trim replacement segments must occupy the original target position");
}

void test_linear_array_creates_evenly_spaced_property_preserving_copies() {
    auto source = mm::WireframeModel::line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "ARRAY";
    properties.effectiveColor = 0xFF4040;
    source.setProperties(properties);
    const auto copies = mm::linearArray2D(source, 4, {2.0, -1.0, 0.0});
    require(copies.size() == 3, "A four-item linear array must generate three copies");
    require(copies[0].vertices()[0] == mm::Vec3{2.0, -1.0, 0.0} &&
            copies[2].vertices()[1] == mm::Vec3{7.0, -3.0, 0.0},
            "Linear array copies must use integer multiples of the spacing vector");
    require(copies[2].properties() == properties,
            "Linear array copies must preserve source properties");
}

void test_polar_array_distributes_copies_around_center_and_preserves_metadata() {
    auto source = mm::WireframeModel::circle({2.0, 0.0, 0.0}, 0.5, 32);
    mm::EntityProperties properties;
    properties.layer = "POLAR";
    source.setProperties(properties);
    const auto copies = mm::polarArray2D(source, 4, {0.0, 0.0, 0.0});
    require(copies.size() == 3, "A four-item polar array must generate three copies");
    require(copies[0].analyticCenter().has_value() &&
            std::abs(copies[0].analyticCenter()->x) < 1e-9 &&
            std::abs(copies[0].analyticCenter()->y - 2.0) < 1e-9,
            "First polar copy must rotate by one equal angular step");
    require(copies[1].analyticCenter().has_value() &&
            std::abs(copies[1].analyticCenter()->x + 2.0) < 1e-9 &&
            std::abs(copies[1].analyticCenter()->y) < 1e-9,
            "Polar array must distribute copies over a full circle");
    require(copies[2].analyticRadius() == source.analyticRadius() &&
            copies[2].properties() == properties,
            "Polar array must preserve analytic and rendering metadata");
}

void test_trim_line_removes_the_picked_side_at_cutting_edge() {
    auto target = mm::WireframeModel::line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "TRIMMED";
    properties.effectiveColor = 0x40A0FF;
    target.setProperties(properties);
    const auto boundary = mm::WireframeModel::line({4.0, -2.0, 0.0}, {4.0, 2.0, 0.0});
    const auto result = mm::trimLine2D(target, {boundary}, {2.0, 0.0, 0.0});
    require(result.has_value() && result->size() == 1,
            "Trim must produce the portion opposite the picked side");
    require((*result)[0].vertices()[0] == mm::Vec3{4.0, 0.0, 0.0} &&
            (*result)[0].vertices()[1] == mm::Vec3{10.0, 0.0, 0.0},
            "Trim must cut the target at its cutting-edge intersection");
    require((*result)[0].properties() == properties,
            "Trimmed geometry must preserve source properties");
}

void test_extend_line_moves_picked_endpoint_to_boundary() {
    auto target = mm::WireframeModel::line({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "EXTENDED";
    properties.effectiveLineType = "DASHED";
    target.setProperties(properties);
    const auto boundary = mm::WireframeModel::line({10.0, -2.0, 0.0}, {10.0, 2.0, 0.0});
    const auto result = mm::extendLine2D(target, {boundary}, {3.5, 0.0, 0.0});
    require(result.has_value(), "Extend must find a cutting edge beyond the picked endpoint");
    require(result->vertices()[0] == mm::Vec3{0.0, 0.0, 0.0} &&
            result->vertices()[1] == mm::Vec3{10.0, 0.0, 0.0},
            "Extend must move only the picked endpoint to the nearest boundary");
    require(result->properties() == properties,
            "Extended geometry must preserve source properties");
}

void test_fillet_trims_two_picked_line_arms_and_adds_tangent_arc() {
    auto horizontal = mm::WireframeModel::line({-5.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    auto vertical = mm::WireframeModel::line({0.0, -5.0, 0.0}, {0.0, 5.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "FILLET";
    properties.effectiveColor = 0xFF8844;
    horizontal.setProperties(properties);
    vertical.setProperties(properties);

    const auto result = mm::filletLinesOnPlane(horizontal, {4.0, 0.0, 0.0},
                                                vertical, {0.0, 4.0, 0.0},
                                                1.0, mm::WorkPlane{});
    require(result.has_value(), "Perpendicular lines must support a positive-radius fillet");
    const auto closeEnough = [](const mm::Vec3& a, const mm::Vec3& b) {
        return std::hypot(a.x - b.x, a.y - b.y) < 1e-8 && std::abs(a.z - b.z) < 1e-8;
    };
    require(closeEnough(result->first.vertices()[0], mm::Vec3{5.0, 0.0, 0.0}) &&
            closeEnough(result->first.vertices()[1], mm::Vec3{1.0, 0.0, 0.0}),
            "Fillet must retain the picked horizontal arm and trim it at tangency");
    require(closeEnough(result->second.vertices()[0], mm::Vec3{0.0, 5.0, 0.0}) &&
            closeEnough(result->second.vertices()[1], mm::Vec3{0.0, 1.0, 0.0}),
            "Fillet must retain the picked vertical arm and trim it at tangency");
    require(closeEnough(result->arc.vertices().front(), mm::Vec3{1.0, 0.0, 0.0}) &&
            closeEnough(result->arc.vertices().back(), mm::Vec3{0.0, 1.0, 0.0}),
            "Fillet arc endpoints must equal both tangent points");
    require(result->first.properties() == properties && result->second.properties() == properties &&
            result->arc.properties() == properties,
            "Fillet replacements and arc must preserve source rendering properties");
}

void test_current_entity_style_resolves_layer_color_and_linetype_choices() {
    std::unordered_map<std::string, mm::EntityProperties> layers;
    mm::EntityProperties wallLayer;
    wallLayer.layer = "Walls";
    wallLayer.effectiveColor = 0xCC4422;
    wallLayer.effectiveLineType = "DASHED";
    layers.emplace(wallLayer.layer, wallLayer);

    const auto byLayer = mm::resolveEntityStyle({"Walls", std::nullopt, "BYLAYER"}, layers);
    require(byLayer.layer == "Walls" && byLayer.colorIndex == 256 && !byLayer.trueColor,
            "Layer dropdown must preserve BYLAYER color semantics");
    require(byLayer.effectiveColor == 0xCC4422 && byLayer.effectiveLineType == "DASHED",
            "BYLAYER selections must resolve the selected layer's effective style");

    const auto explicitStyle = mm::resolveEntityStyle({"Walls", 0x00FF00u, "CENTER"}, layers);
    require(explicitStyle.trueColor == 0x00FF00u && explicitStyle.effectiveColor == 0x00FF00u,
            "Color dropdown selection must override the layer color");
    require(explicitStyle.lineType == "CENTER" && explicitStyle.effectiveLineType == "CENTER",
            "Linetype dropdown selection must override the layer linetype");
}

void test_offset_line_creates_parallel_copy_on_selected_side() {
    mm::WireframeModel line = mm::WireframeModel::line({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    mm::EntityProperties properties;
    properties.layer = "OFFSET_SOURCE";
    line.setProperties(properties);

    const auto above = mm::offsetModel2D(line, 2.0, {1.0, 5.0, 0.0});
    const auto below = mm::offsetModel2D(line, 2.0, {1.0, -5.0, 0.0});

    require(above.has_value() && below.has_value(), "A straight line must support 2D offset");
    require(above->vertices()[0] == mm::Vec3{0.0, 2.0, 0.0} &&
            above->vertices()[1] == mm::Vec3{4.0, 2.0, 0.0},
            "Offset must place a parallel line at the requested distance on the picked side");
    require(below->vertices()[0] == mm::Vec3{0.0, -2.0, 0.0} &&
            below->vertices()[1] == mm::Vec3{4.0, -2.0, 0.0},
            "Offset side selection must work on either side of the source line");
    require(above->properties() == properties, "Offset geometry must preserve entity properties");
}

void test_offset_circle_uses_inside_or_outside_pick() {
    const auto circle = mm::WireframeModel::circle({3.0, 4.0, 0.0}, 5.0, 32);
    const auto outside = mm::offsetModel2D(circle, 2.0, {10.0, 4.0, 0.0});
    const auto inside = mm::offsetModel2D(circle, 2.0, {3.0, 4.0, 0.0});
    require(outside && outside->analyticRadius() == 7.0,
            "Picking outside a circle must increase its radius by the offset distance");
    require(inside && inside->analyticRadius() == 3.0,
            "Picking inside a circle must decrease its radius by the offset distance");
}

void test_mirror_reflects_geometry_across_two_point_axis() {
    mm::EntityProperties properties;
    properties.layer = "MIRROR_TEST";
    auto source = mm::WireframeModel::line({1.0, 2.0, 0.0}, {3.0, 4.0, 0.0});
    source.setProperties(properties);
    const auto mirrored = mm::mirrorModel2D(source, {0.0, -5.0, 0.0}, {0.0, 5.0, 0.0});
    require(mirrored.has_value(), "A non-degenerate mirror axis must produce reflected geometry");
    require(mirrored->vertices()[0] == mm::Vec3{-1.0, 2.0, 0.0} &&
            mirrored->vertices()[1] == mm::Vec3{-3.0, 4.0, 0.0},
            "Mirror must reflect every vertex across the selected axis");
    require(mirrored->properties() == properties, "Mirror must preserve entity properties");
}

void test_rotate_rotates_geometry_around_center_point() {
    mm::EntityProperties properties;
    properties.layer = "ROTATE_TEST";
    auto source = mm::WireframeModel::line({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    source.setProperties(properties);
    const auto rotated = mm::rotateModel2D(source, {0.0, 0.0, 0.0}, 90.0);
    require(rotated.has_value(), "Rotation must produce geometry");
    require(std::abs(rotated->vertices()[0].x - 0.0) < 1e-9 &&
            std::abs(rotated->vertices()[0].y - 1.0) < 1e-9 &&
            std::abs(rotated->vertices()[1].x - (-1.0)) < 1e-9 &&
            std::abs(rotated->vertices()[1].y - 0.0) < 1e-9,
            "90-degree rotation around origin must map (1,0)→(0,1) and (0,1)→(-1,0)");
    require(rotated->properties() == properties, "Rotate must preserve entity properties");
}

void test_rotate_preserves_analytic_center_and_radius() {
    auto circle = mm::WireframeModel::circle({3.0, 0.0, 0.0}, 2.5, 32);
    const auto rotated = mm::rotateModel2D(circle, {0.0, 0.0, 0.0}, 90.0);
    require(rotated.has_value(), "Circle rotation must succeed");
    require(rotated->analyticCenter().has_value() && rotated->analyticRadius().has_value(),
            "Rotated circle must preserve analytic center and radius");
    require(std::abs(rotated->analyticCenter()->x - 0.0) < 1e-9 &&
            std::abs(rotated->analyticCenter()->y - 3.0) < 1e-9 &&
            std::abs(*rotated->analyticRadius() - 2.5) < 1e-9,
            "Circle center (3,0) rotated 90° around origin must become (0,3)");
}

void test_entity_hit_test_selects_nearest_model_edge() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    document.addLine({0.0, 2.0, 0.0}, {4.0, 2.0, 0.0});

    require(mm::hitTestModel2D({2.0, 1.92, 0.0}, document, 0.15) == 1,
            "Selection hit testing must return the nearest entity edge");
    require(!mm::hitTestModel2D({2.0, 1.0, 0.0}, document, 0.15),
            "Selection hit testing must reject points outside tolerance");
}

void test_left_to_right_window_selects_only_fully_contained_models() {
    mm::Document document;
    document.addLine({1.0, 1.0, 0.0}, {3.0, 2.0, 0.0});
    document.addLine({2.0, 3.0, 0.0}, {6.0, 3.0, 0.0});
    document.addLine({7.0, 1.0, 0.0}, {8.0, 2.0, 0.0});
    const auto selected = mm::selectModelsInRect2D({0.0, 0.0, 0.0}, {5.0, 5.0, 0.0}, document, false);
    require(selected == std::vector<std::size_t>{0},
            "Left-to-right window selection must select only fully contained objects");
}

void test_right_to_left_crossing_selects_touching_and_contained_models() {
    mm::Document document;
    document.addLine({1.0, 1.0, 0.0}, {3.0, 2.0, 0.0});
    document.addLine({2.0, 3.0, 0.0}, {6.0, 3.0, 0.0});
    document.addLine({7.0, 1.0, 0.0}, {8.0, 2.0, 0.0});
    const auto selected = mm::selectModelsInRect2D({5.0, 5.0, 0.0}, {0.0, 0.0, 0.0}, document, true);
    require(selected == std::vector<std::size_t>({0, 1}),
            "Right-to-left crossing selection must select contained and boundary-touching objects");
}

void test_crossing_selection_chooses_the_target_portion_inside_the_window() {
    const auto target = mm::WireframeModel::line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const auto pick = mm::crossingSelectionPickPoint2D(target, {4.0, 1.0, 0.0},
                                                       {2.0, -1.0, 0.0});
    require(pick.has_value(), "Crossing target selection must find the line portion inside the window");
    require(std::abs(pick->x - 3.0) < 1e-9 && std::abs(pick->y) < 1e-9,
            "Crossing target pick must use the midpoint of the clipped line portion");
}

void test_projected_3d_crossing_selection_preserves_the_world_target_point() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Top);
    const auto target = mm::WireframeModel::line({-4.0, 0.0, 2.0}, {4.0, 0.0, 2.0});
    const auto first = camera.project({1.0, 1.0, 2.0}, 800, 600);
    const auto second = camera.project({-1.0, -1.0, 2.0}, 800, 600);
    const auto pick = mm::crossingSelectionPickPoint3D(target, first, second, camera, 800, 600);
    require(pick.has_value(), "Projected 3D crossing target selection must find the clipped line");
    require(std::abs(pick->x) < 1e-9 && std::abs(pick->y) < 1e-9 &&
                std::abs(pick->z - 2.0) < 1e-9,
            "Projected crossing selection must interpolate the original world-space line");
}

void test_projected_3d_window_and_crossing_selection() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Top);
    mm::Document document;
    document.addLine({-1.0, 0.0, 2.0}, {1.0, 0.0, 2.0});
    document.addLine({0.0, -3.0, -2.0}, {0.0, 3.0, -2.0});
    const auto a = camera.project({-2.0, -1.0, 0.0}, 800, 600);
    const auto b = camera.project({2.0, 1.0, 0.0}, 800, 600);
    const auto window = mm::selectModelsInRect3D(a, b, document, camera, 800, 600, false);
    require(window == std::vector<std::size_t>{0},
            "3D window selection must use projected geometry and require containment");
    const auto crossing = mm::selectModelsInRect3D(b, a, document, camera, 800, 600, true);
    require(crossing == std::vector<std::size_t>{0, 1},
            "3D crossing selection must select intersecting and contained entities");
}

void test_snap_prefers_nearby_endpoint() {
    mm::Document document;
    document.addLine({1.0, 2.0, 0.0}, {4.0, 2.0, 0.0});
    const auto result = mm::SnapEngine::snap({1.08, 2.04, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::Endpoint, "Nearby vertex must endpoint-snap");
    require(result.point == mm::Vec3{1.0, 2.0, 0.0}, "Endpoint snap coordinate mismatch");
}

void test_snap_marker_symbols_match_cad_reference_conventions() {
    using mm::SnapMarkerSymbol;
    require(mm::snapMarkerSymbol(mm::SnapType::Endpoint) == SnapMarkerSymbol::Square,
            "Endpoint snap must use a square marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Midpoint) == SnapMarkerSymbol::Triangle,
            "Midpoint snap must use a triangle marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Center) == SnapMarkerSymbol::Circle,
            "Center snap must use a circle marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Node) == SnapMarkerSymbol::CrossedCircle,
            "Node snap must use a crossed-circle marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Quadrant) == SnapMarkerSymbol::Diamond,
            "Quadrant snap must use a diamond marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Intersection) == SnapMarkerSymbol::Cross,
            "Intersection snap must use an X marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Insertion) == SnapMarkerSymbol::LinkedSquares,
            "Insertion snap must use linked squares");
    require(mm::snapMarkerSymbol(mm::SnapType::Perpendicular) == SnapMarkerSymbol::RightAngle,
            "Perpendicular snap must use a right-angle marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Tangent) == SnapMarkerSymbol::TangentCircle,
            "Tangent snap must use a tangent circle marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Nearest) == SnapMarkerSymbol::Hourglass,
            "Nearest snap must use an hourglass marker");
    require(mm::snapMarkerSymbol(mm::SnapType::ApparentIntersection) == SnapMarkerSymbol::BoxedCross,
            "Apparent intersection snap must use a boxed X marker");
    require(mm::snapMarkerSymbol(mm::SnapType::Parallel) == SnapMarkerSymbol::ParallelLines,
            "Parallel snap must use two parallel strokes");
}

void test_snap_finds_edge_midpoint() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    const auto result = mm::SnapEngine::snap({2.04, 0.03, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::Midpoint, "Edge center must midpoint-snap");
    require(result.point == mm::Vec3{2.0, 0.0, 0.0}, "Midpoint snap coordinate mismatch");
}

void test_snap_respects_individual_type_enablement() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    mm::SnapTypeMask enabled{};
    enabled[static_cast<std::size_t>(mm::SnapType::Midpoint)] = true;
    const auto midpoint = mm::SnapEngine::snap({2.04, 0.03, 0.0}, document, 0.15, 1.0,
                                                true, false, std::nullopt, &enabled);
    require(midpoint.type == mm::SnapType::Midpoint,
            "An individually enabled midpoint snap must remain available");
    enabled[static_cast<std::size_t>(mm::SnapType::Midpoint)] = false;
    const auto disabled = mm::SnapEngine::snap({2.04, 0.03, 0.0}, document, 0.15, 1.0,
                                                true, false, std::nullopt, &enabled);
    require(disabled.type == mm::SnapType::None,
            "An individually disabled snap type must not be selected");
}

void test_snap_finds_line_intersection() {
    mm::Document document;
    document.addLine({-2.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    document.addLine({0.0, -2.0, 0.0}, {0.0, 2.0, 0.0});
    const auto result = mm::SnapEngine::snap({0.04, 0.03, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::Intersection, "Crossing edges must intersection-snap");
    require(result.point == mm::Vec3{0.0, 0.0, 0.0}, "Intersection coordinate mismatch");
}

void test_snap_finds_circle_center_and_quadrant() {
    mm::Document document;
    document.addModel(mm::WireframeModel::circle({3.0, 4.0, 0.0}, 2.0, 32));
    const auto center = mm::SnapEngine::snap({3.03, 4.02, 0.0}, document, 0.15, 1.0);
    require(center.type == mm::SnapType::Center, "Circle center must center-snap");
    const auto quadrant = mm::SnapEngine::snap({5.04, 4.01, 0.0}, document, 0.15, 1.0);
    require(quadrant.type == mm::SnapType::Quadrant, "Circle cardinal point must quadrant-snap");
    require(std::abs(quadrant.point.x - 5.0) < 1e-9 && std::abs(quadrant.point.y - 4.0) < 1e-9,
            "Quadrant coordinate mismatch");
}

void test_snap_finds_polygon_geometric_center() {
    mm::Document document;
    document.addModel(mm::WireframeModel::rectangle({0.0, 0.0, 0.0}, {4.0, 2.0, 0.0}));
    const auto result = mm::SnapEngine::snap({2.03, 1.02, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::GeometricCenter, "Closed polygon must geometric-center snap");
    require(result.point == mm::Vec3{2.0, 1.0, 0.0}, "Geometric center coordinate mismatch");
}

void test_snap_finds_perpendicular_and_tangent_with_reference_point() {
    mm::Document lineDocument;
    lineDocument.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    const auto perpendicular = mm::SnapEngine::snap({2.04, 0.03, 0.0}, lineDocument, 0.15, 1.0,
                                                     true, false, mm::Vec3{2.0, 3.0, 0.0});
    require(perpendicular.type == mm::SnapType::Perpendicular, "Reference point must enable perpendicular snap");

    mm::Document circleDocument;
    circleDocument.addModel(mm::WireframeModel::circle({0.0, 0.0, 0.0}, 1.0, 64));
    const mm::Vec3 expected{0.5, std::sqrt(0.75), 0.0};
    const auto tangent = mm::SnapEngine::snap({expected.x + 0.02, expected.y + 0.01, 0.0}, circleDocument,
                                              0.15, 1.0, true, false, mm::Vec3{2.0, 0.0, 0.0});
    require(tangent.type == mm::SnapType::Tangent, "Reference point must enable circle tangent snap");
}

void test_snap_finds_nearest_and_extension() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    const auto nearest = mm::SnapEngine::snap({1.37, 0.04, 0.0}, document, 0.15, 1.0, true, false);
    require(nearest.type == mm::SnapType::Nearest, "Edge must nearest-snap between key points");
    const auto extension = mm::SnapEngine::snap({5.03, 0.02, 0.0}, document, 0.15, 1.0, true, false);
    require(extension.type == mm::SnapType::Extension, "Cursor beyond edge must extension-snap");
}

void test_snap_finds_node_insertion_and_apparent_intersection() {
    mm::Document pointDocument;
    pointDocument.addModel(mm::WireframeModel::point({2.0, 3.0, 0.0}));
    const auto node = mm::SnapEngine::snap({2.03, 3.02, 0.0}, pointDocument, 0.15, 1.0, true, false);
    require(node.type == mm::SnapType::Node, "Point entity must node-snap");

    mm::Document blockDocument;
    blockDocument.addModel(mm::WireframeModel::cube(2.0));
    const auto insertion = mm::SnapEngine::snap({0.03, 0.02, 0.0}, blockDocument, 0.15, 1.0, true, false);
    require(insertion.type == mm::SnapType::Insertion, "Primitive origin must insertion-snap");

    mm::Camera camera;
    camera.setView(mm::StandardView::Top);
    mm::Document apparentDocument;
    apparentDocument.addLine({-1.0, 0.0, 1.0}, {3.0, 0.0, 1.0});
    apparentDocument.addLine({0.0, -2.0, -1.0}, {0.0, 1.0, -1.0});
    const auto apparent = mm::SnapEngine::snap3D({400.0, 300.0}, apparentDocument, camera,
                                                800, 600, 10.0, 1.0, 0.0, true, false);
    require(apparent.type == mm::SnapType::ApparentIntersection,
            "Projected crossing at different depths must apparent-intersection snap");
}

void test_snap_falls_back_to_grid() {
    mm::Document document;
    const auto result = mm::SnapEngine::snap({1.21, -2.76, 0.0}, document, 0.15, 0.5);
    require(result.type == mm::SnapType::Grid, "Empty drawing must grid-snap");
    require(result.point == mm::Vec3{1.0, -3.0, 0.0}, "Grid snap coordinate mismatch");
}

void test_grid_snap_can_be_disabled_without_disabling_object_snap() {
    mm::Document document;
    document.addLine({1.0, 2.0, 0.0}, {4.0, 2.0, 0.0});

    const auto endpoint = mm::SnapEngine::snap({1.08, 2.04, 0.0}, document, 0.15, 1.0, true, false);
    require(endpoint.type == mm::SnapType::Endpoint,
            "Endpoint snap must remain active when grid snap is disabled");

    const auto freePoint = mm::SnapEngine::snap({2.31, 3.67, 0.0}, document, 0.15, 1.0, true, false);
    require(freePoint.type == mm::SnapType::None, "Disabled grid snap must not quantize free points");
    require(freePoint.point == mm::Vec3{2.31, 3.67, 0.0}, "Disabled grid snap must preserve cursor coordinates");
}

void test_snap_policy_disables_selection_and_zoom_phases() {
    require(mm::shouldEvaluateSnapping(false, false, false),
            "Normal drafting must continue evaluating OSNAP");
    require(!mm::shouldEvaluateSnapping(true, false, false),
            "Entity-selection phase must suppress OSNAP");
    require(!mm::shouldEvaluateSnapping(false, true, false),
            "Zoom phase must suppress OSNAP");
    require(!mm::shouldEvaluateSnapping(false, false, true),
            "Active camera navigation must suppress OSNAP");
}

void test_ortho_constrains_cursor_to_dominant_axis() {
    const mm::Vec3 anchor{2.0, 3.0, 4.0};

    require(mm::constrainOrtho(anchor, {8.0, 5.0, 4.0}) == mm::Vec3{8.0, 3.0, 4.0},
            "F8 Ortho must lock a mostly-horizontal cursor to the X axis");
    require(mm::constrainOrtho(anchor, {3.0, -5.0, 4.0}) == mm::Vec3{2.0, -5.0, 4.0},
            "F8 Ortho must lock a mostly-vertical cursor to the Y axis");
    require(mm::constrainOrtho(anchor, {5.0, 6.0, 4.0}) == mm::Vec3{5.0, 3.0, 4.0},
            "Equal movement must deterministically prefer the horizontal axis");
}

void test_ortho_preserves_explicit_object_snaps() {
    const mm::Vec3 anchor{0.0, 0.0, 0.0};
    const auto free = mm::applyOrtho(anchor, {{6.0, 2.0, 0.0}, mm::SnapType::None, 0.0});
    require(free.point == mm::Vec3{6.0, 0.0, 0.0},
            "Ortho must constrain free cursor movement");

    const auto endpoint = mm::applyOrtho(anchor, {{6.0, 2.0, 0.0}, mm::SnapType::Endpoint, 2.0});
    require(endpoint.point == mm::Vec3{6.0, 2.0, 0.0},
            "An explicit object snap must override the F8 Ortho constraint");

    const auto endpointDuringCommand = mm::applyOrtho(anchor, {{6.0, 2.0, 0.0}, mm::SnapType::Endpoint, 2.0}, true);
    require(endpointDuringCommand.point == mm::Vec3{6.0, 2.0, 0.0} &&
            endpointDuringCommand.type == mm::SnapType::Endpoint,
            "Object snaps must take priority over F8 Ortho even during an active command");
}

void test_modifier_ortho_strictly_constrains_destination_even_near_object_snap() {
    const mm::SnapResult endpoint{{3.0, 1.0, 0.0}, mm::SnapType::Endpoint, 0.01};
    const auto constrained = mm::applyOrtho({0.0, 0.0, 0.0}, endpoint, false);
    require(constrained.point == mm::Vec3{3.0, 0.0, 0.0} && constrained.type == mm::SnapType::None,
            "Modifier F8 with preserveObjectSnaps=false must strictly constrain and clear snap type");
}

void test_3d_ortho_constrains_to_all_three_global_axes() {
    mm::Camera camera;
    constexpr int width = 900;
    constexpr int height = 700;
    const mm::Vec3 anchor{1.0, -2.0, 0.5};
    const auto alongAxis = [&](const mm::Vec3& axis, double distance) {
        return camera.project(anchor + axis * distance, width, height);
    };

    const auto xLocked = mm::constrainOrtho3D(anchor, alongAxis({1.0, 0.0, 0.0}, 3.0),
                                               camera, width, height);
    const auto yLocked = mm::constrainOrtho3D(anchor, alongAxis({0.0, 1.0, 0.0}, -2.5),
                                               camera, width, height);
    const auto zLocked = mm::constrainOrtho3D(anchor, alongAxis({0.0, 0.0, 1.0}, 4.0),
                                               camera, width, height);

    require(xLocked == mm::Vec3{4.0, -2.0, 0.5}, "3D F8 Ortho must lock movement to global X");
    require(yLocked == mm::Vec3{1.0, -4.5, 0.5}, "3D F8 Ortho must lock movement to global Y");
    require(zLocked == mm::Vec3{1.0, -2.0, 4.5}, "3D F8 Ortho must lock movement to global Z");

    const mm::SnapResult endpoint{{7.0, 8.0, 9.0}, mm::SnapType::Endpoint, 1.0};
    require(mm::applyOrtho3D(anchor, alongAxis({0.0, 0.0, 1.0}, 2.0), endpoint,
                             camera, width, height).point == endpoint.point,
            "Explicit 3D object snaps must override all-axis F8 Ortho");
}

void test_3d_ortho_uses_the_active_work_plane_axes() {
    mm::Camera camera;
    constexpr int width = 900;
    constexpr int height = 700;
    const double diagonal = std::sqrt(0.5);
    const mm::WorkPlane tiltedPlane{{2.0, -1.0, 3.0},
                                    {diagonal, 0.0, diagonal},
                                    {0.0, 1.0, 0.0},
                                    {-diagonal, 0.0, diagonal}};
    const mm::Vec3 anchor = tiltedPlane.origin + tiltedPlane.u * 1.5 + tiltedPlane.v * 0.75;
    const auto alongU = camera.project(anchor + tiltedPlane.u * 4.0, width, height);
    const auto alongV = camera.project(anchor - tiltedPlane.v * 2.5, width, height);
    const auto alongNormal = camera.project(anchor + tiltedPlane.normal * 3.25, width, height);

    const auto uLocked = mm::constrainOrtho3D(anchor, alongU, camera, width, height, tiltedPlane);
    const auto vLocked = mm::constrainOrtho3D(anchor, alongV, camera, width, height, tiltedPlane);
    const auto normalLocked = mm::constrainOrtho3D(anchor, alongNormal, camera, width, height,
                                                   tiltedPlane, true);
    const auto magnitude = [](const mm::Vec3& value) {
        return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    };
    const auto normalOffset = [&](const mm::Vec3& point) {
        const mm::Vec3 delta = point - tiltedPlane.origin;
        return delta.x * tiltedPlane.normal.x + delta.y * tiltedPlane.normal.y +
               delta.z * tiltedPlane.normal.z;
    };
    require(magnitude(uLocked - (anchor + tiltedPlane.u * 4.0)) < 1e-9,
            "F8 must lock to the newly defined work-plane U axis");
    require(magnitude(vLocked - (anchor - tiltedPlane.v * 2.5)) < 1e-9,
            "F8 must lock to the newly defined work-plane V axis");
    require(magnitude(normalLocked - (anchor + tiltedPlane.normal * 3.25)) < 1e-9,
            "Modify F8 must also lock to the work-plane normal/local Z axis");
    require(std::abs(normalOffset(uLocked)) < 1e-9 && std::abs(normalOffset(vLocked)) < 1e-9,
            "Drawing F8 must stay on the work-plane U/V surface");
}

void test_planar_modifiers_use_the_active_tilted_work_plane() {
    const double diagonal = std::sqrt(0.5);
    const mm::WorkPlane plane{{3.0, -2.0, 5.0},
                              {diagonal, 0.0, diagonal},
                              {0.0, 1.0, 0.0},
                              {-diagonal, 0.0, diagonal}};
    const auto world = [&](double u, double v) { return plane.fromPlane({u, v}); };
    const auto normalOffset = [&](const mm::Vec3& point) {
        const auto delta = point - plane.origin;
        return delta.x * plane.normal.x + delta.y * plane.normal.y + delta.z * plane.normal.z;
    };
    const auto requireLocal = [&](const mm::Vec3& point, double u, double v, const std::string& message) {
        const auto local = plane.toPlane(point);
        require(std::hypot(local.x - u, local.y - v) < 1e-8 &&
                    std::abs(normalOffset(point)) < 1e-8, message);
    };

    const auto baseLine = mm::WireframeModel::line(world(0.0, 0.0), world(4.0, 0.0));
    const auto offset = mm::offsetModelOnPlane(baseLine, 1.0, world(2.0, 3.0), plane);
    require(offset && offset->vertices().size() == 2,
            "Tilted work-plane Offset must produce a line");
    requireLocal(offset->vertices()[0], 0.0, 1.0,
                 "Offset start must stay on the tilted work plane");
    requireLocal(offset->vertices()[1], 4.0, 1.0,
                 "Offset end must stay on the tilted work plane");

    const auto aboveAxis = mm::WireframeModel::line(world(1.0, 1.0), world(3.0, 1.0));
    const auto mirrored = mm::mirrorModelOnPlane(aboveAxis, world(0.0, 0.0), world(4.0, 0.0), plane);
    require(mirrored && mirrored->vertices().size() == 2,
            "Tilted work-plane Mirror must produce geometry");
    requireLocal(mirrored->vertices()[0], 1.0, -1.0,
                 "Mirror must reflect in plane-local coordinates");
    requireLocal(mirrored->vertices()[1], 3.0, -1.0,
                 "Mirror result must remain on the tilted work plane");

    const auto polarSource = mm::WireframeModel::point(world(2.0, 0.0));
    const auto polar = mm::polarArrayOnPlane(polarSource, 4, world(0.0, 0.0), plane);
    require(polar.size() == 3, "Tilted work-plane Polar Array must create all copies");
    requireLocal(polar[0].vertices().front(), 0.0, 2.0,
                 "Polar Array must rotate around the work-plane normal");
    requireLocal(polar[1].vertices().front(), -2.0, 0.0,
                 "Polar Array half-turn must remain plane-local");

    const auto trimSource = mm::WireframeModel::line(world(-2.0, 0.0), world(2.0, 0.0));
    const std::vector<mm::WireframeModel> trimBoundaries{
        mm::WireframeModel::line(world(-1.0, -1.0), world(-1.0, 1.0)),
        mm::WireframeModel::line(world(1.0, -1.0), world(1.0, 1.0))};
    const auto trimmed = mm::trimLineOnPlane(trimSource, trimBoundaries, world(0.0, 0.0), plane);
    require(trimmed && trimmed->size() == 2,
            "Trim must find intersections in tilted work-plane coordinates");
    requireLocal((*trimmed)[0].vertices().back(), -1.0, 0.0,
                 "Trim first cut must remain on the work plane");
    requireLocal((*trimmed)[1].vertices().front(), 1.0, 0.0,
                 "Trim second cut must remain on the work plane");

    const auto extendSource = mm::WireframeModel::line(world(0.0, 0.0), world(1.0, 0.0));
    const std::vector<mm::WireframeModel> extendBoundaries{
        mm::WireframeModel::line(world(2.0, -1.0), world(2.0, 1.0))};
    const auto extended = mm::extendLineOnPlane(extendSource, extendBoundaries,
                                                world(0.9, 0.0), plane);
    require(extended && extended->vertices().size() == 2,
            "Extend must find a tilted work-plane boundary");
    requireLocal(extended->vertices().back(), 2.0, 0.0,
                 "Extended endpoint must remain on the tilted work plane");
}

void test_polar_tracking_locks_only_near_90_degree_rays() {
    const mm::Vec3 anchor{1.0, 2.0, 3.0};
    const double tenDegrees = 10.0 * std::numbers::pi / 180.0;
    const mm::SnapResult nearHorizontal{{anchor.x + 10.0 * std::cos(tenDegrees),
                                         anchor.y + 10.0 * std::sin(tenDegrees), anchor.z},
                                        mm::SnapType::None, 0.0};
    const auto locked = mm::applyPolarTracking(anchor, nearHorizontal, 90.0, 12.0, true);
    require(std::abs(locked.point.x - 11.0) < 1e-9 &&
            std::abs(locked.point.y - anchor.y) < 1e-9 && locked.point.z == anchor.z,
            "Polar Tracking must lock a near-horizontal cursor to the 0-degree ray");

    const double thirtyDegrees = 30.0 * std::numbers::pi / 180.0;
    const mm::SnapResult freeCandidate{{anchor.x + 10.0 * std::cos(thirtyDegrees),
                                        anchor.y + 10.0 * std::sin(thirtyDegrees), anchor.z},
                                       mm::SnapType::None, 0.0};
    require(mm::applyPolarTracking(anchor, freeCandidate, 90.0, 12.0, true).point == freeCandidate.point,
            "Polar Tracking must leave cursor movement free outside its angular aperture");

    const mm::SnapResult endpoint{{8.0, 9.0, 4.0}, mm::SnapType::Endpoint, 0.0};
    require(mm::applyPolarTracking(anchor, endpoint, 90.0, 12.0, true).point == endpoint.point,
            "Explicit object snaps must override Polar Tracking");

    const mm::WorkPlane xzPlane{{}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}};
    const mm::SnapResult nearPlaneVertical{{1.5, 2.0, 11.0}, mm::SnapType::None, 0.0};
    const auto planeLocked = mm::applyPolarTracking(anchor, nearPlaneVertical, xzPlane,
                                                     90.0, 12.0, true);
    require(std::abs(planeLocked.point.x - anchor.x) < 1e-9 && planeLocked.point.y == anchor.y,
            "3D Polar Tracking must use the active work-plane axes");
}

void test_temporary_tracking_snaps_to_midpoint_between_acquired_points() {
    const std::vector<mm::Vec3> acquired{{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}};
    const mm::SnapResult raw{{5.0, 0.15, 0.0}, mm::SnapType::None, 0.0};
    const auto tracking = mm::resolveTemporaryPointTracking(raw, acquired, mm::WorkPlane{}, 0.25);
    require(tracking.result.type == mm::SnapType::Midpoint &&
                tracking.result.point == mm::Vec3{5.0, 0.0, 0.0},
            "Two acquired Temp Points must snap at their exact midpoint");
    require(tracking.guides.size() == 1 && tracking.guides.front().from == acquired[0] &&
                tracking.guides.front().to == acquired[1],
            "Two acquired Temp Points must expose their connecting tracking line");
}

void test_temporary_tracking_creates_perpendicular_corner_points() {
    const std::vector<mm::Vec3> acquired{{0.0, 0.0, 0.0}, {10.0, 8.0, 0.0}};
    const mm::SnapResult raw{{0.1, 7.9, 0.0}, mm::SnapType::None, 0.0};
    const auto tracking = mm::resolveTemporaryPointTracking(raw, acquired, mm::WorkPlane{}, 0.25);
    require(tracking.result.type == mm::SnapType::Intersection &&
                tracking.result.point == mm::Vec3{0.0, 8.0, 0.0},
            "Perpendicular U/V rays from two Temp Points must snap at their crossing");
    require(std::find(tracking.derivedPoints.begin(), tracking.derivedPoints.end(),
                      mm::Vec3{0.0, 8.0, 0.0}) != tracking.derivedPoints.end(),
            "A perpendicular tracking crossing must be exposed as a derived Temp Point");
}

void test_temporary_tracking_locks_to_acquired_point_axes() {
    const std::vector<mm::Vec3> acquired{{2.0, 3.0, 0.0}};
    const mm::SnapResult raw{{9.0, 3.15, 0.0}, mm::SnapType::None, 0.0};
    const auto tracking = mm::resolveTemporaryPointTracking(raw, acquired, mm::WorkPlane{}, 0.25);
    require(tracking.locked && tracking.result.point == mm::Vec3{9.0, 3.0, 0.0},
            "A cursor near a Temp Point U/V ray must lock to that tracking axis");
    require(tracking.guides.size() == 1 && tracking.guides.front().from == acquired.front() &&
                tracking.guides.front().to == tracking.result.point,
            "An active Temp Point axis must expose its tracking guide");
}

void test_dynamic_input_parses_absolute_coordinates() {
    const auto point = mm::parseDynamicPoint(L"12.5,-3.25", std::nullopt);
    require(point.has_value(), "Absolute dynamic coordinate must parse");
    require(*point == mm::Vec3{12.5, -3.25, 0.0}, "Absolute coordinate mismatch");
}

void test_dynamic_input_parses_distance_angle() {
    const auto point = mm::parseDynamicPoint(L"10<30", mm::Vec3{2.0, 3.0, 0.0});
    require(point.has_value(), "Distance-angle dynamic input must parse");
    require(std::abs(point->x - 10.6602540378) < 1e-6, "Polar input x mismatch");
    require(std::abs(point->y - 8.0) < 1e-6, "Polar input y mismatch");
}

void test_dynamic_input_single_distance_follows_cursor_direction() {
    const mm::Vec3 origin{2.0, 3.0, 1.0};
    const auto point = mm::parseDynamicPoint(L"10", origin, mm::Vec3{5.0, 7.0, 1.0});
    require(point.has_value(), "A single distance must parse after the first drawing point");
    require(std::abs(point->x - 8.0) < 1e-9, "Directional distance x mismatch");
    require(std::abs(point->y - 11.0) < 1e-9, "Directional distance y mismatch");
    require(std::abs(point->z - 1.0) < 1e-9, "Directional distance must preserve the cursor direction plane");
    require(std::abs(std::hypot(point->x - origin.x, point->y - origin.y) - 10.0) < 1e-9,
            "Directional input must create the exact requested length");
}

void test_dynamic_input_parses_3d_coordinates() {
    const auto point = mm::parseDynamicPoint(L"12.5,-3.25,7.75", std::nullopt);
    require(point.has_value(), "Absolute 3D dynamic coordinate must parse");
    require(*point == mm::Vec3{12.5, -3.25, 7.75}, "Absolute 3D coordinate mismatch");

    const auto relative = mm::parseDynamicPoint(L"@4,-2,3", mm::Vec3{1.0, 2.0, 3.0});
    require(relative.has_value(), "Relative @dX,dY,dZ coordinate must parse");
    require(*relative == mm::Vec3{5.0, 0.0, 6.0}, "Relative 3D coordinate mismatch");
    require(!mm::parseDynamicPoint(L"@4,-2,3", std::nullopt).has_value(),
            "Relative coordinates require an established first point");
}

void test_camera_wheel_zoom_keeps_2d_cursor_world_point_fixed() {
    mm::Camera camera;
    constexpr int width = 1000, height = 700;
    const mm::Vec2 cursor{735.0, 214.0};
    const auto before = camera.unproject2D(cursor, width, height);
    camera.zoom2DAt(cursor, 1.12, width, height);
    const auto after = camera.unproject2D(cursor, width, height);
    require(std::abs(before.x - after.x) < 1e-9 && std::abs(before.y - after.y) < 1e-9,
            "2D wheel zoom must preserve the world point under the mouse cursor");
}

void test_camera_wheel_zoom_keeps_3d_cursor_projection_fixed() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Isometric);
    constexpr int width = 1000, height = 700;
    const mm::Vec3 point{3.5, -1.25, 2.75};
    const auto cursor = camera.project(point, width, height);
    camera.zoom3DAt(cursor, 1.12, width, height);
    const auto after = camera.project(point, width, height);
    require(std::abs(cursor.x - after.x) < 1e-9 && std::abs(cursor.y - after.y) < 1e-9,
            "3D wheel zoom must preserve the projected point under the mouse cursor");
}

void test_camera_unprojects_screen_point_to_work_plane() {
    mm::Camera camera;
    const mm::Vec3 expected{1.25, -0.75, 2.0};
    const auto screen = camera.project(expected, 900, 700);
    const auto world = camera.unprojectToPlane(screen, 900, 700, expected.z);
    require(world.has_value(), "Visible screen point must intersect the 3D work plane");
    require(std::abs(world->x - expected.x) < 1e-6, "Unprojected work-plane x mismatch");
    require(std::abs(world->y - expected.y) < 1e-6, "Unprojected work-plane y mismatch");
    require(std::abs(world->z - expected.z) < 1e-6, "Unprojected work-plane z mismatch");
}

void test_work_plane_is_defined_by_three_non_collinear_points() {
    const auto plane = mm::WorkPlane::fromThreePoints({1.0, 2.0, 3.0},
                                                       {3.0, 2.0, 3.0},
                                                       {1.0, 2.0, 6.0});
    require(plane.has_value(), "Three non-collinear points must define a work plane");
    require(plane->origin == mm::Vec3{1.0, 2.0, 3.0}, "Work-plane origin must be the first point");
    require(std::abs(plane->normal.x) < 1e-9 && std::abs(plane->normal.y + 1.0) < 1e-9 &&
            std::abs(plane->normal.z) < 1e-9,
            "Work-plane normal must follow the three-point right-hand orientation");
    require(plane->fromPlane({2.0, 3.0}) == mm::Vec3{3.0, 2.0, 6.0},
            "Work-plane local coordinates must map through its U/V basis");
    require(!mm::WorkPlane::fromThreePoints({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                             {2.0, 0.0, 0.0}),
            "Collinear points must not replace the active work plane");
}

void test_camera_unprojects_to_arbitrary_three_point_work_plane() {
    mm::Camera camera;
    const auto plane = mm::WorkPlane::fromThreePoints({0.0, 1.5, 0.0},
                                                       {1.0, 1.5, 0.0},
                                                       {0.0, 1.5, 1.0});
    require(plane.has_value(), "Vertical test plane must be valid");
    const mm::Vec3 expected{2.25, 1.5, -0.75};
    const auto screen = camera.project(expected, 900, 700);
    const auto world = camera.unprojectToPlane(screen, 900, 700, *plane);
    require(world.has_value(), "Camera projection line must intersect an arbitrary work plane");
    require(std::abs(world->x - expected.x) < 1e-6 &&
            std::abs(world->y - expected.y) < 1e-6 &&
            std::abs(world->z - expected.z) < 1e-6,
            "Arbitrary work-plane unprojection must round-trip projected points");
}

void test_3d_grid_snap_uses_active_work_plane_basis() {
    mm::Camera camera;
    mm::Document document;
    const auto plane = mm::WorkPlane::fromThreePoints({0.0, 2.0, 0.0},
                                                       {1.0, 2.0, 0.0},
                                                       {0.0, 2.0, 1.0});
    require(plane.has_value(), "Grid test plane must be valid");
    const auto cursor = camera.project({1.2, 2.0, 2.7}, 900, 700);
    const auto snapped = mm::SnapEngine::snap3D(cursor, document, camera, 900, 700,
                                                8.0, 1.0, *plane, false, true);
    require(snapped.type == mm::SnapType::Grid, "Active work plane must retain grid snapping");
    require(std::abs(snapped.point.x - 1.0) < 1e-9 &&
            std::abs(snapped.point.y - 2.0) < 1e-9 &&
            std::abs(snapped.point.z - 3.0) < 1e-9,
            "3D grid snap must quantize the active plane's U/V coordinates");
}

void test_parallel_camera_unprojects_planes_on_either_side_of_view_plane() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Top);
    const mm::Vec3 expected{1.25, -0.75, -3.0};
    const auto screen = camera.project(expected, 900, 700);
    const auto world = camera.unprojectToPlane(screen, 900, 700, expected.z);
    require(world.has_value(), "Parallel camera must unproject work planes at any depth");
    require(std::abs(world->x - expected.x) < 1e-6, "Back-plane unprojected x mismatch");
    require(std::abs(world->y - expected.y) < 1e-6, "Back-plane unprojected y mismatch");
    require(std::abs(world->z - expected.z) < 1e-6, "Back-plane unprojected z mismatch");
}

void test_projected_snap_finds_3d_endpoint() {
    mm::Camera camera;
    mm::Document document;
    const mm::Vec3 endpoint{1.0, 2.0, 3.0};
    document.addLine(endpoint, {-2.0, 0.0, 1.0});
    const auto screen = camera.project(endpoint, 900, 700);
    const auto result = mm::SnapEngine::snap3D({screen.x + 3.0, screen.y - 2.0}, document, camera,
                                                900, 700, 8.0, 1.0, 0.0);
    require(result.type == mm::SnapType::Endpoint, "Projected cursor must snap to a 3D endpoint");
    require(result.point == endpoint, "Projected 3D endpoint snap coordinate mismatch");

    mm::Document cubeDocument;
    cubeDocument.addModel(mm::WireframeModel::cube(2.6));
    const auto cubeEndpoint = cubeDocument.models().front().vertices().front();
    const auto cubeScreen = camera.project(cubeEndpoint, 1532, 754);
    const auto cubeResult = mm::SnapEngine::snap3D(cubeScreen, cubeDocument, camera,
                                                   1532, 754, 10.0, 1.0, 0.0);
    require(cubeResult.type == mm::SnapType::Endpoint,
            "Projected cursor must snap to a 3D cube endpoint before grid fallback");
    require(cubeResult.point == cubeEndpoint, "Projected 3D cube endpoint coordinate mismatch");
}

void test_rectangle_and_circle_geometry() {
    const auto rectangle = mm::WireframeModel::rectangle({-1.0, -2.0, 0.0}, {3.0, 4.0, 0.0});
    require(rectangle.vertices().size() == 4 && rectangle.edges().size() == 4,
            "Rectangle must contain four vertices and edges");
    const auto circle = mm::WireframeModel::circle({1.0, 2.0, 0.0}, 3.0, 32);
    require(circle.vertices().size() == 32 && circle.edges().size() == 32,
            "Circle segment count mismatch");
}

void test_rectangle_preserves_3d_work_plane_height() {
    const auto rectangle = mm::WireframeModel::rectangle({-1.0, -2.0, 4.5}, {3.0, 4.0, 4.5});
    for (const auto& vertex : rectangle.vertices()) {
        require(std::abs(vertex.z - 4.5) < 1e-9,
                "Rectangle vertices must stay on the selected 3D work plane");
    }
}

void test_planar_shapes_follow_arbitrary_work_plane_basis() {
    const auto plane = mm::WorkPlane::fromThreePoints({0.0, 2.0, 0.0},
                                                       {1.0, 2.0, 0.0},
                                                       {0.0, 2.0, 1.0});
    require(plane.has_value(), "Shape test plane must be valid");
    const auto rectangle = mm::WireframeModel::rectangleOnPlane(*plane, {0.0, 0.0}, {3.0, 4.0});
    const auto circle = mm::WireframeModel::circleOnPlane(*plane, {1.0, 1.0}, 2.0, 16);
    for (const auto& vertex : rectangle.vertices())
        require(std::abs(vertex.y - 2.0) < 1e-9, "Rectangle must lie on the arbitrary work plane");
    for (const auto& vertex : circle.vertices())
        require(std::abs(vertex.y - 2.0) < 1e-9, "Circle must lie on the arbitrary work plane");
}

void test_frame_performance_tracker_reports_render_work() {
    mm::FramePerformanceTracker tracker;
    mm::FramePerformanceSample sample;
    sample.cpuFrameMilliseconds = 4.2;
    sample.spatialQueryMilliseconds = 0.7;
    sample.totalEntities = 1'000;
    sample.visibleEntities = 250;
    sample.renderedEntities = 200;
    sample.drawCalls = 410;
    sample.projectedVertices = 820;
    sample.frameBufferGrowths = 3;
    tracker.record(sample, 1.0 / 60.0);

    const auto& stats = tracker.latest();
    require(std::abs(stats.framesPerSecond - 60.0) < 1e-9,
            "Performance tracker must calculate presentation FPS");
    require(stats.culledEntities == 750 && stats.visibleEntities == 250 &&
            stats.renderedEntities == 200,
            "Performance tracker must report total, visible, rendered, and culled entities");
    require(stats.drawCalls == 410 && stats.projectedVertices == 820 &&
            stats.frameBufferGrowths == 3,
            "Performance tracker must retain per-frame render work counters");
}

void test_frame_index_stamp_set_reuses_storage_and_discards_stale_selection() {
    mm::FrameIndexStampSet selected;
    require(selected.assign(10, {1, 4, 8}),
            "First selection assignment must grow reusable stamp storage");
    require(selected.contains(1) && selected.contains(4) && selected.contains(8),
            "Current selected indices must be present in the stamp set");
    require(!selected.contains(2) && !selected.contains(10),
            "Unselected and out-of-range indices must not be present");

    require(!selected.assign(10, {2}),
            "Same-size selection assignment must reuse existing storage");
    require(selected.contains(2) && !selected.contains(1) && !selected.contains(8),
            "Advancing the generation must discard the previous frame selection");
}

void test_projected_bounds_culling_rejects_offscreen_3d_models() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Top);
    constexpr int width = 1200;
    constexpr int height = 800;
    const mm::Bounds3 visible{{-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}};
    const mm::Bounds3 offscreen{{1000.0, 1000.0, -1.0}, {1002.0, 1002.0, 1.0}};

    require(mm::projectedBoundsIntersectsViewport(visible, camera, width, height),
            "Projected 3D bounds at the camera center must remain visible");
    require(!mm::projectedBoundsIntersectsViewport(offscreen, camera, width, height),
            "Projected 3D bounds outside the viewport must be culled");
}
}

int main() {
    try {
        test_vector_arithmetic();
        test_work_plane_axis_glyph_uses_local_xyz_basis();
        test_cube_geometry();
        test_pyramid_geometry();
        test_3dface_geometry_preserves_four_ordered_corners_and_identity();
        test_visual_styles_define_wireframe_opaque_and_transparent_face_alpha();
        test_mirror_preserves_3dface_identity_for_later_dxf_export();
        test_camera_projects_origin_to_view_center();
        test_camera_zoom_scales_the_initial_2d_view();
        test_camera_2d_projection_round_trips_after_zoom();
        test_camera_2d_pan_tracks_middle_button_screen_displacement();
        test_camera_zoom_extents_fits_world_bounds_with_margin();
        test_camera_fit3d_centers_large_nonplanar_bounds();
        test_camera_uses_parallel_projection_at_every_depth();
        test_camera_standard_view_presets();
        test_view_cube_hit_testing();
        test_view_cube_rotates_with_camera_and_global_axes();
        test_view_cube_host_stays_anchored_inside_canvas();
        test_ribbon_groups_commands_into_function_tabs();
        test_modifier_point_cursor_policy_matches_interaction_phase();
        test_trim_and_extend_remain_active_for_multiple_targets();
        test_modify_commands_consume_idle_preselection();
        test_snap_evaluation_requires_an_active_command();
        test_every_modifier_preserves_the_current_3d_view();
        test_idle_enter_repeat_never_steals_keyboard_point_input();
        test_ribbon_compact_buttons_fit_above_full_width_canvas();
        test_document_round_trip();
        test_document_layer_manager_creates_renames_filters_and_deletes_layers();
        test_document_layer_manager_resolves_live_layer_properties();
        test_hidden_and_locked_layers_are_not_selectable_or_snappable();
        test_document_changes_layer_for_selected_models_only();
        test_document_changes_color_for_selected_models_and_supports_bylayer();
        test_document_changes_linetype_for_selected_models_and_supports_bylayer();
        test_document_undo_redo_restores_models_and_layers();
        test_document_undo_redo_restores_layer_changes();
        test_document_undo_history_is_bounded();
        test_dxf_round_trip_preserves_supported_entities();
        test_dxf_round_trip_writes_and_reads_a_four_corner_3dface_entity();
        test_dxf_round_trip_preserves_entity_properties();
        test_dxf_import_defaults_entity_and_rendered_thickness_to_zero();
        test_dxf_reads_closed_lwpolyline();
        test_dxf_insert_expands_block_with_base_scale_rotation_and_byblock_style();
        test_dxf_insert_expands_nested_blocks();
        test_dxf_insert_rejects_undefined_block_reference();
        test_dxf_insert_rejects_recursive_block_references();
        test_dxf_dimension_displays_generated_block_lines_arrows_and_text();
        test_dxf_import_preserves_layer_color_lineweight_and_linetype();
        test_dxf_import_resolves_extended_aci_and_hidden_layers();
        test_dxf_import_can_be_cancelled_before_large_parse();
        test_document_spatial_index_limits_large_drawing_queries();
        test_document_moves_selected_models_by_displacement();
        test_document_copies_selected_models_and_preserves_metadata();
        test_document_deletes_only_selected_models();
        test_document_replaces_one_model_with_trimmed_segments_in_place();
        test_linear_array_creates_evenly_spaced_property_preserving_copies();
        test_polar_array_distributes_copies_around_center_and_preserves_metadata();
        test_trim_line_removes_the_picked_side_at_cutting_edge();
        test_extend_line_moves_picked_endpoint_to_boundary();
        test_fillet_trims_two_picked_line_arms_and_adds_tangent_arc();
        test_current_entity_style_resolves_layer_color_and_linetype_choices();
        test_offset_line_creates_parallel_copy_on_selected_side();
        test_offset_circle_uses_inside_or_outside_pick();
        test_mirror_reflects_geometry_across_two_point_axis();
        test_rotate_rotates_geometry_around_center_point();
        test_rotate_preserves_analytic_center_and_radius();
        test_entity_hit_test_selects_nearest_model_edge();
        test_left_to_right_window_selects_only_fully_contained_models();
        test_right_to_left_crossing_selects_touching_and_contained_models();
        test_crossing_selection_chooses_the_target_portion_inside_the_window();
        test_projected_3d_crossing_selection_preserves_the_world_target_point();
        test_projected_3d_window_and_crossing_selection();
        test_snap_prefers_nearby_endpoint();
        test_snap_marker_symbols_match_cad_reference_conventions();
        test_snap_finds_edge_midpoint();
        test_snap_respects_individual_type_enablement();
        test_snap_finds_line_intersection();
        test_snap_finds_circle_center_and_quadrant();
        test_snap_finds_polygon_geometric_center();
        test_snap_finds_perpendicular_and_tangent_with_reference_point();
        test_snap_finds_nearest_and_extension();
        test_snap_finds_node_insertion_and_apparent_intersection();
        test_snap_falls_back_to_grid();
        test_grid_snap_can_be_disabled_without_disabling_object_snap();
        test_snap_policy_disables_selection_and_zoom_phases();
        test_ortho_constrains_cursor_to_dominant_axis();
        test_ortho_preserves_explicit_object_snaps();
        test_modifier_ortho_strictly_constrains_destination_even_near_object_snap();
        test_3d_ortho_constrains_to_all_three_global_axes();
        test_3d_ortho_uses_the_active_work_plane_axes();
        test_planar_modifiers_use_the_active_tilted_work_plane();
        test_polar_tracking_locks_only_near_90_degree_rays();
        test_temporary_tracking_snaps_to_midpoint_between_acquired_points();
        test_temporary_tracking_creates_perpendicular_corner_points();
        test_temporary_tracking_locks_to_acquired_point_axes();
        test_dynamic_input_parses_absolute_coordinates();
        test_dynamic_input_parses_distance_angle();
        test_dynamic_input_single_distance_follows_cursor_direction();
        test_dynamic_input_parses_3d_coordinates();
        test_camera_wheel_zoom_keeps_2d_cursor_world_point_fixed();
        test_camera_wheel_zoom_keeps_3d_cursor_projection_fixed();
        test_camera_unprojects_screen_point_to_work_plane();
        test_work_plane_is_defined_by_three_non_collinear_points();
        test_camera_unprojects_to_arbitrary_three_point_work_plane();
        test_3d_grid_snap_uses_active_work_plane_basis();
        test_parallel_camera_unprojects_planes_on_either_side_of_view_plane();
        test_projected_snap_finds_3d_endpoint();
        test_rectangle_and_circle_geometry();
        test_rectangle_preserves_3d_work_plane_height();
        test_planar_shapes_follow_arbitrary_work_plane_basis();
        test_frame_performance_tracker_reports_render_work();
        test_frame_index_stamp_set_reuses_storage_and_discards_stale_selection();
        test_projected_bounds_culling_rejects_offscreen_3d_models();
        std::cout << "All tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
