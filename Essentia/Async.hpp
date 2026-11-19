#pragma once

#include <Essentia/Analysis.hpp>

#include <cmath>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace EssentiaNodes
{
// Slots never move. Only their owning producer/consumer may access a slot, and
// release/acquire publishes the entire payload. One slot is the sentinel.
template <typename T, std::size_t Capacity>
class SpscQueue
{
public:
  T* writeSlot() noexcept
  {
    const auto next = (m_write + 1) % (Capacity + 1);
    if(next == m_readPublished.load(std::memory_order_acquire))
      return nullptr;
    return &m_slots[m_write];
  }
  void commitWrite() noexcept
  {
    m_write = (m_write + 1) % (Capacity + 1);
    m_writePublished.store(m_write, std::memory_order_release);
  }
  T* readSlot() noexcept
  {
    if(m_read == m_writePublished.load(std::memory_order_acquire))
      return nullptr;
    return &m_slots[m_read];
  }
  void commitRead() noexcept
  {
    m_read = (m_read + 1) % (Capacity + 1);
    m_readPublished.store(m_read, std::memory_order_release);
  }
  template <typename F>
  void initialize(F&& f)
  {
    for(auto& slot : m_slots)
      f(slot);
  }

private:
  std::array<T, Capacity + 1> m_slots;
  alignas(64) std::atomic<std::size_t> m_readPublished{};
  std::size_t m_read{};
  alignas(64) std::atomic<std::size_t> m_writePublished{};
  std::size_t m_write{};
};

template <typename Processor, bool Timestamped = true>
class Engine
{
public:
  using Settings = typename Processor::Settings;
  using Result = typename Processor::Result;
  static constexpr bool stereo = [] {
    if constexpr(requires { Processor::stereo; })
      return bool(Processor::stereo);
    else
      return false;
  }();
  static constexpr std::size_t queueCapacity = analysisQueueCapacity;
  static_assert(std::is_trivially_copyable_v<Settings>);

  struct Completion
  {
    Result result{};
    std::string error;
    std::uint64_t epoch{};
    std::int64_t startSample{};
    double time{};
    bool valid{};
  };

  Engine()
  {
    m_input.initialize([](Job& slot) {
      slot.frame.audio.reserve(maximumFrameSize);
      if constexpr(stereo)
      {
        slot.frame.left.reserve(maximumFrameSize);
        slot.frame.right.reserve(maximumFrameSize);
      }
    });
    m_output.initialize([](Completion& slot) { slot.error.reserve(diagnosticReserve); });
    m_worker = std::thread([this] { run(); });
  }
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;
  ~Engine()
  {
    m_quit.store(true, std::memory_order_release);
    signal();
    m_worker.join();
  }

  std::uint64_t epoch() const noexcept
  {
    return m_epoch.load(std::memory_order_acquire);
  }
  std::uint64_t dropped() const noexcept
  {
    return m_dropped.load(std::memory_order_relaxed);
  }
  void invalidate() noexcept
  {
    m_epoch.fetch_add(1, std::memory_order_acq_rel);
    signal();
  }

  // Producer only. The writer fills preallocated vectors and cannot retain them.
  template <typename Writer>
  bool submit(
      const Settings& settings, int size, int hop, double rate, std::int64_t start,
      Writer&& writer)
  {
    auto* job = m_input.writeSlot();
    if(!job)
    {
      drop();
      return false;
    }
    job->epoch = epoch();
    job->settings = settings;
    job->frame.audio.resize(size);
    if constexpr(stereo)
    {
      job->frame.left.resize(size);
      job->frame.right.resize(size);
    }
    job->frame.sampleRate = rate;
    job->frame.hopSize = hop;
    job->frame.startSample = start;
    writer(job->frame);
    m_input.commitWrite();
    signal();
    return true;
  }

  // Consumer only; bound the drain even if the worker produces concurrently.
  // Result is borrowed in-place, so neither move nor copy construction is needed.
  template <typename Publish>
  void drain(Publish&& publish)
  {
    for(std::size_t i = 0; i < queueCapacity; ++i)
    {
      auto* result = m_output.readSlot();
      if(!result)
        break;
      if(result->epoch == epoch())
        publish(*result);
      else
        m_dropped.fetch_add(1, std::memory_order_relaxed);
      m_output.commitRead();
    }
  }

private:
  struct Job
  {
    Frame frame;
    Settings settings{};
    std::uint64_t epoch{};
  };

  void signal() noexcept
  {
    m_wake.fetch_add(1, std::memory_order_release);
    m_wake.notify_one();
  }
  void drop() noexcept
  {
    m_dropped.fetch_add(1, std::memory_order_relaxed);
    invalidate();
  }
  static void swapPayload(Frame& a, Frame& b) noexcept
  {
    a.audio.swap(b.audio);
    a.left.swap(b.left);
    a.right.swap(b.right);
  }
  static void validate(const Frame& frame)
  {
    if(!std::isfinite(frame.sampleRate) || frame.sampleRate <= 0.)
      throw std::runtime_error("Analysis sample rate must be finite and positive");
    for(const auto* vector : {&frame.audio, &frame.left, &frame.right})
      for(const Real value : *vector)
        if(!std::isfinite(value))
          throw std::runtime_error("Analysis input contains a nonfinite sample");
  }
  void run() noexcept
  {
    // All native lifetimes, including unwind and final destruction, stay here.
    Frame frame;
    std::unique_ptr<Processor> processor;
    std::uint64_t preparedEpoch{};
    try
    {
      frame.audio.reserve(maximumFrameSize);
      if constexpr(stereo)
      {
        frame.left.reserve(maximumFrameSize);
        frame.right.reserve(maximumFrameSize);
      }
      while(!m_quit.load(std::memory_order_acquire))
      {
        const auto wake = m_wake.load(std::memory_order_acquire);
        if(preparedEpoch != epoch())
          processor.reset();
        auto* job = m_input.readSlot();
        if(!job)
        {
          if(m_quit.load(std::memory_order_acquire))
            break;
          m_wake.wait(wake, std::memory_order_acquire);
          continue;
        }
        if(job->epoch != epoch())
        {
          m_dropped.fetch_add(1, std::memory_order_relaxed);
          m_input.commitRead();
          continue;
        }
        auto* result = m_output.writeSlot();
        if(!result)
        {
          drop();
          processor.reset();
          m_input.commitRead();
          continue;
        }
        swapPayload(frame, job->frame);
        frame.sampleRate = job->frame.sampleRate;
        frame.hopSize = job->frame.hopSize;
        frame.startSample = job->frame.startSample;
        result->epoch = job->epoch;
        result->startSample = frame.startSample;
        if constexpr(Timestamped)
          result->time = (double(frame.startSample) + double(frame.audio.size()))
                         / frame.sampleRate;
        result->valid = false;
        result->error.clear();
        try
        {
          validate(frame);
          if(!processor || preparedEpoch != job->epoch)
          {
            processor.reset();
            processor = std::make_unique<Processor>();
            processor->prepare(frame, job->settings);
            preparedEpoch = job->epoch;
          }
          processor->compute(result->result);
          result->valid = true;
        }
        catch(const std::exception& error)
        {
          result->error = error.what();
          processor.reset();
        }
        catch(...)
        {
          result->error = "Unknown exception in native analysis";
          processor.reset();
        }
        swapPayload(frame, job->frame);
        m_input.commitRead();
        // Publishing an old epoch is harmless: the consumer filters it. Keeping
        // producer indices single-owner avoids reset/flush races.
        m_output.commitWrite();
      }
    }
    catch(...)
    {
      // Resource exhaustion outside a job must still be observable and must not
      // terminate the process from a background thread.
      m_failed.store(true, std::memory_order_release);
      m_epoch.fetch_add(1, std::memory_order_acq_rel);
    }
  }

public:
  bool failed() const noexcept { return m_failed.load(std::memory_order_acquire); }

private:
  SpscQueue<Job, queueCapacity> m_input;
  SpscQueue<Completion, queueCapacity> m_output;
  std::atomic<std::uint64_t> m_epoch{1}, m_dropped{}, m_wake{};
  std::atomic<bool> m_quit{}, m_failed{};
  std::thread m_worker;
};
}
