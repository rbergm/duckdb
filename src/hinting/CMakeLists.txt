
include(FetchContent)

#
# Antlr4 config
#
# See https://github.com/antlr/antlr4/discussions/4418 for reference
#
#

set(ANTLR4_WITH_STATIC_CRT OFF) # using /MD flag for antlr4_runtime (for Visual C++ compilers only)
set(ANTLR_BUILD_SHARED OFF)     # in my case I built the static version
set(ANTLR_TAG 4.13.1)           # to specify the GitHub tag for the jar too
set(DISABLE_WARNINGS ON)        # Antlr gives a few deprecation warnings for me
set(ANTLR_BUILD_CPP_TESTS OFF)  # to avoid creating dist folder
set(ANTLR_BUILD_CPP_EXAMPLES OFF)

set(ANTLR_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/antlr-${ANTLR_TAG}-complete.jar") # a local path for antlr.jar
if(NOT EXISTS "${ANTLR_EXECUTABLE}")
    message(STATUS "Downloading antlr-${ANTLR_TAG}-complete.jar to ${ANTLR_EXECUTABLE}")
    file(
        DOWNLOAD
        "https://www.antlr.org/download/antlr-${ANTLR_TAG}-complete.jar"
        "${ANTLR_EXECUTABLE}"
    )
endif()
FetchContent_Declare(
    antlr
    GIT_REPOSITORY https://github.com/antlr/antlr4
    GIT_TAG ${ANTLR_TAG}
    SOURCE_SUBDIR "runtime/Cpp"
)
FetchContent_MakeAvailable(antlr) # populates antlr_SOURCE_DIR
include(${antlr_SOURCE_DIR}/runtime/Cpp/cmake/FindANTLR.cmake) # populates ANTLR_TARGET

antlr_target(HintBlockGrammar HintBlock.g4 BOTH LISTENER package tud)

add_library(
    planner_hinting
    OBJECT
    intermediate.cpp
    planner_hints.cpp
    join_tree.cpp
    hint_parser.cpp
    ${ANTLR_HintBlockGrammar_CXX_OUTPUTS})

# target_link_libraries(planner_hinting PRIVATE antlr4_static)

target_include_directories(planner_hinting PRIVATE
    "${antlr_SOURCE_DIR}/runtime/Cpp/runtime/src"
    ${ANTLR_HintBlockGrammar_OUTPUT_DIR})

set(ALL_OBJECT_FILES
    ${ALL_OBJECT_FILES}
    $<TARGET_OBJECTS:planner_hinting>
    $<TARGET_OBJECTS:antlr4_static>
    PARENT_SCOPE)
