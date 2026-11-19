#include <Essentia/Node.hpp>

#include <cmath>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Count only the invoking thread, never worker construction or native work.
// Setup, assertions and reporting are deliberately outside the counted scopes.
namespace allocationProbe
{
thread_local bool enabled{};
thread_local std::size_t count{};
void record() noexcept
{
  if(enabled)
    ++count;
}
void* allocate(std::size_t size)
{
  record();
  if(void* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc{};
}
void* allocateAligned(std::size_t size, std::size_t alignment)
{
  record();
  void* p{};
  if(posix_memalign(&p, alignment, size ? size : 1) == 0)
    return p;
  throw std::bad_alloc{};
}
struct Scope
{
  Scope()
  {
    count = 0;
    enabled = true;
  }
  ~Scope() { enabled = false; }
};
}
void* operator new(std::size_t size)
{
  return allocationProbe::allocate(size);
}
void* operator new[](std::size_t size)
{
  return allocationProbe::allocate(size);
}
void operator delete(void* p) noexcept
{
  std::free(p);
}
void operator delete[](void* p) noexcept
{
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
  std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
  std::free(p);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
  return allocationProbe::allocateAligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
  return allocationProbe::allocateAligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* p, std::align_val_t) noexcept
{
  std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
  std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
  std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
  std::free(p);
}

namespace
{
using namespace EssentiaNodes;
using Clock = std::chrono::steady_clock;
constexpr double rate = 48000.;

void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}
bool near(double a, double b)
{
  return std::isfinite(a) && std::abs(a - b) <= 1e-10;
}
template <typename F>
void allocationFree(F&& function)
{
  {
    allocationProbe::Scope scope;
    function();
  }
  require(allocationProbe::count == 0, "callback allocated on the calling thread");
}
template <typename Predicate>
void until(Predicate&& predicate, const char* failure)
{
  const auto deadline = Clock::now() + std::chrono::seconds{10};
  while(!predicate())
  {
    require(Clock::now() < deadline, failure);
    std::this_thread::yield();
  }
}

// The first compute cannot finish until explicitly released. No sleep or guess
// about worker scheduling is used to arrange stale results or queue pressure.
struct Gate
{
  std::atomic<bool> armed{true}, entered{}, released{};
  void wait()
  {
    if(!armed.exchange(false, std::memory_order_acq_rel))
      return;
    entered.store(true, std::memory_order_release);
    until([&] {
      return released.load(std::memory_order_acquire);
    }, "worker gate was not released before its deadline");
  }
  void awaitEntry()
  {
    until([&] {
      return entered.load(std::memory_order_acquire);
    }, "worker did not enter the gated computation");
  }
  void release() noexcept { released.store(true, std::memory_order_release); }
};
struct ReleaseGate
{
  Gate& gate;
  ~ReleaseGate() { gate.release(); }
};

// A real, deliberately elementary processor keeps expected DSP values exact.
// Nonmovable results force the production in-place completion path to be used.
struct SumProcessor
{
  struct Settings
  {
    Gate* gate{};
    double gain{1.};
    bool operator==(const Settings&) const = default;
  };
  struct Result
  {
    Result() = default;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&) = delete;
    Result& operator=(Result&&) = delete;
    double sum{}, energy{}, endTime{};
  };
  Frame* frame{};
  Settings settings;
  void prepare(Frame& input, const Settings& controls)
  {
    frame = &input;
    settings = controls;
  }
  void compute(Result& result)
  {
    if(settings.gate)
      settings.gate->wait();
    result.sum = result.energy = 0.;
    for(const Real sample : frame->audio)
    {
      const double value = double(sample) * settings.gain;
      result.sum += value;
      result.energy += value * value;
    }
    result.endTime
        = (double(frame->startSample) + frame->audio.size()) / frame->sampleRate;
  }
};
struct StereoProcessor : SumProcessor
{
  static constexpr bool stereo = true;
};
struct Observation
{
  double sum{}, energy{}, endTime{};
};
struct Collection
{
  std::array<Observation, 64> results{};
  std::size_t count{};
  bool exhausted{};
  void collect(SumProcessor::Result& result) noexcept
  {
    if(count == results.size())
    {
      exhausted = true;
      return;
    }
    results[count++] = {result.sum, result.energy, result.endTime};
  }
};
template <typename Processor = SumProcessor>
struct AudioFixture
    : AudioNode<AudioFixture<Processor>, Processor>
    , Collection
{
  struct
  {
    ESSENTIA_AUDIO_INPUTS(5, 2);
  } inputs;
  struct
  {
    ESSENTIA_STATUS_OUTPUTS;
  } outputs;
  SumProcessor::Settings controls;
  auto settings() const { return controls; }
  void publish(SumProcessor::Result& result) noexcept { collect(result); }
};
struct DataFixture
    : DataNode<DataFixture, SumProcessor>
    , Collection
{
  struct
  {
    ESSENTIA_DATA_INPUTS;
  } inputs;
  struct
  {
    ESSENTIA_DATA_STATUS_OUTPUTS;
  } outputs;
  SumProcessor::Settings controls;
  auto settings() const { return controls; }
  void publish(SumProcessor::Result& result) noexcept { collect(result); }
};

halp::setup setup(int frames, int channels = 1)
{
  halp::setup result{};
  result.frames = frames;
  result.rate = rate;
  result.input_channels = channels;
  return result;
}
template <typename Processor>
void configure(
    AudioFixture<Processor>& node, double** channels, int count = 1, int window = 5,
    int divisor = 2)
{
  node.inputs.window.value = window;
  node.inputs.hop.value = divisor;
  node.inputs.audio.samples = channels;
  node.inputs.audio.channels = count;
  node.prepare(setup(16, count));
}
template <typename Processor>
void poll(AudioFixture<Processor>& node)
{
  allocationFree([&] { node.process(0); });
}
void poll(DataFixture& node)
{
  allocationFree([&] { node.process(); });
}
template <typename Node>
void awaitCount(Node& node, std::size_t count)
{
  until([&] {
    poll(node);
    return node.count >= count;
  }, "expected result was not published");
  require(
      !node.exhausted && node.count == count, "unexpected extra result was published");
  require(
      node.outputs.valid.value && node.outputs.error.value.empty(),
      "successful publication was not valid and error-free");
}
template <typename Node>
void expect(const Node& node, std::size_t index, double sum, double energy, int end)
{
  require(index < node.count, "expected window is absent");
  const auto& result = node.results[index];
  require(
      near(result.sum, sum) && near(result.energy, energy),
      "window samples differ from the expected sum or energy");
  require(
      near(result.endTime, double(end) / rate),
      "processor received the wrong source time");
}
template <typename Node>
void expectTime(const Node& node, int end)
{
  require(
      near(node.outputs.time.value, double(end) / rate),
      "published analysis time is incorrect");
}

void unevenPartitions()
{
  AudioFixture node;
  std::array<double, 13> samples{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  double* bus[]{samples.data()};
  configure(node, bus);
  int offset = 0;
  // floor(5 / 2) = 2: windows [1..5], [3..7], ..., [9..13].
  for(int frames : {2, 1, 5, 1, 4})
  {
    bus[0] = samples.data() + offset;
    allocationFree([&] { node.process(frames); });
    offset += frames;
  }
  // Some results can already have arrived during capture; wait only if needed.
  if(node.count < 5)
    awaitCount(node, 5);
  require(
      node.count == 5 && !node.exhausted, "uneven partitions changed the window count");
  constexpr std::array<double, 5> sums{15, 25, 35, 45, 55};
  constexpr std::array<double, 5> energies{55, 135, 255, 415, 615};
  for(std::size_t i = 0; i < sums.size(); ++i)
    expect(node, i, sums[i], energies[i], 5 + 2 * int(i));
  expectTime(node, 13);
  poll(node);
  require(
      !node.outputs.valid.value, "Valid persisted into a poll without a new completion");
}

void preparePartitionChange()
{
  AudioFixture node;
  std::array<double, 5> samples{1, 2, 3, 4, 5};
  double* bus[]{samples.data()};
  configure(node, bus);
  allocationFree([&] { node.process(2); });
  node.prepare(setup(1024)); // Only the host partition changes, not the stream.
  bus[0] = samples.data() + 2;
  allocationFree([&] { node.process(3); });
  if(node.count < 1)
    awaitCount(node, 1);
  require(node.count == 1, "prepare lost or duplicated a partial window");
  expect(node, 0, 15., 55., 5);
  expectTime(node, 5);
}

void oversizedCallback()
{
  AudioFixture node;
  // Hop control is a divisor: divisor 1 means two non-overlapping 65536 windows.
  std::vector<double> samples(2 * 65536, 1.);
  std::fill(samples.begin() + 65536, samples.end(), 2.);
  double* bus[]{samples.data()};
  configure(node, bus, 1, 65536, 1);
  allocationFree([&] { node.process(static_cast<int>(samples.size())); });
  if(node.count < 2)
    awaitCount(node, 2);
  require(node.count == 2, "large callback did not produce exactly two windows");
  expect(node, 0, 65536., 65536., 65536);
  expect(node, 1, 131072., 262144., 131072);
  expectTime(node, 131072);
}

enum class Change
{
  Position,
  Flicks,
  Settings,
  Window,
  Hop,
  Reset,
  StopStart,
  Transport
};
void delayedEpoch(Change change)
{
  Gate gate;
  AudioFixture node;
  ReleaseGate release{
      gate}; // Unblocks before the node destructor joins, even on failure.
  std::array<double, 5> old{1, 1, 1, 1, 1};
  std::array<double, 5> fresh{10, 20, 30, 40, 50};
  double* bus[]{old.data()};
  configure(node, bus);
  node.controls.gate = &gate;
  halp::tick_flicks tick{};
  tick.frames = 5;
  tick.position_in_frames = 100;
  tick.start_in_flicks = 1000;
  tick.end_in_flicks = 1100;
  allocationFree([&] { node(tick); });
  gate.awaitEntry();
  bus[0] = fresh.data();
  int size = 5;
  double gain = 1.;
  switch(change)
  {
    case Change::Position:
      tick.position_in_frames = 900;
      break;
    case Change::Flicks:
      tick.position_in_frames = 105;
      break;
    case Change::Settings:
      node.controls.gain = gain = 2.;
      break;
    case Change::Window:
      node.inputs.window.value = size = 4;
      break;
    case Change::Hop:
      node.inputs.hop.value = 1;
      break;
    case Change::Reset:
      allocationFree([&] { node.reset(); });
      break;
    case Change::StopStart:
      allocationFree([&] {
        node.stop();
        node.process(5);
      });
      require(
          !node.outputs.valid.value && node.count == 0,
          "stopped node published or accepted old work");
      allocationFree([&] { node.start(); });
      break;
    case Change::Transport:
      allocationFree([&] { node.transport(std::chrono::milliseconds{37}); });
      break;
  }
  if(change == Change::Position || change == Change::Flicks)
  {
    tick.start_in_flicks = change == Change::Flicks ? 2000 : 1100;
    tick.end_in_flicks = tick.start_in_flicks + 100;
    allocationFree([&] { node(tick); });
  }
  else
    allocationFree([&] { node.process(size); });
  require(
      node.count == 0 && !node.outputs.valid.value,
      "in-flight old result escaped its gate");
  gate.release();
  awaitCount(node, 1);
  expect(
      node, 0, (size == 4 ? 100. : 150.) * gain,
      (size == 4 ? 3000. : 5500.) * gain * gain, size);
  expectTime(node, size);
  require(node.outputs.dropped.value >= 1, "old in-flight result was not rejected");
  poll(node);
  require(
      node.count == 1 && !node.outputs.valid.value,
      "old epoch result appeared after recovery");
}

void dataSeek()
{
  Gate gate;
  DataFixture node;
  ReleaseGate release{gate};
  node.controls.gate = &gate;
  node.prepare(setup(16));
  node.inputs.data.value = std::vector<Real>{1, 2, 3, 4, 5};
  allocationFree([&] { node.process(); });
  node.inputs.data.value.reset();
  gate.awaitEntry();
  allocationFree([&] { node.transport(std::chrono::seconds{42}); });
  node.inputs.data.value = std::vector<Real>{10, 20, 30, 40, 50};
  allocationFree([&] { node.process(); });
  node.inputs.data.value.reset();
  gate.release();
  awaitCount(node, 1);
  require(
      near(node.results[0].sum, 150.) && near(node.results[0].energy, 5500.),
      "data seek did not publish the post-seek vector");
  require(node.outputs.dropped.value >= 1, "data seek published a pre-seek result");
  poll(node);
  require(
      !node.outputs.valid.value && node.count == 1,
      "empty data poll resubmitted a vector");
  node.inputs.data.value = std::vector<Real>{2, 2, 2, 2, 2};
  allocationFree([&] { node.process(); });
  node.inputs.data.value.reset();
  if(node.count < 2)
    awaitCount(node, 2);
  require(
      near(node.results[1].sum, 10.) && near(node.results[1].energy, 20.),
      "next data vector was not processed independently");
}

void overflowRecovery()
{
  Gate gate;
  AudioFixture node;
  ReleaseGate release{gate};
  std::array<double, 5> initial{1, 1, 1, 1, 1};
  // More complete windows than the advertised capacity, with the worker held.
  constexpr int extraWindows = int(analysisQueueCapacity) + 3;
  constexpr int extraSamples = 2 * extraWindows;
  std::array<double, extraSamples> pressure;
  pressure.fill(2.);
  double* bus[]{initial.data()};
  configure(node, bus);
  node.controls.gate = &gate;
  allocationFree([&] { node.process(5); });
  gate.awaitEntry();
  bus[0] = pressure.data();
  allocationFree([&] { node.process(extraSamples); });
  require(
      node.outputs.dropped.value > 0 && !node.outputs.error.value.empty()
          && !node.outputs.valid.value && node.count == 0,
      "queue overflow was not observable before the gated worker resumed");
  gate.release();
  // Dropped is public: every submitted window must be accounted for before
  // recovery. This avoids guessing when the worker has emptied stale work.
  until([&] {
    poll(node);
    return node.outputs.dropped.value >= extraWindows + 1;
  }, "overflowed epoch did not finish rejecting its outstanding windows");
  require(node.count == 0, "an old overflow-epoch result was published");
  std::array<double, 2> recovery{3, 3};
  bus[0] = recovery.data();
  allocationFree([&] { node.process(2); });
  if(node.count < 1)
    awaitCount(node, 1);
  // The source clock and overlapping ring survive queue pressure: the first
  // recovery window is [2,2,2,3,3], then [2,3,3,3,3], at the original hop phase.
  expect(node, 0, 12., 30., 5 + extraSamples + 2);
  expectTime(node, 5 + extraSamples + 2);
  allocationFree([&] { node.process(2); });
  if(node.count < 2)
    awaitCount(node, 2);
  require(
      node.count == 2 && node.outputs.error.value.empty(),
      "overflow did not recover cleanly");
  expect(node, 1, 14., 40., 5 + extraSamples + 4);
  expectTime(node, 5 + extraSamples + 4);
}

void actualStereoRequired()
{
  AudioFixture<StereoProcessor> node;
  std::array<double, 5> left{1, 2, 3, 4, 5}, right{3, 4, 5, 6, 7};
  double* bus[]{left.data(), right.data()};
  configure(node, bus, 1);
  // The first error, not a warmed-up assignment, must be allocation-free.
  allocationFree([&] { node.process(5); });
  require(
      !node.outputs.valid.value && !node.outputs.error.value.empty() && node.count == 0,
      "stereo-required processor accepted a single real channel");
  node.inputs.audio.channels = 2;
  allocationFree([&] { node.process(5); });
  if(node.count < 1)
    awaitCount(node, 1);
  require(
      node.count == 1 && node.outputs.error.value.empty(),
      "real stereo failed to recover from mono input");
  expect(node, 0, 20., 90., 5);
  expectTime(node, 5);
}
}

int main(int argc, char** argv)
{
  const std::pair<const char*, void (*)()> cases[]{
      {"uneven-partitions", unevenPartitions},
      {"prepare-partition-change", preparePartitionChange},
      {"oversized-callback", oversizedCallback},
      {"position-discontinuity", [] { delayedEpoch(Change::Position); }},
      {"flicks-discontinuity", [] { delayedEpoch(Change::Flicks); }},
      {"settings-epoch", [] { delayedEpoch(Change::Settings); }},
      {"window-epoch", [] { delayedEpoch(Change::Window); }},
      {"hop-epoch", [] { delayedEpoch(Change::Hop); }},
      {"reset-epoch", [] { delayedEpoch(Change::Reset); }},
      {"stop-start-epoch", [] { delayedEpoch(Change::StopStart); }},
      {"chrono-transport-epoch", [] { delayedEpoch(Change::Transport); }},
      {"data-seek", dataSeek},
      {"overflow-recovery", overflowRecovery},
      {"actual-stereo-required", actualStereoRequired}};
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
