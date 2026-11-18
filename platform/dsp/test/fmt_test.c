/* gp5_fmt.h against the C library's snprintf.   cc -std=c99 -Wall -I.. fmt_test.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include "gp5_fmt.h"
static int fails;
#define T(want, ...) do { char a[64], b[64]; gp5_fmt(a, sizeof a, __VA_ARGS__); snprintf(b, sizeof b, __VA_ARGS__); \
    if (strcmp(a, want ? want : b)) { fails++; printf("FAIL %-12s got [%s] want [%s]\n", #__VA_ARGS__, a, want ? want : b); } } while (0)
int main(void)
{
    T(NULL, "%d|%d|%d", 0, -42, 2147483647); T(NULL, "%d", (int)0x80000000); T(NULL, "%u|%x|%X", 4000000000u, 0xbeefu, 0xbeefu);
    T(NULL, "%5d|%-3s|%05d|%08x", 42, "a", -42, 0x1234u); T(NULL, "%c%c%%", 'o', 'k'); T(NULL, "%s", "hello world");
    T(NULL, "%.2f|%.0f|%f", 3.14159, 2.4, -0.0625); T(NULL, "%.3f", 440.0); T(NULL, "%8.2f", 1.5); T(NULL, "%lu", 123456ul);
    T("[(null)]", "[%s]", (char *)0);
    { char a[6]; unsigned n = gp5_fmt(a, sizeof a, "%s", "truncated"); if (n != 5 || strcmp(a, "trunc")) { fails++; printf("FAIL truncation [%s] %u\n", a, n); } }
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
