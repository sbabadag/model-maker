cmake_minimum_required(VERSION 3.20)
project(ModelMaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(model_maker_core
    src/geometry.cpp
    src/camera.cpp
    src/view_cube.cpp
    src/ribbon_layout.cpp
    src/performance.cpp
    src/dxf.cpp
    src/document.cpp
    src/drafting.cpp
)
target_include_directories(model_maker_core PUBLIC include)
target_compile_options(model_maker_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(model-maker WIN32
    src/main.cpp
    src/application.cpp
    src/renderer.cpp
    src/view_cube_renderer.cpp
    src/gdi_render_backend.cpp
    src/force_diagram.cpp
)
target_link_libraries(model-maker PRIVATE model_maker_core gdi32 comdlg32 comctl32 msimg32)
target_link_options(model-maker PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_compile_definitions(model-maker PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(model-maker PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(model_maker_tests tests/test_core.cpp)
    target_link_libraries(model_maker_tests PRIVATE model_maker_core)
    target_link_options(model_maker_tests PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME model_maker_tests COMMAND model_maker_tests)

    add_executable(model_maker_render_prep_benchmark tests/benchmark_render_prep.cpp)
    target_link_libraries(model_maker_render_prep_benchmark PRIVATE model_maker_core)
    target_link_options(model_maker_render_prep_benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_render_prep_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)

    add_executable(model_maker_spatial_query_benchmark tests/benchmark_spatial_query.cpp)
    target_link_libraries(model_maker_spatial_query_benchmark PRIVATE model_maker_core)
    target_link_options(model_maker_spatial_query_benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(model_maker_spatial_query_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()
