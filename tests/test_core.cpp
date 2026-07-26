#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_cube.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
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
    require(std::abs(camera.pitch() - 1.5) < 0.001, "Top view pitch mismatch");

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
    require(mm::ViewCube::hitTest(1116, 102, viewportWidth) == mm::StandardView::Top,
            "View cube top face must select top view");
    require(mm::ViewCube::hitTest(1097, 142, viewportWidth) == mm::StandardView::Front,
            "View cube front face must select front view");
    require(mm::ViewCube::hitTest(1137, 142, viewportWidth) == mm::StandardView::Right,
            "View cube right face must select right view");
    require(mm::ViewCube::hitTest(1062, 142, viewportWidth) == mm::StandardView::Left,
            "View cube left control must select left view");
    require(mm::ViewCube::hitTest(1116, 218, viewportWidth) == mm::StandardView::Isometric,
            "View cube home control must select isometric view");
    require(!mm::ViewCube::hitTest(800, 400, viewportWidth).has_value(),
            "Canvas outside the view cube must not select a view");
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

void test_dynamic_input_parses_3d_coordinates() {
    const auto point = mm::parseDynamicPoint(L"12.5,-3.25,7.75", std::nullopt);
    require(point.has_value(), "Absolute 3D dynamic coordinate must parse");
    require(*point == mm::Vec3{12.5, -3.25, 7.75}, "Absolute 3D coordinate mismatch");
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
}

int main() {
    try {
        test_vector_arithmetic();
        test_cube_geometry();
        test_pyramid_geometry();
        test_camera_projects_origin_to_view_center();
        test_camera_uses_parallel_projection_at_every_depth();
        test_camera_standard_view_presets();
        test_view_cube_hit_testing();
        test_document_round_trip();
        test_snap_prefers_nearby_endpoint();
        test_snap_finds_edge_midpoint();
        test_snap_falls_back_to_grid();
        test_grid_snap_can_be_disabled_without_disabling_object_snap();
        test_dynamic_input_parses_absolute_coordinates();
        test_dynamic_input_parses_distance_angle();
        test_dynamic_input_parses_3d_coordinates();
        test_camera_unprojects_screen_point_to_work_plane();
        test_parallel_camera_unprojects_planes_on_either_side_of_view_plane();
        test_projected_snap_finds_3d_endpoint();
        test_rectangle_and_circle_geometry();
        test_rectangle_preserves_3d_work_plane_height();
        std::cout << "All 20 tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
