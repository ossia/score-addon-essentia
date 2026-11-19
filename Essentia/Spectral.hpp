#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>
#include <essentia/utils/tnt/tnt.h>
#include <halp/controls.hpp>

#include <vector>

namespace EssentiaNodes
{
struct SpectralContrastProcessor
{
  struct Settings
  {
    int bands{6};
    Real lowHz{20.f}, highHz{}, neighbourRatio{0.4f}, staticDistribution{0.15f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> contrast, valleys;
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  SpectrumFrame spectral;
  Algorithm algorithm;
  std::vector<Real> contrast, valleys;
};

struct SpectralComplexityProcessor
{
  struct Settings
  {
    Real threshold{0.005f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real complexity{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  SpectrumFrame spectral;
  Algorithm algorithm;
  Real complexity{};
};

struct SpectralPeaksProcessor
{
  struct Settings
  {
    Real lowHz{20.f}, highHz{}, threshold{1e-6f};
    int maximumPeaks{100};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> frequencies, magnitudes;
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  PeakFrame peaks;
};

struct SpectralWhiteningProcessor
{
  struct Settings
  {
    Real lowHz{20.f}, highHz{}, threshold{1e-6f};
    int maximumPeaks{100};
    Real whiteningHz{5000.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> frequencies, magnitudes;
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  PeakFrame peaks;
  Algorithm algorithm;
  std::vector<Real> magnitudes;
};

struct StrongPeakProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real strength{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  SpectrumFrame spectral;
  Algorithm algorithm;
  Real strength{};
};

struct SpectralCentroidTimeProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real centroid{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  Algorithm algorithm;
  Real centroid{};
};

struct LogSpectrumProcessor
{
  struct Settings
  {
    int binsPerSemitone{3}, octaves{7};
    Real rollOn{};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> spectrum, meanTuning;
    Real localTuning{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  SpectrumFrame spectral;
  Algorithm algorithm;
  std::vector<Real> spectrum, meanTuning;
  Real localTuning{};
};

struct PitchSalienceProcessor
{
  struct Settings
  {
    Real lowHz{100.f}, highHz{};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real salience{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  SpectrumFrame spectral;
  Algorithm algorithm;
  Real salience{};
};

struct PanningProcessor
{
  static constexpr bool stereo = true;
  struct Settings
  {
    int averageFrames{}, histogramBins{512}, coefficients{20};
    bool warped{true};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> coefficients;
    int bands{}, coefficientsPerBand{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);
  Frame* input{};
  Frame leftFrame, rightFrame;
  SpectrumFrame leftSpectrum, rightSpectrum;
  Algorithm algorithm;
  TNT::Array2D<Real> coefficients;
};

struct SpectralContrast : AudioNode<SpectralContrast, SpectralContrastProcessor>
{
  halp_meta(name, "Spectral contrast")
  halp_meta(c_name, "essentia_spectral_contrast")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-window magnitude-spectrum contrast per band and natural-log valley "
      "magnitudes; FFT size follows the analysis window.")
  halp_meta(uuid, "11137948-3293-4831-85dd-b76d63871e3d")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_i32<"Bands", halp::irange{1, 32, 6}> bands;
    halp::spinbox_f32<"Low frequency (Hz)", halp::range{1., 96000., 20.}> low;
    halp::spinbox_f32<"High frequency (Hz, 0 = Nyquist)", halp::range{0., 96000., 0.}>
        high;
    halp::spinbox_f32<"Neighbour ratio", halp::range{0.01, 1., 0.4}> neighbour;
    halp::spinbox_f32<"Static distribution", halp::range{0., 1., 0.15}> distribution;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Contrast", std::vector<Real>> contrast;
    halp::val_port<"Valleys (natural log magnitude)", std::vector<Real>> valleys;
  } outputs;
  SpectralContrastProcessor::Settings settings() const;
  void publish(SpectralContrastProcessor::Result&);
};

struct SpectralComplexity : AudioNode<SpectralComplexity, SpectralComplexityProcessor>
{
  halp_meta(name, "Spectral complexity")
  halp_meta(c_name, "essentia_spectral_complexity")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Count up to 100 magnitude-spectrum peaks from 100 to 5000 Hz above the linear "
      "threshold in each Hann-windowed audio frame.")
  halp_meta(uuid, "648298df-622c-4a9d-8e63-e8035dabadcf")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_f32<"Magnitude threshold", halp::range{0., 1., 0.005}> threshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Peak count", Real> complexity;
  } outputs;
  SpectralComplexityProcessor::Settings settings() const;
  void publish(SpectralComplexityProcessor::Result&);
};

struct SpectralPeaks : AudioNode<SpectralPeaks, SpectralPeaksProcessor>
{
  halp_meta(name, "Spectral peaks")
  halp_meta(c_name, "essentia_spectral_peaks")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Interpolated peaks of each Hann-window linear magnitude spectrum; paired "
      "frequency and magnitude vectors in ascending frequency order. Silence yields "
      "empty vectors.")
  halp_meta(uuid, "48bbfe7e-c52a-4824-8eef-941f2654fc0e")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_f32<"Low frequency (Hz)", halp::range{0., 96000., 20.}> low;
    halp::spinbox_f32<"High frequency (Hz, 0 = Nyquist)", halp::range{0., 96000., 0.}>
        high;
    halp::spinbox_f32<"Magnitude threshold", halp::range{0., 1., 0.000001}> threshold;
    halp::spinbox_i32<"Maximum peaks", halp::irange{1, 4096, 100}> maximum;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Frequencies (Hz)", std::vector<Real>> frequencies;
    halp::val_port<"Linear magnitudes", std::vector<Real>> magnitudes;
  } outputs;
  SpectralPeaksProcessor::Settings settings() const;
  void publish(SpectralPeaksProcessor::Result&);
};

struct SpectralWhitening : AudioNode<SpectralWhitening, SpectralWhiteningProcessor>
{
  halp_meta(name, "Spectral whitening")
  halp_meta(c_name, "essentia_spectral_whitening")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Whiten Hann-window spectrum peaks against their spectral envelope. Paired "
      "ascending frequencies and whitened linear magnitudes, not a whitened audio "
      "signal.")
  halp_meta(uuid, "4319f426-c811-4183-b468-244438056786")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_f32<"Low frequency (Hz)", halp::range{0., 96000., 20.}> low;
    halp::spinbox_f32<"High frequency (Hz, 0 = Nyquist)", halp::range{0., 96000., 0.}>
        high;
    halp::spinbox_f32<"Magnitude threshold", halp::range{0., 1., 0.000001}> threshold;
    halp::spinbox_i32<"Maximum peaks", halp::irange{1, 4096, 100}> maximum;
    halp::spinbox_f32<
        "Whitening limit (Hz, 0 = Nyquist)", halp::range{0., 96000., 5000.}>
        whitening;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Frequencies (Hz)", std::vector<Real>> frequencies;
    halp::val_port<"Whitened linear magnitudes", std::vector<Real>> magnitudes;
  } outputs;
  SpectralWhiteningProcessor::Settings settings() const;
  void publish(SpectralWhiteningProcessor::Result&);
};

struct StrongPeak : AudioNode<StrongPeak, StrongPeakProcessor>
{
  halp_meta(name, "Strong peak")
  halp_meta(c_name, "essentia_strong_peak")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Maximum Hann-window spectrum magnitude divided by its half-amplitude width in "
      "log10-frequency units; flat spectra return zero.")
  halp_meta(uuid, "afe6630e-15a6-4d84-97c2-606da2c5450e")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Peak strength", Real> strength;
  } outputs;
  StrongPeakProcessor::Settings settings() const { return {}; }
  void publish(StrongPeakProcessor::Result&);
};

struct SpectralCentroidTime
    : AudioNode<SpectralCentroidTime, SpectralCentroidTimeProcessor>
{
  halp_meta(name, "Spectral centroid (time domain)")
  halp_meta(c_name, "essentia_spectral_centroid_time")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Time-domain centroid estimate in Hz from the first-difference norm / signal norm "
      "on each unwindowed audio frame; not the spectral weighted-mean centroid.")
  halp_meta(uuid, "65199c97-5351-458c-a972-c11d19c06914")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Centroid (Hz)", Real> centroid;
  } outputs;
  SpectralCentroidTimeProcessor::Settings settings() const { return {}; }
  void publish(SpectralCentroidTimeProcessor::Result&);
};

struct LogSpectrum : AudioNode<LogSpectrum, LogSpectrumProcessor>
{
  halp_meta(name, "Log-frequency spectrum")
  halp_meta(c_name, "essentia_log_spectrum")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "NNLS log-frequency magnitude spectrum from MIDI 20 with 1/binsPerSemitone "
      "spacing. Output size is 12*octaves*binsPerSemitone + 2*(binsPerSemitone/2+1), "
      "integer division. Mean tuning is the cumulative per-subsemitone magnitude "
      "profile since reset; local tuning is an exponentially smoothed phase in semitone "
      "cycles, not Hz.")
  halp_meta(uuid, "fff7de6b-9b70-45f7-9d90-dabb6677ae39")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 4);
    halp::spinbox_i32<"Bins per semitone", halp::irange{1, 12, 3}> bins;
    halp::spinbox_i32<"Octaves", halp::irange{1, 9, 7}> octaves;
    halp::spinbox_f32<"Low-frequency roll-on (%)", halp::range{0., 5., 0.}> rollOn;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Log-frequency magnitudes", std::vector<Real>> spectrum;
    halp::val_port<"Mean tuning profile since reset", std::vector<Real>> meanTuning;
    halp::val_port<"Smoothed local tuning (semitone cycles)", Real> localTuning;
  } outputs;
  LogSpectrumProcessor::Settings settings() const;
  void publish(LogSpectrumProcessor::Result&);
};

struct PitchSalience : AudioNode<PitchSalience, PitchSalienceProcessor>
{
  halp_meta(name, "Pitch salience")
  halp_meta(c_name, "essentia_pitch_salience")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Normalized autocorrelation salience of each Hann-window magnitude spectrum, from "
      "0 to 1. Harmonic richness, not a pitch-frequency estimator. Bounds specify "
      "spectral frequency separation; high=0 selects up to 5000 Hz below Nyquist.")
  halp_meta(uuid, "29440e4d-4aac-437e-b270-8f1ec242ad4e")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_f32<"Low separation (Hz)", halp::range{1., 96000., 100.}> low;
    halp::spinbox_f32<"High separation (Hz, 0 = auto)", halp::range{0., 96000., 0.}>
        high;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Pitch salience", Real> salience;
  } outputs;
  PitchSalienceProcessor::Settings settings() const;
  void publish(PitchSalienceProcessor::Result&);
};

struct Panning : AudioNode<Panning, PanningProcessor>
{
  halp_meta(name, "Stereo panning coefficients")
  halp_meta(c_name, "essentia_panning")
  halp_meta(category, "Analysis/Essentia/Spectrum")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Coherent Hann-window spectra of the first two real channels. Row-major "
      "band-by-coefficient matrix of log-panorama IFFT coefficients with explicit "
      "dimensions. Native supported single-band mode. Average frames 0 or 1 is "
      "per-frame; >1 uses exponential histogram averaging with weight 1/frames, reset "
      "on discontinuity.")
  halp_meta(uuid, "0d8f863a-23cd-4eb1-9a5a-b9dd49de1209")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_i32<"Average frames (0 = per-frame)", halp::irange{0, 4096, 0}>
        average;
    halp::spinbox_i32<"Panorama histogram bins", halp::irange{2, 4096, 512}> bins;
    halp::spinbox_i32<"Coefficients", halp::irange{1, 512, 20}> coefficients;
    halp::toggle<"Warped panorama", halp::toggle_setup{.init = true}> warped;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Panning matrix (row-major)", std::vector<Real>> coefficients;
    halp::val_port<"Bands", int> bands;
    halp::val_port<"Coefficients per band", int> coefficientsPerBand;
  } outputs;
  PanningProcessor::Settings settings() const;
  void publish(PanningProcessor::Result&);
};
}
