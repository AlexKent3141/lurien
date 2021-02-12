#include "lurien.h"

using namespace lurien_internals;

int main()
{
  Scope scope1("outer");
  {
    Scope scope2("inner1");
  }
  {
    Scope scope3("inner2");
  }

  return 0;
}
