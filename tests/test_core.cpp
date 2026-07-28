#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_cube.hpp"
#include "model_maker/ribbon_layout.hpp"
#include "model_maker/dxf.hpp"

#include <algorithm>
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

void test_cube_geometry() {
    const auto cube = mm::WireframeModel::cube(2.0);
    require(cube.vertices().size() == 8, "Cube must have 8 vertices");
    require(cube.edges().size() == 12, "Cube must have 12 edges");
}

void test_pyramid_geometry() {
    const auto pyramid = mm::WireframeModel::pyramid(2.0, 3.0);
    require(pyramid.vertices().size() == 5, "Pyramid must have 5 vertices");
    require(pyramid.edges().size() == 8, "Pyramid must have 8 edges");
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
    camera.setView(mm::StandardView::Front);

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
    require(std::abs(camera.pitch() - 1.5707963267948966) < 0.001, "Top view pitch mismatch");

    camera.setView(mm::StandardView::Front);
    require(std::abs(camera.yaw()) < 0.001, "Front view yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Front view pitch mismatch");

    camera.setView(mm::StandardView::Right);
    require(std::abs(camera.yaw() + 1.5707963267948966) < 0.001, "Right view yaw mismatch");
    require(std::abs(camera.pitch()) < 0.001, "Right view pitch mismatch");

    camera.setView(mm::StandardView::Isometric);
    require(std::abs(camera.yaw() + 0.7853981633974483) < 0.001, "Isometric yaw mismatch");
    require(std::abs(camera.pitch() - 0.6154797086703874) < 0.001, "Isometric pitch mismatch");
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

void test_ribbon_groups_commands_into_function_tabs() {
    const auto file = mm::RibbonLayout::commands(mm::RibbonTab::File);
    const auto drawing = mm::RibbonLayout::commands(mm::RibbonTab::Drawing);
    const auto modify = mm::RibbonLayout::commands(mm::RibbonTab::Modify);
    const auto view = mm::RibbonLayout::commands(mm::RibbonTab::View);

    require(file == std::vector<int>({100, 101, 102, 103, 104}),
            "File tab must expose native and DXF read/write commands");
    require(drawing == std::vector<int>({200, 201, 202, 203}),
            "Drawing tab must contain only drawing commands in tool order");
    require(modify == std::vector<int>({500, 501}),
            "Modify tab must group Move and Copy together");
    require(view == std::vector<int>({300, 301, 302, 303, 304, 305, 306}),
            "View tab must group 3D, work-plane, and zoom commands");
}

void test_ribbon_compact_buttons_fit_above_full_width_canvas() {
    constexpr int width = 1280;
    const auto layout = mm::RibbonLayout::layout(mm::RibbonTab::Drawing, width);
    require(layout.ribbonHeight <= 112, "Top ribbon must stay compact");
    require(layout.canvas.left == 0 && layout.canvas.top == layout.ribbonHeight,
            "Canvas must use the full window width directly below the ribbon");
    require(layout.canvas.right == width, "Ribbon must not reserve a left sidebar");
    for (const auto& button : layout.commandButtons) {
        require(button.rect.right <= width && button.rect.bottom <= layout.ribbonHeight,
                "Every icon command button must fit inside the top ribbon");
        require(button.rect.right - button.rect.left <= 74,
                "Ribbon command buttons must use compact icon-button sizing");
    }
}

void test_document_round_trip() {
    mm::Document original;
    original.addModel(mm::WireframeModel::cube(2.0));
    original.addLine({-1.0, 2.0, 0.0}, {3.0, 4.0, 0.0});

    const auto path = std::filesystem::temp_directory_path() / "model-maker-test.mmw";
    original.save(path);

    mm::Document loaded;
    loaded.load(path);
    std::filesystem::remove(path);

    require(loaded.models().size() == 2, "Round trip model count mismatch");
    require(loaded.models()[0].edges().size() == 12, "Round trip cube mismatch");
    require(loaded.models()[1].vertices()[1] == mm::Vec3{3.0, 4.0, 0.0}, "Round trip line mismatch");
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
    require(!loaded.models()[1].properties().visible,
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

void test_projected_3d_window_and_crossing_selection() {
    mm::Camera camera;
    camera.setView(mm::StandardView::Front);
    mm::Document document;
    document.addLine({-1.0, 0.0, 2.0}, {1.0, 0.0, 2.0});
    document.addLine({0.0, -3.0, -2.0}, {0.0, 3.0, -2.0});
    const auto a = camera.project({-2.0, -1.0, 0.0}, 800, 600);
    const auto b = camera.project({2.0, 1.0, 0.0}, 800, 600);
    const auto window = mm::selectModelsInRect3D(a, b, document, camera, 800, 600, false);
    require(window == std::vector<std::size_t>{0},
            "3D window selection must use projected geometry and require containment");
    const auto crossing = mm::selectModelsInRect3D(b, a, document, camera, 800, 600, true);
    require(crossing == std::vector<std::size_t>({0, 1}),
            "3D crossing selection must include projected edges touching the rectangle");
}

void test_snap_prefers_nearby_endpoint() {
    mm::Document document;
    document.addLine({1.0, 2.0, 0.0}, {4.0, 2.0, 0.0});
    const auto result = mm::SnapEngine::snap({1.08, 2.04, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::Endpoint, "Nearby vertex must endpoint-snap");
    require(result.point == mm::Vec3{1.0, 2.0, 0.0}, "Endpoint snap coordinate mismatch");
}

void test_snap_finds_edge_midpoint() {
    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    const auto result = mm::SnapEngine::snap({2.04, 0.03, 0.0}, document, 0.15, 1.0);
    require(result.type == mm::SnapType::Midpoint, "Edge center must midpoint-snap");
    require(result.point == mm::Vec3{2.0, 0.0, 0.0}, "Midpoint snap coordinate mismatch");
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
    camera.setView(mm::StandardView::Front);
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
    camera.setView(mm::StandardView::Front);
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
}

int main() {
    try {
        test_vector_arithmetic();
        test_cube_geometry();
        test_pyramid_geometry();
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
        test_ribbon_groups_commands_into_function_tabs();
        test_ribbon_compact_buttons_fit_above_full_width_canvas();
        test_document_round_trip();
        test_dxf_round_trip_preserves_supported_entities();
        test_dxf_round_trip_preserves_entity_properties();
        test_dxf_reads_closed_lwpolyline();
        test_dxf_insert_expands_block_with_base_scale_rotation_and_byblock_style();
        test_dxf_insert_expands_nested_blocks();
        test_dxf_dimension_displays_generated_block_lines_arrows_and_text();
        test_dxf_import_preserves_layer_color_lineweight_and_linetype();
        test_dxf_import_resolves_extended_aci_and_hidden_layers();
        test_dxf_import_can_be_cancelled_before_large_parse();
        test_document_spatial_index_limits_large_drawing_queries();
        test_document_moves_selected_models_by_displacement();
        test_document_copies_selected_models_and_preserves_metadata();
        test_entity_hit_test_selects_nearest_model_edge();
        test_left_to_right_window_selects_only_fully_contained_models();
        test_right_to_left_crossing_selects_touching_and_contained_models();
        test_projected_3d_window_and_crossing_selection();
        test_snap_prefers_nearby_endpoint();
        test_snap_finds_edge_midpoint();
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
        test_3d_ortho_constrains_to_all_three_global_axes();
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
        std::cout << "All 61 tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
