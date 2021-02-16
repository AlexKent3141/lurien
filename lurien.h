#ifndef __LURIEN_PROFILER_H__
#define __LURIEN_PROFILER_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <iostream>

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
  virtual void HandleOutput(const ThreadOutput&) const = 0;
};

// This is the default receiver implementation which writes to an std::ostream.
class DefaultOutputReceiver : public OutputReceiver
{
public:
  DefaultOutputReceiver(std::ostream& out) : out_(out) {}
  void HandleOutput(const ThreadOutput& output) const;

private:
  std::ostream& out_;

  void PrintSubtree(const ScopeOutput& scope) const;
};

void DefaultOutputReceiver::HandleOutput(const ThreadOutput& output) const
{
  // Recursively print the output data.
  out_ << "ID: " << output.thread_id_ << std::endl;
  for (const auto& child : output.scope_outputs_)
  {
    PrintSubtree(child);
  }
}

void DefaultOutputReceiver::PrintSubtree(const ScopeOutput& scope) const
{
  out_ << scope.name_ << " "
       << scope.cpu_proportion_ << std::endl;

  for (const auto& child : scope.scope_outputs_)
  {
    PrintSubtree(child);
  }
}

namespace details
{
  class ThreadSamplingData;

  std::shared_ptr<ThreadSamplingData> CreateSamplingData();

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
  };

  void TakeSamples();

  std::unique_ptr<OutputReceiver> receiver;
}

// Kick off a thread which periodically samples all threads.
void Init(std::unique_ptr<OutputReceiver> receiver)
{
  if (!details::Ext::sampling_worker)
  {
    details::receiver = std::move(receiver);
    details::Ext::sampling_worker = std::make_unique<std::thread>(
      &details::TakeSamples);
  }
}

// Stop sampling.
void Stop()
{
  if (details::Ext::keep_sampling && details::Ext::sampling_worker)
  {
    details::Ext::keep_sampling = false;
    details::Ext::sampling_worker->join();
  }
}

/* End public interface */

namespace details
{

class ThreadSamplingData :
  public std::enable_shared_from_this<ThreadSamplingData>
{
public:
  ThreadSamplingData();
  ~ThreadSamplingData();
  void Update(const std::string& name);
  void TakeSample();
  
private:
  std::size_t current_scope_hash_;
  std::uint64_t total_samples_;
  std::hash<std::string> hash_fn_;
  std::unordered_map<std::size_t, ScopeOutput*> scope_data_;
  std::mutex sample_sync_;
  ScopeOutput* current_scope_ = nullptr;
  ThreadOutput output_;
};

std::shared_ptr<ThreadSamplingData> CreateSamplingData()
{
  auto sampler = std::make_shared<ThreadSamplingData>();
  {
    std::lock_guard<std::mutex> lk(Ext::sampler_sync);
    Ext::samplers.push_back(sampler->weak_from_this());
  }

  return sampler;
}

ThreadSamplingData::ThreadSamplingData()
:
  current_scope_hash_(0),
  total_samples_(0)
{
}

// This is where the data this thread has accumulated will be output.
ThreadSamplingData::~ThreadSamplingData()
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

  receiver->HandleOutput(output_);
}

void ThreadSamplingData::Update(
  const std::string& name)
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  current_scope_hash_ ^= hash_fn_(name);
  if (!scope_data_.contains(current_scope_hash_))
  {
    std::size_t parent_hash = current_scope_hash_ ^ hash_fn_(name);

    OutputNode* parent = parent_hash == 0
      ? &output_ : (OutputNode*)scope_data_[parent_hash];

    parent->scope_outputs_.push_back( ScopeOutput { {}, name, 0 });
    current_scope_ = &parent->scope_outputs_.back();
    scope_data_.insert( { current_scope_hash_, current_scope_ } );
  }
}

void ThreadSamplingData::TakeSample()
{
  if (current_scope_hash_ != 0)
  {
    std::lock_guard<std::mutex> lk(sample_sync_);
    auto& scope_output = scope_data_.at(current_scope_hash_);
    ++scope_output->samples_;
  }

  ++total_samples_;
}

// This object updates the thread local data when it is constructed and
// destructed.
class Scope
{
public:
  Scope(const std::string& name);
  ~Scope();

private:
  const std::string& name_;
};

Scope::Scope(
  const std::string& name)
:
  name_(name)
{
  Ext::thread_data->Update(name);
}

Scope::~Scope()
{
  Ext::thread_data->Update(name_);
}

void TakeSamples()
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

} // details
} // lurien

#endif // __LURIEN_PROFILER_H__
