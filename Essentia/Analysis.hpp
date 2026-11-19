#pragma once

#include <essentia/algorithm.h>
#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace EssentiaNodes
{
using Real = essentia::Real;
using Algorithm = std::unique_ptr<essentia::standard::Algorithm>;
inline constexpr int maximumFrameSize = 65536;
inline constexpr std::size_t analysisQueueCapacity = 8;
inline constexpr std::size_t diagnosticReserve = 256;

Algorithm
makeAlgorithm(const std::string& name, const essentia::ParameterMap& parameters = {});

struct Frame
{
  std::vector<Real> audio;
  std::vector<Real> left, right;
  double sampleRate{48000.};
  int hopSize{};
  std::int64_t startSample{};
};

struct SpectrumFrame
{
  std::vector<Real> windowed, spectrum;
  int fftSize{};

  SpectrumFrame() = default;
  SpectrumFrame(const SpectrumFrame&) = delete;
  SpectrumFrame& operator=(const SpectrumFrame&) = delete;
  SpectrumFrame(SpectrumFrame&&) = delete;
  SpectrumFrame& operator=(SpectrumFrame&&) = delete;

  void prepare(Frame& input, const std::string& window = "hann");
  void compute();

private:
  Algorithm m_window, m_spectrum;
};

struct PeakFrame
{
  SpectrumFrame spectral;
  std::vector<Real> frequencies, magnitudes;

  PeakFrame() = default;
  PeakFrame(const PeakFrame&) = delete;
  PeakFrame& operator=(const PeakFrame&) = delete;
  PeakFrame(PeakFrame&&) = delete;
  PeakFrame& operator=(PeakFrame&&) = delete;

  void prepare(
      Frame& input, Real minimumHz = 20.f, Real maximumHz = 0.f, Real threshold = 1e-6f,
      int maximumPeaks = 100);
  void compute();

private:
  Algorithm m_peaks;
};
}
