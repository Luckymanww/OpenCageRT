cmake_minimum_required(VERSION 3.24)
project(OpenCageRT VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(OPEN_CAGERT_BUILD_DEMO "Build D3D12 DXR demo (Windows only)" ON)
option(OPEN_CAGERT_BUILD_TESTS "Build CPU unit tests" ON)

add_subdirectory(src/core)

if(WIN32)
  add_subdirectory(src/dxr)
endif()

if(OPEN_CAGERT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(OPEN_CAGERT_BUILD_DEMO AND WIN32)
  add_subdirectory(apps/demo)
endif()
