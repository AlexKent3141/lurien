#ifndef __LURIEN_PROFILER_H__
#define __LURIEN_PROFILER_H__

#include <atomic>
#include <cstdint>
#include <functional>
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
  ScopeInfo() = delete;

  ScopeInfo(
    const std::string& name,
    std::size_t parent_hash);

  std::string name_;
  std::size_t parent_hash_;
  std::uint64_t samples_;
};

ScopeInfo::ScopeInfo(
  const std::string& name,
  std::size_t parent_hash)
:
  name_(name),
  parent_hash_(parent_hash),
  samples_(0)
{
}

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
  std::uint64_t samples_outside_scope_;
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
  samples_outside_scope_(0)
{
}

ThreadSamplingData::~ThreadSamplingData()
{
  std::lock_guard<std::mutex> lk(sample_sync_);

  // TODO: This is where the data we've accumulated will be output.
  std::cout << "ThreadSamplingData d'tor" << std::endl;
  std::cout << "Samples outside scope: " << samples_outside_scope_ << std::endl;
  for (const auto& pair : scope_data_)
  {
    const auto& scope_info = pair.second;
    std::cout << pair.first << " "
              << scope_info.name_ << " "
              << scope_info.parent_hash_ << " "
              << scope_info.samples_ << std::endl;
  }
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
      ScopeInfo(name, parent_hash)
    });
  }
}

void ThreadSamplingData::TakeSample()
{
  if (current_scope_hash_ == 0)
  {
    ++samples_outside_scope_;
  }
  else
  {
    std::lock_guard<std::mutex> lk(sample_sync_);
    auto& scope_info = scope_data_.at(current_scope_hash_);
    ++scope_info.samples_;
  }
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
