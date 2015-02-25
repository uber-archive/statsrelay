#include "../normalize.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    char testbuf[8192];

    strcpy(testbuf, "a.b\0");
    normalize_carbon(testbuf, 4);
    assert(strcmp(testbuf, "a.b\0") == 0);

    strcpy(testbuf, "a..b\0");
    normalize_carbon(testbuf, 5);
    assert(strcmp(testbuf, "a.b\0") == 0);

    strcpy(testbuf, "a...b\0");
    normalize_carbon(testbuf, 6);
    assert(strcmp(testbuf, "a.b\0") == 0);

    strcpy(testbuf, "a...\0");
    normalize_carbon(testbuf, 5);
    assert(strcmp(testbuf, "a.\0") == 0);

    strcpy(testbuf, "a.b.c\0");
    normalize_carbon(testbuf, 6);
    assert(strcmp(testbuf, "a.b.c\0") == 0);

    strcpy(testbuf, "a..b..c\0");
    normalize_carbon(testbuf, 8);
    assert(strcmp(testbuf, "a.b.c\0") == 0);

    strcpy(testbuf, "a....b..c\0");
    normalize_carbon(testbuf, 10);
    assert(strcmp(testbuf, "a.b.c\0") == 0);

}
