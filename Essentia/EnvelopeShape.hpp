#pragma once

#include <Essentia/Analysis.hpp>
#include <Essentia/Node.hpp>

namespace EssentiaNodes
{
struct EnvelopeAttackControl
    : halp::knob_f32<"Envelope attack (ms)", halp::range{0., 10000., 10.}>
{
  halp_meta(
      description,
      "Native Envelope attack low-pass time constant in milliseconds; zero disables "
      "attack smoothing. State starts at zero in every analysis window.");
};

struct EnvelopeReleaseControl
    : halp::knob_f32<"Envelope release (ms)", halp::range{0., 10000., 1500.}>
{
  halp_meta(
      description,
      "Native Envelope release low-pass time constant in milliseconds; zero disables "
      "release smoothing. No tail outside the current window is synthesized.");
};

struct EnvelopeShapeProcessor
{
  struct Settings
  {
    Real attackMs{10.f};
    Real releaseMs{1500.f};
    Real startThreshold{0.2f};
    Real stopThreshold{0.9f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real releaseDerivative{}, attackDerivative{}, flatness{};
    Real logAttackTime{}, attackStart{}, attackStop{};
    Real maxToTotal{}, minToTotal{}, tcToTotal{};
    bool silent{}, flat{}, attackTimeResolved{};
    std::vector<Real> envelope;
  };

  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);

private:
  Algorithm envelopeAlgorithm_, derivative_, flatness_, attack_, maximum_, minimum_,
      centroid_;
  std::vector<Real> envelope_;
  Real releaseDerivative_{}, attackDerivative_{}, flatnessValue_{};
  Real logAttackTime_{}, attackStart_{}, attackStop_{};
  Real maxToTotal_{}, minToTotal_{}, tcToTotal_{};
};

struct EnvelopeShape : AudioNode<EnvelopeShape, EnvelopeShapeProcessor>
{
  halp_meta(name, "Envelope shape");
  halp_meta(c_name, "essentia_envelope_shape");
  halp_meta(category, "Analysis/Essentia/Envelope");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(uuid, "61e4175f-57bb-4242-8576-3367cb7db6c0");
  halp_meta(
      description,
      "Window-local event shape from the native rectified Envelope, reset to zero for "
      "every window, not a continuous follower. "
      "Window and hop are samples and a window divisor. Use a window containing the "
      "complete event and release to obtain complete-event descriptors; "
      "otherwise attack times and extrema/centroid positions describe only this window. "
      "Attack start/stop are window-relative seconds at the host sample rate. "
      "DerivativeSFX returns the native maximum attack step (amplitude/sample) and "
      "post-peak sum of differences divided by sum of envelope (1/sample), "
      "including the peak step; these are not fitted slopes or per-second derivatives. "
      "FlatnessSFX is percentile95/percentile20, with native sentinel 1 when "
      "percentile20 is zero. "
      "LogAttackTime is log10(seconds), with native sentinel -5 for durations <= 0.0001 "
      "s; Attack time resolved distinguishes those cases. "
      "MaxToTotal and MinToTotal use the first extremum index/N; TCToTotal uses "
      "amplitude centroid/(N-1). "
      "Silence gives derivatives 0, flatness 1, log attack -5, attack times/extrema 0 "
      "and centroid 0.5. Silent and flat flags refer to the computed envelope. Requires "
      "at least two samples.");

  struct
  {
    ESSENTIA_AUDIO_INPUTS(16384, 4);
    EnvelopeAttackControl attack;
    EnvelopeReleaseControl release;
    struct : halp::knob_f32<"Attack start threshold", halp::range{0., 1., 0.2}>
    {
      halp_meta(
          description,
          "Native LogAttackTime startAttackThreshold, a fraction of the maximum window "
          "envelope; must not exceed the stop threshold.");
    } startThreshold;
    struct : halp::knob_f32<"Attack stop threshold", halp::range{0., 1., 0.9}>
    {
      halp_meta(
          description,
          "Native LogAttackTime stopAttackThreshold, a fraction of the maximum window "
          "envelope; must be at least the start threshold.");
    } stopThreshold;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Maximum attack step (amplitude/sample)", Real> attackDerivative;
    halp::val_port<"Release derivative ratio (1/sample)", Real> releaseDerivative;
    halp::val_port<"Envelope flatness (P95/P20)", Real> flatness;
    halp::val_port<"Log attack time (log10 seconds)", Real> logAttackTime;
    halp::val_port<"Attack start in window (s)", Real> attackStart;
    halp::val_port<"Attack stop in window (s)", Real> attackStop;
    halp::val_port<"Maximum position / N", Real> maxToTotal;
    halp::val_port<"Minimum position / N", Real> minToTotal;
    halp::val_port<"Temporal centroid / (N-1)", Real> tcToTotal;
    halp::val_port<"Silent envelope", bool> silent;
    halp::val_port<"Flat envelope", bool> flat;
    halp::val_port<"Attack time resolved", bool> attackTimeResolved;
    halp::val_port<"Window envelope", std::vector<Real>> envelope;
  } outputs;

  EnvelopeShapeProcessor::Settings settings() const;
  void publish(EnvelopeShapeProcessor::Result& result);
};

struct StrongDecayProcessor
{
  struct Settings
  {
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real strongDecay{};
    bool silent{}, positiveCentroid{};
  };
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);

private:
  Frame* input_{};
  Algorithm decay_;
  Real value_{};
};

struct StrongDecay : AudioNode<StrongDecay, StrongDecayProcessor>
{
  halp_meta(name, "Strong decay (PCM)");
  halp_meta(c_name, "essentia_strong_decay");
  halp_meta(category, "Analysis/Essentia/Envelope");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(uuid, "e71efe6a-8282-43b4-b318-4f48a66fbc3f");
  halp_meta(
      description,
      "Native StrongDecay on signed mono PCM, not a smoothed envelope: sqrt(sum of "
      "squared PCM / absolute-amplitude temporal centroid in seconds). "
      "The host sample rate supplies the time scale. Window is in samples; hop is a "
      "window divisor. "
      "Measures the current window, not automatically a complete event; include the "
      "entire event for event-level interpretation. "
      "Native output is zero for silence or an impulse solely at sample zero (zero "
      "centroid); flags distinguish these from a measured positive-centroid decay. "
      "Requires at least two samples. The result is amplitude/sqrt(seconds) with "
      "unnormalized sample-sum energy, and depends on window length and sampling "
      "density.");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(16384, 4);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Strong decay", Real> strongDecay;
    halp::val_port<"Silent PCM", bool> silent;
    halp::val_port<"Positive temporal centroid", bool> positiveCentroid;
  } outputs;
  StrongDecayProcessor::Settings settings() const;
  void publish(StrongDecayProcessor::Result& result);
};

struct DecreaseProcessor
{
  struct Settings
  {
    Real attackMs{10.f};
    Real releaseMs{1500.f};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Real pcmSlope{}, envelopeSlope{};
    bool silent{}, flatEnvelope{};
  };
  void prepare(Frame& input, const Settings& settings);
  void compute(Result& result);

private:
  Frame* input_{};
  Algorithm envelopeAlgorithm_, pcmDecrease_, envelopeDecrease_;
  std::vector<Real> envelope_;
  Real pcmSlope_{}, envelopeSlope_{};
};

struct Decrease : AudioNode<Decrease, DecreaseProcessor>
{
  halp_meta(name, "Decrease (PCM and envelope)");
  halp_meta(c_name, "essentia_decrease");
  halp_meta(category, "Analysis/Essentia/Envelope");
  halp_meta(author, "ossia score, Essentia / MTG");
  halp_meta(uuid, "f945077a-f81e-45a0-a105-9214db81c9cc");
  halp_meta(
      description,
      "Two native Decrease linear-regression slopes with deliberately separate domains: "
      "signed PCM and nonnegative rectified window envelope. "
      "Both use range=(N-1)/host sample rate, giving amplitude/second; rising ramps are "
      "positive and falling ramps negative. "
      "Envelope attack/release are native low-pass time constants in milliseconds and "
      "do not alter the PCM slope. "
      "Envelope starts at zero in every window, not a continuous follower. Window is "
      "samples; hop is a window divisor. "
      "Slopes characterize only this window, not necessarily an entire event. Silence "
      "and constant inputs yield native zero slopes; "
      "the flat-envelope flag applies after smoothing (constant nonzero PCM may produce "
      "a rising envelope). Requires at least two samples.");
  struct
  {
    ESSENTIA_AUDIO_INPUTS(16384, 4);
    EnvelopeAttackControl attack;
    EnvelopeReleaseControl release;
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
    halp::val_port<"Signed PCM slope (amplitude/s)", Real> pcmSlope;
    halp::val_port<"Envelope slope (amplitude/s)", Real> envelopeSlope;
    halp::val_port<"Silent PCM", bool> silent;
    halp::val_port<"Flat envelope", bool> flatEnvelope;
  } outputs;
  DecreaseProcessor::Settings settings() const;
  void publish(DecreaseProcessor::Result& result);
};
}
