#include <Essentia/Correlation.hpp>

#include <cmath>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace EssentiaNodes
{
namespace
{
void requireFinite(const std::vector<Real>& values, const char* algorithm)
{
  for(const Real value : values)
    if(!std::isfinite(value))
      throw std::runtime_error{
          std::string{algorithm} + ": non-finite result (degenerate or unstable input)"};
}

void requireSampleRate(double sampleRate)
{
  if(!std::isfinite(sampleRate) || sampleRate <= 0.
     || sampleRate > std::numeric_limits<Real>::max())
    throw std::runtime_error{
        "Correlation analysis requires a finite positive sample rate"};
}

Real warpingCoefficient(double sampleRate)
{
  requireSampleRate(sampleRate);
  // Use the native Real sample rate in the native formula, not a user-defined
  // replacement for the Bark-scale allpass coefficient.
  const Real rate = static_cast<Real>(sampleRate);
  const Real lambda
      = 1.0674 * std::sqrt(2. * std::atan(0.00006583 * rate) / std::acos(-1.)) - 0.1916;
  if(!std::isfinite(lambda) || std::abs(lambda) >= 1.f)
    throw std::runtime_error{
        "Warped correlation requires a stable sample-rate-derived allpass coefficient"};
  return lambda;
}
}

void AutoCorrelationProcessor::prepare(Frame& input, const Settings& settings)
{
  requireSampleRate(input.sampleRate);
  const int size = static_cast<int>(input.audio.size());
  if(size < 1)
    throw std::runtime_error{"Autocorrelation requires a nonempty audio window"};
  if(settings.lagCount < 0 || settings.lagCount > size)
    throw std::runtime_error{
        "Autocorrelation lag count must be 0 (all) or between 1 and window size"};
  if(settings.normalization != CorrelationNormalization::Standard
     && settings.normalization != CorrelationNormalization::Unbiased)
    throw std::runtime_error{"Unknown autocorrelation normalization"};

  essentia::ParameterMap parameters;
  parameters.add(
      "normalization", settings.normalization == CorrelationNormalization::Unbiased
                           ? "unbiased"
                           : "standard");
  parameters.add("generalized", false);
  algorithm = makeAlgorithm("AutoCorrelation", parameters);
  algorithm->input("array").set(input.audio);
  algorithm->output("autoCorrelation").set(correlation);
  lagCount = settings.lagCount == 0 ? size : settings.lagCount;
  lagSeconds = 1. / input.sampleRate;
}

void AutoCorrelationProcessor::compute(Result& result)
{
  algorithm->compute();
  requireFinite(correlation, "Autocorrelation");
  correlation.resize(lagCount);
  result.correlation.swap(correlation);
  result.lagSeconds = lagSeconds;
}

void WarpedAutoCorrelationProcessor::prepare(Frame& input, const Settings& settings)
{
  if(settings.lagCount < 1
     || static_cast<std::size_t>(settings.lagCount) >= input.audio.size())
    throw std::runtime_error{
        "Warped autocorrelation lag count must be positive and strictly smaller than "
        "window size"};
  warping = warpingCoefficient(input.sampleRate);
  essentia::ParameterMap parameters;
  // Upstream calls this maxLag, but produces exactly maxLag elements [0,L).
  parameters.add("maxLag", settings.lagCount);
  parameters.add("sampleRate", static_cast<Real>(input.sampleRate));
  algorithm = makeAlgorithm("WarpedAutoCorrelation", parameters);
  algorithm->input("array").set(input.audio);
  algorithm->output("warpedAutoCorrelation").set(correlation);
}

void WarpedAutoCorrelationProcessor::compute(Result& result)
{
  algorithm->compute();
  requireFinite(correlation, "Warped autocorrelation");
  result.correlation.swap(correlation);
  result.warping = warping;
}

void LPCProcessor::prepare(Frame& input, const Settings& settings)
{
  requireSampleRate(input.sampleRate);
  if(settings.order < 2)
    throw std::runtime_error{"LPC order must be at least 2"};
  const auto required
      = static_cast<std::size_t>(settings.order) + (settings.warped ? 1u : 0u);
  if(input.audio.size() <= required)
    throw std::runtime_error{
        settings.warped ? "Warped LPC requires window size > order + 1"
                        : "Regular LPC requires window size > order"};
  if(settings.warped)
    warpingCoefficient(input.sampleRate);

  essentia::ParameterMap parameters;
  parameters.add("order", settings.order);
  parameters.add("type", settings.warped ? "warped" : "regular");
  parameters.add("sampleRate", static_cast<Real>(input.sampleRate));
  algorithm = makeAlgorithm("LPC", parameters);
  algorithm->input("frame").set(input.audio);
  algorithm->output("lpc").set(coefficients);
  algorithm->output("reflection").set(reflection);
}

void LPCProcessor::compute(Result& result)
{
  algorithm->compute();
  requireFinite(coefficients, "LPC");
  requireFinite(reflection, "LPC reflection coefficients");
  result.coefficients.swap(coefficients);
  result.reflection.swap(reflection);
}

void WelchProcessor::prepare(Frame& input, const Settings& settings)
{
  requireSampleRate(input.sampleRate);
  if(input.audio.size() < 3)
    throw std::runtime_error{
        "Welch requires at least 3 samples for a nonzero Hann window"};
  if(input.hopSize < 1 || static_cast<std::size_t>(input.hopSize) >= input.audio.size())
    throw std::runtime_error{
        "Welch requires overlapping frames: hop must be positive and smaller than "
        "window size"};
  if(settings.averagingFrames < 1 || settings.averagingFrames > 128)
    throw std::runtime_error{"Welch averaging frames must be between 1 and 128"};

  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hopSize = input.hopSize;
  sampleRate = input.sampleRate;
  averagingFrames = settings.averagingFrames;
  historyFrames = 0;
  nextSample = input.startSample;
  fftSize = 1;
  while(fftSize < frameSize)
    fftSize *= 2;
  frequencies.resize(fftSize / 2 + 1);
  const double binHz = sampleRate / fftSize;
  for(std::size_t bin = 0; bin < frequencies.size(); ++bin)
    frequencies[bin] = static_cast<Real>(bin * binHz);

  essentia::ParameterMap parameters;
  // Never use upstream's implicit resize path: it does not update _frameSize.
  // The pinned dependency patches window-energy normalization to use N rather
  // than fftSize, so arbitrary non-power-of-two windows retain power/Hz units.
  parameters.add("frameSize", frameSize);
  parameters.add("fftSize", fftSize);
  parameters.add("sampleRate", static_cast<Real>(sampleRate));
  parameters.add("windowType", "hann");
  parameters.add("scaling", "density");
  parameters.add("averagingFrames", averagingFrames);
  algorithm = makeAlgorithm("Welch", parameters);
  algorithm->input("frame").set(input.audio);
  algorithm->output("psd").set(psd);
}

void WelchProcessor::compute(Result& result)
{
  if(frame->audio.size() != static_cast<std::size_t>(frameSize)
     || frame->hopSize != hopSize || frame->sampleRate != sampleRate
     || frame->startSample != nextSample)
    throw std::runtime_error{
        "Welch requires consecutive frames with unchanged window, hop and sample rate; "
        "reset after discontinuities"};

  algorithm->compute();
  historyFrames = std::min(historyFrames + 1, averagingFrames);
  // Native history starts zero-filled and always divides by averagingFrames.
  // Until it is full, publish the mean of actual observations, not fake silence.
  if(historyFrames < averagingFrames)
  {
    const Real startupCorrection = static_cast<Real>(averagingFrames) / historyFrames;
    for(auto& value : psd)
      value *= startupCorrection;
  }
  requireFinite(psd, "Welch PSD");
  nextSample += hopSize;
  result.psd.swap(psd);
  result.frequencies = frequencies;
  result.historyFrames = historyFrames;
}

void DCTProcessor::prepare(Frame& input, const Settings& settings)
{
  if(input.audio.empty())
    throw std::runtime_error{"DCT requires a nonempty data vector"};
  if(settings.coefficientCount < 1
     || static_cast<std::size_t>(settings.coefficientCount) > input.audio.size())
    throw std::runtime_error{
        "DCT coefficient count must be positive and not exceed data length"};
  if(settings.type != CosineTransformType::Orthonormal_II
     && settings.type != CosineTransformType::HTK_scaled_II)
    throw std::runtime_error{"Unknown DCT type"};
  // Native DCT builds a dense coefficients-by-input cosine table. Bound that
  // worker allocation rather than allowing a full 65536-square table.
  constexpr std::size_t maxTableEntries = 16 * 1024 * 1024;
  if(input.audio.size()
     > maxTableEntries / static_cast<std::size_t>(settings.coefficientCount))
    throw std::runtime_error{
        "DCT cosine table exceeds 16 million entries; reduce data length or coefficient "
        "count"};

  essentia::ParameterMap parameters;
  parameters.add("inputSize", static_cast<int>(input.audio.size()));
  parameters.add("outputSize", settings.coefficientCount);
  parameters.add(
      "dctType", settings.type == CosineTransformType::Orthonormal_II ? 2 : 3);
  parameters.add("liftering", 0);
  algorithm = makeAlgorithm("DCT", parameters);
  algorithm->input("array").set(input.audio);
  algorithm->output("dct").set(coefficients);
}

void DCTProcessor::compute(Result& result)
{
  algorithm->compute();
  requireFinite(coefficients, "DCT");
  result.coefficients.swap(coefficients);
}

AutoCorrelationProcessor::Settings AutoCorrelation::settings() const
{
  return {inputs.normalization.value, inputs.lagCount.value};
}

void AutoCorrelation::publish(AutoCorrelationProcessor::Result& result)
{
  outputs.correlation.value.swap(result.correlation);
  outputs.lagSeconds.value = result.lagSeconds;
}

WarpedAutoCorrelationProcessor::Settings WarpedAutoCorrelation::settings() const
{
  return {inputs.lagCount.value};
}

void WarpedAutoCorrelation::publish(WarpedAutoCorrelationProcessor::Result& result)
{
  outputs.correlation.value.swap(result.correlation);
  outputs.warping.value = result.warping;
}

LPCProcessor::Settings LPC::settings() const
{
  return {inputs.order.value, inputs.warped.value};
}

void LPC::publish(LPCProcessor::Result& result)
{
  outputs.coefficients.value.swap(result.coefficients);
  outputs.reflection.value.swap(result.reflection);
}

WelchProcessor::Settings Welch::settings() const
{
  return {inputs.averagingFrames.value};
}

void Welch::publish(WelchProcessor::Result& result)
{
  outputs.psd.value.swap(result.psd);
  outputs.frequencies.value.swap(result.frequencies);
  outputs.historyFrames.value = result.historyFrames;
}

DCTProcessor::Settings DCT::settings() const
{
  return {inputs.coefficientCount.value, inputs.type.value};
}

void DCT::publish(DCTProcessor::Result& result)
{
  outputs.coefficients.value.swap(result.coefficients);
}
}
