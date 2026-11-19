#pragma once

#include <Essentia/Async.hpp>
#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#define ESSENTIA_AUDIO_INPUTS(WindowDefault, HopDefault) \
  halp::dynamic_audio_bus<"Audio", double> audio;        \
  EssentiaNodes::WindowControl<WindowDefault> window;    \
  EssentiaNodes::HopControl<HopDefault> hop;             \
  halp::impulse_button<"Reset"> reset

#define ESSENTIA_DATA_INPUTS                                                    \
  halp::val_port<"Data", std::optional<std::vector<EssentiaNodes::Real>>> data; \
  halp::impulse_button<"Reset"> reset

#define ESSENTIA_STATUS_OUTPUTS                     \
  halp::val_port<"Valid", bool> valid;              \
  EssentiaNodes::ErrorOutput error;                 \
  halp::val_port<"Analysis time (s)", double> time; \
  halp::val_port<"Dropped frames", int> dropped

#define ESSENTIA_DATA_STATUS_OUTPUTS   \
  halp::val_port<"Valid", bool> valid; \
  EssentiaNodes::ErrorOutput error;    \
  halp::val_port<"Dropped frames", int> dropped

namespace EssentiaNodes
{
struct ErrorOutput : halp::val_port<"Error", std::string>
{
  ErrorOutput() { value.reserve(diagnosticReserve); }
};

template <int Initial>
using WindowControl
    = halp::spinbox_i32<"Window (samples)", halp::irange{1, maximumFrameSize, Initial}>;
template <int Initial>
using HopControl = halp::spinbox_i32<"Hop divisor", halp::irange{1, 16, Initial}>;

template <typename Derived, typename Processor, bool Timestamped = true>
class NodeState
{
public:
  halp_flag(process_exec);
  template <typename Rep, typename Period>
  void transport(std::chrono::duration<Rep, Period>)
  {
    self().reset();
  }

protected:
  using Settings = typename Processor::Settings;
  using EngineType = Engine<Processor, Timestamped>;
  EngineType m_engine;
  std::optional<Settings> m_settings;
  double m_rate{48000.};
  bool m_running{true};

  Derived& self() noexcept { return static_cast<Derived&>(*this); }

  void invalidatePayload()
  {
    self().outputs.valid.value = false;
    if constexpr(requires(Derived& d) { d.invalidate(); })
      self().invalidate();
  }
  void restartEpoch()
  {
    m_engine.invalidate();
    m_seenEpoch = m_engine.epoch();
    invalidatePayload();
    self().outputs.error.value.clear();
    if constexpr(Timestamped)
      self().outputs.time.value = 0.;
  }
  void beginTick()
  {
    if constexpr(requires(Derived& d) { d.begin_tick(); })
      self().begin_tick();
    self().outputs.valid.value = false;
    synchronize();
  }
  void synchronize()
  {
    const auto current = m_engine.epoch();
    if(current != m_seenEpoch)
    {
      m_seenEpoch = current;
      invalidatePayload();
      self().outputs.error.value = "Analysis queue overflow: continuity lost";
    }
    self().outputs.dropped.value = static_cast<int>(
        std::min<std::uint64_t>(m_engine.dropped(), std::numeric_limits<int>::max()));
    if(m_engine.failed())
    {
      invalidatePayload();
      self().outputs.error.value = "Analysis worker stopped after resource failure";
    }
  }
  void poll()
  {
    synchronize();
    m_engine.drain([this](typename EngineType::Completion& completion) {
      if(!m_running)
        return;
      if(completion.valid)
      {
        self().publish(completion.result);
        self().outputs.valid.value = true;
        self().outputs.error.value.clear();
      }
      else
      {
        invalidatePayload();
        self().outputs.error.value.swap(completion.error);
      }
      if constexpr(Timestamped)
        self().outputs.time.value = completion.time;
    });
    synchronize();
  }
  void inputError(const char* message)
  {
    restartEpoch();
    self().outputs.error.value = message;
    // Drain old completions, never publish an old success over an input error.
    m_engine.drain([](auto&) { });
  }

private:
  std::uint64_t m_seenEpoch{1};
};

template <typename Derived, typename Processor>
class AudioNode : public NodeState<Derived, Processor>
{
  using Base = NodeState<Derived, Processor>;
  static constexpr bool stereo = Engine<Processor>::stereo;

public:
  // Avendish's execution wrapper needs the tick alias as well as operator().
  using tick = halp::tick_flicks;

  AudioNode()
      : m_audio(maximumFrameSize)
  {
    if constexpr(stereo)
    {
      m_left.resize(maximumFrameSize);
      m_right.resize(maximumFrameSize);
    }
  }

  void prepare(halp::setup setup)
  {
    if(!m_prepared || this->m_rate != setup.rate
       || m_setupChannels != setup.input_channels)
    {
      this->m_rate = setup.rate;
      m_setupChannels = setup.input_channels;
      m_prepared = true;
      reset();
    }
  }
  void reset()
  {
    this->restartEpoch();
    m_samples = 0;
    m_nextFrameEnd = 0;
    m_write = 0;
    m_haveTick = false;
  }
  void start()
  {
    this->m_running = true;
    reset();
  }
  void stop()
  {
    this->m_running = false;
    reset();
  }
  void operator()(halp::tick_flicks tick)
  {
    if(m_haveTick
       && (tick.position_in_frames != m_expectedPosition
           || ((tick.start_in_flicks != 0 || tick.end_in_flicks != 0)
               && tick.start_in_flicks != m_expectedFlicks)))
      reset();
    process(tick.frames);
    m_haveTick = true;
    m_expectedPosition = tick.position_in_frames + tick.frames;
    m_expectedFlicks = tick.end_in_flicks;
  }

  void process(int frames)
  {
    this->beginTick();
    auto& input = this->self().inputs;
    if(input.reset.value)
    {
      reset();
      input.reset.value.reset();
    }
    const auto settings = this->self().settings();
    const int window = input.window.value;
    const int divisor = input.hop.value;
    const int channels = input.audio.channels;
    if(!this->m_settings || !(settings == *this->m_settings) || window != m_window
       || divisor != m_divisor || channels != m_channels)
    {
      reset();
      this->m_settings = settings;
      m_window = window;
      m_divisor = divisor;
      m_channels = channels;
    }
    if(!this->m_running)
    {
      this->poll();
      return;
    }
    if(window < 1 || window > maximumFrameSize || divisor < 1 || divisor > 16)
    {
      this->inputError("Window must be 1..65536 samples and hop divisor 1..16");
      return;
    }
    if(!std::isfinite(this->m_rate) || this->m_rate <= 0.)
    {
      this->inputError("Analysis sample rate must be finite and positive");
      return;
    }
    if(frames < 0)
    {
      this->inputError("Audio frame count cannot be negative");
      return;
    }
    if(frames == 0)
    {
      this->poll();
      return;
    }
    if(channels < (stereo ? 2 : 1) || !input.audio.samples)
    {
      reset();
      this->inputError(
          stereo ? "Stereo analysis requires two actual audio channels"
                 : "Audio analysis requires at least one channel");
      return;
    }
    for(int channel = 0; channel < channels; ++channel)
      if(!input.audio.samples[channel])
      {
        reset();
        this->inputError("Audio channel has no sample buffer");
        return;
      }
    const int hop = std::max(1, window / divisor);
    if(m_nextFrameEnd == 0)
      m_nextFrameEnd = window;

    // A host block is only a transport partition. Stream every sample through
    // the bounded ring, submitting every completed hop, even for huge blocks.
    for(int sample = 0; sample < frames; ++sample)
    {
      double mono = 0.;
      for(int channel = 0; channel < channels; ++channel)
        mono += input.audio.samples[channel][sample] / double(channels);
      m_audio[m_write] = static_cast<Real>(mono);
      if constexpr(stereo)
      {
        m_left[m_write] = static_cast<Real>(input.audio.samples[0][sample]);
        m_right[m_write] = static_cast<Real>(input.audio.samples[1][sample]);
      }
      m_write = (m_write + 1) % window;
      ++m_samples;
      if(m_samples == m_nextFrameEnd)
      {
        this->m_engine.submit(
            settings, window, hop, this->m_rate, m_samples - window,
            [this, window](Frame& frame) {
          copyRing(m_audio, frame.audio, window);
          if constexpr(stereo)
          {
            copyRing(m_left, frame.left, window);
            copyRing(m_right, frame.right, window);
          }
        });
        m_nextFrameEnd += hop;
      }
    }
    this->poll();
  }

private:
  void copyRing(const std::vector<Real>& ring, std::vector<Real>& output, int size)
  {
    const int first = size - m_write;
    std::copy_n(ring.data() + m_write, first, output.data());
    std::copy_n(ring.data(), m_write, output.data() + first);
  }
  std::vector<Real> m_audio, m_left, m_right;
  std::int64_t m_samples{}, m_nextFrameEnd{};
  std::int64_t m_expectedPosition{}, m_expectedFlicks{};
  int m_write{}, m_window{}, m_divisor{}, m_channels{};
  int m_setupChannels{};
  bool m_haveTick{}, m_prepared{};
};

template <typename Derived, typename Processor>
class DataNode : public NodeState<Derived, Processor, false>
{
public:
  void prepare(halp::setup setup)
  {
    if(!m_prepared || this->m_rate != setup.rate)
    {
      this->m_rate = setup.rate;
      m_prepared = true;
      reset();
    }
  }
  void reset() { this->restartEpoch(); }
  void start()
  {
    this->m_running = true;
    reset();
  }
  void stop()
  {
    this->m_running = false;
    reset();
  }
  void operator()() { process(); }
  void process()
  {
    this->beginTick();
    auto& input = this->self().inputs;
    if(input.reset.value)
    {
      reset();
      input.reset.value.reset();
    }
    const auto settings = this->self().settings();
    if(!this->m_settings || !(settings == *this->m_settings))
    {
      reset();
      this->m_settings = settings;
    }
    if(!this->m_running)
    {
      this->poll();
      return;
    }
    if(input.data.value)
    {
      const auto& data = *input.data.value;
      if(data.empty() || data.size() > maximumFrameSize)
      {
        this->inputError("Data analysis requires 1..65536 values");
        return;
      }
      if(!std::isfinite(this->m_rate) || this->m_rate <= 0.)
      {
        this->inputError("Analysis sample rate must be finite and positive");
        return;
      }
      const int size = static_cast<int>(data.size());
      if(size != m_size)
      {
        reset();
        m_size = size;
      }
      this->m_engine.submit(
          settings, size, size, this->m_rate, 0, [&data](Frame& frame) {
        std::copy(data.begin(), data.end(), frame.audio.begin());
      });
    }
    this->poll();
  }

private:
  int m_size{};
  bool m_prepared{};
};
}
