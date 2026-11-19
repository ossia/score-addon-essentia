#include <Essentia/EnvelopeShape.hpp>

#include <cmath>

#include <algorithm>
#include <stdexcept>

namespace EssentiaNodes
{
namespace
{
void requireWindow(const Frame& input)
{
  if(input.audio.size() < 2)
    throw std::runtime_error("Envelope/event analysis requires at least two samples");
  if(!std::isfinite(input.sampleRate) || input.sampleRate <= 0.)
    throw std::runtime_error(
        "Envelope/event analysis requires a positive finite sample rate");
  const Real sampleRate = static_cast<Real>(input.sampleRate);
  const Real duration = static_cast<Real>((input.audio.size() - 1) / input.sampleRate);
  if(!std::isfinite(sampleRate) || sampleRate <= 0.f || !std::isfinite(duration)
     || duration <= 0.f)
    throw std::runtime_error(
        "Envelope/event sample rate or window duration exceeds native precision");
}

Algorithm makeWindowEnvelope(
    Frame& input, Real attackMs, Real releaseMs, std::vector<Real>& envelope)
{
  if(!std::isfinite(attackMs) || attackMs < 0.f || !std::isfinite(releaseMs)
     || releaseMs < 0.f)
    throw std::runtime_error(
        "Envelope attack and release must be finite nonnegative milliseconds");
  auto algorithm = makeAlgorithm("Envelope");
  algorithm->configure(
      "sampleRate", static_cast<Real>(input.sampleRate), "attackTime", attackMs,
      "releaseTime", releaseMs, "applyRectification", true);
  algorithm->input("signal").set(input.audio);
  algorithm->output("signal").set(envelope);
  return algorithm;
}

void requireFinite(Real value, const char* message)
{
  if(!std::isfinite(value))
    throw std::runtime_error(message);
}

void envelopeBounds(const std::vector<Real>& envelope, Real& minimum, Real& maximum)
{
  minimum = maximum = envelope.front();
  for(Real value : envelope)
  {
    if(!std::isfinite(value) || value < 0.f)
      throw std::runtime_error(
          "Envelope preprocessing produced a nonfinite or negative value");
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
}
}

void EnvelopeShapeProcessor::prepare(Frame& input, const Settings& settings)
{
  requireWindow(input);
  if(!std::isfinite(settings.startThreshold) || !std::isfinite(settings.stopThreshold)
     || settings.startThreshold < 0.f || settings.stopThreshold > 1.f
     || settings.startThreshold > settings.stopThreshold)
    throw std::runtime_error("Attack thresholds must satisfy 0 <= start <= stop <= 1");

  envelopeAlgorithm_
      = makeWindowEnvelope(input, settings.attackMs, settings.releaseMs, envelope_);
  derivative_ = makeAlgorithm("DerivativeSFX");
  derivative_->input("envelope").set(envelope_);
  derivative_->output("derAvAfterMax").set(releaseDerivative_);
  derivative_->output("maxDerBeforeMax").set(attackDerivative_);
  flatness_ = makeAlgorithm("FlatnessSFX");
  flatness_->input("envelope").set(envelope_);
  flatness_->output("flatness").set(flatnessValue_);
  attack_ = makeAlgorithm("LogAttackTime");
  attack_->configure(
      "sampleRate", static_cast<Real>(input.sampleRate), "startAttackThreshold",
      settings.startThreshold, "stopAttackThreshold", settings.stopThreshold);
  attack_->input("signal").set(envelope_);
  attack_->output("logAttackTime").set(logAttackTime_);
  attack_->output("attackStart").set(attackStart_);
  attack_->output("attackStop").set(attackStop_);
  maximum_ = makeAlgorithm("MaxToTotal");
  maximum_->input("envelope").set(envelope_);
  maximum_->output("maxToTotal").set(maxToTotal_);
  minimum_ = makeAlgorithm("MinToTotal");
  minimum_->input("envelope").set(envelope_);
  minimum_->output("minToTotal").set(minToTotal_);
  centroid_ = makeAlgorithm("TCToTotal");
  centroid_->input("envelope").set(envelope_);
  centroid_->output("TCToTotal").set(tcToTotal_);
}

void EnvelopeShapeProcessor::compute(Result& result)
{
  // Standard Envelope resets its low-pass state in every compute(). This is
  // intentional: overlapping windows are independent event-shape observations.
  envelopeAlgorithm_->compute();
  Real minimum{}, maximum{};
  envelopeBounds(envelope_, minimum, maximum);
  derivative_->compute();
  flatness_->compute();
  attack_->compute();
  maximum_->compute();
  minimum_->compute();
  centroid_->compute();

  for(Real value :
      {releaseDerivative_, attackDerivative_, flatnessValue_, logAttackTime_,
       attackStart_, attackStop_, maxToTotal_, minToTotal_, tcToTotal_})
    requireFinite(value, "Envelope shape produced a nonfinite descriptor");

  result.releaseDerivative = releaseDerivative_;
  result.attackDerivative = attackDerivative_;
  result.flatness = flatnessValue_;
  result.logAttackTime = logAttackTime_;
  result.attackStart = attackStart_;
  result.attackStop = attackStop_;
  result.maxToTotal = maxToTotal_;
  result.minToTotal = minToTotal_;
  result.tcToTotal = tcToTotal_;
  result.silent = maximum == 0.f;
  result.flat = minimum == maximum;
  // Mirror the native duration comparison, rather than interpreting -5 as an
  // error or replacing its well-defined short-attack/silence sentinel.
  result.attackTimeResolved = (attackStop_ - attackStart_) > 10e-5;
  result.envelope.swap(envelope_);
}

EnvelopeShapeProcessor::Settings EnvelopeShape::settings() const
{
  return {
      inputs.attack.value, inputs.release.value, inputs.startThreshold.value,
      inputs.stopThreshold.value};
}

void EnvelopeShape::publish(EnvelopeShapeProcessor::Result& result)
{
  outputs.attackDerivative.value = result.attackDerivative;
  outputs.releaseDerivative.value = result.releaseDerivative;
  outputs.flatness.value = result.flatness;
  outputs.logAttackTime.value = result.logAttackTime;
  outputs.attackStart.value = result.attackStart;
  outputs.attackStop.value = result.attackStop;
  outputs.maxToTotal.value = result.maxToTotal;
  outputs.minToTotal.value = result.minToTotal;
  outputs.tcToTotal.value = result.tcToTotal;
  outputs.silent.value = result.silent;
  outputs.flat.value = result.flat;
  outputs.attackTimeResolved.value = result.attackTimeResolved;
  outputs.envelope.value.swap(result.envelope);
}

void StrongDecayProcessor::prepare(Frame& input, const Settings&)
{
  requireWindow(input);
  input_ = &input;
  decay_ = makeAlgorithm("StrongDecay");
  decay_->configure("sampleRate", static_cast<Real>(input.sampleRate));
  decay_->input("signal").set(input.audio);
  decay_->output("strongDecay").set(value_);
}

void StrongDecayProcessor::compute(Result& result)
{
  bool silent = true;
  bool positiveCentroid = false;
  for(std::size_t i = 0; i < input_->audio.size(); ++i)
  {
    const Real sample = input_->audio[i];
    requireFinite(sample, "StrongDecay requires finite PCM samples");
    if(sample != 0.f)
    {
      silent = false;
      positiveCentroid |= i != 0;
    }
  }
  decay_->compute();
  requireFinite(value_, "StrongDecay produced a nonfinite descriptor");
  result.strongDecay = value_;
  result.silent = silent;
  result.positiveCentroid = positiveCentroid;
}

StrongDecayProcessor::Settings StrongDecay::settings() const
{
  return {};
}

void StrongDecay::publish(StrongDecayProcessor::Result& result)
{
  outputs.strongDecay.value = result.strongDecay;
  outputs.silent.value = result.silent;
  outputs.positiveCentroid.value = result.positiveCentroid;
}

void DecreaseProcessor::prepare(Frame& input, const Settings& settings)
{
  requireWindow(input);
  input_ = &input;
  envelopeAlgorithm_
      = makeWindowEnvelope(input, settings.attackMs, settings.releaseMs, envelope_);
  const Real range = static_cast<Real>((input.audio.size() - 1) / input.sampleRate);
  pcmDecrease_ = makeAlgorithm("Decrease");
  pcmDecrease_->configure("range", range);
  pcmDecrease_->input("array").set(input.audio);
  pcmDecrease_->output("decrease").set(pcmSlope_);
  envelopeDecrease_ = makeAlgorithm("Decrease");
  envelopeDecrease_->configure("range", range);
  envelopeDecrease_->input("array").set(envelope_);
  envelopeDecrease_->output("decrease").set(envelopeSlope_);
}

void DecreaseProcessor::compute(Result& result)
{
  bool silent = true;
  for(Real sample : input_->audio)
  {
    requireFinite(sample, "Decrease requires finite PCM samples");
    silent &= sample == 0.f;
  }
  envelopeAlgorithm_->compute();
  Real minimum{}, maximum{};
  envelopeBounds(envelope_, minimum, maximum);
  pcmDecrease_->compute();
  envelopeDecrease_->compute();
  requireFinite(pcmSlope_, "PCM Decrease produced a nonfinite slope");
  requireFinite(envelopeSlope_, "Envelope Decrease produced a nonfinite slope");
  result.pcmSlope = pcmSlope_;
  result.envelopeSlope = envelopeSlope_;
  result.silent = silent;
  result.flatEnvelope = minimum == maximum;
}

DecreaseProcessor::Settings Decrease::settings() const
{
  return {inputs.attack.value, inputs.release.value};
}

void Decrease::publish(DecreaseProcessor::Result& result)
{
  outputs.pcmSlope.value = result.pcmSlope;
  outputs.envelopeSlope.value = result.envelopeSlope;
  outputs.silent.value = result.silent;
  outputs.flatEnvelope.value = result.flatEnvelope;
}
}
