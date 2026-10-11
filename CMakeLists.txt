cmake_minimum_required(VERSION 3.20)
project(your-sloppy-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCASCADE REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_library(viewer-core STATIC
  src/Camera.cpp
  src/Renderer.cpp
  src/StepLoader.cpp)
target_include_directories(viewer-core PUBLIC src)
target_include_directories(viewer-core SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(viewer-core
  PUBLIC PkgConfig::EPOXY
  PRIVATE TKernel TKMath TKG3d TKBRep TKTopAlgo TKMesh TKLCAF TKXCAF TKXSBase TKDESTEP)
target_compile_options(viewer-core PRIVATE -Wall -Wextra)

pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4 libadwaita-1)

add_executable(your-sloppy-viewer
  src/main.cpp
  src/Window.cpp)
target_link_libraries(your-sloppy-viewer PRIVATE viewer-core PkgConfig::GTK Threads::Threads)
target_compile_options(your-sloppy-viewer PRIVATE -Wall -Wextra)

add_executable(camera_test EXCLUDE_FROM_ALL tests/camera_test.cpp)
target_link_libraries(camera_test PRIVATE viewer-core)

pkg_check_modules(EGL IMPORTED_TARGET egl)
if(EGL_FOUND)
  add_executable(render_test EXCLUDE_FROM_ALL tests/render_test.cpp)
  target_link_libraries(render_test PRIVATE viewer-core PkgConfig::EGL)
endif()

add_executable(make_fixture EXCLUDE_FROM_ALL tools/make_fixture.cpp)
target_include_directories(make_fixture SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(make_fixture PRIVATE
  TKernel TKMath TKG3d TKBRep TKTopAlgo TKPrim TKBO TKFillet
  TKLCAF TKXCAF TKXSBase TKDESTEP)
