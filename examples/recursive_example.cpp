#include <iostream>
#include <memory>

#define LURIEN_ENABLED
#include "lurien/lurien.h"

int recursive_func(int depth)
{
  LURIEN_SCOPE(recursive)
  if (depth == 0) return 0;
  return recursive_func(depth - 1) + depth * depth;
}

void func()
{
  LURIEN_SCOPE(func)
  int total = 0;
  for (int i = 0; i < 1000; i++)
  {
    total += recursive_func(1000);
  }

  std::cout << total << "\n";
}

int main()
{
  LURIEN_INIT(std::make_unique<lurien::DefaultOutputReceiver>(std::cout));

  func();

  LURIEN_STOP
}
