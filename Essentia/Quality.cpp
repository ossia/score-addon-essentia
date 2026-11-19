#include <Essentia/Quality.hpp>
#include <essentia/essentiamath.h>

#include <cmath>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace EssentiaNodes
{
namespace
{
void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error{message};
}

void finite(double value, const char* message)
{
  require(std::isfinite(value), message);
}

void frameShape(const Frame& frame, int minimum)
{
  require(
      frame.audio.size() >= static_cast<std::size_t>(minimum),
      "Analysis window is too short for this detector");
  require(
      frame.hopSize > 0 && frame.hopSize <= static_cast<int>(frame.audio.size()),
      "Hop must be positive and no larger than the analysis window");
  require(
      std::isfinite(frame.sampleRate) && frame.sampleRate > 0.,
      "Sample rate must be finite and positive");
}

void fixedFrame(const Frame& frame, int size, int hop)
{
  require(
      frame.audio.size() == static_cast<std::size_t>(size) && frame.hopSize == hop,
      "Detector frame/hop changed without preparation");
}

// Native central slices use N/2 +/- H/2; odd H leaves a sample unexamined
// between adjacent frames, so it is not a supported cadence for these nodes.
int centralStart(const Frame& frame)
{
  frameShape(frame, 4);
  require(
      frame.hopSize < static_cast<int>(frame.audio.size()) && frame.hopSize >= 2
          && frame.hopSize % 2 == 0,
      "This detector requires overlapping windows and a positive even hop");
  return static_cast<int>(frame.audio.size()) / 2 - frame.hopSize / 2;
}

void silenceThreshold(float value)
{
  require(
      std::isfinite(value) && value < 0.f,
      "Silence threshold must be finite and below 0 dB");
}

Real power(const std::vector<Real>& audio)
{
  // Match native strict power comparisons, including their float arithmetic.
  return essentia::instantPower(audio);
}

void append(std::vector<double>& destination, const std::vector<double>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

void clockLimit(std::int64_t calls, int hop, int size)
{
  // Native detectors contain signed int products as well as unsigned clocks.
  require(
      calls <= (std::numeric_limits<int>::max() - size) / hop,
      "Native detector clock exhausted; reset the analysis epoch");
}
}

void QualityIntervalState::bind(Frame& input)
{
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  calls = 0;
  origin = static_cast<double>(input.startSample) / input.sampleRate;
  starts.clear();
  ends.clear();
  algorithm->input("frame").set(input.audio);
  algorithm->output("starts").set(starts);
  algorithm->output("ends").set(ends);
}

void QualityIntervalState::computeIntervals(QualityIntervals& result)
{
  fixedFrame(*frame, frameSize, hop);
  clockLimit(calls, hop, frameSize);
  starts.clear();
  ends.clear();
  algorithm->compute();
  ++calls;
  require(starts.size() == ends.size(), "Native detector returned unpaired intervals");
  result.starts.clear();
  result.ends.clear();
  result.starts.reserve(starts.size());
  result.ends.reserve(ends.size());
  for(std::size_t i = 0; i < starts.size(); ++i)
  {
    finite(starts[i], "Native detector returned a nonfinite event start");
    finite(ends[i], "Native detector returned a nonfinite event end");
    require(starts[i] <= ends[i], "Native detector returned a reversed interval");
    result.starts.push_back(origin + starts[i]);
    result.ends.push_back(origin + ends[i]);
  }
  result.coverageStart = coverageStart;
}

void ClickProcessor::prepare(Frame& input, const Settings& settings)
{
  const int start = centralStart(input);
  const int end = static_cast<int>(input.audio.size()) / 2 + input.hopSize / 2;
  require(
      settings.order >= 1 && settings.order < static_cast<int>(input.audio.size())
          && start >= settings.order
          && end + settings.order <= static_cast<int>(input.audio.size()),
      "Click detector needs LPC history before and after the entire central hop");
  silenceThreshold(settings.silenceThreshold);
  finite(settings.threshold, "Click threshold must be finite");
  require(
      std::isfinite(settings.powerThreshold) && settings.powerThreshold > 0.f,
      "Power clipping factor must be finite and positive");
  algorithm = makeAlgorithm("ClickDetector");
  algorithm->configure(
      "sampleRate", input.sampleRate, "frameSize", static_cast<int>(input.audio.size()),
      "hopSize", input.hopSize, "order", settings.order, "detectionThreshold",
      settings.threshold, "powerEstimationThreshold", settings.powerThreshold,
      "silenceThreshold", settings.silenceThreshold);
  bind(input);
  coverageStart = origin + start / input.sampleRate;
}

void ClickProcessor::compute(Result& result)
{
  computeIntervals(result);
}

void DiscontinuityProcessor::prepare(Frame& input, const Settings& settings)
{
  const int start = centralStart(input);
  const int end = static_cast<int>(input.audio.size()) / 2 + input.hopSize / 2;
  const int analysisSize = end - start;
  require(
      settings.order >= 1 && start >= settings.order,
      "Discontinuity detector needs at least LPC-order samples before the central hop");
  require(
      settings.kernel >= 1 && settings.kernel % 2 == 1 && settings.kernel < analysisSize,
      "Median kernel must be odd, positive, and smaller than the central hop");
  require(
      settings.subframe >= 1 && settings.subframe <= analysisSize,
      "Subframe size must fit inside the central hop");
  require(
      std::isfinite(settings.threshold) && settings.threshold >= 1.f,
      "Detection factor must be finite and at least one");
  finite(settings.energyThreshold, "Energy threshold must be finite");
  silenceThreshold(settings.silenceThreshold);
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  coverageStart = (static_cast<double>(input.startSample) + start) / input.sampleRate;
  algorithm = makeAlgorithm("DiscontinuityDetector");
  algorithm->configure(
      "frameSize", frameSize, "hopSize", hop, "order", settings.order, "kernelSize",
      settings.kernel, "subFrameSize", settings.subframe, "detectionThreshold",
      settings.threshold, "energyThreshold", settings.energyThreshold,
      "silenceThreshold", settings.silenceThreshold);
  algorithm->input("frame").set(input.audio);
  algorithm->output("discontinuityLocations").set(locations);
  algorithm->output("discontinuityAmplitudes").set(amplitudes);
}

void DiscontinuityProcessor::compute(Result& result)
{
  fixedFrame(*frame, frameSize, hop);
  locations.clear();
  amplitudes.clear();
  algorithm->compute();
  require(
      locations.size() == amplitudes.size(),
      "Discontinuity locations and amplitudes do not match");
  result.times.clear();
  result.times.reserve(locations.size());
  for(std::size_t i = 0; i < locations.size(); ++i)
  {
    require(
        std::isfinite(locations[i]) && locations[i] >= 0.f && locations[i] < frameSize,
        "Discontinuity detector returned an invalid sample index");
    finite(
        amplitudes[i], "Discontinuity detector returned a nonfinite prediction error");
    result.times.push_back(
        (static_cast<double>(frame->startSample) + locations[i]) / frame->sampleRate);
  }
  result.amplitudes.swap(amplitudes);
  result.coverageStart = coverageStart;
}

void GapsProcessor::prepare(Frame& input, const Settings& settings)
{
  const int start = centralStart(input);
  const int end = static_cast<int>(input.audio.size()) / 2 + input.hopSize / 2;
  require(start >= 1, "Gap detector needs one sample before its processing slice");
  require(
      settings.kernel >= 1 && settings.kernel % 2 == 1
          && settings.kernel < static_cast<int>(input.audio.size()),
      "Gap median kernel must be odd, positive, and smaller than the window");
  finite(settings.silenceThreshold, "Silence threshold must be finite");
  finite(settings.powerThreshold, "Context power threshold must be finite");
  require(
      std::isfinite(settings.contextMs) && settings.contextMs > 0.f,
      "Gap context duration must be finite and positive");
  const double contextSamples = settings.contextMs * input.sampleRate / 1000.;
  // Keep pre/post evidence within this frame. The pinned native continuation
  // buffer otherwise appends overlapped samples as though they were contiguous.
  require(
      contextSamples >= 1. && contextSamples <= start
          && contextSamples <= static_cast<int>(input.audio.size()) - end,
      "Gap context must contain samples and fit in both overlap margins");
  require(
      std::isfinite(settings.minimumMs) && std::isfinite(settings.maximumMs)
          && settings.minimumMs > 0.f && settings.maximumMs >= settings.minimumMs,
      "Gap duration bounds must be positive, finite, and ordered");
  algorithm = makeAlgorithm("GapsDetector");
  algorithm->configure(
      "sampleRate", input.sampleRate, "frameSize", static_cast<int>(input.audio.size()),
      "hopSize", input.hopSize, "kernelSize", settings.kernel, "silenceThreshold",
      settings.silenceThreshold, "prepowerThreshold", settings.powerThreshold,
      "prepowerTime", settings.contextMs, "postpowerTime", settings.contextMs,
      "minimumTime", settings.minimumMs, "maximumTime", settings.maximumMs, "attackTime",
      0.f, "releaseTime", 0.f);
  bind(input);
  coverageStart = origin + start / input.sampleRate;
}

void GapsProcessor::compute(Result& result)
{
  computeIntervals(result);
}

void SaturationProcessor::prepare(Frame& input, const Settings& settings)
{
  const int start = centralStart(input);
  const int end = static_cast<int>(input.audio.size()) / 2 + input.hopSize / 2;
  require(
      start >= 2 && start < end && end <= static_cast<int>(input.audio.size()),
      "Saturation detector needs two samples before its complete central hop");
  require(
      std::isfinite(settings.energyThreshold) && settings.energyThreshold <= 0.f,
      "Saturation energy threshold must be finite and at most 0 dB");
  require(
      std::isfinite(settings.differentialThreshold)
          && settings.differentialThreshold >= 0.f && std::isfinite(settings.minimumMs)
          && settings.minimumMs >= 0.f,
      "Saturation difference and duration must be finite and nonnegative");
  algorithm = makeAlgorithm("SaturationDetector");
  algorithm->configure(
      "sampleRate", input.sampleRate, "frameSize", static_cast<int>(input.audio.size()),
      "hopSize", input.hopSize, "energyThreshold", settings.energyThreshold,
      "differentialThreshold", settings.differentialThreshold, "minimumDuration",
      settings.minimumMs);
  bind(input);
  coverageStart = origin + (start - 1) / input.sampleRate;
}

void SaturationProcessor::compute(Result& result)
{
  computeIntervals(result);
}

void NoiseBurstProcessor::prepare(Frame& input, const Settings& settings)
{
  frameShape(input, 3);
  require(
      static_cast<int>(input.audio.size()) - input.hopSize >= 2,
      "Noise burst detector requires at least two overlapping samples");
  require(
      std::isfinite(settings.threshold) && settings.threshold > 0.f,
      "Noise burst threshold factor must be finite and positive");
  require(
      std::isfinite(settings.alpha) && settings.alpha > 0.f && settings.alpha < 1.f,
      "Noise burst EMA alpha must be strictly between zero and one");
  silenceThreshold(settings.silenceThreshold);
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  ownedStart = (frameSize - hop) / 2;
  ownedEnd = ownedStart + hop;
  first = true;
  coverageStart = (static_cast<double>(input.startSample) + 1.) / input.sampleRate;
  algorithm = makeAlgorithm("NoiseBurstDetector");
  algorithm->configure(
      "threshold", settings.threshold, "alpha", settings.alpha, "silenceThreshold",
      settings.silenceThreshold);
  algorithm->input("frame").set(input.audio);
  algorithm->output("indexes").set(indexes);
}

void NoiseBurstProcessor::compute(Result& result)
{
  fixedFrame(*frame, frameSize, hop);
  indexes.clear();
  algorithm->compute();
  result.times.clear();
  result.times.reserve(indexes.size());
  const int begin = first ? 1 : ownedStart;
  for(Real index : indexes)
  {
    require(
        std::isfinite(index) && index >= 0.f && index < frameSize - 2,
        "Noise burst detector returned an invalid derivative index");
    // dd[i] = x[i+2] - 2*x[i+1] + x[i], centered on audio sample i+1.
    const int center = static_cast<int>(index) + 1;
    if(center >= begin && center < ownedEnd)
      result.times.push_back(
          (static_cast<double>(frame->startSample) + center) / frame->sampleRate);
  }
  first = false;
  result.coverageStart = coverageStart;
  result.coverageEnd
      = (static_cast<double>(frame->startSample) + ownedEnd) / frame->sampleRate;
}

void SNRProcessor::prepare(Frame& input, const Settings& settings)
{
  frameShape(input, 4);
  require(input.audio.size() % 2 == 0, "SNR requires an even FFT window size");
  require(
      std::isfinite(settings.noiseThreshold) && settings.noiseThreshold <= 0.f,
      "Noise threshold must be finite and at most 0 dB");
  require(
      settings.minimumNoiseFrames >= 15, "SNR requires at least 15 noise-only frames");
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  noiseFrames = 0;
  minimumNoiseFrames = settings.minimumNoiseFrames;
  noisePower = essentia::db2pow(settings.noiseThreshold);
  algorithm = makeAlgorithm("SNR");
  algorithm->configure(
      "frameSize", frameSize, "sampleRate", input.sampleRate, "noiseThreshold",
      settings.noiseThreshold, "MMSEAlpha", 0.98f, "MAAlpha", 0.95f, "NoiseAlpha", 0.9f,
      "useBroadbadNoiseCorrection", true);
  algorithm->input("frame").set(input.audio);
  algorithm->output("instantSNR").set(instant);
  algorithm->output("averagedSNR").set(averaged);
  algorithm->output("spectralSNR").set(spectral);
}

void SNRProcessor::compute(Result& result)
{
  fixedFrame(*frame, frameSize, hop);
  const Real currentPower = power(frame->audio);
  finite(currentPower, "SNR input power is nonfinite");
  const bool noise = currentPower < noisePower;
  // Hann endpoints have zero weight: an endpoint-only impulse does not
  // supply any noise PSD evidence, even though its raw frame power is nonzero.
  const bool noiseEvidence
      = noise && currentPower > 0.f
        && std::any_of(frame->audio.begin() + 1, frame->audio.end() - 1, [](Real value) {
    return value != 0.f;
  });
  if(noiseEvidence && noiseFrames < std::numeric_limits<int>::max())
    ++noiseFrames;
  // The pinned dependency patch initializes averaging and guards zero PSD
  // denominators. Exact silence is a real native input, not fabricated noise.
  spectral.clear();
  algorithm->compute();
  result.instant.reset();
  result.averaged.reset();
  result.spectral.clear();
  result.noiseFrames = noiseFrames;
  result.learning = noise;
  result.learned = noiseFrames >= minimumNoiseFrames;
  result.ready = result.learned && !noise;
  if(!result.ready)
    return;
  finite(instant, "SNR estimator returned a nonfinite signal estimate");
  finite(averaged, "SNR estimator returned a nonfinite averaged estimate");
  for(Real value : spectral)
    require(
        std::isfinite(value) && value >= 0.f,
        "SNR estimator returned an invalid spectral estimate");
  result.instant = instant;
  result.averaged = averaged;
  result.spectral.swap(spectral);
}

void FalseStereoProcessor::prepare(Frame& input, const Settings& settings)
{
  frameShape(input, 2);
  require(
      input.left.size() == input.audio.size()
          && input.right.size() == input.audio.size(),
      "False stereo detection requires two actual input channels");
  silenceThreshold(settings.silenceThreshold);
  require(
      std::isfinite(settings.correlationThreshold)
          && settings.correlationThreshold >= -1.f
          && settings.correlationThreshold <= 1.f,
      "Correlation threshold must be between -1 and 1");
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  silencePower = essentia::db2pow(settings.silenceThreshold);
  stereoFrame.resize(input.audio.size());
  algorithm = makeAlgorithm("FalseStereoDetector");
  algorithm->configure(
      "silenceThreshold", settings.silenceThreshold, "correlationThreshold",
      settings.correlationThreshold);
  algorithm->input("frame").set(stereoFrame);
  algorithm->output("isFalseStereo").set(falseStereo);
  algorithm->output("correlation").set(correlation);
}

void FalseStereoProcessor::compute(Result& result)
{
  fixedFrame(*frame, frameSize, hop);
  require(
      frame->left.size() == stereoFrame.size()
          && frame->right.size() == stereoFrame.size(),
      "False stereo detection requires two full input channels");
  result = {};
  const Real leftPower = power(frame->left);
  const Real rightPower = power(frame->right);
  finite(leftPower, "Left channel power is nonfinite");
  finite(rightPower, "Right channel power is nonfinite");
  result.silent = leftPower < silencePower && rightPower < silencePower;
  const bool leftConstant
      = std::all_of(frame->left.begin(), frame->left.end(), [&](Real value) {
    return value == frame->left.front();
  });
  const bool rightConstant
      = std::all_of(frame->right.begin(), frame->right.end(), [&](Real value) {
    return value == frame->right.front();
  });
  const Real leftDeviation = essentia::stddev(frame->left, essentia::mean(frame->left));
  const Real rightDeviation
      = essentia::stddev(frame->right, essentia::mean(frame->right));
  finite(leftDeviation, "Left channel variance is nonfinite");
  finite(rightDeviation, "Right channel variance is nonfinite");
  result.degenerate
      = leftConstant || rightConstant || leftDeviation * rightDeviation == 0.f;
  // Pearson correlation is undefined when either variance is zero. Return an
  // explicitly absent statistic rather than claiming zero correlation or mono.
  if(result.silent || result.degenerate)
    return;
  for(std::size_t i = 0; i < stereoFrame.size(); ++i)
    stereoFrame[i] = essentia::StereoSample{frame->left[i], frame->right[i]};
  falseStereo = 0;
  correlation = 0.f;
  algorithm->compute();
  finite(correlation, "False stereo detector returned nonfinite correlation");
  result.correlation = correlation;
  result.falseStereo = falseStereo != 0;
  result.ready = true;
}

void StartStopSilenceProcessor::prepare(Frame& input, const Settings& settings)
{
  frameShape(input, 1);
  require(
      std::isfinite(settings.threshold) && settings.threshold <= 0.f,
      "Silence threshold must be finite and at most 0 dB");
  frame = &input;
  frameSize = static_cast<int>(input.audio.size());
  hop = input.hopSize;
  calls = 0;
  heardSound = false;
  origin = static_cast<double>(input.startSample) / input.sampleRate;
  silencePower = essentia::db2pow(settings.threshold);
  algorithm = makeAlgorithm("StartStopSilence");
  algorithm->configure("threshold", settings.threshold);
  algorithm->input("frame").set(input.audio);
  algorithm->output("startFrame").set(startFrame);
  algorithm->output("stopFrame").set(stopFrame);
}

void StartStopSilenceProcessor::compute(Result& result)
{
  fixedFrame(*frame, frameSize, hop);
  require(
      calls < std::numeric_limits<int>::max(),
      "Silence frame counter exhausted; reset the analysis epoch");
  const Real currentPower = power(frame->audio);
  finite(currentPower, "Silence detector input power is nonfinite");
  result.silent = currentPower < silencePower;
  algorithm->compute();
  ++calls;
  heardSound = heardSound || !result.silent;
  result.heardSound = heardSound;
  result.start.reset();
  result.candidateEnd.reset();
  result.endPending = true;
  if(heardSound)
  {
    require(
        startFrame >= 0 && stopFrame >= startFrame && stopFrame < calls,
        "Silence detector returned invalid frame indices");
    result.start = origin + static_cast<double>(startFrame) * hop / frame->sampleRate;
    if(result.silent)
    {
      result.candidateEnd
          = origin
            + (static_cast<double>(stopFrame) * hop + frameSize) / frame->sampleRate;
      result.endPending = false;
    }
  }
}

ClickProcessor::Settings ClickDetector::settings() const
{
  return {
      inputs.order.value, inputs.threshold.value, inputs.powerThreshold.value,
      inputs.silenceThreshold.value};
}

void ClickDetector::begin_tick()
{
  outputs.starts.value.clear();
  outputs.ends.value.clear();
}

void ClickDetector::invalidate()
{
  begin_tick();
  outputs.coverageStart.value.reset();
}

void ClickDetector::publish(ClickProcessor::Result& result)
{
  append(outputs.starts.value, result.starts);
  append(outputs.ends.value, result.ends);
  outputs.coverageStart.value = result.coverageStart;
}

DiscontinuityProcessor::Settings DiscontinuityDetector::settings() const
{
  return {inputs.order.value,           inputs.kernel.value,
          inputs.subframe.value,        inputs.threshold.value,
          inputs.energyThreshold.value, inputs.silenceThreshold.value};
}

void DiscontinuityDetector::begin_tick()
{
  outputs.times.value.clear();
  outputs.amplitudes.value.clear();
}

void DiscontinuityDetector::invalidate()
{
  begin_tick();
  outputs.coverageStart.value.reset();
}

void DiscontinuityDetector::publish(DiscontinuityProcessor::Result& result)
{
  append(outputs.times.value, result.times);
  outputs.amplitudes.value.insert(
      outputs.amplitudes.value.end(), result.amplitudes.begin(),
      result.amplitudes.end());
  outputs.coverageStart.value = result.coverageStart;
}

GapsProcessor::Settings GapsDetector::settings() const
{
  return {inputs.kernel.value,         inputs.silenceThreshold.value,
          inputs.powerThreshold.value, inputs.contextMs.value,
          inputs.minimumMs.value,      inputs.maximumMs.value};
}

void GapsDetector::begin_tick()
{
  outputs.starts.value.clear();
  outputs.ends.value.clear();
}

void GapsDetector::invalidate()
{
  begin_tick();
  outputs.coverageStart.value.reset();
}

void GapsDetector::publish(GapsProcessor::Result& result)
{
  append(outputs.starts.value, result.starts);
  append(outputs.ends.value, result.ends);
  outputs.coverageStart.value = result.coverageStart;
}

SaturationProcessor::Settings SaturationDetector::settings() const
{
  return {
      inputs.energyThreshold.value, inputs.differentialThreshold.value,
      inputs.minimumMs.value};
}

void SaturationDetector::begin_tick()
{
  outputs.starts.value.clear();
  outputs.ends.value.clear();
}

void SaturationDetector::invalidate()
{
  begin_tick();
  outputs.coverageStart.value.reset();
}

void SaturationDetector::publish(SaturationProcessor::Result& result)
{
  append(outputs.starts.value, result.starts);
  append(outputs.ends.value, result.ends);
  outputs.coverageStart.value = result.coverageStart;
}

NoiseBurstProcessor::Settings NoiseBurstDetector::settings() const
{
  return {inputs.threshold.value, inputs.silenceThreshold.value, inputs.alpha.value};
}

void NoiseBurstDetector::begin_tick()
{
  outputs.times.value.clear();
}

void NoiseBurstDetector::invalidate()
{
  begin_tick();
  outputs.coverageStart.value.reset();
  outputs.coverageEnd.value.reset();
}

void NoiseBurstDetector::publish(NoiseBurstProcessor::Result& result)
{
  append(outputs.times.value, result.times);
  outputs.coverageStart.value = result.coverageStart;
  outputs.coverageEnd.value = result.coverageEnd;
}

SNRProcessor::Settings SNR::settings() const
{
  return {inputs.noiseThreshold.value, inputs.minimumNoiseFrames.value};
}

void SNR::invalidate()
{
  outputs.ready.value = false;
  outputs.learned.value = false;
  outputs.learning.value = false;
  outputs.noiseFrames.value = 0;
  outputs.instant.value.reset();
  outputs.averaged.value.reset();
  outputs.spectral.value.clear();
}

void SNR::publish(SNRProcessor::Result& result)
{
  outputs.ready.value = result.ready;
  outputs.learned.value = result.learned;
  outputs.learning.value = result.learning;
  outputs.noiseFrames.value = result.noiseFrames;
  outputs.instant.value = result.instant;
  outputs.averaged.value = result.averaged;
  outputs.spectral.value.swap(result.spectral);
}

FalseStereoProcessor::Settings FalseStereoDetector::settings() const
{
  return {inputs.silenceThreshold.value, inputs.correlationThreshold.value};
}

void FalseStereoDetector::invalidate()
{
  outputs.ready.value = false;
  outputs.falseStereo.value = false;
  outputs.correlation.value.reset();
  outputs.silent.value = false;
  outputs.degenerate.value = false;
}

void FalseStereoDetector::publish(FalseStereoProcessor::Result& result)
{
  outputs.ready.value = result.ready;
  outputs.falseStereo.value = result.falseStereo;
  outputs.correlation.value = result.correlation;
  outputs.silent.value = result.silent;
  outputs.degenerate.value = result.degenerate;
}

StartStopSilenceProcessor::Settings StartStopSilence::settings() const
{
  return {inputs.threshold.value};
}

void StartStopSilence::invalidate()
{
  outputs.heardSound.value = false;
  outputs.silent.value = false;
  outputs.endPending.value = true;
  outputs.start.value.reset();
  outputs.candidateEnd.value.reset();
}

void StartStopSilence::publish(StartStopSilenceProcessor::Result& result)
{
  outputs.heardSound.value = result.heardSound;
  outputs.silent.value = result.silent;
  outputs.endPending.value = result.endPending;
  outputs.start.value = result.start;
  outputs.candidateEnd.value = result.candidateEnd;
}
}
