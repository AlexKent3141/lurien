#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include <iostream>

namespace lurien_internals
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

class ThreadData
{
public:
  ThreadData();
  ~ThreadData();
  void Update(const std::string& name);
  
private:
  std::size_t current_scope_hash_;
  std::hash<std::string> hash_fn_;
  std::unordered_map<std::size_t, ScopeInfo> scope_data_;
};

ThreadData::ThreadData()
{
  // Add the root scope.
  scope_data_.insert(
  {
    current_scope_hash_,
    ScopeInfo("root", 0)
  });
}

ThreadData::~ThreadData()
{
  // TODO: This is where the data we've accumulated will be output.
  std::cout << "ThreadData d'tor" << std::endl;
  for (const auto& pair : scope_data_)
  {
    const auto& scope_info = pair.second;
    std::cout << pair.first << " "
              << scope_info.name_ << " "
              << scope_info.parent_hash_ << " "
              << scope_info.samples_ << std::endl;
  }
}

void ThreadData::Update(
  const std::string& name)
{
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

thread_local static ThreadData thread_data;

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
  thread_data.Update(name);
}

Scope::~Scope()
{
  thread_data.Update(name_);
}

} // lurien_internals
