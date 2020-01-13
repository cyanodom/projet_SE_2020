#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  int i = 12;
  printf("bonjour : %s %d %d %d %d %d\n", "bonjour", &("bonkjour"), &i, i, strlen("bonjour"));
  return EXIT_SUCCESS;
}
