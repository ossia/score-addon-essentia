cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

if(NOT TARGET score_lib_base)
  include(ScoreExternalAddon)
endif()
project(score_addon_essentia LANGUAGES C CXX)

find_package(Avendish QUIET)
if(NOT Avendish_FOUND)
  include(FetchContent)
  FetchContent_Declare(Avendish
    GIT_REPOSITORY https://github.com/celtera/avendish
    GIT_TAG 019d39c9e13d562a731682744f17d42a6daeca18)
  FetchContent_Populate(Avendish)
  list(APPEND CMAKE_PREFIX_PATH "${avendish_SOURCE_DIR}")
  find_package(Avendish REQUIRED)
endif()
if(COMMAND score_common_setup)
  score_common_setup()
endif()

include(cmake/Essentia.cmake)

add_library(score_essentia_processors STATIC
  Essentia/Analysis.cpp
  Essentia/Statistics.cpp
  Essentia/Filterbanks.cpp
  Essentia/Spectral.cpp
  Essentia/Tonal.cpp
  Essentia/Correlation.cpp
  Essentia/EnvelopeShape.cpp
  Essentia/Quality.cpp)
target_compile_features(score_essentia_processors PUBLIC cxx_std_23)
target_include_directories(score_essentia_processors PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(score_essentia_processors PUBLIC score_essentia_native score_plugin_avnd)
set_target_properties(score_essentia_processors PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF UNITY_BUILD OFF)

avnd_addon_init(NAME score_addon_essentia)

# Each row names a stable serialized process, not a runtime-selected algorithm.
# Shared native implementations are compiled once, separately from factories.
set(_essentia_nodes
  "Statistics|AudioStatistics|essentia_audio_statistics"
  "Statistics|EffectiveDuration|essentia_effective_duration"
  "Statistics|SilenceRate|essentia_silence_rate"
  "Statistics|Loudness|essentia_loudness"
  "Statistics|Larm|essentia_larm"
  "Statistics|LoudnessVickers|essentia_loudness_vickers"
  "Statistics|DistributionShape|essentia_distribution_shape"
  "Statistics|PowerMean|essentia_power_mean"
  "Filterbanks|BarkBands|essentia_bark_bands"
  "Filterbanks|ERBBands|essentia_erb_bands"
  "Filterbanks|TriangularBands|essentia_triangular_bands"
  "Filterbanks|TriangularBarkBands|essentia_triangular_bark_bands"
  "Filterbanks|BFCC|essentia_bfcc"
  "Filterbanks|GFCC|essentia_gfcc"
  "Filterbanks|SpectrumToCent|essentia_spectrum_to_cent"
  "Spectral|SpectralContrast|essentia_spectral_contrast"
  "Spectral|SpectralComplexity|essentia_spectral_complexity"
  "Spectral|SpectralPeaks|essentia_spectral_peaks"
  "Spectral|SpectralWhitening|essentia_spectral_whitening"
  "Spectral|StrongPeak|essentia_strong_peak"
  "Spectral|SpectralCentroidTime|essentia_spectral_centroid_time"
  "Spectral|LogSpectrum|essentia_log_spectrum"
  "Spectral|PitchSalience|essentia_pitch_salience"
  "Spectral|Panning|essentia_panning"
  "Tonal|Chroma|essentia_chroma"
  "Tonal|InstantKey|essentia_instant_key"
  "Tonal|TuningFrequency|essentia_tuning_frequency"
  "Tonal|PitchSalienceFunction|essentia_pitch_salience_function"
  "Tonal|HarmonicFeatures|essentia_harmonic_features"
  "Tonal|Dissonance|essentia_dissonance"
  "Correlation|AutoCorrelation|essentia_auto_correlation"
  "Correlation|WarpedAutoCorrelation|essentia_warped_auto_correlation"
  "Correlation|LPC|essentia_lpc"
  "Correlation|Welch|essentia_welch"
  "Correlation|DCT|essentia_dct"
  "EnvelopeShape|EnvelopeShape|essentia_envelope_shape"
  "EnvelopeShape|StrongDecay|essentia_strong_decay"
  "EnvelopeShape|Decrease|essentia_decrease"
  "Quality|ClickDetector|essentia_click_detector"
  "Quality|DiscontinuityDetector|essentia_discontinuity_detector"
  "Quality|GapsDetector|essentia_gaps_detector"
  "Quality|SaturationDetector|essentia_saturation_detector"
  "Quality|NoiseBurstDetector|essentia_noise_burst_detector"
  "Quality|SNR|essentia_snr"
  "Quality|FalseStereoDetector|essentia_false_stereo_detector"
  "Quality|StartStopSilence|essentia_start_stop_silence")
foreach(_node IN LISTS _essentia_nodes)
  string(REPLACE "|" ";" _fields "${_node}")
  list(GET _fields 0 _family)
  list(GET _fields 1 _class)
  list(GET _fields 2 _c_name)
  avnd_addon_object(
    BASE score_addon_essentia C_NAME "${_c_name}"
    CLASS "${_class}" NAMESPACE EssentiaNodes
    DEPENDENCIES score_essentia_processors
    SOURCES "Essentia/${_family}.hpp")
endforeach()
avnd_addon_finalize(NAME score_addon_essentia
  UUID 203cc2b8-51a4-4c03-832d-a781996ff074 VERSION 1)

if(COMMAND score_register_license)
  score_register_license(Essentia
    URL "https://essentia.upf.edu"
    HEADER "Essentia 2.1-beta6-dev, MTG / Universitat Pompeu Fabra; AGPL-3.0-or-later. Native safety and numerical corrections are distributed in cmake/essentia-safety.patch."
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

option(SCORE_ESSENTIA_ANALYSIS_PROBE "Build the Essentia registry and DSP analysis probe" OFF)
if(SCORE_ESSENTIA_ANALYSIS_PROBE)
  add_executable(score_essentia_analysis_probe EXCLUDE_FROM_ALL tools/AnalysisProbe.cpp)
  target_compile_features(score_essentia_analysis_probe PRIVATE cxx_std_23)
  target_link_libraries(score_essentia_analysis_probe PRIVATE
    score_essentia_native ${QT_PREFIX}::Core)
  set_target_properties(score_essentia_analysis_probe PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF UNITY_BUILD OFF)
endif()

if(SCORE_TESTING)
  add_executable(score_essentia_native_regression EXCLUDE_FROM_ALL tests/NativeRegression.cpp)
  target_compile_features(score_essentia_native_regression PRIVATE cxx_std_23)
  target_link_libraries(score_essentia_native_regression PRIVATE score_essentia_native)
  set_target_properties(score_essentia_native_regression PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF UNITY_BUILD OFF)
  add_test(NAME Essentia_native_regression COMMAND score_essentia_native_regression)
  foreach(_suite Runtime Quality)
    string(TOLOWER "${_suite}" _lower)
    add_executable(score_essentia_${_lower}_regression EXCLUDE_FROM_ALL "tests/${_suite}Regression.cpp")
    target_link_libraries(score_essentia_${_lower}_regression PRIVATE score_essentia_processors)
    set_target_properties(score_essentia_${_lower}_regression PROPERTIES
      AUTOMOC OFF AUTOUIC OFF AUTORCC OFF UNITY_BUILD OFF)
    add_test(NAME Essentia_${_lower}_regression COMMAND score_essentia_${_lower}_regression)
  endforeach()
endif()
