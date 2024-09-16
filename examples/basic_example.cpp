#include <iostream>
#include <memory>
#include <thread>

#define LURIEN_ENABLED
#include "lurien/lurien.h"

const int TARGET = 1e8;

void func2(int* count)
{
  LURIEN_SCOPE(func2)

  for (int i = 0; i < TARGET; i++)
  {
    ++*count;
  }
}

void func()
{
  LURIEN_SCOPE(outer)

  int count = 0;

  {
    LURIEN_SCOPE(inner2)

    for (int i = 0; i < TARGET / 2; i++)
    {
      ++count;
    }

    {
      LURIEN_SCOPE(inner3)

      for (int i = 0; i < TARGET / 10; i++)
      {
        ++count;
      }
    }
  }

  for (int i = 0; i < 2; i++)
  {
    func2(&count);
  }
}

int main()
{
  LURIEN_INIT(std::make_unique<lurien::DefaultOutputReceiver>(std::cout));

  std::thread t1(&func);
  std::thread t2(&func);
  std::thread t3(&func);

  t1.join();
  t2.join();
  t3.join();

  LURIEN_STOP

  return 0;
}
