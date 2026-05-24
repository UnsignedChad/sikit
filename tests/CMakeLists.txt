find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(sikit_tests
    smoke_test.cpp
    sexpr_test.cpp
    model_test.cpp
    parser_test.cpp
    camera_test.cpp
    zone_mesher_test.cpp
    segment_mesher_test.cpp
    via_mesher_test.cpp
    hit_test.cpp
    touchstone_test.cpp
    impedance_test.cpp
    diff_pair_test.cpp
    trace_impedance_test.cpp
    sparam_test.cpp
)

target_link_libraries(sikit_tests PRIVATE
    sikit_core
    Catch2::Catch2WithMain
)

target_compile_options(sikit_tests PRIVATE
    -Wall -Wextra -Wpedantic
)

catch_discover_tests(sikit_tests)
