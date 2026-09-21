add_library(opencagert_core STATIC
  math.cpp
  cage_builder.cpp
  clipper.cpp
  animation.cpp
  micro_scene.cpp
  image_parity.cpp
)

target_include_directories(opencagert_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(opencagert_core PRIVATE NOMINMAX)

if(MSVC)
  target_compile_options(opencagert_core PRIVATE /W4 /permissive-)
endif()
