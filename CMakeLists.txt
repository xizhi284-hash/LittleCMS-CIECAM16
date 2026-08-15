cmake_minimum_required(VERSION 3.16...3.29)

project(lcms2_ciecam16
  VERSION 2.19
  LANGUAGES C
)

# Fork branding: full human-facing version string. project(VERSION) itself must
# stay numeric (CMake rejects "2.19-c16.1"); it also feeds the .pc Version field
# and the CMake config-version file, which expect the upstream-style number.
set(LCMS2_CIECAM16_FULL_VERSION "${PROJECT_VERSION}-c16.1")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(Lcms2Options)
include(Lcms2Features)
include(Lcms2Library)
include(Lcms2Tools)
include(Lcms2Plugins)
include(Lcms2Tests)
include(Lcms2Packaging)

lcms2_setup_options()
lcms2_detect_features()

if(LCMS2_BUILD_TESTS)
  enable_testing()
endif()

lcms2_add_library()

if(LCMS2_BUILD_TOOLS)
  lcms2_add_tools()
endif()

lcms2_add_plugins()

if(LCMS2_BUILD_TESTS)
  lcms2_add_tests()
endif()

lcms2_setup_packaging()


