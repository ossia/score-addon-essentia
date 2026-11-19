#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <cstdint>
#include <vector>

namespace EssentiaNodes
{
struct AudioStatisticsProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real mean{}, median{}, variance{}, minimum{}, maximum{};
    int minimumIndex{}, maximumIndex{};
    Real energy{}, power{}, leq{}, duration{}, skewness{}, kurtosis{};
    bool momentsValid{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Result m_values;
  Real m_spread{};
  std::vector<Real> m_moments;
  Algorithm m_mean, m_median, m_variance, m_minimum, m_maximum;
  Algorithm m_energy, m_power, m_leq, m_duration, m_central, m_shape;
};

struct EffectiveDurationProcessor
{
  struct Settings
  {
    Real thresholdRatio{0.4f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real duration{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Real m_duration{};
  Algorithm m_algorithm;
};

struct SilenceRateProcessor
{
  struct Settings
  {
    Real thresholdDb{-60.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    bool silent{};
    Real thresholdPower{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Real m_silent{}, m_threshold{};
  Algorithm m_algorithm;
};

struct LoudnessProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real loudness{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Real m_loudness{};
  Algorithm m_algorithm;
};

struct LarmProcessor
{
  struct Settings
  {
    Real attackMs{10.f}, releaseMs{1500.f}, power{1.5f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real loudness{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Real m_loudness{};
  Algorithm m_algorithm;
};

struct LoudnessVickersProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real loudness{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  std::int64_t m_nextSample{};
  Real m_loudness{};
  Algorithm m_algorithm;
};

struct DistributionShapeProcessor
{
  struct Settings
  {
    Real range{1.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> rawMoments, centralMoments;
    Real spread{}, skewness{}, kurtosis{};
    bool hasMass{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  std::vector<Real> m_rawMoments, m_centralMoments;
  Real m_spread{}, m_skewness{}, m_kurtosis{};
  Algorithm m_raw, m_central, m_shape;
};

struct PowerMeanProcessor
{
  struct Settings
  {
    Real power{1.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real mean{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

private:
  Frame* m_frame{};
  Real m_mean{}, m_power{};
  Algorithm m_algorithm;
};

struct AudioStatistics : AudioNode<AudioStatistics, AudioStatisticsProcessor>
{
  halp_meta(name, "Audio statistics");
  halp_meta(c_name, "essentia_audio_statistics");
  halp_meta(category, "Analysis/Essentia/Statistics");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Signed PCM window statistics. Population variance and power use amplitude "
      "squared; energy is the sum of squares. Extrema indices are zero-based within the "
      "window, with first ties retained. Leq is unweighted dB relative to unit power, "
      "floored at -90 dB, not calibrated SPL. Singleton basic statistics are valid; "
      "Moments valid is false and shape outputs must be ignored. For constant windows "
      "of at least two samples native skewness is 0 and excess kurtosis is -3.");
  halp_meta(uuid, "7d0a7b07-03e7-4796-a0fa-0471944187db");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Mean (amplitude)", Real> mean;
    halp::val_port<"Median (amplitude)", Real> median;
    halp::val_port<"Variance (amplitude squared)", Real> variance;
    halp::val_port<"Minimum (amplitude)", Real> minimum;
    halp::val_port<"Minimum index (samples)", int> minimumIndex;
    halp::val_port<"Maximum (amplitude)", Real> maximum;
    halp::val_port<"Maximum index (samples)", int> maximumIndex;
    halp::val_port<"Energy (sum amplitude squared)", Real> energy;
    halp::val_port<"Power (amplitude squared)", Real> power;
    halp::val_port<"Leq (dB)", Real> leq;
    halp::val_port<"Duration (s)", Real> duration;
    halp::val_port<"Moments valid", bool> momentsValid;
    halp::val_port<"Skewness", Real> skewness;
    halp::val_port<"Excess kurtosis", Real> kurtosis;
  } outputs;
  AudioStatisticsProcessor::Settings settings() const { return {}; }
  void publish(AudioStatisticsProcessor::Result&);
};

struct EffectiveDuration : AudioNode<EffectiveDuration, EffectiveDurationProcessor>
{
  halp_meta(name, "Effective duration");
  halp_meta(c_name, "essentia_effective_duration");
  halp_meta(category, "Analysis/Essentia/Statistics");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Time within each PCM window whose absolute amplitude reaches the chosen fraction "
      "of its peak and the native -90 dB amplitude floor. Accepts signed samples or an "
      "envelope; does not create an envelope or accumulate across windows. Silence "
      "yields zero seconds.");
  halp_meta(uuid, "e691b3db-2c0d-4c70-a45c-094654ef910f");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::hslider_f32<"Peak threshold ratio", halp::range{0., 1., 0.4}> thresholdRatio;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Effective duration (s)", Real> duration;
  } outputs;
  EffectiveDurationProcessor::Settings settings() const
  {
    return {inputs.thresholdRatio.value};
  }
  void publish(EffectiveDurationProcessor::Result& r)
  {
    outputs.duration.value = r.duration;
  }
};

struct SilenceRate : AudioNode<SilenceRate, SilenceRateProcessor>
{
  halp_meta(name, "Window silence");
  halp_meta(c_name, "essentia_silence_rate");
  halp_meta(category, "Analysis/Essentia/Statistics");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native SilenceRate classifies a signed PCM window as silent when its mean-square "
      "power is strictly below the configured threshold. The dB control is converted to "
      "linear power; this is one window's Boolean classification, not an accumulated "
      "silence percentage. Equality is not silent.");
  halp_meta(uuid, "3699ea53-9b88-438f-92b2-0908c76903e5");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::hslider_f32<"Silence threshold (dB)", halp::range{-120., 0., -60.}>
        thresholdDb;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Silent", bool> silent;
    halp::val_port<"Threshold (power)", Real> thresholdPower;
  } outputs;
  SilenceRateProcessor::Settings settings() const { return {inputs.thresholdDb.value}; }
  void publish(SilenceRateProcessor::Result& r)
  {
    outputs.silent.value = r.silent;
    outputs.thresholdPower.value = r.thresholdPower;
  }
};

struct Loudness : AudioNode<Loudness, LoudnessProcessor>
{
  halp_meta(name, "Stevens loudness");
  halp_meta(c_name, "essentia_loudness");
  halp_meta(category, "Analysis/Essentia/Loudness");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native Loudness applies Stevens' power law to signed PCM: sum(sample squared) "
      "raised to 0.67. Relative energy-based units, dependent on window length; not "
      "LUFS, dB, or calibrated sones. Silence is zero.");
  halp_meta(uuid, "13f62fac-f5f2-41f2-9afd-41b2556c5f21");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Loudness (energy power 0.67)", Real> loudness;
  } outputs;
  LoudnessProcessor::Settings settings() const { return {}; }
  void publish(LoudnessProcessor::Result& r) { outputs.loudness.value = r.loudness; }
};

struct Larm : AudioNode<Larm, LarmProcessor>
{
  halp_meta(name, "LARM window loudness");
  halp_meta(c_name, "essentia_larm");
  halp_meta(category, "Analysis/Essentia/Loudness");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native Larm of signed PCM: rectified attack/release envelope followed by a power "
      "mean and 20 log10. Envelope state restarts for each window in the pinned native "
      "implementation, so this is independent-window loudness rather than a continuous "
      "long-term meter. No RLB weighting is added. The native silence floor is -100 "
      "dB.");
  halp_meta(uuid, "9c8ae280-d542-48f5-81cc-0a516ee67f1d");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(4096, 1);
    halp::hslider_f32<"Attack (ms)", halp::range{0., 1000., 10.}> attackMs;
    halp::hslider_f32<"Release (ms)", halp::range{0., 10000., 1500.}> releaseMs;
    halp::hslider_f32<"Averaging power", halp::range{-8., 8., 1.5}> power;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"LARM (dB)", Real> loudness;
  } outputs;
  LarmProcessor::Settings settings() const
  {
    return {inputs.attackMs.value, inputs.releaseMs.value, inputs.power.value};
  }
  void publish(LarmProcessor::Result& r) { outputs.loudness.value = r.loudness; }
};

struct LoudnessVickers : AudioNode<LoudnessVickers, LoudnessVickersProcessor>
{
  halp_meta(name, "Vickers loudness (44.1 kHz)");
  halp_meta(c_name, "essentia_loudness_vickers");
  halp_meta(category, "Analysis/Essentia/Loudness");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Continuous native Vickers B-curve-compensated loudness in dB, with 35 ms "
      "exponential mean-square memory. Requires exactly 44100 Hz and nonoverlapping "
      "windows (hop divisor 1). State resets on stream discontinuity; no long-term "
      "post-averaging is performed. Native silence is -90 dB.");
  halp_meta(uuid, "f3b08779-fd46-4021-9a9f-dd724b69d914");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 1);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Vickers loudness (dB)", Real> loudness;
  } outputs;
  LoudnessVickersProcessor::Settings settings() const { return {}; }
  void publish(LoudnessVickersProcessor::Result& r)
  {
    outputs.loudness.value = r.loudness;
  }
};

struct DistributionShape : DataNode<DistributionShape, DistributionShapeProcessor>
{
  halp_meta(name, "Weighted distribution statistics");
  halp_meta(c_name, "essentia_distribution_shape");
  halp_meta(category, "Analysis/Essentia/Statistics");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Nonnegative weights over equally spaced positions from 0 to Range; not signed "
      "PCM or sample-point observations. At least two bins are required. Raw and "
      "central moments contain orders 0 through 4; order k has coordinate units to "
      "power k. Spread is variance in coordinate units squared. Zero total mass is "
      "valid with Has mass false, zero moments/spread/skewness and native excess "
      "kurtosis -3.");
  halp_meta(uuid, "26fee200-9609-448c-8e35-f4ac97f34534");
  struct
  {
    ESSENTIA_DATA_INPUTS;
    halp::hslider_f32<"Range (coordinate units)", halp::range{0.000001, 1000000., 1.}>
        range;
  } inputs;
  struct
  {
    ESSENTIA_DATA_STATUS_OUTPUTS;
    halp::val_port<"Raw moments (orders 0..4)", std::vector<Real>> rawMoments;
    halp::val_port<"Central moments (orders 0..4)", std::vector<Real>> centralMoments;
    halp::val_port<"Spread (coordinate units squared)", Real> spread;
    halp::val_port<"Skewness", Real> skewness;
    halp::val_port<"Excess kurtosis", Real> kurtosis;
    halp::val_port<"Has mass", bool> hasMass;
  } outputs;
  DistributionShapeProcessor::Settings settings() const { return {inputs.range.value}; }
  void publish(DistributionShapeProcessor::Result&);
};

struct PowerMean : DataNode<PowerMean, PowerMeanProcessor>
{
  halp_meta(name, "Generalized power mean");
  halp_meta(c_name, "essentia_power_mean");
  halp_meta(category, "Analysis/Essentia/Statistics");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Native power mean of nonnegative data values, not signed PCM. Power 0 uses the "
      "native geometric mean (zero inputs yield zero), 1 the arithmetic mean, 2 the "
      "quadratic mean, and -1 the harmonic mean. Negative powers require strictly "
      "positive values. At least one value is required; the result has the same units "
      "as the input.");
  halp_meta(uuid, "b7fd85ee-f937-464d-8de2-78e9d1b257e0");
  struct
  {
    ESSENTIA_DATA_INPUTS;
    halp::hslider_f32<"Power", halp::range{-8., 8., 1.}> power;
  } inputs;
  struct
  {
    ESSENTIA_DATA_STATUS_OUTPUTS;
    halp::val_port<"Mean", Real> mean;
  } outputs;
  PowerMeanProcessor::Settings settings() const { return {inputs.power.value}; }
  void publish(PowerMeanProcessor::Result& r) { outputs.mean.value = r.mean; }
};
}
