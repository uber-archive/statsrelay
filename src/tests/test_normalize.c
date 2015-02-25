#include "../normalize.h"

#include <assert.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char testbuf[8192];

    strncpy(testbuf, "a.b\0", 4);
    normalize_carbon(testbuf, 4);
    assert(testbuf == "a.b\0");

    strncpy(testbuf, "a..b\0", 5);
    normalize_carbon(testbuf, 5);
    assert(testbuf == "a.b\0");

    strncpy(testbuf, "a...b\0", 6);
    normalize_carbon(testbuf, 6);
    assert(testbuf == "a.b\0");

}
