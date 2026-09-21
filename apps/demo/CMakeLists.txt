add_executable(OpenCageRTDemo WIN32
  main.cpp
  d3d12_app.cpp
  demo_metrics.cpp
  demo_metrics_format.cpp
)

find_package(Git QUIET)
set(OPENCAGERT_GIT_HASH "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE OPENCAGERT_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT OPENCAGERT_GIT_HASH)
    set(OPENCAGERT_GIT_HASH "unknown")
  endif()
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/git_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/git_version.h @ONLY)

target_link_libraries(OpenCageRTDemo PRIVATE
  opencagert_core
  opencagert_dxr_shell
  d3d12.lib
  dxgi.lib
  dxguid.lib
  shell32.lib
)

target_include_directories(OpenCageRTDemo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(OpenCageRTDemo m1_shell_shader m3_micro_shader)

add_custom_command(TARGET OpenCageRTDemo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/src/dxr/m1_shell.dxil
    $<TARGET_FILE_DIR:OpenCageRTDemo>/m1_shell.dxil
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/src/dxr/m3_micro.dxil
    $<TARGET_FILE_DIR:OpenCageRTDemo>/m3_micro.dxil
  COMMENT "Copy DXIL shaders next to OpenCageRTDemo.exe"
)

if(MSVC)
  target_compile_options(OpenCageRTDemo PRIVATE /W4 /permissive-)
  target_compile_definitions(OpenCageRTDemo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
