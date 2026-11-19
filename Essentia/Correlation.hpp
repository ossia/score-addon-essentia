#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <cstdint>
#include <vector>

namespace EssentiaNodes
{
enum class CorrelationNormalization
{
  Standard,
  Unbiased
};
enum class CosineTransformType
{
  Orthonormal_II,
  HTK_scaled_II
};

struct AutoCorrelationProcessor
{
  struct Settings
  {
    CorrelationNormalization normalization{CorrelationNormalization::Standard};
    int lagCount{};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> correlation;
    double lagSeconds{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

  Algorithm algorithm;
  std::vector<Real> correlation;
  int lagCount{};
  double lagSeconds{};
};

struct WarpedAutoCorrelationProcessor
{
  struct Settings
  {
    int lagCount{32};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> correlation;
    Real warping{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

  Algorithm algorithm;
  std::vector<Real> correlation;
  Real warping{};
};

struct LPCProcessor
{
  struct Settings
  {
    int order{10};
    bool warped{};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> coefficients;
    std::vector<Real> reflection;
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

  Algorithm algorithm;
  std::vector<Real> coefficients;
  std::vector<Real> reflection;
};

struct WelchProcessor
{
  struct Settings
  {
    int averagingFrames{8};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> psd;
    std::vector<Real> frequencies;
    int historyFrames{};
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

  Algorithm algorithm;
  std::vector<Real> psd;
  std::vector<Real> frequencies;
  Frame* frame{};
  int frameSize{};
  int fftSize{};
  int hopSize{};
  int averagingFrames{};
  int historyFrames{};
  std::int64_t nextSample{};
  double sampleRate{};
};

struct DCTProcessor
{
  struct Settings
  {
    int coefficientCount{13};
    CosineTransformType type{CosineTransformType::Orthonormal_II};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<Real> coefficients;
  };
  void prepare(Frame&, const Settings&);
  void compute(Result&);

  Algorithm algorithm;
  std::vector<Real> coefficients;
};

struct AutoCorrelation : AudioNode<AutoCorrelation, AutoCorrelationProcessor>
{
  halp_meta(name, "Autocorrelation");
  halp_meta(c_name, "essentia_auto_correlation");
  halp_meta(category, "Analysis/Essentia/Correlation");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Nonnegative audio lags, without mean subtraction: standard sums x[n] x[n+k], "
      "unbiased divides by N-k. Index zero is signal energy (standard) or mean square "
      "(unbiased), not Pearson correlation.");
  halp_meta(uuid, "ef97ec4c-997d-4fd1-a4fb-f7e30214a579");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::enum_t<CorrelationNormalization, "Normalization"> normalization;
    struct : halp::spinbox_i32<"Lag count", halp::range{0, 65536, 0}>
    {
      halp_meta(description, "Number of lags, beginning at zero; 0 returns all N lags.");
    } lagCount;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Autocorrelation", std::vector<Real>> correlation;
    halp::val_port<"Lag spacing (s)", double> lagSeconds;
  } outputs;
  AutoCorrelationProcessor::Settings settings() const;
  void publish(AutoCorrelationProcessor::Result&);
};

struct WarpedAutoCorrelation
    : AudioNode<WarpedAutoCorrelation, WarpedAutoCorrelationProcessor>
{
  halp_meta(name, "Warped autocorrelation");
  halp_meta(c_name, "essentia_warped_auto_correlation");
  halp_meta(category, "Analysis/Essentia/Correlation");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Allpass-warped audio autocorrelation. L entries represent warped orders 0 "
      "through L-1, not uniform time lags. L must be smaller than N. Warping is derived "
      "from the audio sample rate; entry zero is signal energy.");
  halp_meta(uuid, "567d57cf-2d8a-48ba-bb75-a6157a31a8e2");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_i32<"Lag count", halp::range{1, 65535, 32}> lagCount;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Warped autocorrelation", std::vector<Real>> correlation;
    halp::val_port<"Warping coefficient", Real> warping;
  } outputs;
  WarpedAutoCorrelationProcessor::Settings settings() const;
  void publish(WarpedAutoCorrelationProcessor::Result&);
};

struct LPC : AudioNode<LPC, LPCProcessor>
{
  halp_meta(name, "Linear predictive coefficients");
  halp_meta(c_name, "essentia_lpc");
  halp_meta(category, "Analysis/Essentia/Correlation");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "Audio LPC: order+1 dimensionless prediction-polynomial coefficients and order "
      "reflection coefficients. Regular requires N>order; sample-rate-derived warping "
      "requires N>order+1. Essentia returns zero coefficients on silence.");
  halp_meta(uuid, "47ff52d8-85e0-41d3-827c-1d0ee26a2c48");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 4);
    halp::spinbox_i32<"Order", halp::range{2, 1024, 10}> order;
    halp::toggle<"Warped"> warped;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"LPC coefficients", std::vector<Real>> coefficients;
    halp::val_port<"Reflection coefficients", std::vector<Real>> reflection;
  } outputs;
  LPCProcessor::Settings settings() const;
  void publish(LPCProcessor::Result&);
};

struct Welch : AudioNode<Welch, WelchProcessor>
{
  halp_meta(name, "Welch power spectral density");
  halp_meta(c_name, "essentia_welch");
  halp_meta(category, "Analysis/Essentia/Spectrum");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "One-sided linear power/Hz density of Hann-windowed audio, averaged over "
      "consecutive overlapping frames. FFT pads N to the next power of two; frequencies "
      "span DC to Nyquist. Startup averages only available history. N>=3 and hop<N "
      "required.");
  halp_meta(uuid, "a5c8808a-c018-4671-8848-4aa8b4eab48c");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 2);
    halp::spinbox_i32<"Averaging frames", halp::range{1, 128, 8}> averagingFrames;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"PSD (power/Hz)", std::vector<Real>> psd;
    halp::val_port<"Frequencies (Hz)", std::vector<Real>> frequencies;
    halp::val_port<"History frames", int> historyFrames;
  } outputs;
  WelchProcessor::Settings settings() const;
  void publish(WelchProcessor::Result&);
};

struct DCT : DataNode<DCT, DCTProcessor>
{
  halp_meta(name, "Discrete cosine transform");
  halp_meta(c_name, "essentia_dct");
  halp_meta(category, "Analysis/Essentia/Data");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(
      description,
      "DCT of a data vector, not an audio bus. Coefficient count must not exceed input "
      "length. Orthonormal II is native type 2; HTK-scaled II is native type 3 (DC "
      "multiplied by sqrt(2), not an inverse DCT). No liftering.");
  halp_meta(uuid, "b266ab26-341f-46cf-8033-cf863b994919");
  struct
  {
    ESSENTIA_DATA_INPUTS;
    halp::spinbox_i32<"Coefficient count", halp::range{1, 4096, 13}> coefficientCount;
    halp::enum_t<CosineTransformType, "Type"> type;
  } inputs;
  struct
  {
    ESSENTIA_DATA_STATUS_OUTPUTS;
    halp::val_port<"DCT coefficients", std::vector<Real>> coefficients;
  } outputs;
  DCTProcessor::Settings settings() const;
  void publish(DCTProcessor::Result&);
};
}
