include_guard(GLOBAL)

set(SCORE_ESSENTIA_REVISION "23ae7baddecfc3227ba23ad69fa11f73ac5293a4")
set(SCORE_ESSENTIA_ROOT "" CACHE PATH
    "Installed score-patched Essentia DSP profile; empty builds the pinned dependency")
set(SCORE_ESSENTIA_SOURCE_DIR "" CACHE PATH
    "Writable staged source tree of the pinned Essentia revision; empty downloads the archive")

function(_score_essentia_build_fftw)
  include(FetchContent)
  # Populate first so EXCLUDE_FROM_ALL also works before CMake 3.28.
  FetchContent_Declare(score_essentia_fftw
    URL https://fftw.org/fftw-3.3.10.tar.gz
    URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR score_essentia_no_cmake_project)
  FetchContent_MakeAvailable(score_essentia_fftw)

  # FFTW's old policy baseline must honor these normal variables, not change
  # shared cache options or inherit score's choices of libraries and tests.
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS 3.13)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.13)
  endif()
  foreach(_option IN ITEMS
      BUILD_SHARED_LIBS BUILD_TESTS ENABLE_OPENMP ENABLE_THREADS WITH_COMBINED_THREADS
      ENABLE_LONG_DOUBLE ENABLE_QUAD_PRECISION ENABLE_SSE ENABLE_SSE2 ENABLE_AVX ENABLE_AVX2)
    set(${_option} OFF)
  endforeach()
  set(ENABLE_FLOAT ON)
  set(DISABLE_FORTRAN ON)
  # The upstream build also tests these results without checking its options.
  set(Threads_FOUND FALSE)
  set(OPENMP_FOUND FALSE)
  foreach(_feature IN ITEMS THREADS OPENMP SIMD SSE SSE2 AVX AVX2 FMA)
    set(HAVE_${_feature} FALSE)
  endforeach()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
  set(CMAKE_UNITY_BUILD OFF)
  set(CMAKE_AUTOMOC OFF)
  set(CMAKE_AUTOUIC OFF)
  set(CMAKE_AUTORCC OFF)
  add_subdirectory("${score_essentia_fftw_SOURCE_DIR}" "${score_essentia_fftw_BINARY_DIR}"
    EXCLUDE_FROM_ALL)
  target_include_directories(fftw3f INTERFACE
    "$<BUILD_INTERFACE:${score_essentia_fftw_SOURCE_DIR}/api>")
  if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Intel")
    # FFTW 3.3.10 adds /bigobj as a definition instead of a compiler option.
    get_target_property(_definitions fftw3f COMPILE_DEFINITIONS)
    list(REMOVE_ITEM _definitions /bigobj)
    set_property(TARGET fftw3f PROPERTY COMPILE_DEFINITIONS "${_definitions}")
    target_compile_options(fftw3f PRIVATE /bigobj)
  endif()
endfunction()

function(_score_add_essentia)
  set(_patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/essentia-safety.patch")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}")
  if(SCORE_ESSENTIA_ROOT)
    find_path(_include_dir algorithmfactory.h
      PATHS "${SCORE_ESSENTIA_ROOT}/include/essentia" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    find_library(_library NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}essentia${CMAKE_STATIC_LIBRARY_SUFFIX}"
      PATHS "${SCORE_ESSENTIA_ROOT}/lib" "${SCORE_ESSENTIA_ROOT}/lib64"
      NO_DEFAULT_PATH NO_CACHE REQUIRED)
    set(_patch_header "${_include_dir}/score_essentia_patch_level.h")
    if(NOT EXISTS "${_patch_header}")
      message(FATAL_ERROR
        "SCORE_ESSENTIA_ROOT must contain the score-patched DSP profile. "
        "Unpatched upstream has unsafe Panning/YinFFT and invalid detector state. "
        "Clear SCORE_ESSENTIA_ROOT to build the pinned patched dependency.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch_header}")
    file(SHA256 "${_patch}" _patch_sha)
    file(READ "${_patch_header}" _patch_header_text)
    if(NOT _patch_header_text MATCHES "#define SCORE_ESSENTIA_PATCH_LEVEL 1([\r\n]|$)"
       OR NOT _patch_header_text MATCHES "#define SCORE_ESSENTIA_UPSTREAM_REVISION \"${SCORE_ESSENTIA_REVISION}\""
       OR NOT _patch_header_text MATCHES "#define SCORE_ESSENTIA_PATCH_SHA256 \"${_patch_sha}\"")
      message(FATAL_ERROR "Installed Essentia does not match this addon's reviewed revision and safety patch; rebuild it.")
    endif()
    add_library(score_essentia_native STATIC IMPORTED GLOBAL)
    set_target_properties(score_essentia_native PROPERTIES IMPORTED_LOCATION "${_library}")
    target_include_directories(score_essentia_native INTERFACE "${_include_dir}" "${_include_dir}/..")
    set(_visibility INTERFACE)
  else()
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
    set(_portability_patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/essentia-portability.patch")
    set(_profile_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_essentia_profile.py")
    file(GLOB_RECURSE _algorithm_headers LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${_src}/algorithms/*.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${_patch_script}" "${_portability_patch}" "${_profile_script}"
      "${_source}/VERSION" "${_source}/utils/algorithms_info.py" ${_algorithm_headers})
    foreach(_patch_file IN ITEMS "${_patch}" "${_portability_patch}")
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

    # Same core and bundled helpers as Waf's --lightweight=fftw,libsamplerate.
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
      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
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
    set(_visibility PUBLIC)
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

  if(TARGET FFTW3::fftw3f)
    set(_fftw FFTW3::fftw3f)
  elseif(TARGET fftw3f)
    set(_fftw fftw3f)
  else()
    if(SCORE_ESSENTIA_ROOT)
      set(_fftw_include_hints "${SCORE_ESSENTIA_ROOT}/include")
      set(_fftw_library_hints "${SCORE_ESSENTIA_ROOT}/lib" "${SCORE_ESSENTIA_ROOT}/lib64")
    endif()
    find_path(SCORE_ESSENTIA_FFTW_INCLUDE_DIR fftw3.h HINTS ${_fftw_include_hints} NO_CACHE)
    find_library(SCORE_ESSENTIA_FFTW_LIBRARY NAMES fftw3f fftw3f-3 libfftw3f-3
      HINTS ${_fftw_library_hints} NO_CACHE)
    if(SCORE_ESSENTIA_FFTW_INCLUDE_DIR AND SCORE_ESSENTIA_FFTW_LIBRARY)
      add_library(score_essentia_fftw_system UNKNOWN IMPORTED)
      set_target_properties(score_essentia_fftw_system PROPERTIES
        IMPORTED_LOCATION "${SCORE_ESSENTIA_FFTW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SCORE_ESSENTIA_FFTW_INCLUDE_DIR}")
      if(UNIX)
        target_link_libraries(score_essentia_fftw_system INTERFACE m)
      endif()
      set(_fftw score_essentia_fftw_system)
    else()
      _score_essentia_build_fftw()
      set(_fftw fftw3f)
    endif()
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(score_essentia_native ${_visibility}
    ${_eigen} ${_fftw} ${_samplerate} Threads::Threads)
  target_compile_features(score_essentia_native ${_visibility} cxx_std_17)
  target_compile_definitions(score_essentia_native ${_visibility} ESSENTIA_STATIC=1)
  if(WIN32)
    # Match the empty replacement in upstream config.h, including in headers
    # that check OS_WIN32 before they reach config.h.
    target_compile_definitions(score_essentia_native ${_visibility}
      OS_WIN32= NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
  endif()
  if(MSVC)
    target_compile_options(score_essentia_native ${_visibility} $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
  endif()
  if(EMSCRIPTEN)
    # Match score's final link so Essentia exceptions can be caught by callers.
    target_compile_options(score_essentia_native ${_visibility} $<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>)
    target_link_options(score_essentia_native INTERFACE -fwasm-exceptions)
  endif()
endfunction()

_score_add_essentia()
