add_library(opencagert_dxr_shell STATIC
  dxr_shell.cpp
  dxr_micro.cpp
)

target_include_directories(opencagert_dxr_shell PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(opencagert_dxr_shell PRIVATE NOMINMAX)

target_link_libraries(opencagert_dxr_shell PUBLIC d3d12.lib opencagert_core)

if(MSVC)
  target_compile_options(opencagert_dxr_shell PRIVATE /W4 /permissive-)
endif()

if(WIN32)
  file(GLOB _dxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
  list(SORT _dxc_candidates)
  list(REVERSE _dxc_candidates)
  list(GET _dxc_candidates 0 DXC_EXECUTABLE)
  if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "DXC not found in Windows SDK")
  endif()

  set(M1_SHELL_DXIL ${CMAKE_CURRENT_BINARY_DIR}/m1_shell.dxil)
  add_custom_command(
    OUTPUT ${M1_SHELL_DXIL}
    COMMAND ${DXC_EXECUTABLE} -T lib_6_3
            -Fo ${M1_SHELL_DXIL}
            ${CMAKE_SOURCE_DIR}/shaders/m1_shell.hlsl
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/m1_shell.hlsl
    COMMENT "Compiling M1 DXR shell shader (DXC)"
  )
  add_custom_target(m1_shell_shader DEPENDS ${M1_SHELL_DXIL})

  set(M3_MICRO_DXIL ${CMAKE_CURRENT_BINARY_DIR}/m3_micro.dxil)
  add_custom_command(
    OUTPUT ${M3_MICRO_DXIL}
    COMMAND ${DXC_EXECUTABLE} -T lib_6_3
            -Fo ${M3_MICRO_DXIL}
            ${CMAKE_SOURCE_DIR}/shaders/m3_micro.hlsl
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/m3_micro.hlsl
    COMMENT "Compiling M3 micro DXR shader (DXC)"
  )
  add_custom_target(m3_micro_shader DEPENDS ${M3_MICRO_DXIL})
  add_dependencies(opencagert_dxr_shell m1_shell_shader m3_micro_shader)
endif()
