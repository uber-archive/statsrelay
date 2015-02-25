#include "normalize.h"

#include "log.h"

#include <string.h>

int normalize_carbon(char *key, size_t len) {
    // Removes repeated dots from a key name
    static char normalized_key[8192];
    char *ptr1, *ptr2;
    int cur_pos = 0;
    if (len < 2) {
        return 0;
    }
    ptr1 = key;
    ptr2 = key+1;
    while (ptr2 < key+len) {
        if (*ptr1 == '.') {
            if (*ptr2 != '.') {
                normalized_key[cur_pos] = *ptr1;
                cur_pos++;
                ptr1 = ptr2;
            }
            ptr2++;
        } else {
            normalized_key[cur_pos] = *ptr1;
            cur_pos++;
            ptr1++;
            ptr2++;
        }
    }
    normalized_key[cur_pos] = *ptr1;
    normalized_key[cur_pos+1] = '\0';
    for (int i=0; i < cur_pos+2; i++) {
        key[i] = normalized_key[i];
    }
    return 0;
}
