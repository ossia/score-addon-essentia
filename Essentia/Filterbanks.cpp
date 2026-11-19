#include <Essentia/Filterbanks.hpp>

#include <cmath>

#include <algorithm>
#include <stdexcept>

namespace EssentiaNodes
{
namespace
{
void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}

const char* domainName(int domain)
{
  require(domain == 0 || domain == 1, "Unknown filterbank magnitude/power domain");
  return domain == 0 ? "power" : "magnitude";
}

const char* normalizationName(int normalization, bool area = false)
{
  require(
      normalization >= 0 && normalization <= (area ? 2 : 1),
      "Unknown filterbank normalization");
  switch(normalization)
  {
    case 1:
      return "unit_max";
    case 2:
      return "unit_tri";
    default:
      return "unit_sum";
  }
}

const char* compressionName(int compression, int domain)
{
  domainName(domain);
  require(compression >= 0 && compression <= 2, "Unknown cepstral compression");
  switch(compression)
  {
    case 1:
      return "log";
    case 2:
      return "natural"; // Essentia calls uncompressed linear bands 'natural'.
    default:
      return domain == 0 ? "dbpow" : "dbamp";
  }
}

Real upperFrequency(const Frame& input, Real low, Real high)
{
  require(
      std::isfinite(input.sampleRate) && input.sampleRate > 0.,
      "Filterbanks require a finite positive sample rate");
  require(
      std::isfinite(low) && low >= 0.f && std::isfinite(high) && high >= 0.f,
      "Frequency bounds must be finite and nonnegative");
  const Real nyquist = input.sampleRate / 2.;
  const Real upper = high == 0.f ? nyquist : high;
  require(
      upper > low && upper <= nyquist,
      "Upper frequency must exceed lower frequency and not exceed Nyquist (0 = "
      "Nyquist)");
  return upper;
}

void bandCount(int bands)
{
  require(bands >= 2 && bands <= 256, "Filterbank band count must be between 2 and 256");
}

void cepstralShape(int bands, int coefficients, int dct)
{
  bandCount(bands);
  require(
      coefficients >= 1 && coefficients <= bands,
      "Cepstral coefficient count must be between one and the band count");
  require(dct == 2 || dct == 3, "Cepstral DCT type must be 2 or 3");
}

void finite(const std::vector<Real>& values, const char* message)
{
  for(Real value : values)
    require(std::isfinite(value), message);
}

void resolvedCenters(const std::vector<Real>& grid, double binHz)
{
  for(std::size_t i = 1; i < grid.size(); ++i)
    require(
        grid[i] - grid[i - 1] >= binHz,
        "Filter centers are closer than one FFT bin: increase the analysis window or "
        "reduce the band count");
}

void erbCenters(
    std::vector<Real>& grid, int count, Real low, Real high, Real width, double binHz)
{
  require(
      std::isfinite(width) && width > 0.f,
      "ERB filter width must be finite and positive");
  constexpr double earQ = 9.26449;
  constexpr double minimumBandwidth = 24.7;
  constexpr double bandwidth = earQ * minimumBandwidth;
  grid.resize(count);
  for(int i = 1; i <= count; ++i)
    grid[count - i]
        = -bandwidth
          + std::exp(
                i * (std::log(low + bandwidth) - std::log(high + bandwidth)) / count)
                * (high + bandwidth);
  resolvedCenters(grid, binHz);
  require(
      width * (low / earQ + minimumBandwidth) >= binHz,
      "ERB filter bandwidth is narrower than one FFT bin: increase the window or filter "
      "width");
}

void barkCenters(std::vector<Real>& grid, int count, Real low, Real high, double binHz)
{
  const double first = 6. * std::asinh(low / 600.);
  const double last = 6. * std::asinh(high / 600.);
  grid.resize(count);
  for(int i = 0; i < count; ++i)
    grid[i] = 600. * std::sinh((first + (last - first) * i / (count - 1)) / 6.);
  resolvedCenters(grid, binHz);
  // The native filter has a one-Bark-wide plateau, including at the low edge.
  require(
      600. * std::sinh((first + 0.5) / 6.) - low >= binHz,
      "Bark filter core is narrower than one FFT bin: increase the analysis window");
}

void resolvedTriangles(const std::vector<Real>& edges, double binHz, bool rounded)
{
  finite(edges, "Triangle frequencies overflow: reduce the frequency range or spacing");
  for(std::size_t i = 0; i + 2 < edges.size(); ++i)
  {
    require(
        edges[i] < edges[i + 1] && edges[i + 1] < edges[i + 2],
        "Triangle vertices must be strictly increasing");
    // A positive-weight FFT bin must lie strictly inside each triangle.
    const double firstInside = (std::floor(edges[i] / binHz) + 1.) * binHz;
    require(
        firstInside < edges[i + 2],
        "A triangle contains no nonzero-weight FFT bin: increase the window or reduce "
        "the band count");
    if(rounded)
    {
      const auto first = std::floor(edges[i] / binHz + 0.5);
      const auto middle = std::floor(edges[i + 1] / binHz + 0.5);
      const auto last = std::floor(edges[i + 2] / binHz + 0.5);
      require(
          first < middle && middle < last,
          "Cent triangle vertices share an FFT bin: increase the window, first center "
          "or cent spacing");
    }
  }
}

essentia::ParameterMap parameters(
    Frame& input, const SpectrumFrame& spectral, int count, Real low, Real high,
    int domain)
{
  essentia::ParameterMap p;
  // Exact inputSize is essential: native Bark filters' implicit resize path is stale.
  p.add("inputSize", static_cast<int>(spectral.spectrum.size()));
  p.add("sampleRate", Real(input.sampleRate));
  p.add("numberBands", count);
  p.add("lowFrequencyBound", low);
  p.add("highFrequencyBound", high);
  p.add("type", domainName(domain));
  return p;
}
}

void FilterbankProcessorState::bind(const char* coefficientOutput)
{
  algorithm->input("spectrum").set(spectral.spectrum);
  algorithm->output("bands").set(bands);
  if(coefficientOutput)
    algorithm->output(coefficientOutput).set(coefficients);
  if(nativeFrequencies)
    algorithm->output("frequencies").set(frequencies);
}

void FilterbankProcessorState::compute(Result& result)
{
  spectral.compute();
  algorithm->compute();
  finite(bands, "Native filterbank returned nonfinite band values");
  finite(
      coefficients,
      "Native cepstrum returned nonfinite coefficients; natural-log BFCC is undefined "
      "on exact silence");
  finite(frequencies, "Native filterbank returned nonfinite center frequencies");
  result.bands.swap(bands);
  result.coefficients.swap(coefficients);
  if(nativeFrequencies)
    result.frequencies.swap(frequencies);
  else
    result.frequencies.assign(grid.begin(), grid.end());
}

void BarkBandsProcessor::prepare(Frame& input, const Settings& s)
{
  static constexpr Real edges[]{0,    50,    100,   150,   200,  300,  400,  510,
                                630,  770,   920,   1080,  1270, 1480, 1720, 2000,
                                2320, 2700,  3150,  3700,  4400, 5300, 6400, 7700,
                                9500, 12000, 15500, 20500, 27000};
  require(s.bands >= 1 && s.bands <= 28, "Bark band count must be between 1 and 28");
  upperFrequency(input, 0.f, edges[s.bands]);
  spectral.prepare(input);
  require(
      input.sampleRate / spectral.fftSize <= 50.,
      "Fixed Bark bands require FFT-bin spacing at most 50 Hz: increase the analysis "
      "window");
  essentia::ParameterMap p;
  p.add("sampleRate", Real(input.sampleRate));
  p.add("numberBands", s.bands);
  algorithm = makeAlgorithm("BarkBands", p);
  bind();
}

void ERBBandsProcessor::prepare(Frame& input, const Settings& s)
{
  bandCount(s.bands);
  const Real high = upperFrequency(input, s.low, s.high);
  spectral.prepare(input);
  erbCenters(grid, s.bands, s.low, high, s.width, input.sampleRate / spectral.fftSize);
  auto p = parameters(input, spectral, s.bands, s.low, high, s.domain);
  p.add("width", s.width);
  algorithm = makeAlgorithm("ERBBands", p);
  bind();
}

void TriangularBandsProcessor::prepare(Frame& input, const Settings& s)
{
  bandCount(s.bands);
  const Real high = upperFrequency(input, s.low, s.high);
  require(s.spacing == 0 || s.spacing == 1, "Unknown triangle frequency spacing");
  require(
      s.spacing == 0 || s.low > 0.f,
      "Logarithmic triangle spacing requires lower frequency > 0 Hz");
  spectral.prepare(input);
  std::vector<Real> edges(s.bands + 2);
  for(int i = 0; i < s.bands + 2; ++i)
  {
    const double fraction = double(i) / (s.bands + 1);
    edges[i] = s.spacing == 0
                   ? s.low + (high - s.low) * fraction
                   : s.low * std::exp(std::log(double(high) / s.low) * fraction);
  }
  edges.front() = s.low;
  edges.back() = high;
  resolvedTriangles(edges, input.sampleRate / spectral.fftSize, false);
  grid.assign(edges.begin() + 1, edges.end() - 1);
  essentia::ParameterMap p;
  p.add("inputSize", static_cast<int>(spectral.spectrum.size()));
  p.add("sampleRate", Real(input.sampleRate));
  p.add("frequencyBands", edges);
  p.add("weighting", "linear");
  p.add("type", domainName(s.domain));
  p.add("normalize", normalizationName(s.normalization, true));
  p.add("log", s.log);
  algorithm = makeAlgorithm("TriangularBands", p);
  bind();
}

void TriangularBarkBandsProcessor::prepare(Frame& input, const Settings& s)
{
  bandCount(s.bands);
  const Real high = upperFrequency(input, s.low, s.high);
  spectral.prepare(input);
  barkCenters(grid, s.bands, s.low, high, input.sampleRate / spectral.fftSize);
  auto p = parameters(input, spectral, s.bands, s.low, high, s.domain);
  p.add("normalize", normalizationName(s.normalization));
  p.add("weighting", "warping"); // The pinned implementation always uses Bark warping.
  p.add("log", s.log);
  algorithm = makeAlgorithm("TriangularBarkBands", p);
  bind();
}

void BFCCProcessor::prepare(Frame& input, const Settings& s)
{
  cepstralShape(s.bands, s.coefficients, s.dct);
  require(s.liftering >= 0 && s.liftering <= 256, "Liftering must be between 0 and 256");
  const Real high = upperFrequency(input, s.low, s.high);
  spectral.prepare(input);
  barkCenters(grid, s.bands, s.low, high, input.sampleRate / spectral.fftSize);
  grid.clear();
  auto p = parameters(input, spectral, s.bands, s.low, high, s.domain);
  p.add("numberCoefficients", s.coefficients);
  p.add("normalize", normalizationName(s.normalization));
  p.add("weighting", "warping");
  p.add("logType", compressionName(s.compression, s.domain));
  p.add("dctType", s.dct);
  p.add("liftering", s.liftering);
  algorithm = makeAlgorithm("BFCC", p);
  bind("bfcc");
}

void GFCCProcessor::prepare(Frame& input, const Settings& s)
{
  cepstralShape(s.bands, s.coefficients, s.dct);
  require(
      std::isfinite(s.silenceThreshold) && s.silenceThreshold > 0.f,
      "GFCC silence threshold must be finite and positive");
  const Real high = upperFrequency(input, s.low, s.high);
  spectral.prepare(input);
  erbCenters(grid, s.bands, s.low, high, 1.f, input.sampleRate / spectral.fftSize);
  grid.clear();
  auto p = parameters(input, spectral, s.bands, s.low, high, s.domain);
  p.add("numberCoefficients", s.coefficients);
  p.add("logType", compressionName(s.compression, s.domain));
  p.add("dctType", s.dct);
  p.add("silenceThreshold", s.silenceThreshold);
  algorithm = makeAlgorithm("GFCC", p);
  bind("gfcc");
}

void SpectrumToCentProcessor::prepare(Frame& input, const Settings& s)
{
  require(s.bands >= 1 && s.bands <= 1024, "Cent band count must be between 1 and 1024");
  require(
      std::isfinite(s.minimum) && s.minimum > 0.f,
      "First cent-band center must be finite and positive");
  require(
      std::isfinite(s.resolution) && s.resolution > 0.f,
      "Cent spacing must be finite and positive");
  const Real high = upperFrequency(input, s.minimum, s.high);
  spectral.prepare(input);
  std::vector<Real> edges(s.bands + 2);
  for(int i = -1; i <= s.bands; ++i)
    edges[i + 1] = s.minimum * std::pow(2., double(s.resolution) * i / 1200.);
  require(
      std::isfinite(edges.back()) && edges.back() <= high,
      "Cent filter outer edge exceeds the upper frequency bound: reduce bands, first "
      "center or cent spacing");
  resolvedTriangles(edges, input.sampleRate / spectral.fftSize, true);
  essentia::ParameterMap p;
  p.add("inputSize", static_cast<int>(spectral.spectrum.size()));
  p.add("sampleRate", Real(input.sampleRate));
  p.add("minimumFrequency", s.minimum);
  p.add("centBinResolution", s.resolution);
  p.add("bands", s.bands);
  p.add("type", domainName(s.domain));
  p.add("normalize", normalizationName(s.normalization));
  p.add("log", s.log);
  nativeFrequencies = true;
  algorithm = makeAlgorithm("SpectrumToCent", p);
  bind();
}

BarkBandsProcessor::Settings BarkBands::settings() const
{
  return {inputs.bands.value};
}
void BarkBands::publish(BarkBandsProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
}

ERBBandsProcessor::Settings ERBBands::settings() const
{
  return {
      inputs.bands.value, inputs.low.value, inputs.high.value, inputs.width.value,
      static_cast<int>(inputs.domain.value)};
}
void ERBBands::publish(ERBBandsProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.frequencies.value.swap(r.frequencies);
}

TriangularBandsProcessor::Settings TriangularBands::settings() const
{
  return {
      inputs.bands.value,
      inputs.low.value,
      inputs.high.value,
      static_cast<int>(inputs.spacing.value),
      static_cast<int>(inputs.domain.value),
      static_cast<int>(inputs.normalization.value),
      inputs.log.value};
}
void TriangularBands::publish(TriangularBandsProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.frequencies.value.swap(r.frequencies);
}

TriangularBarkBandsProcessor::Settings TriangularBarkBands::settings() const
{
  return {
      inputs.bands.value,
      inputs.low.value,
      inputs.high.value,
      static_cast<int>(inputs.domain.value),
      static_cast<int>(inputs.normalization.value),
      inputs.log.value};
}
void TriangularBarkBands::publish(TriangularBarkBandsProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.frequencies.value.swap(r.frequencies);
}

BFCCProcessor::Settings BFCC::settings() const
{
  return {
      inputs.bands.value,
      inputs.coefficients.value,
      inputs.low.value,
      inputs.high.value,
      static_cast<int>(inputs.domain.value),
      static_cast<int>(inputs.normalization.value),
      static_cast<int>(inputs.compression.value),
      inputs.dct.value,
      inputs.liftering.value};
}
void BFCC::publish(BFCCProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.coefficients.value.swap(r.coefficients);
}

GFCCProcessor::Settings GFCC::settings() const
{
  return {
      inputs.bands.value,
      inputs.coefficients.value,
      inputs.low.value,
      inputs.high.value,
      static_cast<int>(inputs.domain.value),
      static_cast<int>(inputs.compression.value),
      inputs.dct.value,
      inputs.silenceThreshold.value};
}
void GFCC::publish(GFCCProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.coefficients.value.swap(r.coefficients);
}

SpectrumToCentProcessor::Settings SpectrumToCent::settings() const
{
  return {
      inputs.bands.value,
      inputs.minimum.value,
      inputs.resolution.value,
      inputs.high.value,
      static_cast<int>(inputs.domain.value),
      static_cast<int>(inputs.normalization.value),
      inputs.log.value};
}
void SpectrumToCent::publish(SpectrumToCentProcessor::Result& r)
{
  outputs.bands.value.swap(r.bands);
  outputs.frequencies.value.swap(r.frequencies);
}
}
