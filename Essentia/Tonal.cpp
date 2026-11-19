#include <Essentia/Tonal.hpp>

#include <cmath>

#include <algorithm>
#include <limits>
#include <numeric>
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

void finite(Real value, const char* message)
{
  require(std::isfinite(value), message);
}

void finite(const std::vector<Real>& values, const char* message)
{
  for(const auto value : values)
    finite(value, message);
}

bool audible(const Frame& frame, Real threshold)
{
  double energy{};
  for(const auto sample : frame.audio)
    energy += double(sample) * sample;
  return energy > double(threshold) * threshold * frame.audio.size();
}

Real upperFrequency(const Frame& frame, Real minimum, Real maximum)
{
  finite(minimum, "Minimum frequency must be finite");
  finite(maximum, "Maximum frequency must be finite");
  require(
      minimum > 0.f && maximum >= 0.f,
      "Frequency bounds must be positive (maximum 0 selects Nyquist)");
  const Real upper = maximum == 0.f ? Real(frame.sampleRate / 2.) : maximum;
  require(
      upper > minimum && upper <= frame.sampleRate / 2.,
      "Frequency interval must be ordered and within Nyquist");
  return upper;
}

void validateThreshold(Real threshold)
{
  require(
      std::isfinite(threshold) && threshold >= 0.f,
      "Silence RMS threshold must be finite and nonnegative");
}
}

void TonalProfile::prepare(
    Frame& frame, int size, Real reference, int harmonics, Real minimum, Real maximum)
{
  require(
      size >= 12 && size <= 240 && size % 12 == 0,
      "HPCP size must be a multiple of 12 between 12 and 240");
  require(
      std::isfinite(reference) && reference > 0.f,
      "HPCP reference frequency must be positive and finite");
  require(
      harmonics >= 0 && harmonics <= 32,
      "HPCP harmonic contributions must be between 0 and 32");
  const Real upper = upperFrequency(frame, minimum, maximum);
  require(upper - minimum >= 200.f, "HPCP frequency interval must span at least 200 Hz");
  peaks.prepare(frame, minimum, upper, 1e-6f, 1000);
  essentia::ParameterMap parameters;
  parameters.add("size", size);
  parameters.add("referenceFrequency", reference);
  parameters.add("harmonics", harmonics);
  parameters.add("bandPreset", false);
  parameters.add("minFrequency", minimum);
  parameters.add("maxFrequency", upper);
  parameters.add("sampleRate", Real(frame.sampleRate));
  parameters.add("weightType", "squaredCosine");
  parameters.add("normalized", "unitMax");
  parameters.add("maxShifted", false);
  parameters.add("nonLinear", false);
  parameters.add("windowSize", 1.f);
  hpcp = makeAlgorithm("HPCP", parameters);
  hpcp->input("frequencies").set(peaks.frequencies);
  hpcp->input("magnitudes").set(peaks.magnitudes);
  hpcp->output("hpcp").set(profile);
}

void TonalProfile::compute()
{
  peaks.compute();
  hpcp->compute();
  finite(profile, "HPCP produced nonfinite chroma");
}

void ChromaProcessor::prepare(Frame& frame, const Settings& settings)
{
  require(
      settings.binsPerSemitone >= 1 && settings.binsPerSemitone <= 20,
      "HPCP requires 1 to 20 bins per semitone");
  tonal.prepare(
      frame, 12 * settings.binsPerSemitone, settings.reference, settings.harmonics,
      settings.minimum, settings.maximum);
  highResolutionEnabled = settings.binsPerSemitone >= 10;
  if(highResolutionEnabled)
  {
    highResolution = makeAlgorithm("HighResolutionFeatures");
    highResolution->input("hpcp").set(tonal.profile);
    highResolution->output("equalTemperedDeviation").set(deviation);
    highResolution->output("nonTemperedEnergyRatio").set(nonTemperedEnergy);
    highResolution->output("nonTemperedPeaksEnergyRatio").set(nonTemperedPeaksEnergy);
  }
}

void ChromaProcessor::compute(Result& result)
{
  tonal.compute();
  result.highResolutionValid
      = highResolutionEnabled
        && std::any_of(
            tonal.profile.begin(), tonal.profile.end(), [](Real x) { return x > 0.f; });
  result.deviation = result.nonTemperedEnergy = result.nonTemperedPeaksEnergy = 0.f;
  if(result.highResolutionValid)
  {
    highResolution->compute();
    finite(deviation, "HighResolutionFeatures produced nonfinite deviation");
    finite(nonTemperedEnergy, "HighResolutionFeatures produced nonfinite energy ratio");
    finite(
        nonTemperedPeaksEnergy, "HighResolutionFeatures produced nonfinite peak ratio");
    result.deviation = deviation;
    result.nonTemperedEnergy = nonTemperedEnergy;
    result.nonTemperedPeaksEnergy = nonTemperedPeaksEnergy;
  }
  result.chroma.swap(tonal.profile);
}

void KeyProcessor::prepare(Frame& input, const Settings& settings)
{
  validateThreshold(settings.rmsThreshold);
  require(
      std::isfinite(settings.minimumStrength) && settings.minimumStrength >= 0.f
          && settings.minimumStrength <= 1.f,
      "Minimum key strength must be in [0,1]");
  frame = &input;
  configuration = settings;
  tonal.prepare(input, 36, settings.reference, 4, settings.minimum, settings.maximum);
  essentia::ParameterMap parameters;
  parameters.add("pcpSize", 36);
  parameters.add("profileType", "bgate");
  parameters.add("usePolyphony", true);
  parameters.add("useThreeChords", true);
  parameters.add("useMajMin", false);
  keyAlgorithm = makeAlgorithm("Key", parameters);
  keyAlgorithm->input("pcp").set(tonal.profile);
  keyAlgorithm->output("key").set(key);
  keyAlgorithm->output("scale").set(scale);
  keyAlgorithm->output("strength").set(strength);
  keyAlgorithm->output("firstToSecondRelativeStrength").set(relativeStrength);
}

void KeyProcessor::compute(Result& result)
{
  result.hasKey = false;
  result.key.clear();
  result.scale.clear();
  result.strength = result.relativeStrength = 0.f;
  tonal.compute();
  const double mean = std::accumulate(tonal.profile.begin(), tonal.profile.end(), 0.)
                      / tonal.profile.size();
  double variance{};
  for(const auto value : tonal.profile)
    variance += (value - mean) * (value - mean);
  // Flat/empty chroma has no defined correlation. Native Key otherwise divides
  // the first/second strength difference by a possibly zero best correlation.
  if(audible(*frame, configuration.rmsThreshold) && variance > 1e-12)
  {
    keyAlgorithm->compute();
    if(std::isfinite(strength) && std::isfinite(relativeStrength) && strength > 0.f
       && strength >= configuration.minimumStrength)
    {
      result.hasKey = true;
      result.key = key;
      result.scale = scale;
      result.strength = strength;
      result.relativeStrength = relativeStrength;
    }
  }
  result.chroma.swap(tonal.profile);
}

void TuningProcessor::prepare(Frame& input, const Settings& settings)
{
  validateThreshold(settings.rmsThreshold);
  frame = &input;
  configuration = settings;
  const Real upper = upperFrequency(input, settings.minimum, settings.maximum);
  peaks.prepare(input, settings.minimum, upper, 1e-6f, 1000);
  // Arbitrary native resolutions can round outside the histogram. A fixed
  // 1-cent resolution divides 100 exactly and preserves stable output meaning.
  essentia::ParameterMap parameters;
  parameters.add("resolution", 1.f);
  tuning = makeAlgorithm("TuningFrequency", parameters);
  tuning->input("frequencies").set(peaks.frequencies);
  tuning->input("magnitudes").set(peaks.magnitudes);
  tuning->output("tuningFrequency").set(frequency);
  tuning->output("tuningCents").set(cents);
  contributingFrames = 0;
  frequency = cents = 0.f;
}

void TuningProcessor::compute(Result& result)
{
  peaks.compute();
  result.frameContributed
      = audible(*frame, configuration.rmsThreshold) && !peaks.frequencies.empty();
  if(result.frameContributed)
  {
    tuning->compute();
    finite(frequency, "TuningFrequency produced nonfinite frequency");
    finite(cents, "TuningFrequency produced nonfinite cents");
    if(contributingFrames < std::numeric_limits<std::int64_t>::max())
      ++contributingFrames;
  }
  result.hasEstimate = contributingFrames > 0;
  result.frequency = result.hasEstimate ? frequency : 0.f;
  result.cents = result.hasEstimate ? cents : 0.f;
  result.contributingFrames = contributingFrames;
}

void SalienceProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      settings.harmonics >= 1 && settings.harmonics <= 100,
      "Pitch salience requires 1 to 100 harmonics");
  require(
      std::isfinite(settings.harmonicWeight) && settings.harmonicWeight >= 0.f
          && settings.harmonicWeight <= 1.f,
      "Salience harmonic decay must be in [0,1]");
  require(
      std::isfinite(settings.magnitudeThreshold) && settings.magnitudeThreshold >= 0.f,
      "Salience magnitude threshold must be finite and nonnegative");
  require(
      std::isfinite(settings.compression) && settings.compression > 0.f
          && settings.compression <= 1.f,
      "Salience magnitude exponent must be in (0,1]");
  require(input.sampleRate / 2. > 55., "Pitch salience needs Nyquist above 55 Hz");
  peaks.prepare(input, 20.f, 0.f, 1e-6f, 1000);
  essentia::ParameterMap parameters;
  parameters.add("binResolution", 10.f);
  parameters.add("referenceFrequency", 55.f);
  parameters.add("numberHarmonics", settings.harmonics);
  parameters.add("harmonicWeight", settings.harmonicWeight);
  parameters.add("magnitudeThreshold", settings.magnitudeThreshold);
  parameters.add("magnitudeCompression", settings.compression);
  salienceAlgorithm = makeAlgorithm("PitchSalienceFunction", parameters);
  salienceAlgorithm->input("frequencies").set(peaks.frequencies);
  salienceAlgorithm->input("magnitudes").set(peaks.magnitudes);
  salienceAlgorithm->output("salienceFunction").set(salience);
  parameters.clear();
  parameters.add("binResolution", 10.f);
  parameters.add("referenceFrequency", 55.f);
  parameters.add("minFrequency", 55.f);
  parameters.add(
      "maxFrequency",
      Real(std::min(input.sampleRate / 2., 55. * std::exp2(599. / 120.))));
  peakAlgorithm = makeAlgorithm("PitchSalienceFunctionPeaks", parameters);
  peakAlgorithm->input("salienceFunction").set(salience);
  peakAlgorithm->output("salienceBins").set(peakBins);
  peakAlgorithm->output("salienceValues").set(peakValues);
}

void SalienceProcessor::compute(Result& result)
{
  peaks.compute();
  salienceAlgorithm->compute();
  finite(salience, "PitchSalienceFunction produced nonfinite salience");
  peakAlgorithm->compute();
  require(
      peakBins.size() == peakValues.size(),
      "Salience peak bins and values have different sizes");
  finite(peakBins, "PitchSalienceFunctionPeaks produced nonfinite bins");
  finite(peakValues, "PitchSalienceFunctionPeaks produced nonfinite values");
  result.peakFrequencies.resize(peakBins.size());
  for(std::size_t i = 0; i < peakBins.size(); ++i)
    result.peakFrequencies[i] = 55.f * std::exp2(peakBins[i] / 120.f);
  result.salience.swap(salience);
  result.peakBins.swap(peakBins);
  result.peakValues.swap(peakValues);
}

void DissonanceProcessor::prepare(Frame& input, const Settings& settings)
{
  require(
      settings.maximumPeaks >= 1 && settings.maximumPeaks <= 1000,
      "Dissonance requires a peak limit between 1 and 1000");
  const Real upper = upperFrequency(input, settings.minimum, settings.maximum);
  peaks.prepare(
      input, settings.minimum, upper, settings.magnitudeThreshold,
      settings.maximumPeaks);
  dissonanceAlgorithm = makeAlgorithm("Dissonance");
  dissonanceAlgorithm->input("frequencies").set(peaks.frequencies);
  dissonanceAlgorithm->input("magnitudes").set(peaks.magnitudes);
  dissonanceAlgorithm->output("dissonance").set(dissonance);
}

void DissonanceProcessor::compute(Result& result)
{
  peaks.compute();
  // Native Dissonance defines empty, zero-loudness and single-peak inputs as
  // zero. Do not gate on voicing or square/rescale the native peak magnitudes.
  dissonanceAlgorithm->compute();
  finite(dissonance, "Dissonance produced nonfinite roughness");
  result.dissonance = dissonance;
  result.frequencies.swap(peaks.frequencies);
  result.magnitudes.swap(peaks.magnitudes);
}

void HarmonicProcessor::prepare(Frame& input, const Settings& settings)
{
  validateThreshold(settings.rmsThreshold);
  require(
      std::isfinite(settings.confidenceThreshold) && settings.confidenceThreshold >= 0.f
          && settings.confidenceThreshold <= 1.f,
      "Voicing confidence threshold must be in [0,1]");
  require(
      std::isfinite(settings.tolerance) && settings.tolerance > 0.f
          && settings.tolerance < 0.5f,
      "Harmonic tolerance must be in (0,0.5)");
  require(
      settings.harmonics >= 5 && settings.harmonics <= 100,
      "Harmonic features require 5 to 100 harmonic slots");
  require(settings.maximumPitch > 0.f, "Maximum F0 must be positive");
  upperFrequency(input, settings.minimumPitch, settings.maximumPitch);
  require(
      input.sampleRate / settings.minimumPitch < std::numeric_limits<int>::max() - 1.,
      "Minimum F0 produces an unrepresentable native pitch lag");
  frame = &input;
  configuration = settings;
  peaks.prepare(input, 20.f, 0.f, 1e-6f, 1000);
  require(
      peaks.spectral.fftSize >= 8,
      "Harmonic features require at least eight FFT samples");
  const int tauMin = std::min(
      int(std::floor(input.sampleRate / settings.maximumPitch)),
      peaks.spectral.fftSize / 2);
  const int tauMax = std::min(
      int(std::ceil(input.sampleRate / settings.minimumPitch)),
      peaks.spectral.fftSize / 2);
  require(tauMax > tauMin, "F0 range cannot be resolved by this analysis window");
  // The pinned dependency patches YinFFT's shared weight-mask race and its
  // out-of-range interpolation above 25.1 kHz; do not use an unpatched library.
  essentia::ParameterMap parameters;
  parameters.add("frameSize", peaks.spectral.fftSize);
  parameters.add("sampleRate", Real(input.sampleRate));
  parameters.add("minFrequency", settings.minimumPitch);
  parameters.add("maxFrequency", settings.maximumPitch);
  parameters.add("weighting", "custom");
  yin = makeAlgorithm("PitchYinFFT", parameters);
  yin->input("spectrum").set(peaks.spectral.spectrum);
  yin->output("pitch").set(pitch);
  yin->output("pitchConfidence").set(confidence);
  parameters.clear();
  parameters.add("maxHarmonics", settings.harmonics);
  parameters.add("tolerance", settings.tolerance);
  harmonicPeaks = makeAlgorithm("HarmonicPeaks", parameters);
  harmonicPeaks->input("frequencies").set(peaks.frequencies);
  harmonicPeaks->input("magnitudes").set(peaks.magnitudes);
  harmonicPeaks->input("pitch").set(pitch);
  harmonicPeaks->output("harmonicFrequencies").set(frequencies);
  harmonicPeaks->output("harmonicMagnitudes").set(magnitudes);
  inharmonicityAlgorithm = makeAlgorithm("Inharmonicity");
  inharmonicityAlgorithm->input("frequencies").set(frequencies);
  inharmonicityAlgorithm->input("magnitudes").set(magnitudes);
  inharmonicityAlgorithm->output("inharmonicity").set(inharmonicity);
  tristimulusAlgorithm = makeAlgorithm("Tristimulus");
  tristimulusAlgorithm->input("frequencies").set(frequencies);
  tristimulusAlgorithm->input("magnitudes").set(magnitudes);
  tristimulusAlgorithm->output("tristimulus").set(tristimulus);
}

void HarmonicProcessor::compute(Result& result)
{
  result.voiced = result.ratioDefined = false;
  result.pitch = result.confidence = result.inharmonicity = 0.f;
  result.oddEnergy = result.evenEnergy = result.oddToEvenRatio = 0.;
  result.frequencies.clear();
  result.magnitudes.clear();
  result.tristimulus.clear();
  if(!audible(*frame, configuration.rmsThreshold))
    return;
  peaks.compute();
  if(peaks.frequencies.empty())
    return;
  yin->compute();
  finite(pitch, "PitchYinFFT produced nonfinite F0");
  finite(confidence, "PitchYinFFT produced nonfinite confidence");
  result.confidence = confidence;
  if(confidence <= 0.f || confidence < configuration.confidenceThreshold
     || pitch < configuration.minimumPitch || pitch > configuration.maximumPitch)
    return;
  harmonicPeaks->compute();
  finite(frequencies, "HarmonicPeaks produced nonfinite frequencies");
  finite(magnitudes, "HarmonicPeaks produced nonfinite magnitudes");
  require(
      frequencies.size() == magnitudes.size(),
      "Harmonic frequency and magnitude vectors have different sizes");
  double odd{}, even{};
  // HarmonicPeaks always reserves every slot, even for absent harmonics.
  // Unlike native OddToEvenHarmonicEnergyRatio, slot zero is ODD (h=1).
  for(std::size_t i = 0; i < magnitudes.size(); ++i)
  {
    const double energy = double(magnitudes[i]) * magnitudes[i];
    if(i % 2 == 0)
      odd += energy;
    else
      even += energy;
  }
  if(odd + even <= 0.)
    return;
  inharmonicityAlgorithm->compute();
  tristimulusAlgorithm->compute();
  finite(inharmonicity, "Inharmonicity produced a nonfinite result");
  finite(tristimulus, "Tristimulus produced nonfinite results");
  result.voiced = true;
  result.pitch = pitch;
  result.inharmonicity = inharmonicity;
  result.oddEnergy = odd;
  result.evenEnergy = even;
  result.ratioDefined = even > 0.;
  result.oddToEvenRatio = result.ratioDefined ? odd / even : 0.;
  result.frequencies.swap(frequencies);
  result.magnitudes.swap(magnitudes);
  result.tristimulus.swap(tristimulus);
}

ChromaProcessor::Settings Chroma::settings() const
{
  return {
      inputs.bins.value, inputs.reference.value, inputs.harmonics.value,
      inputs.minimum.value, inputs.maximum.value};
}

void Chroma::publish(ChromaProcessor::Result& result)
{
  outputs.chroma.value.swap(result.chroma);
  outputs.highResolutionValid.value = result.highResolutionValid;
  outputs.deviation.value = result.deviation;
  outputs.nonTemperedEnergy.value = result.nonTemperedEnergy;
  outputs.nonTemperedPeaksEnergy.value = result.nonTemperedPeaksEnergy;
}

KeyProcessor::Settings InstantKey::settings() const
{
  return {
      inputs.reference.value, inputs.minimum.value, inputs.maximum.value,
      inputs.rmsThreshold.value, inputs.minimumStrength.value};
}

void InstantKey::publish(KeyProcessor::Result& result)
{
  outputs.hasKey.value = result.hasKey;
  outputs.key.value.swap(result.key);
  outputs.scale.value.swap(result.scale);
  outputs.strength.value = result.strength;
  outputs.relativeStrength.value = result.relativeStrength;
  outputs.chroma.value.swap(result.chroma);
}

TuningProcessor::Settings TuningFrequency::settings() const
{
  return {inputs.minimum.value, inputs.maximum.value, inputs.rmsThreshold.value};
}

void TuningFrequency::publish(TuningProcessor::Result& result)
{
  outputs.hasEstimate.value = result.hasEstimate;
  outputs.frameContributed.value = result.frameContributed;
  outputs.frequency.value = result.frequency;
  outputs.cents.value = result.cents;
  outputs.contributingFrames.value = result.contributingFrames;
}

SalienceProcessor::Settings PitchSalienceFunction::settings() const
{
  return {
      inputs.harmonics.value, inputs.harmonicWeight.value,
      inputs.magnitudeThreshold.value, inputs.compression.value};
}

void PitchSalienceFunction::publish(SalienceProcessor::Result& result)
{
  outputs.salience.value.swap(result.salience);
  outputs.peakBins.value.swap(result.peakBins);
  outputs.peakValues.value.swap(result.peakValues);
  outputs.peakFrequencies.value.swap(result.peakFrequencies);
}

DissonanceProcessor::Settings Dissonance::settings() const
{
  return {
      inputs.minimum.value, inputs.maximum.value, inputs.magnitudeThreshold.value,
      inputs.maximumPeaks.value};
}

void Dissonance::publish(DissonanceProcessor::Result& result)
{
  outputs.dissonance.value = result.dissonance;
  outputs.frequencies.value.swap(result.frequencies);
  outputs.magnitudes.value.swap(result.magnitudes);
}

HarmonicProcessor::Settings HarmonicFeatures::settings() const
{
  return {inputs.minimumPitch.value,        inputs.maximumPitch.value,
          inputs.confidenceThreshold.value, inputs.rmsThreshold.value,
          inputs.tolerance.value,           inputs.harmonics.value};
}

void HarmonicFeatures::publish(HarmonicProcessor::Result& result)
{
  outputs.voiced.value = result.voiced;
  outputs.pitch.value = result.pitch;
  outputs.confidence.value = result.confidence;
  outputs.frequencies.value.swap(result.frequencies);
  outputs.magnitudes.value.swap(result.magnitudes);
  outputs.inharmonicity.value = result.inharmonicity;
  outputs.tristimulus.value.swap(result.tristimulus);
  outputs.oddEnergy.value = result.oddEnergy;
  outputs.evenEnergy.value = result.evenEnergy;
  outputs.ratioDefined.value = result.ratioDefined;
  outputs.oddToEvenRatio.value = result.oddToEvenRatio;
}
}
