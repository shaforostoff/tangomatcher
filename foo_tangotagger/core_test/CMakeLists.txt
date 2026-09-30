# The embedded lyrics and the title matching, checked without a host.
add_executable(core_test main.cpp)
target_link_libraries(core_test PRIVATE tangotagger_core)
set_target_properties(core_test PROPERTIES FOLDER "tests")
if(MSVC)
    target_compile_options(core_test PRIVATE /W4)
endif()
add_test(NAME core_test COMMAND core_test)
