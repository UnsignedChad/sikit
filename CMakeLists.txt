cmake_minimum_required(VERSION 3.25)
project(sikit
    VERSION 0.0.1
    DESCRIPTION "Open-source Signal Integrity analysis for KiCad PCBs"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)

# Core static library: parsing, model, render, geometry helpers.
# Linked by both the GUI binary and the test binary. SI-specific modules
# (touchstone, sparam, solver, specs) will land here as they're written.
add_library(sikit_core STATIC
    src/sexpr/SExpr.cpp
    src/parser/KicadPcbParser.cpp
    src/model/HitTest.cpp
    src/model/Stackup.cpp
    src/render/Camera2D.cpp
    src/render/ZoneMesher.cpp
    src/render/SegmentMesher.cpp
    src/render/ViaMesher.cpp
    src/touchstone/Touchstone.cpp
    src/touchstone/TouchstoneWriter.cpp
    src/touchstone/TouchstoneCsv.cpp
    src/impedance/Impedance.cpp
    src/highspeed/DiffPair.cpp
    src/analysis/TraceImpedance.cpp
    src/analysis/ChannelSynthesis.cpp
    src/ibis/Ibis.cpp
    src/ibis/Ami.cpp
    src/dispersion/DjordjevicSarkar.cpp
    src/sparam/SParam.cpp
    src/eye/Eye.cpp
    src/eye/EyeMetrics.cpp
    src/specs/EyeMask.cpp
    src/dsp/Fft.cpp
    src/dsp/ChannelResponse.cpp
    src/em2d/CrossSection.cpp
    src/em2d/FdmSolver.cpp
    src/em2d/Rlgc.cpp
)
target_include_directories(sikit_core PUBLIC src)
target_include_directories(sikit_core PUBLIC third_party)
target_link_libraries(sikit_core PUBLIC
    Eigen3::Eigen
    spdlog::spdlog
)
target_compile_options(sikit_core PRIVATE -Wall -Wextra -Wpedantic)

# GUI executable.
add_executable(sikit
    src/main.cpp
    src/MainWindow.cpp
    src/PcbCanvas.cpp
    src/LayerPanel.cpp
    src/EyeWindow.cpp
)
target_link_libraries(sikit PRIVATE
    sikit_core
    Qt6::Widgets
    Qt6::OpenGLWidgets
    CLI11::CLI11
)
target_compile_options(sikit PRIVATE -Wall -Wextra -Wpedantic)

option(SIKIT_BUILD_TESTS "Build sikit unit tests" ON)
if(SIKIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
