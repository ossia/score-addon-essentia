#include <Essentia/Spectral.hpp>

#include <cmath>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace EssentiaNodes
{
namespace
{
void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}

void finite(Real value, const char* algorithm)
{
  if(!std::isfinite(value))
    throw std::runtime_error(std::string{algorithm} + ": nonfinite native result");
}

void finite(const std::vector<Real>& values, const char* algorithm)
{
  for(Real value : values)
    finite(value, algorithm);
}

Real upperFrequency(Real requested, const Frame& input)
{
  require(
      std::isfinite(input.sampleRate) && input.sampleRate > 0.,
      "Spectral analysis requires a finite positive sample rate");
  const Real nyquist = static_cast<Real>(input.sampleRate / 2.);
  require(
      std::isfinite(requested) && requested >= 0.f && requested <= nyquist,
      "Upper frequency must be zero (automatic) or within Nyquist");
  return requested == 0.f ? nyquist : requested;
}

// Payload exchange preserves every vector object's bound address and restores
// the worker's stereo input even if an FFT throws.
struct AudioExchange
{
  std::vector<Real>& first;
  std::vector<Real>& second;
  AudioExchange(std::vector<Real>& a, std::vector<Real>& b)
      : first{a}
      , second{b}
  {
    first.swap(second);
  }
  ~AudioExchange() { first.swap(second); }
};
}

void SpectralContrastProcessor::prepare(Frame& input, const Settings& settings)
{
  const Real high = upperFrequency(settings.highHz, input);
  require(
      settings.bands >= 1 && settings.bands <= 32,
      "SpectralContrast requires 1 to 32 bands");
  require(
      std::isfinite(settings.lowHz) && settings.lowHz > 0.f && settings.lowHz < high,
      "SpectralContrast requires 0 < low frequency < high frequency");
  require(
      std::isfinite(settings.neighbourRatio) && settings.neighbourRatio > 0.f
          && settings.neighbourRatio <= 1.f,
      "SpectralContrast neighbour ratio must be in (0, 1]");
  require(
      std::isfinite(settings.staticDistribution) && settings.staticDistribution >= 0.f
          && settings.staticDistribution <= 1.f,
      "SpectralContrast static distribution must be in [0, 1]");
  spectral.prepare(input);

  // Mirror the pinned native band construction before it forms sort iterators.
  // Tiny FFTs or degenerate bounds otherwise create zero/negative-width bands.
  const Real binWidth = static_cast<Real>(input.sampleRate) / spectral.fftSize;
  const Real partToScale = 1.f - settings.staticDistribution;
  const int totalBins = std::round(high / binWidth);
  const Real scaledUpper = std::round(partToScale * totalBins) * binWidth;
  const int staticBins = std::round((1.f - partToScale) * totalBins / settings.bands);
  const Real ratio = std::pow(scaledUpper / settings.lowHz, Real(1.) / settings.bands);
  Real frequency = settings.lowHz;
  int lastBin = std::round(frequency / binWidth);
  int position = lastBin;
  for(int band = 0; band < settings.bands; ++band)
  {
    frequency *= ratio;
    const int width = std::round(frequency / binWidth - lastBin + staticBins);
    require(
        width > 0 && position >= 0 && position < spectral.fftSize / 2 + 1,
        "SpectralContrast bands exceed FFT resolution; increase window or reduce bands");
    position += width;
    lastBin = std::round(frequency / binWidth);
  }

  algorithm = makeAlgorithm("SpectralContrast");
  algorithm->configure(
      "frameSize", spectral.fftSize, "sampleRate", input.sampleRate, "numberBands",
      settings.bands, "lowFrequencyBound", settings.lowHz, "highFrequencyBound", high,
      "neighbourRatio", settings.neighbourRatio, "staticDistribution",
      settings.staticDistribution);
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("spectralContrast").set(contrast);
  algorithm->output("spectralValley").set(valleys);
}

void SpectralContrastProcessor::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(contrast, "SpectralContrast");
  finite(valleys, "SpectralContrast");
  result.contrast.swap(contrast);
  result.valleys.swap(valleys);
}

void SpectralComplexityProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      input.audio.size() >= 3,
      "SpectralComplexity requires at least three audio samples for safe peak "
      "detection");
  require(
      std::isfinite(input.sampleRate) && input.sampleRate >= 10000.,
      "SpectralComplexity's native 100-5000 Hz search requires sample rate >= 10000 Hz");
  require(
      std::isfinite(settings.threshold) && settings.threshold >= 0.f,
      "SpectralComplexity requires a nonnegative finite magnitude threshold");
  spectral.prepare(input);
  algorithm = makeAlgorithm("SpectralComplexity");
  algorithm->configure(
      "sampleRate", input.sampleRate, "magnitudeThreshold", settings.threshold);
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("spectralComplexity").set(complexity);
}

void SpectralComplexityProcessor::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(complexity, "SpectralComplexity");
  result.complexity = complexity;
}

void SpectralPeaksProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      input.audio.size() >= 3,
      "SpectralPeaks requires at least three audio samples for safe peak detection");
  peaks.prepare(
      input, settings.lowHz, settings.highHz, settings.threshold, settings.maximumPeaks);
}

void SpectralPeaksProcessor::compute(Result& result)
{
  peaks.compute();
  finite(peaks.frequencies, "SpectralPeaks");
  finite(peaks.magnitudes, "SpectralPeaks");
  result.frequencies.swap(peaks.frequencies);
  result.magnitudes.swap(peaks.magnitudes);
}

void SpectralWhiteningProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      input.audio.size() >= 3,
      "SpectralWhitening requires at least three audio samples for safe peak detection");
  const Real whiteningHz = upperFrequency(settings.whiteningHz, input);
  // The native 100-Hz envelope grid unconditionally accesses its final two points.
  require(
      whiteningHz * 1.2f >= 100.f && input.sampleRate / 2. >= 100.,
      "SpectralWhitening requires at least two 100-Hz envelope-grid points");
  peaks.prepare(
      input, settings.lowHz, settings.highHz, settings.threshold, settings.maximumPeaks);
  algorithm = makeAlgorithm("SpectralWhitening");
  algorithm->configure("sampleRate", input.sampleRate, "maxFrequency", whiteningHz);
  algorithm->input("spectrum").set(peaks.spectral.spectrum);
  algorithm->input("frequencies").set(peaks.frequencies);
  algorithm->input("magnitudes").set(peaks.magnitudes);
  algorithm->output("magnitudes").set(magnitudes);
}

void SpectralWhiteningProcessor::compute(Result& result)
{
  peaks.compute();
  algorithm->compute();
  finite(peaks.frequencies, "SpectralWhitening");
  finite(magnitudes, "SpectralWhitening");
  require(
      peaks.frequencies.size() == magnitudes.size(),
      "SpectralWhitening returned mismatched frequency and magnitude vectors");
  result.frequencies.swap(peaks.frequencies);
  result.magnitudes.swap(magnitudes);
}

void StrongPeakProcessor::prepare(Frame& input, const Settings&)
{
  spectral.prepare(input);
  algorithm = makeAlgorithm("StrongPeak");
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("strongPeak").set(strength);
}

void StrongPeakProcessor::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(strength, "StrongPeak");
  result.strength = strength;
}

void SpectralCentroidTimeProcessor::prepare(Frame& input, const Settings&)
{
  require(
      input.audio.size() >= 2,
      "SpectralCentroidTime requires at least two audio samples");
  require(
      std::isfinite(input.sampleRate) && input.sampleRate > 0.,
      "SpectralCentroidTime requires a finite positive sample rate");
  algorithm = makeAlgorithm("SpectralCentroidTime");
  algorithm->configure("sampleRate", input.sampleRate);
  algorithm->input("array").set(input.audio);
  algorithm->output("centroid").set(centroid);
}

void SpectralCentroidTimeProcessor::compute(Result& result)
{
  algorithm->compute();
  finite(centroid, "SpectralCentroidTime");
  result.centroid = centroid;
}

void LogSpectrumProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      settings.binsPerSemitone >= 1 && settings.binsPerSemitone <= 12,
      "LogSpectrum requires 1 to 12 bins per semitone");
  require(
      settings.octaves >= 1 && settings.octaves <= 9,
      "LogSpectrum requires 1 to 9 octaves");
  require(
      std::isfinite(settings.rollOn) && settings.rollOn >= 0.f && settings.rollOn <= 5.f,
      "LogSpectrum roll-on must be in [0, 5] percent");
  spectral.prepare(input);
  algorithm = makeAlgorithm("LogSpectrum");
  algorithm->configure(
      "frameSize", spectral.fftSize / 2 + 1, "sampleRate", input.sampleRate,
      "binsPerSemitone", settings.binsPerSemitone, "nOctave", settings.octaves, "rollOn",
      settings.rollOn);
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("logFreqSpectrum").set(spectrum);
  algorithm->output("meanTuning").set(meanTuning);
  algorithm->output("localTuning").set(localTuning);
}

void LogSpectrumProcessor::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(spectrum, "LogSpectrum");
  finite(meanTuning, "LogSpectrum");
  finite(localTuning, "LogSpectrum");
  result.spectrum.swap(spectrum);
  result.meanTuning.swap(meanTuning);
  result.localTuning = localTuning;
}

void PitchSalienceProcessor::prepare(Frame& input, const Settings& settings)
{
  const Real nyquist = upperFrequency(0.f, input);
  require(
      std::isfinite(settings.lowHz) && settings.lowHz > 0.f,
      "PitchSalience requires a finite positive lower separation");
  require(
      std::isfinite(settings.highHz) && settings.highHz >= 0.f,
      "PitchSalience requires a nonnegative finite upper separation");
  const Real high = settings.highHz == 0.f
                        ? std::min(5000.f, std::nextafter(nyquist, 0.f))
                        : settings.highHz;
  require(
      settings.lowHz < high && high < nyquist,
      "PitchSalience requires 0 < low separation < high separation < Nyquist");
  spectral.prepare(input);
  const Real bins = spectral.fftSize / 2 + 1;
  const int lowIndex = settings.lowHz * bins / nyquist;
  const int highIndex = high * bins / nyquist;
  require(
      lowIndex >= 1 && highIndex > lowIndex && highIndex < int(bins),
      "PitchSalience separation range needs nonzero distinct FFT lags; increase window");
  algorithm = makeAlgorithm("PitchSalience");
  algorithm->configure(
      "sampleRate", input.sampleRate, "lowBoundary", settings.lowHz, "highBoundary",
      high);
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("pitchSalience").set(salience);
}

void PitchSalienceProcessor::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(salience, "PitchSalience");
  result.salience = salience;
}

void PanningProcessor::prepare(Frame& frame, const Settings& settings)
{
  require(
      frame.left.size() >= 2 && frame.left.size() == frame.right.size()
          && frame.left.size() == frame.audio.size(),
      "Panning requires two real, equally sized audio channels with at least two "
      "samples");
  require(
      settings.averageFrames >= 0 && settings.averageFrames <= 4096,
      "Panning average frames must be in [0, 4096]");
  require(
      settings.histogramBins >= 2 && settings.histogramBins <= 4096,
      "Panning histogram requires 2 to 4096 bins");
  require(
      settings.coefficients >= 1 && settings.coefficients <= 512
          && settings.coefficients <= 2 * (settings.histogramBins - 1),
      "Panning coefficient count exceeds the native histogram IFFT size");
  input = &frame;
  leftFrame.sampleRate = rightFrame.sampleRate = frame.sampleRate;
  leftFrame.audio.resize(frame.left.size());
  rightFrame.audio.resize(frame.right.size());
  leftSpectrum.prepare(leftFrame);
  rightSpectrum.prepare(rightFrame);
  algorithm = makeAlgorithm("Panning");
  algorithm->configure(
      "sampleRate", frame.sampleRate, "averageFrames", settings.averageFrames,
      "panningBins", settings.histogramBins, "numCoeffs", settings.coefficients,
      "numBands", 1, "warpedPanorama", settings.warped);
  algorithm->input("spectrumLeft").set(leftSpectrum.spectrum);
  algorithm->input("spectrumRight").set(rightSpectrum.spectrum);
  algorithm->output("panningCoeffs").set(coefficients);
}

void PanningProcessor::compute(Result& result)
{
  {
    AudioExchange left{input->left, leftFrame.audio};
    AudioExchange right{input->right, rightFrame.audio};
    leftSpectrum.compute();
    rightSpectrum.compute();
  }
  algorithm->compute();
  result.bands = coefficients.dim1();
  result.coefficientsPerBand = coefficients.dim2();
  result.coefficients.resize(result.bands * result.coefficientsPerBand);
  for(int band = 0; band < result.bands; ++band)
    for(int coefficient = 0; coefficient < result.coefficientsPerBand; ++coefficient)
    {
      const Real value = coefficients[band][coefficient];
      finite(value, "Panning");
      result.coefficients[band * result.coefficientsPerBand + coefficient] = value;
    }
}

SpectralContrastProcessor::Settings SpectralContrast::settings() const
{
  return {
      inputs.bands.value, inputs.low.value, inputs.high.value, inputs.neighbour.value,
      inputs.distribution.value};
}
void SpectralContrast::publish(SpectralContrastProcessor::Result& result)
{
  outputs.contrast.value.swap(result.contrast);
  outputs.valleys.value.swap(result.valleys);
}

SpectralComplexityProcessor::Settings SpectralComplexity::settings() const
{
  return {inputs.threshold.value};
}
void SpectralComplexity::publish(SpectralComplexityProcessor::Result& result)
{
  outputs.complexity.value = result.complexity;
}

SpectralPeaksProcessor::Settings SpectralPeaks::settings() const
{
  return {
      inputs.low.value, inputs.high.value, inputs.threshold.value, inputs.maximum.value};
}
void SpectralPeaks::publish(SpectralPeaksProcessor::Result& result)
{
  outputs.frequencies.value.swap(result.frequencies);
  outputs.magnitudes.value.swap(result.magnitudes);
}

SpectralWhiteningProcessor::Settings SpectralWhitening::settings() const
{
  return {
      inputs.low.value, inputs.high.value, inputs.threshold.value, inputs.maximum.value,
      inputs.whitening.value};
}
void SpectralWhitening::publish(SpectralWhiteningProcessor::Result& result)
{
  outputs.frequencies.value.swap(result.frequencies);
  outputs.magnitudes.value.swap(result.magnitudes);
}

void StrongPeak::publish(StrongPeakProcessor::Result& result)
{
  outputs.strength.value = result.strength;
}
void SpectralCentroidTime::publish(SpectralCentroidTimeProcessor::Result& result)
{
  outputs.centroid.value = result.centroid;
}

LogSpectrumProcessor::Settings LogSpectrum::settings() const
{
  return {inputs.bins.value, inputs.octaves.value, inputs.rollOn.value};
}
void LogSpectrum::publish(LogSpectrumProcessor::Result& result)
{
  outputs.spectrum.value.swap(result.spectrum);
  outputs.meanTuning.value.swap(result.meanTuning);
  outputs.localTuning.value = result.localTuning;
}

PitchSalienceProcessor::Settings PitchSalience::settings() const
{
  return {inputs.low.value, inputs.high.value};
}
void PitchSalience::publish(PitchSalienceProcessor::Result& result)
{
  outputs.salience.value = result.salience;
}

PanningProcessor::Settings Panning::settings() const
{
  return {
      inputs.average.value, inputs.bins.value, inputs.coefficients.value,
      inputs.warped.value};
}
void Panning::publish(PanningProcessor::Result& result)
{
  outputs.coefficients.value.swap(result.coefficients);
  outputs.bands.value = result.bands;
  outputs.coefficientsPerBand.value = result.coefficientsPerBand;
}
}
