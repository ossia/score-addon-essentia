include_guard(GLOBAL)

set(SCORE_ESSENTIA_REVISION "23ae7baddecfc3227ba23ad69fa11f73ac5293a4")
set(SCORE_ESSENTIA_ROOT "" CACHE PATH
    "Installed score-patched Essentia DSP profile; empty builds the pinned dependency")
set(SCORE_ESSENTIA_SOURCE_DIR "" CACHE PATH
    "Existing isolated pinned Essentia source tree to patch/build instead of downloading")
set(SCORE_ESSENTIA_BUILD_JOBS 8 CACHE STRING "Parallel jobs for the native Essentia build")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SCORE_ESSENTIA_FFTW REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(SCORE_ESSENTIA_SRC REQUIRED IMPORTED_TARGET samplerate)
if(NOT TARGET Eigen3::Eigen)
  find_package(Eigen3 3.3.4 REQUIRED NO_MODULE)
endif()

file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/essentia-safety.patch" _essentia_patch_sha)
if(SCORE_ESSENTIA_ROOT)
  find_path(SCORE_ESSENTIA_INCLUDE_DIR algorithmfactory.h
    PATHS "${SCORE_ESSENTIA_ROOT}/include/essentia" NO_DEFAULT_PATH NO_CACHE REQUIRED)
  find_library(SCORE_ESSENTIA_LIBRARY NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}essentia${CMAKE_STATIC_LIBRARY_SUFFIX}"
    PATHS "${SCORE_ESSENTIA_ROOT}/lib" "${SCORE_ESSENTIA_ROOT}/lib64"
    NO_DEFAULT_PATH NO_CACHE REQUIRED)
  set(_essentia_patch_header "${SCORE_ESSENTIA_INCLUDE_DIR}/score_essentia_patch_level.h")
  if(NOT EXISTS "${_essentia_patch_header}")
    message(FATAL_ERROR
      "SCORE_ESSENTIA_ROOT must contain the score-patched DSP profile. "
      "Unpatched upstream has unsafe Panning/YinFFT and invalid detector state. "
      "Clear SCORE_ESSENTIA_ROOT to build the pinned patched dependency.")
  endif()
  file(READ "${_essentia_patch_header}" _essentia_patch_header_text)
  if(NOT _essentia_patch_header_text MATCHES "SCORE_ESSENTIA_PATCH_LEVEL 1"
     OR NOT _essentia_patch_header_text MATCHES "${SCORE_ESSENTIA_REVISION}"
     OR NOT _essentia_patch_header_text MATCHES "${_essentia_patch_sha}")
    message(FATAL_ERROR "Installed Essentia does not match this addon's reviewed revision and safety patch; rebuild it.")
  endif()
else()
  if(CMAKE_CROSSCOMPILING OR MSVC)
    message(FATAL_ERROR
      "For this toolchain, provide SCORE_ESSENTIA_ROOT with the matching patched native DSP SDK. "
      "The automatic upstream Waf build supports native Unix toolchains.")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(Git REQUIRED)
  include(ExternalProject)

  # Waf requires an Eigen pkg-config entry even when score provides the headers
  # as a CMake target. Describe the actual target, not another system Eigen ABI.
  get_target_property(_essentia_eigen_includes Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
  set(_essentia_eigen_root "")
  foreach(_include IN LISTS _essentia_eigen_includes)
    if(_include MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
      set(_include "${CMAKE_MATCH_1}")
    endif()
    if(EXISTS "${_include}/Eigen/Core")
      set(_essentia_eigen_root "${_include}")
      break()
    endif()
  endforeach()
  if(NOT _essentia_eigen_root)
    message(FATAL_ERROR "Cannot resolve Eigen3::Eigen headers for Essentia's upstream build")
  endif()
  if(EXISTS "${_essentia_eigen_root}/Eigen/Version")
    file(READ "${_essentia_eigen_root}/Eigen/Version" _eigen_version)
    string(REGEX MATCH "EIGEN_VERSION_STRING[ \t]+\"([^\"]+)\"" _unused "${_eigen_version}")
    set(_essentia_eigen_version "${CMAKE_MATCH_1}")
  else()
    file(READ "${_essentia_eigen_root}/Eigen/src/Core/util/Macros.h" _eigen_version)
    foreach(_part WORLD MAJOR MINOR)
      string(REGEX MATCH "EIGEN_${_part}_VERSION[ \t]+([0-9]+)" _unused "${_eigen_version}")
      set(_eigen_${_part} "${CMAKE_MATCH_1}")
    endforeach()
    set(_essentia_eigen_version "${_eigen_WORLD}.${_eigen_MAJOR}.${_eigen_MINOR}")
  endif()
  if(NOT _essentia_eigen_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "Cannot determine the Eigen version used by Essentia")
  endif()
  set(_essentia_pc "${CMAKE_CURRENT_BINARY_DIR}/native/pkgconfig")
  file(MAKE_DIRECTORY "${_essentia_pc}")
  file(WRITE "${_essentia_pc}/eigen3.pc"
    "Name: Eigen3\nDescription: Eigen headers provided by the host\nVersion: ${_essentia_eigen_version}\nCflags: -I${_essentia_eigen_root}\n")
  set(_essentia_pc_path "${_essentia_pc}")
  if(NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
    string(APPEND _essentia_pc_path ":$ENV{PKG_CONFIG_PATH}")
  endif()
  set(_essentia_prefix "${CMAKE_CURRENT_BINARY_DIR}/native/install")
  set(SCORE_ESSENTIA_INCLUDE_DIR "${_essentia_prefix}/include/essentia")
  set(SCORE_ESSENTIA_LIBRARY "${_essentia_prefix}/lib/libessentia.a")
  file(MAKE_DIRECTORY "${SCORE_ESSENTIA_INCLUDE_DIR}")

  if(SCORE_ESSENTIA_SOURCE_DIR)
    set(_essentia_download SOURCE_DIR "${SCORE_ESSENTIA_SOURCE_DIR}" DOWNLOAD_COMMAND "")
  else()
    set(_essentia_download
      GIT_REPOSITORY https://github.com/MTG/essentia.git
      GIT_TAG "${SCORE_ESSENTIA_REVISION}"
      GIT_SUBMODULES ""
      UPDATE_DISCONNECTED TRUE)
  endif()
  set(_essentia_env "${CMAKE_COMMAND}" -E env
    "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
    "PKG_CONFIG_PATH=${_essentia_pc_path}")
  set(_essentia_arch)
  if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(_essentia_arch --arch=arm64)
  endif()
  ExternalProject_Add(score_essentia_dependency
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/native"
    ${_essentia_download}
    BUILD_IN_SOURCE TRUE
    PATCH_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patch_essentia.py"
      <SOURCE_DIR> --git "${GIT_EXECUTABLE}"
    CONFIGURE_COMMAND ${_essentia_env} "${Python3_EXECUTABLE}" <SOURCE_DIR>/waf configure
      --build-static --lightweight=fftw,libsamplerate --std=c++17
      --prefix=${_essentia_prefix} ${_essentia_arch}
    BUILD_COMMAND ${_essentia_env} "${Python3_EXECUTABLE}" <SOURCE_DIR>/waf build
      -j${SCORE_ESSENTIA_BUILD_JOBS}
    INSTALL_COMMAND ${_essentia_env} "${Python3_EXECUTABLE}" <SOURCE_DIR>/waf install
    BUILD_BYPRODUCTS "${SCORE_ESSENTIA_LIBRARY}")
  ExternalProject_Add_StepDependencies(score_essentia_dependency patch
    "${CMAKE_CURRENT_LIST_DIR}/patch_essentia.py"
    "${CMAKE_CURRENT_LIST_DIR}/essentia-safety.patch")
endif()

add_library(score_essentia_native STATIC IMPORTED GLOBAL)
set_target_properties(score_essentia_native PROPERTIES
  IMPORTED_LOCATION "${SCORE_ESSENTIA_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${SCORE_ESSENTIA_INCLUDE_DIR};${SCORE_ESSENTIA_INCLUDE_DIR}/..")
target_link_libraries(score_essentia_native INTERFACE
  Eigen3::Eigen PkgConfig::SCORE_ESSENTIA_FFTW PkgConfig::SCORE_ESSENTIA_SRC Threads::Threads)
if(TARGET score_essentia_dependency)
  add_dependencies(score_essentia_native score_essentia_dependency)
endif()
