#include <Essentia/Statistics.hpp>

#include <cmath>

#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>

namespace EssentiaNodes
{
namespace
{
void requireSize(const Frame& frame, std::size_t minimum, const char* name)
{
  if(frame.audio.size() < minimum)
    throw std::runtime_error(
        std::string{name}
        + (minimum == 1 ? ": requires at least one sample or data value"
                        : ": requires at least two data bins"));
}

void requireRate(const Frame& frame, const char* name)
{
  if(!std::isfinite(frame.sampleRate) || frame.sampleRate <= 0.
     || frame.sampleRate > std::numeric_limits<Real>::max())
    throw std::runtime_error(
        std::string{name} + ": sample rate must be finite and positive");
}

void requireFiniteInput(const Frame& frame, const char* name)
{
  requireSize(frame, 1, name);
  for(Real value : frame.audio)
    if(!std::isfinite(value))
      throw std::runtime_error(std::string{name} + ": input values must be finite");
}

void requireFiniteResult(Real value, const char* name)
{
  if(!std::isfinite(value))
    throw std::runtime_error(std::string{name} + ": native result is nonfinite; input magnitude or parameter range exceeds numerical limits");
}

Algorithm scalarAlgorithm(
    const char* name, const char* inputName, Frame& frame, const char* outputName,
    Real& output, const essentia::ParameterMap& parameters = {})
{
  auto algorithm = makeAlgorithm(name, parameters);
  algorithm->input(inputName).set(frame.audio);
  algorithm->output(outputName).set(output);
  return algorithm;
}
}

void AudioStatisticsProcessor::prepare(Frame& frame, const Settings&)
{
  requireSize(frame, 1, "AudioStatistics");
  requireRate(frame, "AudioStatistics");
  m_frame = &frame;
  m_values = {};
  m_values.momentsValid = frame.audio.size() >= 2;
  m_mean = scalarAlgorithm("Mean", "array", frame, "mean", m_values.mean);
  m_median = scalarAlgorithm("Median", "array", frame, "median", m_values.median);
  m_variance
      = scalarAlgorithm("Variance", "array", frame, "variance", m_values.variance);

  essentia::ParameterMap minimum;
  minimum.add("type", "min");
  m_minimum
      = scalarAlgorithm("MinMax", "array", frame, "real", m_values.minimum, minimum);
  m_minimum->output("int").set(m_values.minimumIndex);
  essentia::ParameterMap maximum;
  maximum.add("type", "max");
  m_maximum
      = scalarAlgorithm("MinMax", "array", frame, "real", m_values.maximum, maximum);
  m_maximum->output("int").set(m_values.maximumIndex);

  m_energy = scalarAlgorithm("Energy", "array", frame, "energy", m_values.energy);
  m_power = scalarAlgorithm("InstantPower", "array", frame, "power", m_values.power);
  m_leq = scalarAlgorithm("Leq", "signal", frame, "leq", m_values.leq);
  essentia::ParameterMap duration;
  duration.add("sampleRate", frame.sampleRate);
  m_duration = scalarAlgorithm(
      "Duration", "signal", frame, "duration", m_values.duration, duration);

  // Native CentralMoments refuses a singleton, unlike all basic descriptors.
  // Keep basic values usable and expose this explicit secondary validity bit.
  if(m_values.momentsValid)
  {
    essentia::ParameterMap central;
    central.add("mode", "sample");
    m_central = makeAlgorithm("CentralMoments", central);
    m_central->input("array").set(frame.audio);
    m_central->output("centralMoments").set(m_moments);
    m_shape = makeAlgorithm("DistributionShape");
    m_shape->input("centralMoments").set(m_moments);
    m_shape->output("spread").set(m_spread);
    m_shape->output("skewness").set(m_values.skewness);
    m_shape->output("kurtosis").set(m_values.kurtosis);
  }
}

void AudioStatisticsProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "AudioStatistics");
  m_mean->compute();
  m_median->compute();
  m_variance->compute();
  m_minimum->compute();
  m_maximum->compute();
  m_energy->compute();
  m_power->compute();
  m_leq->compute();
  m_duration->compute();
  if(m_values.momentsValid)
  {
    m_central->compute();
    for(Real moment : m_moments)
      requireFiniteResult(moment, "AudioStatistics moments");
    m_shape->compute();
    requireFiniteResult(m_spread, "AudioStatistics spread");
  }
  for(Real value :
      {m_values.mean, m_values.median, m_values.variance, m_values.minimum,
       m_values.maximum, m_values.energy, m_values.power, m_values.leq,
       m_values.duration, m_values.skewness, m_values.kurtosis})
    requireFiniteResult(value, "AudioStatistics");
  result = m_values;
}

void AudioStatistics::publish(AudioStatisticsProcessor::Result& r)
{
  outputs.mean.value = r.mean;
  outputs.median.value = r.median;
  outputs.variance.value = r.variance;
  outputs.minimum.value = r.minimum;
  outputs.minimumIndex.value = r.minimumIndex;
  outputs.maximum.value = r.maximum;
  outputs.maximumIndex.value = r.maximumIndex;
  outputs.energy.value = r.energy;
  outputs.power.value = r.power;
  outputs.leq.value = r.leq;
  outputs.duration.value = r.duration;
  outputs.momentsValid.value = r.momentsValid;
  outputs.skewness.value = r.skewness;
  outputs.kurtosis.value = r.kurtosis;
}

void EffectiveDurationProcessor::prepare(Frame& frame, const Settings& settings)
{
  requireSize(frame, 1, "EffectiveDuration");
  requireRate(frame, "EffectiveDuration");
  if(!std::isfinite(settings.thresholdRatio) || settings.thresholdRatio < 0.f
     || settings.thresholdRatio > 1.f)
    throw std::runtime_error(
        "EffectiveDuration: peak threshold ratio must be between 0 and 1");
  m_frame = &frame;
  essentia::ParameterMap parameters;
  parameters.add("sampleRate", frame.sampleRate);
  parameters.add("thresholdRatio", settings.thresholdRatio);
  m_algorithm = scalarAlgorithm(
      "EffectiveDuration", "signal", frame, "effectiveDuration", m_duration, parameters);
}

void EffectiveDurationProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "EffectiveDuration");
  m_algorithm->compute();
  requireFiniteResult(m_duration, "EffectiveDuration");
  result.duration = m_duration;
}

void SilenceRateProcessor::prepare(Frame& frame, const Settings& settings)
{
  requireSize(frame, 1, "SilenceRate");
  if(!std::isfinite(settings.thresholdDb))
    throw std::runtime_error("SilenceRate: threshold in dB must be finite");
  m_threshold = static_cast<Real>(std::pow(10., settings.thresholdDb / 10.));
  if(!std::isfinite(m_threshold) || m_threshold <= 0.f)
    throw std::runtime_error(
        "SilenceRate: threshold must convert to finite, positive linear power");
  m_frame = &frame;
  essentia::ParameterMap parameters;
  parameters.add("thresholds", std::vector<Real>{m_threshold});
  // Outputs are dynamic: configuration must create threshold_0 before binding.
  m_algorithm = scalarAlgorithm(
      "SilenceRate", "frame", frame, "threshold_0", m_silent, parameters);
}

void SilenceRateProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "SilenceRate");
  m_algorithm->compute();
  requireFiniteResult(m_silent, "SilenceRate");
  result.silent = m_silent == 1.f;
  result.thresholdPower = m_threshold;
}

void LoudnessProcessor::prepare(Frame& frame, const Settings&)
{
  requireSize(frame, 1, "Loudness");
  m_frame = &frame;
  m_algorithm = scalarAlgorithm("Loudness", "signal", frame, "loudness", m_loudness);
}

void LoudnessProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "Loudness");
  m_algorithm->compute();
  requireFiniteResult(m_loudness, "Loudness");
  result.loudness = m_loudness;
}

void LarmProcessor::prepare(Frame& frame, const Settings& settings)
{
  requireSize(frame, 1, "Larm");
  requireRate(frame, "Larm");
  // The pinned native implementation converts sampleRate to int for Envelope.
  if(frame.sampleRate >= std::numeric_limits<int>::max()
     || frame.sampleRate != std::floor(frame.sampleRate))
    throw std::runtime_error(
        "Larm: sample rate must be a positive integer representable by the native "
        "envelope");
  if(!std::isfinite(settings.attackMs) || settings.attackMs < 0.f
     || !std::isfinite(settings.releaseMs) || settings.releaseMs < 0.f)
    throw std::runtime_error(
        "Larm: attack and release times must be finite and nonnegative");
  if(!std::isfinite(settings.power))
    throw std::runtime_error("Larm: averaging power must be finite");
  m_frame = &frame;
  essentia::ParameterMap parameters;
  parameters.add("sampleRate", frame.sampleRate);
  parameters.add("attackTime", settings.attackMs);
  parameters.add("releaseTime", settings.releaseMs);
  parameters.add("power", settings.power);
  m_algorithm = scalarAlgorithm("Larm", "signal", frame, "larm", m_loudness, parameters);
}

void LarmProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "Larm");
  m_algorithm->compute();
  requireFiniteResult(m_loudness, "Larm");
  result.loudness = m_loudness;
}

void LoudnessVickersProcessor::prepare(Frame& frame, const Settings&)
{
  requireSize(frame, 1, "LoudnessVickers");
  if(frame.sampleRate != 44100.)
    throw std::runtime_error(
        "LoudnessVickers: requires exactly 44100 Hz; no resampling is performed");
  if(frame.hopSize != static_cast<int>(frame.audio.size()))
    throw std::runtime_error(
        "LoudnessVickers: requires nonoverlapping windows (hop divisor 1)");
  m_frame = &frame;
  m_nextSample = frame.startSample;
  essentia::ParameterMap parameters;
  parameters.add("sampleRate", frame.sampleRate);
  m_algorithm = scalarAlgorithm(
      "LoudnessVickers", "signal", frame, "loudness", m_loudness, parameters);
}

void LoudnessVickersProcessor::compute(Result& result)
{
  requireFiniteInput(*m_frame, "LoudnessVickers");
  if(m_frame->startSample != m_nextSample)
    throw std::runtime_error(
        "LoudnessVickers: noncontiguous input requires a processor reset");
  m_algorithm->compute();
  requireFiniteResult(m_loudness, "LoudnessVickers");
  m_nextSample = m_frame->startSample + static_cast<std::int64_t>(m_frame->audio.size());
  result.loudness = m_loudness;
}

void DistributionShapeProcessor::prepare(Frame& frame, const Settings& settings)
{
  requireSize(frame, 2, "DistributionShape");
  if(!std::isfinite(settings.range) || settings.range <= 0.f)
    throw std::runtime_error(
        "DistributionShape: coordinate range must be finite and positive");
  m_frame = &frame;
  essentia::ParameterMap raw;
  raw.add("range", settings.range);
  m_raw = makeAlgorithm("RawMoments", raw);
  m_raw->input("array").set(frame.audio);
  m_raw->output("rawMoments").set(m_rawMoments);
  essentia::ParameterMap central;
  central.add("mode", "pdf");
  central.add("range", settings.range);
  m_central = makeAlgorithm("CentralMoments", central);
  m_central->input("array").set(frame.audio);
  m_central->output("centralMoments").set(m_centralMoments);
  m_shape = makeAlgorithm("DistributionShape");
  m_shape->input("centralMoments").set(m_centralMoments);
  m_shape->output("spread").set(m_spread);
  m_shape->output("skewness").set(m_skewness);
  m_shape->output("kurtosis").set(m_kurtosis);
}

void DistributionShapeProcessor::compute(Result& result)
{
  requireSize(*m_frame, 2, "DistributionShape");
  bool hasMass = false;
  for(Real value : m_frame->audio)
  {
    if(!std::isfinite(value) || value < 0.f)
      throw std::runtime_error(
          "DistributionShape: data must be finite nonnegative weights, not signed PCM");
    hasMass |= value > 0.f;
  }
  m_raw->compute();
  m_central->compute();
  for(Real value : m_rawMoments)
    requireFiniteResult(value, "RawMoments");
  for(Real value : m_centralMoments)
    requireFiniteResult(value, "CentralMoments");
  m_shape->compute();
  for(Real value : {m_spread, m_skewness, m_kurtosis})
    requireFiniteResult(value, "DistributionShape");
  result.rawMoments.swap(m_rawMoments);
  result.centralMoments.swap(m_centralMoments);
  result.spread = m_spread;
  result.skewness = m_skewness;
  result.kurtosis = m_kurtosis;
  result.hasMass = hasMass;
}

void DistributionShape::publish(DistributionShapeProcessor::Result& result)
{
  outputs.rawMoments.value.swap(result.rawMoments);
  outputs.centralMoments.value.swap(result.centralMoments);
  outputs.spread.value = result.spread;
  outputs.skewness.value = result.skewness;
  outputs.kurtosis.value = result.kurtosis;
  outputs.hasMass.value = result.hasMass;
}

void PowerMeanProcessor::prepare(Frame& frame, const Settings& settings)
{
  requireSize(frame, 1, "PowerMean");
  if(!std::isfinite(settings.power))
    throw std::runtime_error("PowerMean: power must be finite");
  m_frame = &frame;
  m_power = settings.power;
  essentia::ParameterMap parameters;
  parameters.add("power", settings.power);
  // Power zero is dispatched by native PowerMean to native GeometricMean.
  m_algorithm
      = scalarAlgorithm("PowerMean", "array", frame, "powerMean", m_mean, parameters);
}

void PowerMeanProcessor::compute(Result& result)
{
  requireSize(*m_frame, 1, "PowerMean");
  for(Real value : m_frame->audio)
  {
    if(!std::isfinite(value) || value < 0.f)
      throw std::runtime_error(
          "PowerMean: data must be finite and nonnegative, not signed PCM");
    if(m_power < 0.f && value == 0.f)
      throw std::runtime_error(
          "PowerMean: negative powers require strictly positive data");
  }
  m_algorithm->compute();
  requireFiniteResult(m_mean, "PowerMean");
  result.mean = m_mean;
}
}
