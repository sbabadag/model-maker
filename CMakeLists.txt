cmake_minimum_required(VERSION 3.20)
project(ModelMaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 (optional for the ribbon UI). Override with -DQT_ROOT=<path>.
set(QT_ROOT "C:/Qt/6.9.3/mingw_64" CACHE PATH "Qt6 root directory")
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 QUIET COMPONENTS Widgets Core Gui)

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

set(MODELMAKER_QT_SOURCES
    src/qt_main_window.cpp
    include/model_maker/qt_main_window.hpp
    src/opengl_render_backend.cpp
    include/model_maker/opengl_render_backend.hpp)
set(MODELMAKER_QT_MISSING "")
foreach(qt_file ${MODELMAKER_QT_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${qt_file}")
        list(APPEND MODELMAKER_QT_MISSING "${qt_file}")
    endif()
endforeach()
if(Qt6_FOUND AND MODELMAKER_QT_MISSING STREQUAL "")
    add_executable(model-maker
        src/main.cpp
        src/application.cpp
        ${MODELMAKER_QT_SOURCES}
        src/renderer.cpp
        src/view_cube_renderer.cpp
        src/gdi_render_backend.cpp
        src/force_diagram.cpp
    )
    target_include_directories(model-maker PRIVATE include)
    target_link_libraries(model-maker PRIVATE model_maker_core Qt6::Widgets Qt6::Core Qt6::Gui
        gdi32 comdlg32 comctl32 msimg32 opengl32 dwmapi)
    target_link_options(model-maker PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows)
    target_compile_definitions(model-maker PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX QT_NO_ENTRYPOINT)
    target_compile_options(model-maker PRIVATE -Wall -Wextra -Wpedantic)
else()
    message(WARNING "Qt6 not found or Qt sources missing (${MODELMAKER_QT_MISSING}) - "
                    "skipping the model-maker executable; core library, tests and "
                    "benchmarks still build.")
endif()

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
