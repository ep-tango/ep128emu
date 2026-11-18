cmake_minimum_required(VERSION 3.16)
project(twodfx_portable LANGUAGES C)

# All production sources and assets are contained under src/.
set(TWODFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
file(GLOB TWODFX_ORIGINAL_EGGS CONFIGURE_DEPENDS
  "${TWODFX_ROOT}/demos/egg_*.c")
list(LENGTH TWODFX_ORIGINAL_EGGS TWODFX_EGG_COUNT)
if(NOT TWODFX_EGG_COUNT EQUAL 22)
  message(FATAL_ERROR "Expected all 22 v1.1.4 egg translation units, found ${TWODFX_EGG_COUNT}")
endif()

add_library(twodfx_emu STATIC
  "${TWODFX_ROOT}/core/twodfx_emu.c"
  "${TWODFX_ROOT}/core/twodfx_raster.c"
  "${TWODFX_ROOT}/core/twodfx_3d.c"
  "${TWODFX_ROOT}/assets/logo_2dfx_asset.c"
  "${TWODFX_ROOT}/demos/firmware_page.c"
  ${TWODFX_ORIGINAL_EGGS}
)
set_target_properties(twodfx_emu PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

target_include_directories(twodfx_emu PUBLIC
  "$<BUILD_INTERFACE:${TWODFX_ROOT}/include>")
target_include_directories(twodfx_emu PRIVATE
  "${TWODFX_ROOT}/core"
  "${TWODFX_ROOT}/assets"
  "${TWODFX_ROOT}/demos"
)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(TWODFX_BUILD_TESTS_DEFAULT ON)
else()
  set(TWODFX_BUILD_TESTS_DEFAULT OFF)
endif()
option(TWODFX_BUILD_TESTS "Build portable 2dfx tests" ${TWODFX_BUILD_TESTS_DEFAULT})
if(TWODFX_BUILD_TESTS)
  enable_language(CXX)
  enable_testing()
  add_executable(twodfx_eggs_smoke tests/test_eggs_smoke.c)
  target_link_libraries(twodfx_eggs_smoke PRIVATE twodfx_emu)
  add_test(NAME original_eggs_and_firmware_page COMMAND twodfx_eggs_smoke)
  add_executable(twodfx_cpp_bridge_smoke tests/test_cpp_bridge.cpp)
  set_target_properties(twodfx_cpp_bridge_smoke PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
  target_include_directories(twodfx_cpp_bridge_smoke PRIVATE "${TWODFX_ROOT}/adapter")
  target_link_libraries(twodfx_cpp_bridge_smoke PRIVATE twodfx_emu)
  add_test(NAME cpp_adapter_compiles COMMAND twodfx_cpp_bridge_smoke)
endif()

if(TWODFX_BUILD_TESTS)
  foreach(name protocol raster_vectors demo_state layers resource_queue)
    add_executable(twodfx_${name} tests/test_${name}.c)
    target_link_libraries(twodfx_${name} PRIVATE twodfx_emu)
    add_test(NAME ${name} COMMAND twodfx_${name})
  endforeach()
endif()
