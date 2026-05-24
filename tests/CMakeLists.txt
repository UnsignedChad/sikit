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
    touchstone_writer_test.cpp
    impedance_test.cpp
    diff_pair_test.cpp
    stackup_test.cpp
    trace_impedance_test.cpp
    channel_synthesis_test.cpp
    ibis_test.cpp
    djordjevic_sarkar_test.cpp
    channel_dispersion_test.cpp
    sparam_test.cpp
    eye_test.cpp
    eye_ramp_test.cpp
    eye_metrics_test.cpp
    reference_data_test.cpp
    eye_mask_test.cpp
    fft_test.cpp
    channel_test.cpp
    em2d_test.cpp
    rlgc_test.cpp
    fdm_refined_test.cpp
)

target_link_libraries(sikit_tests PRIVATE
    sikit_core
    Catch2::Catch2WithMain
)

target_compile_options(sikit_tests PRIVATE
    -Wall -Wextra -Wpedantic
)

catch_discover_tests(sikit_tests)
