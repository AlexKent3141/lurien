#ifndef __LURIEN_PROFILER_H__
#define __LURIEN_PROFILER_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lurien
{

struct ScopeOutput;

struct OutputNode
{
  std::list<ScopeOutput> scope_outputs_;
};

struct ScopeOutput : OutputNode
{
  std::string name_;
  std::uint64_t samples_;
  double cpu_proportion_;
};

struct ThreadOutput : OutputNode
{
  std::thread::id thread_id_;
};

struct OutputReceiver
{
public:
  virtual ~OutputReceiver() = default;
  virtual void HandleOutput(
    const ThreadOutput&) const = 0;
};

// This is the default receiver implementation which writes to an std::ostream.
class DefaultOutputReceiver : public OutputReceiver
{
public:
  DefaultOutputReceiver(std::ostream& out) : out_(out) {}
  inline void HandleOutput(
    const ThreadOutput& output) const;

private:
  std::ostream& out_;
  mutable std::mutex output_sync_;

  inline void PrintSubtree(
    const ScopeOutput& scope,
    int depth) const;
};

inline void DefaultOutputReceiver::HandleOutput(
  const ThreadOutput& output) const
{
  std::lock_guard<std::mutex> lk(output_sync_);

  out_ << std::hex << std::showbase;
  out_ << "Thread ID: " << output.thread_id_ << std::endl;
  out_ << std::dec << std::noshowbase;

  // Recursively print the output data.
  for (const auto& child : output.scope_outputs_)
  {
    PrintSubtree(child, 0);
  }
}

inline void DefaultOutputReceiver::PrintSubtree(
  const ScopeOutput& scope,
  int depth) const
{
  out_ << std::string(2*depth, ' ')
       << scope.name_ << " "
       << scope.cpu_proportion_ << std::endl;

  for (const auto& child : scope.scope_outputs_)
  {
    PrintSubtree(child, depth + 1);
  }
}

namespace details
{

class ThreadSamplingData;

inline std::shared_ptr<ThreadSamplingData> CreateSamplingData();

// All variables which need to have external linkage are static members
// of this struct.
struct Ext
{
  inline static std::atomic<bool> keep_sampling = true;
  inline static std::unique_ptr<std::thread> sampling_worker;
  inline static std::mutex sampler_sync;
  inline static std::vector<std::weak_ptr<ThreadSamplingData>> samplers;
  inline thread_local static std::shared_ptr<ThreadSamplingData> thread_data =
    CreateSamplingData();
  inline static std::unique_ptr<OutputReceiver> receiver;
};

class ThreadSamplingData :
  public std::enable_shared_from_this<ThreadSamplingData>
{
public:
  inline ThreadSamplingData();
  inline ~ThreadSamplingData();
  inline void Update(const std::string& name);
  inline void TakeSample();
  
private:
  std::size_t current_scope_hash_;
  std::uint64_t total_samples_;
  std::hash<std::string> hash_fn_;
  std::unordered_map<std::size_t, ScopeOutput*> scope_data_;
  std::mutex sample_sync_;
  ScopeOutput* current_scope_ = nullptr;
  ThreadOutput output_;
};

inline std::shared_ptr<ThreadSamplingData> CreateSamplingData()
{
  auto sampler = std::make_shared<ThreadSamplingData>();
  {
    std::lock_guard<std::mutex> lk(Ext::sampler_sync);
    Ext::samplers.push_back(sampler->weak_from_this());
  }

  return sampler;
}

inline ThreadSamplingData::ThreadSamplingData()
:
  current_scope_hash_(0),
  total_samples_(0)
{
}

// This is where the data this thread has accumulated will be output.
inline ThreadSamplingData::~ThreadSamplingData()
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  output_.thread_id_ = std::this_thread::get_id();

  // Accumulate the probabilities so that an outer scope's usage is at
  // least the sum of its inner scope's usages.
  std::function<std::uint64_t(ScopeOutput&)> accumulate_stats =
    [this, &accumulate_stats] (ScopeOutput& parent)
  {
    for (ScopeOutput& child : parent.scope_outputs_)
    {
      parent.samples_ += accumulate_stats(child);
    }

    parent.cpu_proportion_ = double(parent.samples_) / total_samples_;

    return parent.samples_;
  };

  for (ScopeOutput& child : output_.scope_outputs_)
  {
    accumulate_stats(child);
  }

  Ext::receiver->HandleOutput(output_);
}

inline void ThreadSamplingData::Update(
  const std::string& name)
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  current_scope_hash_ ^= hash_fn_(name);

  if (current_scope_hash_ == 0)
  {
    current_scope_ = nullptr;
  }
  else if (!scope_data_.contains(current_scope_hash_))
  {
    std::size_t parent_hash = current_scope_hash_ ^ hash_fn_(name);

    OutputNode* parent = parent_hash == 0
      ? &output_ : (OutputNode*)scope_data_[parent_hash];

    parent->scope_outputs_.push_back( ScopeOutput { {}, name, 0, 0 });
    current_scope_ = &parent->scope_outputs_.back();
    scope_data_.insert( { current_scope_hash_, current_scope_ } );
  }
  else
  {
    current_scope_ = scope_data_[current_scope_hash_];
  }
}

inline void ThreadSamplingData::TakeSample()
{
  std::lock_guard<std::mutex> lk(sample_sync_);
  if (current_scope_ != nullptr)
  {
    ++current_scope_->samples_;
  }

  ++total_samples_;
}

inline void TakeSamples()
{
  while (Ext::keep_sampling)
  {
    std::lock_guard<std::mutex> lk(Ext::sampler_sync);
    for (auto& sampler : Ext::samplers)
    {
      auto shared_sampler = sampler.lock();
      if (shared_sampler)
      {
        shared_sampler->TakeSample();
      }
    }
  }
}

// Kick off a thread which periodically samples all threads.
inline void Init(std::unique_ptr<OutputReceiver> receiver)
{
  if (!Ext::sampling_worker)
  {
    Ext::receiver = std::move(receiver);
    Ext::sampling_worker = std::make_unique<std::thread>(
      &details::TakeSamples);
  }
}

// Stop sampling.
inline void Stop()
{
  if (Ext::keep_sampling && Ext::sampling_worker)
  {
    Ext::keep_sampling = false;
    Ext::sampling_worker->join();
  }
}

// This object updates the thread local data when it is constructed and
// destructed.
class Scope
{
public:
  inline Scope(const std::string& name);
  inline ~Scope();

private:
  std::string name_;
};

inline Scope::Scope(
  const std::string& name)
:
  name_(name)
{
  Ext::thread_data->Update(name);
}

inline Scope::~Scope()
{
  Ext::thread_data->Update(name_);
}

} // details

#if not defined(LURIEN_ENABLED)
#define LURIEN_INIT(receiver)
#define LURIEN_STOP
#define LURIEN_SCOPE(name)

#else

#define LURIEN_INIT(receiver) \
  lurien::details::Init(receiver);

#define LURIEN_STOP \
  lurien::details::Stop();

#define LURIEN_SCOPE(name) \
  lurien::details::Scope scope_##name(#name);

#endif

} // lurien

#endif // __LURIEN_PROFILER_H__
