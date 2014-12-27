#include <assert.h>
#include <string.h>

#include "../hashlib.h"


int main(int argc, char **argv) {
	assert(stats_hash("apple", strlen("apple"), UINT32_MAX) == 2699884538l);
	assert(stats_hash("banana", strlen("banana"), UINT32_MAX) == 558421143l);
	assert(stats_hash("orange", strlen("orange"), UINT32_MAX) == 2279140812l);
	assert(stats_hash("lemon", strlen("lemon"), UINT32_MAX) == 4183924513l);
	return 0;
}
