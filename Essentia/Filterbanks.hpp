#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>

namespace EssentiaNodes
{
namespace FilterbankControls
{
struct Domain
{
  halp__enum_combobox("Band domain", Power, Power, Magnitude)
  halp_meta(
      description,
      "Power sums squared FFT magnitudes; Magnitude sums linear FFT magnitudes.")
};
struct Normalization
{
  halp__enum_combobox("Normalization", Sum, Sum, Peak)
  halp_meta(
      description,
      "Sum normalizes sampled filter weights to one; Peak leaves peak weights at one.")
};
struct TriangleNormalization
{
  halp__enum_combobox("Normalization", Sum, Sum, Peak, Area)
  halp_meta(
      description,
      "Sum: unit sampled weight sum; Peak: unit vertex; Area: unit theoretical triangle "
      "area in Hz.")
};
struct Compression
{
  halp__enum_combobox("Cepstral compression", Decibels, Decibels, NaturalLog, None)
  halp_meta(
      description,
      "Before DCT: 10 log10(power), 20 log10(magnitude), natural logarithm, or no "
      "compression.")
};
struct Spacing
{
  halp__enum_combobox("Frequency spacing", Linear, Linear, Logarithmic)
  halp_meta(
      description,
      "Triangle vertices equally spaced in Hz or geometrically spaced; logarithmic "
      "spacing requires lower Hz > 0.")
};
using Low = halp::knob_f32<"Lower frequency (Hz)", halp::range{0., 96000., 0.}>;
using High
    = halp::knob_f32<"Upper frequency (Hz; 0 = Nyquist)", halp::range{0., 96000., 0.}>;
using Log = halp::toggle<"Log2(1 + band)">;
using Bands = halp::spinbox_i32<"Bands", halp::range{2, 256, 40}>;
using Coefficients = halp::spinbox_i32<"Coefficients", halp::range{1, 256, 13}>;
using Dct = halp::spinbox_i32<"DCT type", halp::range{2, 3, 2}>;
}

// All bound objects stay in the worker processor; only vector payloads move.
struct FilterbankProcessorState
{
  struct Result
  {
    std::vector<Real> bands, coefficients, frequencies;
  };
  SpectrumFrame spectral;
  Algorithm algorithm;
  std::vector<Real> bands, coefficients, frequencies, grid;
  bool nativeFrequencies{};
  void compute(Result& result);
  void bind(const char* coefficientOutput = nullptr);
};

struct BarkBandsProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{27};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct ERBBandsProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{40};
    float low{40.f}, high{}, width{1.f};
    int domain{};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct TriangularBandsProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{40};
    float low{}, high{};
    int spacing{}, domain{}, normalization{};
    bool log{};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct TriangularBarkBandsProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{24};
    float low{}, high{};
    int domain{}, normalization{};
    bool log{};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct BFCCProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{40}, coefficients{13};
    float low{}, high{};
    int domain{}, normalization{}, compression{}, dct{2}, liftering{};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct GFCCProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{40}, coefficients{13};
    float low{40.f}, high{};
    int domain{}, compression{}, dct{2};
    float silenceThreshold{1e-10f};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};
struct SpectrumToCentProcessor : FilterbankProcessorState
{
  struct Settings
  {
    int bands{72};
    float minimum{220.f}, resolution{100.f}, high{};
    int domain{}, normalization{};
    bool log{};
    bool operator==(const Settings&) const = default;
  };
  void prepare(Frame&, const Settings&);
};

struct BarkBands : AudioNode<BarkBands, BarkBandsProcessor>
{
  halp_meta(name, "Bark bands")
  halp_meta(c_name, "essentia_bark_bands")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio to fixed rectangular Bark-band energies: sums of "
      "squared linear FFT magnitudes, not dB or PSD. Band count selects a prefix of "
      "Essentia's fixed 0–27000 Hz edges.")
  halp_meta(uuid, "ca989bb2-ae14-4d4c-9b3a-e75857c14943")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    halp::spinbox_i32<"Bands", halp::range{1, 28, 27}> bands;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Band power", std::vector<Real>> bands;
  } outputs;
  BarkBandsProcessor::Settings settings() const;
  void publish(BarkBandsProcessor::Result&);
};

struct ERBBands : AudioNode<ERBBands, ERBBandsProcessor>
{
  halp_meta(name, "ERB bands")
  halp_meta(c_name, "essentia_erb_bands")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio through an ERB-spaced gammatone bank. Bands are "
      "weighted sums of linear FFT magnitude or squared magnitude, not dB or power per "
      "Hz; centers are Hz.")
  halp_meta(uuid, "69487aa6-a5fa-48c5-a8c6-3da391492bb8")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    FilterbankControls::Bands bands;
    halp::knob_f32<"Lower frequency (Hz)", halp::range{0., 96000., 40.}> low;
    FilterbankControls::High high;
    halp::knob_f32<"Width (ERB multiple)", halp::range{0.1, 10., 1.}> width;
    FilterbankControls::Domain domain;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"Center frequencies (Hz)", std::vector<Real>> frequencies;
  } outputs;
  ERBBandsProcessor::Settings settings() const;
  void publish(ERBBandsProcessor::Result&);
};

struct TriangularBands : AudioNode<TriangularBands, TriangularBandsProcessor>
{
  halp_meta(name, "Triangular bands")
  halp_meta(c_name, "essentia_triangular_bands")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio through overlapping triangles linear in Hz, with "
      "linearly or logarithmically spaced vertices. Weighted FFT magnitude or power; "
      "optional log2(1 + band). Area normalization divides by triangle area in Hz. "
      "Bounds are outer vertices; centers are Hz.")
  halp_meta(uuid, "60a282aa-c9c0-42e5-a2cf-f992e4fc011c")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    FilterbankControls::Bands bands;
    FilterbankControls::Low low;
    FilterbankControls::High high;
    FilterbankControls::Spacing spacing;
    FilterbankControls::Domain domain;
    FilterbankControls::TriangleNormalization normalization;
    FilterbankControls::Log log;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"Center frequencies (Hz)", std::vector<Real>> frequencies;
  } outputs;
  TriangularBandsProcessor::Settings settings() const;
  void publish(TriangularBandsProcessor::Result&);
};

struct TriangularBarkBands : AudioNode<TriangularBarkBands, TriangularBarkBandsProcessor>
{
  halp_meta(name, "Triangular Bark bands")
  halp_meta(c_name, "essentia_triangular_bark_bands")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio through Rastamat-style Bark-spaced filters with "
      "asymmetric exponential skirts. Weighted FFT magnitude or power; optional log2(1 "
      "+ complete band sum). Bounds select first/last centers in Hz, not hard cutoffs.")
  halp_meta(uuid, "791432a6-ac27-4970-8985-cdb02ff5570c")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    halp::spinbox_i32<"Bands", halp::range{2, 256, 24}> bands;
    FilterbankControls::Low low;
    FilterbankControls::High high;
    FilterbankControls::Domain domain;
    FilterbankControls::Normalization normalization;
    FilterbankControls::Log log;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"Center frequencies (Hz)", std::vector<Real>> frequencies;
  } outputs;
  TriangularBarkBandsProcessor::Settings settings() const;
  void publish(TriangularBarkBandsProcessor::Result&);
};

struct BFCC : AudioNode<BFCC, BFCCProcessor>
{
  halp_meta(name, "Bark-frequency cepstral coefficients")
  halp_meta(c_name, "essentia_bfcc")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio to Bark filterbank cepstral coefficients (DCT, "
      "including coefficient zero). Bands retain linear magnitude/power units; "
      "coefficients transform the selected compression, not Hz. Bounds select Bark "
      "filter centers. Natural-log compression is undefined on exact silence and "
      "reports an error.")
  halp_meta(uuid, "6f13a633-77f4-4e61-95f4-5cf414f9d01a")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    FilterbankControls::Bands bands;
    FilterbankControls::Coefficients coefficients;
    FilterbankControls::Low low;
    FilterbankControls::High high;
    FilterbankControls::Domain domain;
    FilterbankControls::Normalization normalization;
    FilterbankControls::Compression compression;
    FilterbankControls::Dct dct;
    halp::spinbox_i32<"Liftering (0 = off)", halp::range{0, 256, 0}> liftering;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"BFCC", std::vector<Real>> coefficients;
  } outputs;
  BFCCProcessor::Settings settings() const;
  void publish(BFCCProcessor::Result&);
};

struct GFCC : AudioNode<GFCC, GFCCProcessor>
{
  halp_meta(name, "Gammatone-frequency cepstral coefficients")
  halp_meta(c_name, "essentia_gfcc")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio to ERB-spaced gammatone cepstral coefficients (DCT, "
      "including coefficient zero). Bands retain linear magnitude/power units; "
      "coefficients transform the selected compression, not Hz. Silence threshold is in "
      "the selected linear band domain.")
  halp_meta(uuid, "f1ecc269-5d0b-4e51-804f-0cf44b17fa36")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(8192, 4);
    FilterbankControls::Bands bands;
    FilterbankControls::Coefficients coefficients;
    halp::knob_f32<"Lower frequency (Hz)", halp::range{0., 96000., 40.}> low;
    FilterbankControls::High high;
    FilterbankControls::Domain domain;
    FilterbankControls::Compression compression;
    FilterbankControls::Dct dct;
    halp::knob_f32<"Silence threshold", halp::range{1e-15, 1., 1e-10}> silenceThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"GFCC", std::vector<Real>> coefficients;
  } outputs;
  GFCCProcessor::Settings settings() const;
  void publish(GFCCProcessor::Result&);
};

struct SpectrumToCent : AudioNode<SpectrumToCent, SpectrumToCentProcessor>
{
  halp_meta(name, "Cent-spaced spectrum")
  halp_meta(c_name, "essentia_spectrum_to_cent")
  halp_meta(category, "Analysis/Essentia/Filterbanks")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Hann-windowed mono audio through cent-spaced triangular filters. Bands are "
      "weighted linear FFT magnitude/power, optionally log2(1 + band); frequency output "
      "is Hz, not cents. Minimum is the first center. Upper bound limits the outermost "
      "edge; invalid grids error rather than truncate.")
  halp_meta(uuid, "d536d660-6b15-43bd-bc07-3246a6ae24dd")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(16384, 4);
    halp::spinbox_i32<"Bands", halp::range{1, 1024, 72}> bands;
    halp::knob_f32<"First center (Hz)", halp::range{1., 96000., 220.}> minimum;
    halp::knob_f32<"Spacing (cents)", halp::range{1., 1200., 100.}> resolution;
    FilterbankControls::High high;
    FilterbankControls::Domain domain;
    FilterbankControls::Normalization normalization;
    FilterbankControls::Log log;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Bands", std::vector<Real>> bands;
    halp::val_port<"Center frequencies (Hz)", std::vector<Real>> frequencies;
  } outputs;
  SpectrumToCentProcessor::Settings settings() const;
  void publish(SpectrumToCentProcessor::Result&);
};
}
