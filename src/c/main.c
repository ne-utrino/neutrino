#include <stdio.h>
#include "value.h"

int main(int argc, char *argv[]) {
  value_ptr_t v = new_integer(10);
  printf("%i\n", get_value_tag(v));
  return 0;
}