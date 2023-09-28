#include <stdio.h>

#include "mm_alloc.h"

int main(int argc, char **argv) {
    int *data, *data2, *d1, *d2, *d3, *d4;

    data = (int *) mm_malloc(10000000000000 * sizeof(int));
    if (data == NULL)
        printf("hello null\n");
    printf("malloc sanity test successful!\n");
    return 0;
}