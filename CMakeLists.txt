cmake_minimum_required(VERSION 3.20)
project(your-sloppy-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCASCADE REQUIRED)
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

set(OCCT_LIBS
  TKernel TKMath TKG3d TKBRep TKTopAlgo TKMesh
  TKService TKV3d TKOpenGl
  TKLCAF TKXCAF TKXSBase TKDESTEP)

add_executable(your-sloppy-viewer
  src/main.cpp
  src/App.cpp
  src/GlfwOcctWindow.cpp
  src/StepLoader.cpp
  src/Viewer.cpp)
target_include_directories(your-sloppy-viewer SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(your-sloppy-viewer PRIVATE ${OCCT_LIBS} glfw OpenGL::GL X11::X11 Threads::Threads)
target_compile_options(your-sloppy-viewer PRIVATE -Wall -Wextra)

add_executable(make_fixture EXCLUDE_FROM_ALL tools/make_fixture.cpp)
target_include_directories(make_fixture SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(make_fixture PRIVATE
  TKernel TKMath TKG3d TKBRep TKTopAlgo TKPrim TKBO TKFillet
  TKLCAF TKXCAF TKXSBase TKDESTEP)
