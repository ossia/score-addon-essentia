include_guard(GLOBAL)

set(SCORE_ESSENTIA_REVISION "23ae7baddecfc3227ba23ad69fa11f73ac5293a4")
set(SCORE_ESSENTIA_SOURCE_DIR "" CACHE PATH
    "Writable staged source tree of the pinned Essentia revision; empty downloads the archive")

function(_score_add_essentia)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(Git REQUIRED)
  include(FetchContent)
  if(SCORE_ESSENTIA_SOURCE_DIR)
    file(REAL_PATH "${SCORE_ESSENTIA_SOURCE_DIR}" _source
      BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  else()
    FetchContent_Declare(score_essentia_source
      URL "https://codeload.github.com/MTG/essentia/tar.gz/${SCORE_ESSENTIA_REVISION}"
      URL_HASH SHA256=41d8ce1308b3471059c67f9e6c857a97e766dfb0b0b741cfedea6db624738c32
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(score_essentia_source)
    file(REAL_PATH "${score_essentia_source_SOURCE_DIR}" _source)
  endif()
  set(_src "${_source}/src")
  set(_generated "${CMAKE_CURRENT_BINARY_DIR}/native/generated")
  set(_patch_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patch_essentia.py")
  set(_patches
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/essentia-safety.patch"
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/essentia-portability.patch"
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/essentia-ossia-fft.patch")
  set(_profile_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_essentia_profile.py")
  file(GLOB_RECURSE _algorithm_headers LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${_src}/algorithms/*.h")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_patch_script}" ${_patches} "${_profile_script}"
    "${_source}/VERSION" "${_source}/utils/algorithms_info.py" ${_algorithm_headers})
  foreach(_patch_file IN LISTS _patches)
    file(STRINGS "${_patch_file}" _patched_files REGEX "^\\+\\+\\+ b/")
    foreach(_patched_file IN LISTS _patched_files)
      string(SUBSTRING "${_patched_file}" 6 -1 _patched_path)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}/${_patched_path}")
    endforeach()
  endforeach()

  # A downloaded archive can sit under the enclosing score worktree. Do not
  # let git apply discover that repository and skip the archive's paths.
  get_filename_component(_source_parent "${_source}" DIRECTORY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_source_parent}"
      "${Python3_EXECUTABLE}" -B "${_patch_script}" "${_source}" --git "${GIT_EXECUTABLE}"
    RESULT_VARIABLE _patch_result OUTPUT_VARIABLE _patch_output ERROR_VARIABLE _patch_errors)
  if(NOT _patch_result STREQUAL "0")
    message(FATAL_ERROR "Could not patch Essentia (${_patch_result}):\n${_patch_output}${_patch_errors}")
  endif()
  file(MAKE_DIRECTORY "${_generated}")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -B "${_profile_script}" "${_source}" "${_generated}"
    RESULT_VARIABLE _profile_result OUTPUT_VARIABLE _profile_output ERROR_VARIABLE _profile_errors)
  if(NOT _profile_result STREQUAL "0")
    message(FATAL_ERROR "Could not generate the Essentia DSP profile (${_profile_result}):\n${_profile_output}${_profile_errors}")
  endif()
  include("${_generated}/sources.cmake")

  # Use the lightweight core and bundled helpers, with libossia for FFTs.
  file(GLOB_RECURSE _core LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${_src}/essentia/*.cpp")
  list(FILTER _core EXCLUDE REGEX "/(audiocontext|jsonconvert|yamlast)\\.cpp$")
  add_library(score_essentia_native STATIC
    ${_core}
    ${score_essentia_algorithm_sources}
    "${_generated}/essentia_algorithms_reg.cpp"
    "${_src}/3rdparty/spline/splineutil.cpp"
    "${_src}/3rdparty/nnls/nnls.c"
    "${_src}/3rdparty/cephes/bessel/gamma.cpp"
    "${_src}/3rdparty/cephes/bessel/hyperg.cpp"
    "${_src}/3rdparty/cephes/bessel/iv.cpp"
    "${_src}/3rdparty/cephes/bessel/polevl.cpp")
  set_target_properties(score_essentia_native PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON DISABLE_PRECOMPILE_HEADERS ON
    UNITY_BUILD OFF AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  target_include_directories(score_essentia_native
    PUBLIC "${_generated}" "${_src}" "${_src}/essentia"
    PRIVATE
      "${_src}/essentia/scheduler" "${_src}/essentia/streaming"
      "${_src}/essentia/streaming/algorithms" "${_src}/essentia/utils"
      "${_src}/3rdparty" "${_src}/3rdparty/spline"
      "${_src}/3rdparty/nnls" "${_src}/3rdparty/cephes/bessel")
  target_compile_definitions(score_essentia_native PRIVATE
    EIGEN_MPL2_ONLY EIGEN_PERMANENTLY_DISABLE_STUPID_WARNINGS)
  if(MSVC)
    target_compile_options(score_essentia_native PRIVATE
      /W2 /bigobj /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      target_compile_options(score_essentia_native PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(score_essentia_native PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall>)
  endif()

  if(NOT TARGET Eigen3::Eigen)
    find_package(Eigen3 QUIET CONFIG)
    if(TARGET Eigen3::Eigen AND Eigen3_VERSION AND Eigen3_VERSION VERSION_LESS 3.3.4)
      message(FATAL_ERROR "The Essentia DSP profile needs Eigen 3.3.4 or newer (found ${Eigen3_VERSION}).")
    endif()
  endif()
  if(NOT TARGET SampleRate::samplerate)
    find_package(SampleRate QUIET CONFIG)
  endif()
  if(NOT TARGET Eigen3::Eigen OR NOT TARGET SampleRate::samplerate)
    find_package(PkgConfig REQUIRED)
  endif()
  set(_eigen Eigen3::Eigen)
  if(NOT TARGET Eigen3::Eigen)
    pkg_check_modules(SCORE_ESSENTIA_EIGEN REQUIRED IMPORTED_TARGET "eigen3>=3.3.4")
    set(_eigen PkgConfig::SCORE_ESSENTIA_EIGEN)
  endif()
  set(_samplerate SampleRate::samplerate)
  if(NOT TARGET SampleRate::samplerate)
    pkg_check_modules(SCORE_ESSENTIA_SRC REQUIRED IMPORTED_TARGET samplerate)
    set(_samplerate PkgConfig::SCORE_ESSENTIA_SRC)
  endif()

  find_package(Threads REQUIRED)
  target_link_libraries(score_essentia_native PUBLIC ${_eigen} ${_samplerate} Threads::Threads)
  if(TARGET ossia)
    target_link_libraries(score_essentia_native PUBLIC ossia)
  elseif(TARGET ossia::ossia)
    target_link_libraries(score_essentia_native PUBLIC ossia::ossia)
  else()
    # Out-of-tree score add-ons resolve libossia symbols from the score host.
    target_link_libraries(score_essentia_native PRIVATE score_lib_base)
  endif()
  target_compile_features(score_essentia_native PUBLIC cxx_std_20)
  target_compile_definitions(score_essentia_native PUBLIC ESSENTIA_STATIC=1)
  if(WIN32)
    # Match the empty replacement in upstream config.h, including in headers
    # that check OS_WIN32 before they reach config.h.
    target_compile_definitions(score_essentia_native PUBLIC
      OS_WIN32= NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
  endif()
  if(MSVC)
    target_compile_options(score_essentia_native PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
  endif()
  if(EMSCRIPTEN)
    # Match score's final link so Essentia exceptions can be caught by callers.
    target_compile_options(score_essentia_native PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>)
    target_link_options(score_essentia_native INTERFACE -fwasm-exceptions)
  endif()
endfunction()

_score_add_essentia()
