#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>

#include <cmath>

#include <algorithm>
#include <complex>
#include <iostream>
#include <memory>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using essentia::Real;
using Algorithm = std::unique_ptr<essentia::standard::Algorithm>;
using Factory = essentia::standard::AlgorithmFactory;

void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}
bool approximatelyEqual(double a, double b, double tolerance = 1e-5)
{
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}

void requireFFTRejection(const Algorithm& algorithm)
{
  try
  {
    algorithm->compute();
  }
  catch(const essentia::EssentiaException&)
  {
    return;
  }
  throw std::runtime_error("FFT accepted an unsupported input length");
}

void realFFT()
{
  Algorithm forward{Factory::create("FFT", "size", 6)};
  Algorithm inverse{Factory::create("IFFT", "size", 6)};
  Algorithm rawInverse{Factory::create("IFFT", "size", 6, "normalize", false)};
  std::vector<Real> frame, roundTrip;
  std::vector<std::complex<Real>> spectrum;
  forward->input("frame").set(frame);
  forward->output("fft").set(spectrum);
  for(auto* algorithm : {inverse.get(), rawInverse.get()})
  {
    algorithm->input("fft").set(spectrum);
    algorithm->output("frame").set(roundTrip);
  }

  for(int size : {6, 10, 10})
  {
    frame.assign(size, 0.f);
    frame[1] = 1.f;
    forward->compute();
    require(spectrum.size() == std::size_t(size / 2 + 1), "FFT padded a real frame");
    for(std::size_t k = 0; k < spectrum.size(); ++k)
    {
      const auto expected = std::polar(1.f, -2.f * std::numbers::pi_v<Real> * k / size);
      require(approximatelyEqual(std::abs(spectrum[k] - expected), 0.), "FFT bin differs");
    }

    for(auto* algorithm : {inverse.get(), rawInverse.get()})
    {
      algorithm->compute();
      require(roundTrip.size() == frame.size(), "IFFT changed the real frame length");
      const auto scale = algorithm == inverse.get() ? 1.f : Real(size);
      for(int i = 0; i < size; ++i)
        require(approximatelyEqual(roundTrip[i], frame[i] * scale), "IFFT scaling differs");
    }
  }

  frame.assign(3, 0.f);
  requireFFTRejection(forward);
  frame.clear();
  requireFFTRejection(forward);
  spectrum.resize(1);
  requireFFTRejection(inverse);
  spectrum.clear();
  requireFFTRejection(inverse);
}

void complexFFT()
{
  using Complex = std::complex<Real>;
  constexpr int size = 6;
  Algorithm forward{Factory::create("FFTC", "size", size, "negativeFrequencies", true)};
  Algorithm prefix{Factory::create("FFTC", "size", size)};
  Algorithm inverse{Factory::create("IFFTC", "size", 5)};
  Algorithm rawInverse{Factory::create("IFFTC", "size", 5, "normalize", false)};
  std::vector<Complex> frame(size), spectrum, inPlace, roundTrip;
  for(int i = 0; i < size; ++i)
    frame[i] = std::polar(1.f, 2.f * std::numbers::pi_v<Real> * (1 - i) / size);
  forward->input("frame").set(frame);
  forward->output("fft").set(spectrum);
  forward->compute();
  require(spectrum.size() == size, "FFTC padded a complex frame");
  require(approximatelyEqual(std::abs(spectrum.back()), size), "FFTC discarded a negative bin");

  inPlace = frame;
  prefix->input("frame").set(inPlace);
  prefix->output("fft").set(inPlace);
  prefix->compute();
  require(inPlace.size() == size / 2 + 1, "FFTC returned negative bins when disabled");
  for(std::size_t k = 0; k < inPlace.size(); ++k)
    require(approximatelyEqual(std::abs(inPlace[k] - spectrum[k]), 0.), "FFTC in-place prefix differs");

  for(auto* algorithm : {inverse.get(), rawInverse.get()})
  {
    algorithm->input("fft").set(spectrum);
    algorithm->output("frame").set(roundTrip);
    for(int inverseSize : {size, 5, 1})
    {
      spectrum.assign(inverseSize, {});
      spectrum.back() = 1.f;
      algorithm->compute();
      require(roundTrip.size() == spectrum.size(), "IFFTC changed the complex frame length");
      const Real scale = algorithm == inverse.get() ? 1.f / inverseSize : 1.f;
      for(int i = 0; i < inverseSize; ++i)
      {
        const auto expected = std::polar(scale, -2.f * std::numbers::pi_v<Real> * i / inverseSize);
        require(approximatelyEqual(std::abs(roundTrip[i] - expected), 0.), "IFFTC scaling differs");
      }
    }
  }

  frame.assign(3, {});
  requireFFTRejection(forward);
  frame.clear();
  requireFFTRejection(forward);
  spectrum.clear();
  requireFFTRejection(inverse);
}

void firstSound()
{
  Algorithm detector{Factory::create("StartStopSilence")};
  std::vector<Real> frame(64, 0.25f);
  int start{}, stop{};
  detector->input("frame").set(frame);
  detector->output("startFrame").set(start);
  detector->output("stopFrame").set(stop);
  detector->compute();
  std::fill(frame.begin(), frame.end(), 0.f);
  detector->compute();
  require(
      start == 0 && stop == 0,
      "trailing silence moved a sound that began at frame zero");
  detector->reset();
  detector->compute();
  frame[0] = 1.f;
  detector->compute();
  require(start == 1 && stop == 1, "reset did not restart the first-sound clock");
}

void welchEnergy()
{
  for(const int size : {63, 64, 1000})
  {
    int fftSize = 1;
    while(fftSize < size)
      fftSize *= 2;
    Algorithm welch{Factory::create(
        "Welch", "frameSize", size, "fftSize", fftSize, "sampleRate", 48000.,
        "averagingFrames", 1, "scaling", "density")};
    std::vector<Real> frame(size, 1.f), psd;
    welch->input("frame").set(frame);
    welch->output("psd").set(psd);
    welch->compute();
    const double power = std::accumulate(psd.begin(), psd.end(), 0.) * 48000. / fftSize;
    require(approximatelyEqual(power, 1., 2e-5), "zero padding changed integrated Welch power");
  }
}

void barkCompression()
{
  std::vector<Real> spectrum(4097), linear, compressed;
  for(std::size_t i = 0; i < spectrum.size(); ++i)
    spectrum[i] = 0.1f + Real(i % 31) / 50.f;
  Algorithm plain{Factory::create(
      "TriangularBarkBands", "inputSize", 4097, "sampleRate", 48000.,
      "highFrequencyBound", 20000., "log", false)};
  Algorithm logarithmic{Factory::create(
      "TriangularBarkBands", "inputSize", 4097, "sampleRate", 48000.,
      "highFrequencyBound", 20000., "log", true)};
  plain->input("spectrum").set(spectrum);
  plain->output("bands").set(linear);
  logarithmic->input("spectrum").set(spectrum);
  logarithmic->output("bands").set(compressed);
  plain->compute();
  logarithmic->compute();
  require(linear.size() == compressed.size(), "Bark compression changed the band count");
  for(std::size_t i = 0; i < linear.size(); ++i)
    require(
        approximatelyEqual(compressed[i], std::log2(1. + linear[i])),
        "Bark compression was applied before band accumulation completed");
}

void constantStereo()
{
  Algorithm detector{Factory::create("FalseStereoDetector")};
  std::vector<essentia::StereoSample> frame(512);
  for(int i = 0; i < 512; ++i)
    frame[i] = essentia::StereoSample{Real(std::sin(i * 0.1)), 0.5f};
  int duplicated{};
  Real correlation{};
  detector->input("frame").set(frame);
  detector->output("isFalseStereo").set(duplicated);
  detector->output("correlation").set(correlation);
  detector->compute();
  require(
      correlation == 0.f && !duplicated,
      "constant right channel yielded an undefined Pearson correlation");
}

std::pair<Real, Real> pitch(std::vector<Real>& spectrum, const char* weighting, int rate)
{
  Algorithm yin{Factory::create(
      "PitchYinFFT", "frameSize", 2048, "sampleRate", rate, "weighting", weighting,
      "maxFrequency", rate / 2.)};
  Real frequency{}, confidence{};
  yin->input("spectrum").set(spectrum);
  yin->output("pitch").set(frequency);
  yin->output("pitchConfidence").set(confidence);
  yin->compute();
  return {frequency, confidence};
}
void weightingIsolation()
{
  std::vector<Real> spectrum(1025);
  for(int i = 0; i < 1025; ++i)
    spectrum[i] = 0.01f + Real(std::abs(std::sin(i * 0.71))) * 0.2f;
  spectrum[19] = 1.f;
  spectrum[38] = 0.8f;
  const auto before = pitch(spectrum, "Z", 48000);
  (void)pitch(spectrum, "A", 48000);
  const auto after = pitch(spectrum, "Z", 48000);
  require(
      approximatelyEqual(before.first, after.first)
          && approximatelyEqual(before.second, after.second),
      "one YinFFT instance changed another instance's Z weighting");
}
void highRatePitch()
{
  std::vector<Real> spectrum(1025, 0.f);
  spectrum[10] = 1.f;
  spectrum[20] = 0.5f;
  for(const char* weighting : {"custom", "A", "B", "C", "D", "Z"})
  {
    const auto result = pitch(spectrum, weighting, 96000);
    require(
        std::isfinite(result.first) && std::isfinite(result.second),
        "high-rate YinFFT read beyond its weighting table");
  }
}

void snrAfterSilence()
{
  Algorithm snr{Factory::create("SNR", "frameSize", 512, "sampleRate", 48000.)};
  std::vector<Real> frame(512, 0.f), spectrum;
  Real instant{}, average{};
  snr->input("frame").set(frame);
  snr->output("instantSNR").set(instant);
  snr->output("averagedSNR").set(average);
  snr->output("spectralSNR").set(spectrum);
  for(int i = 0; i < 20; ++i)
    snr->compute();
  for(int i = 0; i < 512; ++i)
    frame[i] = Real(0.5 * std::sin(i * 0.13));
  snr->compute();
  require(
      std::isfinite(instant) && std::isfinite(average),
      "exact silence poisoned SNR history");
  require(
      std::all_of(
          spectrum.begin(), spectrum.end(), [](Real x) { return std::isfinite(x); }),
      "exact silence poisoned spectral SNR history");
}

void panningEndpoints()
{
  Algorithm pan{Factory::create("Panning", "averageFrames", 1, "sampleRate", 48000.)};
  std::vector<Real> left(1025, 0.f), right(1025, 1.f);
  TNT::Array2D<Real> coefficients;
  pan->input("spectrumLeft").set(left);
  pan->input("spectrumRight").set(right);
  pan->output("panningCoeffs").set(coefficients);
  for(int direction = 0; direction < 2; ++direction)
  {
    pan->compute();
    require(
        coefficients.dim1() == 1 && coefficients.dim2() == 20,
        "panning result shape changed");
    for(int i = 0; i < 20; ++i)
      require(
          std::isfinite(coefficients[0][i]),
          "hard panning produced a nonfinite coefficient");
    left.swap(right);
    pan->reset();
  }
}

void gapChronology()
{
  Algorithm detector{Factory::create(
      "GapsDetector", "frameSize", 2048, "hopSize", 1024, "sampleRate", 48000.,
      "attackTime", 0., "releaseTime", 0., "prepowerTime", 5., "postpowerTime", 5.)};
  std::vector<Real> frame(2048), starts, ends;
  detector->input("frame").set(frame);
  detector->output("starts").set(starts);
  detector->output("ends").set(ends);
  std::vector<Real> observedStarts, observedEnds;
  for(int offset = 0; offset < 5120; offset += 1024)
  {
    for(int i = 0; i < 2048; ++i)
    {
      const int sample = offset + i;
      frame[i] = (sample >= 700 && sample < 1400) || sample >= 2200 ? 0.5f : 0.f;
    }
    starts.clear();
    ends.clear();
    detector->compute();
    observedStarts.insert(observedStarts.end(), starts.begin(), starts.end());
    observedEnds.insert(observedEnds.end(), ends.begin(), ends.end());
  }
  require(
      observedStarts.size() == 1 && observedEnds.size() == 1,
      "an earlier rising flank consumed a later gap candidate");
  require(
      approximatelyEqual(observedStarts[0], 1400. / 48000., 1. / 48000.)
          && approximatelyEqual(observedEnds[0], 2200. / 48000., 1. / 48000.),
      "gap endpoints are not in source time");
}

void gapPostContext()
{
  Algorithm detector{Factory::create(
      "GapsDetector", "frameSize", 2048, "hopSize", 1024, "sampleRate", 48000.,
      "attackTime", 0., "releaseTime", 0., "prepowerTime", 5., "postpowerTime", 5.)};
  std::vector<Real> frame(2048), starts, ends, observedStarts, observedEnds;
  detector->input("frame").set(frame);
  detector->output("starts").set(starts);
  detector->output("ends").set(ends);
  for(int offset = 0; offset < 4096; offset += 1024)
  {
    for(int i = 0; i < 2048; ++i)
    {
      const int sample = offset + i;
      // Only the requested 240 post-context samples have enough power.
      // Including later silence in that average incorrectly rejects this gap.
      frame[i] = sample < 800 ? 0.5f : sample >= 1400 && sample < 1640 ? 0.05f : 0.f;
    }
    starts.clear();
    ends.clear();
    detector->compute();
    observedStarts.insert(observedStarts.end(), starts.begin(), starts.end());
    observedEnds.insert(observedEnds.end(), ends.begin(), ends.end());
  }
  require(
      observedStarts.size() == 1 && observedEnds.size() == 1,
      "samples beyond the configured post-context changed gap acceptance");
  require(
      approximatelyEqual(observedStarts[0], 800. / 48000., 1. / 48000.)
          && approximatelyEqual(observedEnds[0], 1400. / 48000., 1. / 48000.),
      "post-context changed gap endpoints");
}

void silenceLifetime()
{
  std::vector<Real> frame(64, 0.f);
  Real rate{};
  // Under LeakSanitizer this also checks ownership of dynamically declared ports.
  for(int i = 0; i < 100; ++i)
  {
    Algorithm detector{
        Factory::create("SilenceRate", "thresholds", std::vector<Real>{1e-4f})};
    detector->input("frame").set(frame);
    detector->output("threshold_0").set(rate);
    detector->compute();
    require(rate == 1.f, "zero energy was not silent");
  }
}
}

int main(int argc, char** argv)
{
  essentia::init();
  const std::pair<const char*, void (*)()> cases[]{
      {"real-fft", realFFT},
      {"complex-fft", complexFFT},
      {"first-sound", firstSound},
      {"welch-energy", welchEnergy},
      {"bark-compression", barkCompression},
      {"constant-stereo", constantStereo},
      {"yin-weighting-isolation", weightingIsolation},
      {"yin-high-rate", highRatePitch},
      {"snr-after-silence", snrAfterSilence},
      {"panning-endpoints", panningEndpoints},
      {"gap-chronology", gapChronology},
      {"gap-post-context", gapPostContext},
      {"silence-lifetime", silenceLifetime}};
  int failures = 0;
  int selected = 0;
  for(const auto& [name, test] : cases)
  {
    if(argc > 1 && std::string_view(argv[1]) != name)
      continue;
    ++selected;
    try
    {
      test();
      std::cout << "PASS " << name << '\n';
    }
    catch(const std::exception& e)
    {
      ++failures;
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  }
  if(!selected)
  {
    std::cerr << "Unknown regression name\n";
    return 2;
  }
  return failures ? 1 : 0;
}
