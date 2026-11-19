#pragma once
#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace EssentiaNodes
{
// Stationary shared front end: A is bin zero, then ascending pitch class.
struct TonalProfile
{
  PeakFrame peaks;
  Algorithm hpcp;
  std::vector<Real> profile;
  void
  prepare(Frame&, int size, Real reference, int harmonics, Real minimum, Real maximum);
  void compute();
};

struct ChromaProcessor
{
  struct Settings
  {
    int binsPerSemitone{10};
    Real reference{440.f};
    int harmonics{};
    Real minimum{40.f}, maximum{};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> chroma;
    bool highResolutionValid{};
    Real deviation{}, nonTemperedEnergy{}, nonTemperedPeaksEnergy{};
  };
  TonalProfile tonal;
  Algorithm highResolution;
  Real deviation{}, nonTemperedEnergy{}, nonTemperedPeaksEnergy{};
  bool highResolutionEnabled{};
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct KeyProcessor
{
  struct Settings
  {
    Real reference{440.f}, minimum{40.f}, maximum{}, rmsThreshold{0.00001f};
    Real minimumStrength{0.1f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    bool hasKey{};
    std::string key, scale;
    Real strength{}, relativeStrength{};
    std::vector<Real> chroma;
  };
  Frame* frame{};
  Settings configuration;
  TonalProfile tonal;
  Algorithm keyAlgorithm;
  std::string key, scale;
  Real strength{}, relativeStrength{};
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct TuningProcessor
{
  struct Settings
  {
    Real minimum{40.f}, maximum{}, rmsThreshold{0.00001f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    bool hasEstimate{}, frameContributed{};
    Real frequency{}, cents{};
    std::int64_t contributingFrames{};
  };
  Frame* frame{};
  Settings configuration;
  PeakFrame peaks;
  Algorithm tuning;
  Real frequency{}, cents{};
  std::int64_t contributingFrames{};
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct SalienceProcessor
{
  struct Settings
  {
    int harmonics{20};
    Real harmonicWeight{0.8f}, magnitudeThreshold{40.f}, compression{1.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> salience, peakBins, peakValues, peakFrequencies;
  };
  PeakFrame peaks;
  Algorithm salienceAlgorithm, peakAlgorithm;
  std::vector<Real> salience, peakBins, peakValues;
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct DissonanceProcessor
{
  struct Settings
  {
    Real minimum{20.f}, maximum{}, magnitudeThreshold{1e-6f};
    int maximumPeaks{100};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real dissonance{};
    std::vector<Real> frequencies, magnitudes;
  };
  PeakFrame peaks;
  Algorithm dissonanceAlgorithm;
  Real dissonance{};
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct HarmonicProcessor
{
  struct Settings
  {
    Real minimumPitch{40.f}, maximumPitch{2000.f}, confidenceThreshold{0.8f};
    Real rmsThreshold{0.00001f}, tolerance{0.2f};
    int harmonics{20};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    bool voiced{}, ratioDefined{};
    Real pitch{}, confidence{}, inharmonicity{};
    double oddEnergy{}, evenEnergy{}, oddToEvenRatio{};
    std::vector<Real> frequencies, magnitudes, tristimulus;
  };
  Frame* frame{};
  Settings configuration;
  PeakFrame peaks;
  Algorithm yin, harmonicPeaks, inharmonicityAlgorithm, tristimulusAlgorithm;
  Real pitch{}, confidence{}, inharmonicity{};
  std::vector<Real> frequencies, magnitudes, tristimulus;
  void prepare(Frame&, const Settings&);
  void compute(Result&);
};

struct Chroma : AudioNode<Chroma, ChromaProcessor>
{
  halp_meta(name, "HPCP chroma");
  halp_meta(c_name, "essentia_chroma");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Windowed spectral-peak HPCP, A first, unit maximum. Size is 12 times "
      "bins/semitone. High-resolution descriptors require at least 120 bins and nonzero "
      "chroma; deviation is in semitones.");
  halp_meta(uuid, "e9cbf373-e460-47f5-b9de-ba86a83b9e3d");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 4);
    halp::spinbox_i32<"Bins per semitone", halp::range{1, 20, 10}> bins;
    halp::knob_f32<"Reference A (Hz)", halp::range{400., 480., 440.}> reference;
    halp::spinbox_i32<"Harmonic contributions", halp::range{0, 32, 0}> harmonics;
    halp::knob_f32<"Minimum frequency (Hz)", halp::range{1., 2000., 40.}> minimum;
    halp::knob_f32<"Maximum frequency (Hz, 0=Nyquist)", halp::range{0., 48000., 0.}>
        maximum;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Chroma (A first)", std::vector<Real>> chroma;
    halp::val_port<"High-resolution valid", bool> highResolutionValid;
    halp::val_port<"Equal-tempered deviation (semitones)", Real> deviation;
    halp::val_port<"Non-tempered energy ratio", Real> nonTemperedEnergy;
    halp::val_port<"Non-tempered peak energy ratio", Real> nonTemperedPeaksEnergy;
  } outputs;
  ChromaProcessor::Settings settings() const;
  void publish(ChromaProcessor::Result&);
};

struct InstantKey : AudioNode<InstantKey, KeyProcessor>
{
  halp_meta(name, "Instantaneous key");
  halp_meta(c_name, "essentia_instant_key");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Per-window standard Key on 36-bin HPCP using the bgate profile, not an "
      "accumulated EOF estimate. Silence, flat chroma or undefined/weak correlation "
      "yields Has key=false and empty key/scale.");
  halp_meta(uuid, "88244ac4-3cf8-4683-a87e-19f7970019af");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    halp::knob_f32<"Reference A (Hz)", halp::range{400., 480., 440.}> reference;
    halp::knob_f32<"Minimum frequency (Hz)", halp::range{1., 2000., 40.}> minimum;
    halp::knob_f32<"Maximum frequency (Hz, 0=Nyquist)", halp::range{0., 48000., 0.}>
        maximum;
    halp::knob_f32<"Silence RMS threshold", halp::range{0., 0.1, 0.00001}> rmsThreshold;
    halp::knob_f32<"Minimum key strength", halp::range{0., 1., 0.1}> minimumStrength;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Has key", bool> hasKey;
    halp::val_port<"Key", std::string> key;
    halp::val_port<"Scale", std::string> scale;
    halp::val_port<"Strength", Real> strength;
    halp::val_port<"First-to-second relative strength", Real> relativeStrength;
    halp::val_port<"Chroma (A first)", std::vector<Real>> chroma;
  } outputs;
  KeyProcessor::Settings settings() const;
  void publish(KeyProcessor::Result&);
};

struct TuningFrequency : AudioNode<TuningFrequency, TuningProcessor>
{
  halp_meta(name, "Accumulated tuning frequency");
  halp_meta(c_name, "essentia_tuning_frequency");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native energy-weighted accumulated tuning histogram at 1-cent resolution; A "
      "reference, cents wrapped to [-35,65). Silence holds history. Reset, "
      "reconfiguration and input gaps clear history; Has estimate distinguishes no "
      "evidence from 440 Hz.");
  halp_meta(uuid, "15a0cc90-ebb3-4a7a-bc32-4a730866619b");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    halp::knob_f32<"Minimum frequency (Hz)", halp::range{1., 2000., 40.}> minimum;
    halp::knob_f32<"Maximum frequency (Hz, 0=Nyquist)", halp::range{0., 48000., 0.}>
        maximum;
    halp::knob_f32<"Silence RMS threshold", halp::range{0., 0.1, 0.00001}> rmsThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Has estimate", bool> hasEstimate;
    halp::val_port<"Frame contributed", bool> frameContributed;
    halp::val_port<"Tuning A (Hz)", Real> frequency;
    halp::val_port<"Deviation from 440 Hz (cents)", Real> cents;
    halp::val_port<"Contributing windows since reset", std::int64_t> contributingFrames;
  } outputs;
  TuningProcessor::Settings settings() const;
  void publish(TuningProcessor::Result&);
};

struct PitchSalienceFunction : AudioNode<PitchSalienceFunction, SalienceProcessor>
{
  halp_meta(name, "Pitch salience and peaks");
  halp_meta(c_name, "essentia_pitch_salience_function");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Spectral-peak harmonic salience on a fixed 600-bin grid: bin 0 = 55 Hz, 10 "
      "cents/bin, frequency = 55 * 2^(bin/120). Peak bins, Hz and saliences are paired "
      "and amplitude-ordered. This is not an FFT-bin vector.");
  halp_meta(uuid, "0bf6cb02-9f6c-4a42-bcaa-c4458baacbb7");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 4);
    halp::spinbox_i32<"Harmonics", halp::range{1, 100, 20}> harmonics;
    halp::knob_f32<"Harmonic decay", halp::range{0., 1., 0.8}> harmonicWeight;
    halp::knob_f32<"Peak dynamic range (dB)", halp::range{0., 120., 40.}>
        magnitudeThreshold;
    halp::knob_f32<"Magnitude exponent", halp::range{0.01, 1., 1.}> compression;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Salience (600 cent bins)", std::vector<Real>> salience;
    halp::val_port<"Peak bin indices", std::vector<Real>> peakBins;
    halp::val_port<"Peak saliences", std::vector<Real>> peakValues;
    halp::val_port<"Peak frequencies (Hz)", std::vector<Real>> peakFrequencies;
  } outputs;
  SalienceProcessor::Settings settings() const;
  void publish(SalienceProcessor::Result&);
};

struct Dissonance : AudioNode<Dissonance, DissonanceProcessor>
{
  halp_meta(name, "Sensory dissonance");
  halp_meta(c_name, "essentia_dissonance");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native sensory roughness of windowed spectral peaks, independent of pitch "
      "voicing. Dimensionless: linear peak magnitudes are weighted by squared "
      "A-weighting, normalized by their total, pair contributions capped per peak, and "
      "summed then halved (native range 0..0.5, not rescaled). Silence, no retained "
      "peaks or one peak gives zero. Peak threshold uses linear normalized-window "
      "magnitudes; frequency bounds and peak limit affect normalization.");
  halp_meta(uuid, "8f7c43e0-2c90-4f0b-97b3-469ab80e1f56");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 4);
    halp::knob_f32<"Minimum frequency (Hz)", halp::range{1., 2000., 20.}> minimum;
    halp::knob_f32<"Maximum frequency (Hz, 0=Nyquist)", halp::range{0., 48000., 0.}>
        maximum;
    halp::knob_f32<"Peak threshold (linear magnitude)", halp::range{0., 1., 0.000001}>
        magnitudeThreshold;
    halp::spinbox_i32<"Maximum peaks", halp::range{1, 1000, 100}> maximumPeaks;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Dissonance (native normalized roughness)", Real> dissonance;
    halp::val_port<"Peak frequencies (Hz, ascending)", std::vector<Real>> frequencies;
    halp::val_port<"Peak magnitudes (linear)", std::vector<Real>> magnitudes;
  } outputs;
  DissonanceProcessor::Settings settings() const;
  void publish(DissonanceProcessor::Result&);
};

struct HarmonicFeatures : AudioNode<HarmonicFeatures, HarmonicProcessor>
{
  halp_meta(name, "Harmonic features");
  halp_meta(c_name, "essentia_harmonic_features");
  halp_meta(category, "Analysis/Essentia/Tonal");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Spectral peaks and native YinFFT F0 drive HarmonicPeaks, Inharmonicity and "
      "magnitude-based Tristimulus. Harmonic slot i means harmonic i+1, missing peaks "
      "have zero magnitude. Odd/even uses squared magnitudes with fundamental odd; zero "
      "even energy makes ratio undefined. Unvoiced windows clear "
      "vectors/F0/descriptors; scalar descriptors are meaningful only when "
      "Voiced=true.");
  halp_meta(uuid, "95d82f5a-0fdf-4e3a-8a7e-d389db70cf48");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 4);
    halp::knob_f32<"Minimum F0 (Hz)", halp::range{20., 2000., 40.}> minimumPitch;
    halp::knob_f32<"Maximum F0 (Hz)", halp::range{40., 10000., 2000.}> maximumPitch;
    halp::knob_f32<"Voicing confidence threshold", halp::range{0., 1., 0.8}>
        confidenceThreshold;
    halp::knob_f32<"Silence RMS threshold", halp::range{0., 0.1, 0.00001}> rmsThreshold;
    halp::knob_f32<"Harmonic tolerance (fraction of F0)", halp::range{0.01, 0.49, 0.2}>
        tolerance;
    halp::spinbox_i32<"Harmonics", halp::range{5, 100, 20}> harmonics;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Voiced", bool> voiced;
    halp::val_port<"F0 (Hz)", Real> pitch;
    halp::val_port<"Pitch confidence", Real> confidence;
    halp::val_port<"Harmonic frequencies (Hz)", std::vector<Real>> frequencies;
    halp::val_port<"Harmonic magnitudes", std::vector<Real>> magnitudes;
    halp::val_port<"Inharmonicity", Real> inharmonicity;
    halp::val_port<"Tristimulus", std::vector<Real>> tristimulus;
    halp::val_port<"Odd harmonic energy", double> oddEnergy;
    halp::val_port<"Even harmonic energy", double> evenEnergy;
    halp::val_port<"Odd/even ratio defined", bool> ratioDefined;
    halp::val_port<"Odd/even energy ratio", double> oddToEvenRatio;
  } outputs;
  HarmonicProcessor::Settings settings() const;
  void publish(HarmonicProcessor::Result&);
};
}
