#include "lurien.h"

using namespace lurien::internals;

void func()
{
  Scope scope1("outer");
  int count1 = 0;
  int count2 = 0;
  {
    Scope scope2("inner1");

    for (int i = 0; i < 1e8; i++)
    {
      ++count1;
    }
  }
  {
    Scope scope3("inner2");

    for (int i = 0; i < 1e8; i++)
    {
      ++count2;
    }
  }
}

int main()
{
  lurien::Init();

  std::thread t1(&func);
  std::thread t2(&func);
  std::thread t3(&func);
  std::thread t4(&func);
  std::thread t5(&func);

  t1.join();
  t2.join();
  t3.join();
  t4.join();
  t5.join();

  lurien::Stop();

  return 0;
}
