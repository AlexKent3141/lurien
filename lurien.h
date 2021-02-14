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
}

// Kick off a thread which periodically samples all threads.
void Init()
{
  if (!details::Ext::sampling_worker)
  {
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

namespace details
{

struct ScopeInfo
{
  std::string name_;
  std::size_t parent_hash_;
  std::uint64_t samples_;
};

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
  std::unordered_map<std::size_t, ScopeInfo> scope_data_;
  std::mutex sample_sync_;
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

struct ScopeOutput
{
  std::string name_;
  double cpu_proportion_;
  std::uint64_t samples_;
  std::size_t hash_;
  std::list<ScopeOutput> scope_outputs_;
};

struct ThreadOutput
{
  std::thread::id thread_id_;
  std::list<ScopeOutput> scope_outputs_;
};

// This is where the data this thread has accumulated will be output.
ThreadSamplingData::~ThreadSamplingData()
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  ThreadOutput thread_output;
  thread_output.thread_id_ = std::this_thread::get_id();

  if (total_samples_ == 0)
  {
    // TODO: What should happen in this situation?
    return;
  }

  std::list<std::reference_wrapper<ScopeOutput>> leaves;

  auto add_scopes_with_parent =
    [this, &leaves] (
      std::size_t parent_hash,
      std::list<ScopeOutput>& child_scopes)
  {
    for (const auto& pair : scope_data_)
    {
      const ScopeInfo& scope_info = pair.second;
      if (scope_info.parent_hash_ == parent_hash)
      {
        child_scopes.emplace_back(
          ScopeOutput
          {
            scope_info.name_,
            double(scope_info.samples_) / total_samples_,
            scope_info.samples_,
            pair.first
          });

        leaves.push_back(child_scopes.back());
      }
    }
  };

  add_scopes_with_parent(0, thread_output.scope_outputs_);

  while (!leaves.empty())
  {
    ScopeOutput& leaf = leaves.front();
    leaves.pop_front();
    std::cout << leaf.name_ << " "
              << leaf.hash_ << " "
              << leaf.cpu_proportion_ << std::endl;

    add_scopes_with_parent(leaf.hash_, leaf.scope_outputs_);
  }

  // TODO: accumulate the probabilities so that an outer scope's usage is at
  // least the sum of its inner scope's usages.
}

void ThreadSamplingData::Update(
  const std::string& name)
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  current_scope_hash_ ^= hash_fn_(name);
  if (!scope_data_.contains(current_scope_hash_))
  {
    std::size_t parent_hash = current_scope_hash_ ^ hash_fn_(name);
    scope_data_.insert(
    {
      current_scope_hash_,
      ScopeInfo { name, parent_hash, 0 }
    });
  }
}

void ThreadSamplingData::TakeSample()
{
  if (current_scope_hash_ != 0)
  {
    std::lock_guard<std::mutex> lk(sample_sync_);
    auto& scope_info = scope_data_.at(current_scope_hash_);
    ++scope_info.samples_;
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
