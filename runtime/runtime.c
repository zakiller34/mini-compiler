#include <stdint.h>
#include <stdio.h>

/// @brief Read a 64-bit integer from stdin
/// @ensures result == value parsed from stdin
int64_t read_int(void) {
    int64_t val;
    scanf("%ld", &val);
    return val;
}

/// @brief Print a 64-bit integer to stdout followed by newline
/// @requires x is a valid int64_t
void print_int(int64_t x) { printf("%ld\n", x); }
