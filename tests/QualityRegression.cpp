#include <Essentia/Quality.hpp>

#include <cmath>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace EssentiaNodes;
constexpr double sampleRate = 48000.;

void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}

bool nearSample(double time, std::int64_t sample, double tolerance = 0.25)
{
  return std::isfinite(time) && std::abs(time * sampleRate - sample) <= tolerance;
}

Frame makeFrame(int size, int hop, std::int64_t origin = 0)
{
  return Frame{std::vector<Real>(size, 0.f), {}, {}, sampleRate, hop, origin};
}

void noiseBurstOverlap()
{
  auto frame = makeFrame(512, 256);
  NoiseBurstProcessor processor;
  processor.prepare(frame, {});
  NoiseBurstProcessor::Result result;
  std::vector<double> observed;

  // Exercise the real event publisher with native results, without depending on
  // worker timing to put multiple completed windows into a single callback.
  NoiseBurstDetector node;
  node.inputs.window.value = 512;
  node.inputs.hop.value = 2;
  node.begin_tick();
  for(int offset = 0; offset <= 1024; offset += frame.hopSize)
  {
    frame.startSample = offset;
    for(int i = 0; i < 512; ++i)
    {
      const int sample = offset + i;
      frame.audio[i] = sample == 100 || sample == 400 ? -1.f : 0.f;
    }
    processor.compute(result);
    require(
        result.coverageStart == 1. / sampleRate,
        "noise-burst startup coverage moved with the frame");
    require(
        result.coverageEnd == (offset + 384.) / sampleRate,
        "noise-burst coverage did not advance by the owned hop");
    // Native dd[i] is centered on i+1 and only positive peaks are detected:
    // negative impulses produce one peak, not a pair of adjacent events.
    if(offset == 0)
      require(
          result.times == std::vector<double>{100. / sampleRate},
          "startup impulse before the central slice was lost or mis-timed");
    else if(offset == 256)
      require(
          result.times == std::vector<double>{400. / sampleRate},
          "overlap impulse was not emitted by its owning frame");
    else
      require(result.times.empty(), "noise-burst event survived into a later frame");
    observed.insert(observed.end(), result.times.begin(), result.times.end());
    node.publish(result);
  }
  const std::vector<double> expected{100. / sampleRate, 400. / sampleRate};
  require(
      observed == expected,
      "overlapping noise-burst frames lost or duplicated impulses");
  require(
      node.outputs.times.value == expected,
      "publishing multiple completed windows discarded or duplicated events");
  node.begin_tick();
  require(node.outputs.times.value.empty(), "next callback retained noise-burst events");
}

void gapsAcrossFrames()
{
  constexpr std::int64_t origin = 98765;
  constexpr int gapStart = 1400;
  constexpr int gapEnd = 2600;
  auto frame = makeFrame(2048, 1024, origin);
  GapsProcessor processor;
  processor.prepare(frame, {});
  GapsProcessor::Result result;
  std::vector<double> starts, ends;
  for(int offset = 0; offset <= 6144; offset += frame.hopSize)
  {
    frame.startSample = origin + offset;
    for(int i = 0; i < 2048; ++i)
    {
      const int sample = offset + i;
      frame.audio[i] = sample >= gapStart && sample < gapEnd ? 0.f : 0.5f;
    }
    processor.compute(result);
    require(result.starts.size() == result.ends.size(), "gap endpoints became unpaired");
    require(
        nearSample(result.coverageStart, origin + 512),
        "gap coverage lost the nonzero stream origin");
    // The rising edge belongs to the frame starting at 2048. Native completion
    // checks run before edge detection, so its post-power-confirmed interval is
    // returned on the following call, even though context fits in that frame.
    if(offset <= 2048)
      require(result.starts.empty(), "gap was published before native completion");
    else if(offset == 3072)
      require(
          result.starts.size() == 1,
          "completed gap was not published after its context delay");
    else
      require(result.starts.empty(), "completed gap was repeated in a later frame");
    starts.insert(starts.end(), result.starts.begin(), result.starts.end());
    ends.insert(ends.end(), result.ends.begin(), result.ends.end());
  }
  require(
      starts.size() == 1 && ends.size() == 1,
      "overlapping frames did not produce exactly one completed gap");
  // The centered odd median preserves these long binary-mask transitions.
  require(
      nearSample(starts[0], origin + gapStart) && nearSample(ends[0], origin + gapEnd),
      "gap interval was shifted by a hop or lost its source origin");
}

void saturationAcrossFrames()
{
  constexpr std::int64_t origin = 12017;
  auto frame = makeFrame(512, 256, origin);
  SaturationProcessor processor;
  processor.prepare(frame, {});
  SaturationProcessor::Result result;
  std::vector<double> starts, ends;
  for(int offset = 0; offset <= 1024; offset += frame.hopSize)
  {
    frame.startSample = origin + offset;
    for(int i = 0; i < 512; ++i)
    {
      const int sample = offset + i;
      frame.audio[i] = sample >= 200 && sample < 400 ? 0.99f : 0.f;
    }
    processor.compute(result);
    require(
        result.starts.size() == result.ends.size(),
        "saturation endpoints became unpaired");
    if(offset == 0)
      require(
          result.starts.empty(),
          "unfinished saturation was published from the first slice");
    else if(offset == 256)
      require(
          result.starts.size() == 1, "saturation continuation was lost across the hop");
    else
      require(
          result.starts.empty(), "completed saturation was repeated in a later frame");
    starts.insert(starts.end(), result.starts.begin(), result.starts.end());
    ends.insert(ends.end(), result.ends.begin(), result.ends.end());
  }
  require(
      starts.size() == 1 && ends.size() == 1,
      "overlapping saturation frames did not produce exactly one interval");
  // The first plateau sample has a large difference from its predecessor.
  // Native flatness therefore begins at 201, and ends at the first zero, 400.
  // A quarter-sample allowance covers native float seconds, not a one-sample bug.
  require(
      nearSample(starts[0], origin + 201) && nearSample(ends[0], origin + 400),
      "saturation boundaries do not match the source flatness interval");
}

Real noiseSample(std::uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return Real((static_cast<int>(state >> 16) - 32768) * (0.001 / 32768.));
}

void requireNoEstimate(const SNRProcessor::Result& result)
{
  require(
      !result.ready && !result.instant && !result.averaged && result.spectral.empty(),
      "unready SNR published a stale or premature signal estimate");
}

void snrReadinessHistory()
{
  auto frame = makeFrame(512, 256);
  SNRProcessor processor;
  processor.prepare(frame, {-40.f, 15});
  SNRProcessor::Result result;
  auto compute = [&] {
    processor.compute(result);
    frame.startSample += frame.hopSize;
  };
  for(int i = 0; i < 20; ++i)
  {
    compute();
    require(
        result.noiseFrames == 0 && !result.learned && result.learning,
        "exact silence counted as learned noise evidence");
    requireNoEstimate(result);
  }
  for(int i = 0; i < 512; ++i)
    frame.audio[i] = Real(0.25 * std::sin(i * 0.13));
  compute();
  require(
      !result.learned && !result.learning && result.noiseFrames == 0,
      "signal following exact silence was treated as learned noise");
  requireNoEstimate(result);

  std::uint32_t state = 0x53a9b401u;
  for(int count = 1; count <= 15; ++count)
  {
    for(auto& sample : frame.audio)
      sample = noiseSample(state);
    compute();
    require(
        result.noiseFrames == count && result.learning
            && result.learned == (count >= 15),
        "SNR learning did not transition at the required nonzero noise history");
    requireNoEstimate(result);
  }
  for(int call = 0; call < 3; ++call)
  {
    for(int i = 0; i < 512; ++i)
      frame.audio[i]
          = Real(0.25 * std::sin((frame.startSample + i) * 0.13)) + noiseSample(state);
    compute();
    require(
        result.ready && result.learned && !result.learning && result.noiseFrames == 15,
        "learned SNR did not become ready for signal frames");
    require(
        result.instant && result.averaged && std::isfinite(*result.instant)
            && std::isfinite(*result.averaged),
        "silence or noise history poisoned the signal SNR estimates");
    require(
        result.spectral.size() == 257
            && std::all_of(
                result.spectral.begin(), result.spectral.end(),
                [](Real value) { return std::isfinite(value) && value >= 0.f; }),
        "learned SNR did not provide a finite one-sided spectral estimate");
  }
  for(auto& sample : frame.audio)
    sample = noiseSample(state);
  compute();
  require(
      result.learned && result.learning && result.noiseFrames == 16,
      "later noise discarded learned SNR history");
  requireNoEstimate(result);
}

void falseStereoReadiness()
{
  auto frame = makeFrame(512, 256);
  frame.left.resize(512);
  frame.right.resize(512);
  for(int i = 0; i < 512; ++i)
    frame.left[i] = Real(0.3 * std::sin(i * 0.13) + 0.1 * std::cos(i * 0.037));
  FalseStereoProcessor processor;
  processor.prepare(frame, {});
  FalseStereoProcessor::Result result;
  for(const Real scale : {1.f, 0.5f, -1.f})
  {
    for(int i = 0; i < 512; ++i)
    {
      frame.right[i] = scale * frame.left[i];
      frame.audio[i] = (frame.left[i] + frame.right[i]) * 0.5f;
    }
    processor.compute(result);
    require(
        result.ready && !result.silent && !result.degenerate && result.correlation,
        "nonconstant channel pair was not ready for correlation");
    require(
        std::isfinite(*result.correlation)
            && std::abs(*result.correlation - (scale > 0.f ? 1. : -1.)) < 1e-5,
        "stereo correlation lost channel scaling or polarity");
    require(
        result.falseStereo == (scale > 0.f),
        "false-stereo classification confused duplication with anticorrelation");
    frame.startSample += frame.hopSize;
  }
  std::fill(frame.right.begin(), frame.right.end(), 0.5f);
  for(int i = 0; i < 512; ++i)
    frame.audio[i] = (frame.left[i] + frame.right[i]) * 0.5f;
  processor.compute(result);
  require(
      !result.ready && !result.silent && result.degenerate && !result.correlation
          && !result.falseStereo,
      "constant channel retained a correlation or a ready false-stereo estimate");
}
}

int main(int argc, char** argv)
{
  const std::pair<const char*, void (*)()> cases[]{
      {"noise-burst-overlap", noiseBurstOverlap},
      {"gaps-across-frames", gapsAcrossFrames},
      {"saturation-across-frames", saturationAcrossFrames},
      {"snr-readiness-history", snrReadinessHistory},
      {"false-stereo-readiness", falseStereoReadiness}};
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
