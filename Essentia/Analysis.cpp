#include <Essentia/Analysis.hpp>

#include <cmath>

#include <algorithm>
#include <bit>
#include <mutex>
#include <stdexcept>

namespace EssentiaNodes
{
Algorithm
makeAlgorithm(const std::string& name, const essentia::ParameterMap& parameters)
{
  // Essentia's registry is process-global. Never shut it down while another node
  // (or another client of Essentia) may still own an algorithm.
  static std::once_flag initialized;
  std::call_once(initialized, [] { essentia::init(); });
  Algorithm algorithm{essentia::standard::AlgorithmFactory::create(name)};
  // In particular, never construct an unconfigured raw factory object: several
  // native destructors require resources installed by default configuration.
  if(!parameters.empty())
    algorithm->configure(parameters);
  return algorithm;
}

void SpectrumFrame::prepare(Frame& input, const std::string& window)
{
  const auto size = input.audio.size();
  if(size < 2 || size > maximumFrameSize)
    throw std::runtime_error("Spectrum requires an analysis window of 2..65536 samples");
  fftSize = static_cast<int>(std::bit_ceil(static_cast<unsigned>(size)));
  windowed.resize(fftSize);
  spectrum.resize(fftSize / 2 + 1);

  essentia::ParameterMap parameters;
  parameters.add("size", static_cast<int>(size));
  parameters.add("zeroPadding", fftSize - static_cast<int>(size));
  parameters.add("type", window);
  parameters.add("zeroPhase", false);
  parameters.add("normalized", true);
  parameters.add("splitPadding", false);
  parameters.add("symmetric", true);
  m_window = makeAlgorithm("Windowing", parameters);
  parameters.clear();
  parameters.add("size", fftSize);
  m_spectrum = makeAlgorithm("Spectrum", parameters);
  m_window->input("frame").set(input.audio);
  m_window->output("frame").set(windowed);
  m_spectrum->input("frame").set(windowed);
  m_spectrum->output("spectrum").set(spectrum);
}

void SpectrumFrame::compute()
{
  m_window->compute();
  m_spectrum->compute();
  for(const Real magnitude : spectrum)
    if(!std::isfinite(magnitude) || magnitude < 0)
      throw std::runtime_error("Spectrum produced a nonfinite or negative magnitude");
}

void PeakFrame::prepare(
    Frame& input, Real minimumHz, Real maximumHz, Real threshold, int maximumPeaks)
{
  if(!std::isfinite(input.sampleRate) || input.sampleRate <= 0.)
    throw std::runtime_error("Spectral peaks require a finite positive sample rate");
  if(input.audio.size() < 3)
    throw std::runtime_error("Spectral peaks require at least three input samples");
  const Real nyquist = static_cast<Real>(input.sampleRate / 2.);
  if(maximumHz == 0)
    maximumHz = nyquist;
  if(!std::isfinite(minimumHz) || !std::isfinite(maximumHz) || minimumHz < 0
     || maximumHz <= minimumHz || maximumHz > nyquist)
    throw std::runtime_error(
        "Spectral peak bounds must satisfy 0 <= minimum < maximum <= Nyquist");
  if(!std::isfinite(threshold) || threshold < 0 || maximumPeaks < 1)
    throw std::runtime_error(
        "Spectral peak threshold must be nonnegative and peak count positive");
  spectral.prepare(input);
  essentia::ParameterMap parameters;
  parameters.add("sampleRate", input.sampleRate);
  parameters.add("minFrequency", minimumHz);
  parameters.add("maxFrequency", maximumHz);
  parameters.add("magnitudeThreshold", threshold);
  parameters.add("maxPeaks", maximumPeaks);
  parameters.add("orderBy", "frequency");
  m_peaks = makeAlgorithm("SpectralPeaks", parameters);
  m_peaks->input("spectrum").set(spectral.spectrum);
  m_peaks->output("frequencies").set(frequencies);
  m_peaks->output("magnitudes").set(magnitudes);
}

void PeakFrame::compute()
{
  spectral.compute();
  frequencies.clear();
  magnitudes.clear();
  m_peaks->compute();
  if(frequencies.size() != magnitudes.size())
    throw std::runtime_error(
        "Spectral peaks returned mismatched frequency and magnitude counts");
  for(std::size_t i = 0; i < frequencies.size(); ++i)
    if(!std::isfinite(frequencies[i]) || !std::isfinite(magnitudes[i])
       || frequencies[i] < 0 || magnitudes[i] < 0
       || (i && frequencies[i] < frequencies[i - 1]))
      throw std::runtime_error(
          "Spectral peaks produced invalid frequencies or magnitudes");
}
}
