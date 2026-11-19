#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace EssentiaNodes
{
// At most eight completed windows are drained per callback, with no more than
// one event per input sample. Reserve the collection bound off the audio thread.
template <halp::static_string Name, typename T = double>
struct QualityEventOutput : halp::val_port<Name, std::vector<T>>
{
  QualityEventOutput() { this->value.reserve(analysisQueueCapacity * maximumFrameSize); }
};

struct QualityIntervals
{
  std::vector<double> starts, ends;
  double coverageStart{};
};

// Bound native vectors stay stationary; only their contents are cleared per call.
struct QualityIntervalState
{
  Frame* frame{};
  Algorithm algorithm;
  std::vector<Real> starts, ends;
  double origin{}, coverageStart{};
  int frameSize{}, hop{};
  std::int64_t calls{};

  void bind(Frame& input);
  void computeIntervals(QualityIntervals& result);
};

struct ClickProcessor : QualityIntervalState
{
  struct Settings
  {
    int order{12};
    float threshold{30.f};
    int powerThreshold{10}, silenceThreshold{-50};
    bool operator==(const Settings&) const = default;
  };
  using Result = QualityIntervals;
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct DiscontinuityProcessor
{
  struct Settings
  {
    int order{3}, kernel{7}, subframe{32};
    float threshold{8.f}, energyThreshold{-60.f};
    int silenceThreshold{-50};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<double> times;
    std::vector<Real> amplitudes;
    double coverageStart{};
  };
  Frame* frame{};
  Algorithm algorithm;
  std::vector<Real> locations, amplitudes;
  int frameSize{}, hop{};
  double coverageStart{};
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct GapsProcessor : QualityIntervalState
{
  struct Settings
  {
    int kernel{11};
    float silenceThreshold{-50.f}, powerThreshold{-30.f};
    float contextMs{5.f}, minimumMs{10.f}, maximumMs{3500.f};
    bool operator==(const Settings&) const = default;
  };
  using Result = QualityIntervals;
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct SaturationProcessor : QualityIntervalState
{
  struct Settings
  {
    float energyThreshold{-1.f}, differentialThreshold{0.001f}, minimumMs{0.005f};
    bool operator==(const Settings&) const = default;
  };
  using Result = QualityIntervals;
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct NoiseBurstProcessor
{
  struct Settings
  {
    int threshold{8}, silenceThreshold{-50};
    float alpha{0.9f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::vector<double> times;
    double coverageStart{}, coverageEnd{};
  };
  Frame* frame{};
  Algorithm algorithm;
  std::vector<Real> indexes;
  int frameSize{}, hop{}, ownedStart{}, ownedEnd{};
  double coverageStart{};
  bool first{true};
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct SNRProcessor
{
  struct Settings
  {
    float noiseThreshold{-40.f};
    int minimumNoiseFrames{15};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::optional<double> instant, averaged;
    std::vector<Real> spectral;
    int noiseFrames{};
    bool learned{}, ready{}, learning{};
  };
  Frame* frame{};
  Algorithm algorithm;
  Real instant{}, averaged{}, noisePower{};
  std::vector<Real> spectral;
  int frameSize{}, hop{}, noiseFrames{}, minimumNoiseFrames{};
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct FalseStereoProcessor
{
  static constexpr bool stereo = true;
  struct Settings
  {
    int silenceThreshold{-70};
    float correlationThreshold{0.9995f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::optional<double> correlation;
    bool falseStereo{}, ready{}, silent{}, degenerate{};
  };
  Frame* frame{};
  Algorithm algorithm;
  std::vector<essentia::StereoSample> stereoFrame;
  Real correlation{}, silencePower{};
  int falseStereo{}, frameSize{}, hop{};
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct StartStopSilenceProcessor
{
  struct Settings
  {
    int threshold{-60};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    std::optional<double> start, candidateEnd;
    bool heardSound{}, silent{}, endPending{true};
  };
  Frame* frame{};
  Algorithm algorithm;
  Real silencePower{};
  int startFrame{}, stopFrame{}, frameSize{}, hop{};
  std::int64_t calls{};
  double origin{};
  bool heardSound{};
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);
};

struct ClickDetector : AudioNode<ClickDetector, ClickProcessor>
{
  halp_meta(name, "Click detector")
  halp_meta(c_name, "essentia_click_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "LPC click intervals in analysis-stream seconds; fixed overlapping windows, "
      "central-hop coverage begins after startup context.")
  halp_meta(uuid, "778fece8-9e42-4be2-9418-44dcb6865bf1")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 2);
    halp::spinbox_i32<"LPC order", halp::range{1, 256, 12}> order;
    halp::spinbox_f32<"Detection threshold (dB)", halp::range{0., 100., 30.}> threshold;
    halp::spinbox_i32<"Power clipping factor", halp::irange{1, 100, 10}> powerThreshold;
    halp::spinbox_i32<"Silence threshold (dB)", halp::irange{-120, -1, -50}>
        silenceThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    QualityEventOutput<"Starts (s)"> starts;
    QualityEventOutput<"Ends (s)"> ends;
    halp::val_port<"Coverage begins (s)", std::optional<double>> coverageStart;
  } outputs;
  ClickProcessor::Settings settings() const;
  void begin_tick();
  void invalidate();
  void publish(ClickProcessor::Result& result);
};

struct DiscontinuityDetector : AudioNode<DiscontinuityDetector, DiscontinuityProcessor>
{
  halp_meta(name, "Discontinuity detector")
  halp_meta(c_name, "essentia_discontinuity_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Central-hop LPC discontinuity locations in seconds and prediction-error "
      "amplitudes; fixed overlapping windows.")
  halp_meta(uuid, "1f520be9-9f1b-40d6-9008-ffbc30764542")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 2);
    halp::spinbox_i32<"LPC order", halp::range{1, 256, 3}> order;
    halp::spinbox_i32<"Median kernel (odd)", halp::range{1, 255, 7}> kernel;
    halp::spinbox_i32<"Subframe samples", halp::range{1, 4096, 32}> subframe;
    halp::spinbox_f32<"Detection factor", halp::range{1., 100., 8.}> threshold;
    halp::spinbox_f32<"Subframe energy (dB)", halp::range{-120., 0., -60.}>
        energyThreshold;
    halp::spinbox_i32<"Silence threshold (dB)", halp::irange{-120, -1, -50}>
        silenceThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    QualityEventOutput<"Locations (s)"> times;
    QualityEventOutput<"Prediction errors", Real> amplitudes;
    halp::val_port<"Coverage begins (s)", std::optional<double>> coverageStart;
  } outputs;
  DiscontinuityProcessor::Settings settings() const;
  void begin_tick();
  void invalidate();
  void publish(DiscontinuityProcessor::Result& result);
};

struct GapsDetector : AudioNode<GapsDetector, GapsProcessor>
{
  halp_meta(name, "Gaps detector")
  halp_meta(c_name, "essentia_gaps_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Completed low-energy gap intervals in seconds, with pre/post sound evidence. "
      "Fixed overlapping windows; no EOF completion of pending gaps. Rectified envelope "
      "without rate-dependent smoothing.")
  halp_meta(uuid, "7d28caed-b4b5-460b-907b-88fadc55585f")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(2048, 2);
    halp::spinbox_i32<"Median kernel (odd)", halp::range{1, 255, 11}> kernel;
    halp::spinbox_f32<"Silence threshold (dB)", halp::range{-120., 0., -50.}>
        silenceThreshold;
    halp::spinbox_f32<"Context power (dB)", halp::range{-120., 0., -30.}> powerThreshold;
    halp::spinbox_f32<"Context duration (ms)", halp::range{0.01, 100., 5.}> contextMs;
    halp::spinbox_f32<"Minimum gap (ms)", halp::range{0.01, 10000., 10.}> minimumMs;
    halp::spinbox_f32<"Maximum gap (ms)", halp::range{0.01, 60000., 3500.}> maximumMs;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    QualityEventOutput<"Starts (s)"> starts;
    QualityEventOutput<"Ends (s)"> ends;
    halp::val_port<"Coverage begins (s)", std::optional<double>> coverageStart;
  } outputs;
  GapsProcessor::Settings settings() const;
  void begin_tick();
  void invalidate();
  void publish(GapsProcessor::Result& result);
};

struct SaturationDetector : AudioNode<SaturationDetector, SaturationProcessor>
{
  halp_meta(name, "Saturation detector")
  halp_meta(c_name, "essentia_saturation_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Completed high-amplitude flat-region intervals in seconds; not true-peak "
      "metering. Fixed overlapping windows; unfinished saturation remains pending.")
  halp_meta(uuid, "2f5e7dd8-6301-46f4-9e9e-bb76cdb6c0e8")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 2);
    halp::spinbox_f32<"Energy threshold (dB)", halp::range{-60., 0., -1.}>
        energyThreshold;
    halp::spinbox_f32<"Maximum sample difference", halp::range{0., 1., 0.001}>
        differentialThreshold;
    halp::spinbox_f32<"Minimum saturation (ms)", halp::range{0., 1000., 0.005}>
        minimumMs;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    QualityEventOutput<"Starts (s)"> starts;
    QualityEventOutput<"Ends (s)"> ends;
    halp::val_port<"Coverage begins (s)", std::optional<double>> coverageStart;
  } outputs;
  SaturationProcessor::Settings settings() const;
  void begin_tick();
  void invalidate();
  void publish(SaturationProcessor::Result& result);
};

struct NoiseBurstDetector : AudioNode<NoiseBurstDetector, NoiseBurstProcessor>
{
  halp_meta(name, "Noise burst detector")
  halp_meta(c_name, "essentia_noise_burst_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Positive second-derivative events in seconds at derivative centers. Fixed-hop "
      "EMA; overlap deduplicated by central-hop ownership. First window additionally "
      "covers startup samples from index 1; trailing context is pending.")
  halp_meta(uuid, "306c6f6d-bce3-4120-be03-32ab8b358afd")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 2);
    halp::spinbox_i32<"Threshold factor", halp::irange{1, 100, 8}> threshold;
    halp::spinbox_i32<"Silence threshold (dB)", halp::irange{-120, -1, -50}>
        silenceThreshold;
    halp::spinbox_f32<"EMA alpha per hop", halp::range{0.001, 0.999, 0.9}> alpha;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    QualityEventOutput<"Locations (s)"> times;
    halp::val_port<"Coverage begins (s)", std::optional<double>> coverageStart;
    halp::val_port<"Coverage ends exclusive (s)", std::optional<double>> coverageEnd;
  } outputs;
  NoiseBurstProcessor::Settings settings() const;
  void begin_tick();
  void invalidate();
  void publish(NoiseBurstProcessor::Result& result);
};

struct SNR : AudioNode<SNR, SNRProcessor>
{
  halp_meta(name, "Signal to noise ratio")
  halp_meta(c_name, "essentia_snr")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Gaussian-noise SNR estimator with fixed windows. Supply initial nonzero "
      "noise-only frames below the noise threshold. Ready gates finite signal-frame dB "
      "estimates; spectral SNR is linear. Broadband correction enabled.")
  halp_meta(uuid, "21c21039-6902-416a-9a76-8aac57911fbc")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 2);
    halp::spinbox_f32<"Noise threshold (dB)", halp::range{-120., 0., -40.}>
        noiseThreshold;
    halp::spinbox_i32<"Required noise frames", halp::range{15, 10000, 15}>
        minimumNoiseFrames;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Ready", bool> ready;
    halp::val_port<"Noise learned", bool> learned;
    halp::val_port<"Learning noise", bool> learning;
    halp::val_port<"Nonzero noise frames", int> noiseFrames;
    halp::val_port<"SNR (dB)", std::optional<double>> instant;
    halp::val_port<"Averaged SNR (dB)", std::optional<double>> averaged;
    halp::val_port<"Spectral SNR (linear)", std::vector<Real>> spectral;
  } outputs;
  SNRProcessor::Settings settings() const;
  void invalidate();
  void publish(SNRProcessor::Result& result);
};

struct FalseStereoDetector : AudioNode<FalseStereoDetector, FalseStereoProcessor>
{
  halp_meta(name, "False stereo detector")
  halp_meta(c_name, "essentia_false_stereo_detector")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Pearson channel similarity on the first two actual input channels. Ready=false "
      "with absent correlation for silence or zero-variance channels; anticorrelation "
      "is not false stereo.")
  halp_meta(uuid, "0fe5671e-0660-4a0a-a7bc-e2ebb789cb27")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(1024, 2);
    halp::spinbox_i32<"Silence threshold (dB)", halp::irange{-120, -1, -70}>
        silenceThreshold;
    halp::spinbox_f32<"Correlation threshold", halp::range{-1., 1., 0.9995}>
        correlationThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Ready", bool> ready;
    halp::val_port<"False stereo", bool> falseStereo;
    halp::val_port<"Correlation", std::optional<double>> correlation;
    halp::val_port<"Silent", bool> silent;
    halp::val_port<"Zero variance", bool> degenerate;
  } outputs;
  FalseStereoProcessor::Settings settings() const;
  void invalidate();
  void publish(FalseStereoProcessor::Result& result);
};

struct StartStopSilence : AudioNode<StartStopSilence, StartStopSilenceProcessor>
{
  halp_meta(name, "Start / stop silence")
  halp_meta(c_name, "essentia_start_stop_silence")
  halp_meta(category, "Analysis/Essentia/Quality")
  halp_meta(author, "ossia score, Essentia / MTG")
  halp_meta(
      description,
      "Live first/last nonsilent fixed-frame tracking. Start is the first sound frame "
      "start; candidate end is the last sound frame end, available only during trailing "
      "silence. Candidate ends are provisional, never EOF-finalized; all-silent input "
      "has no start/end.")
  halp_meta(uuid, "cb876330-9387-4c52-9a80-1b0f502a5426")
  struct
  {
    ESSENTIA_AUDIO_INPUTS(512, 1);
    halp::spinbox_i32<"Silence threshold (dB)", halp::irange{-120, 0, -60}> threshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Sound observed", bool> heardSound;
    halp::val_port<"Current frame silent", bool> silent;
    halp::val_port<"End pending", bool> endPending;
    halp::val_port<"First sound frame start (s)", std::optional<double>> start;
    halp::val_port<"Candidate sound end (s)", std::optional<double>> candidateEnd;
  } outputs;
  StartStopSilenceProcessor::Settings settings() const;
  void invalidate();
  void publish(StartStopSilenceProcessor::Result& result);
};
}
