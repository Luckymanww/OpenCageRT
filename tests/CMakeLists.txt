add_executable(opencagert_tests
  test_main.cpp
  test_math.cpp
  test_clipper.cpp
  test_cage_builder.cpp
  test_micro_scene.cpp
  test_parity.cpp
)

target_link_libraries(opencagert_tests PRIVATE opencagert_core)

if(MSVC)
  target_compile_options(opencagert_tests PRIVATE /W4 /permissive-)
endif()

include(CTest)
add_test(NAME opencagert_tests COMMAND opencagert_tests)
