/* COW's control-key encoding: plain char + 96, matching x11window.c, which is
 * what the distress macro table in data.c is keyed on. */
#include <assert.h>
#include <stdio.h>
static unsigned char cow_ctrl(char plain){ return (unsigned char)(plain + 96); }
int main(void){
    struct { char c; unsigned char want; const char *macro; } t[] = {
        {'o', 0xcf, "ogg"},
        {'t', 0xd4, "taking"},
        {'b', 0xc2, "bomb"},
        {'c', 0xc3, "space_control"},
    };
    for (unsigned i=0;i<sizeof t/sizeof*t;i++){
        unsigned char got = cow_ctrl(t[i].c);
        printf("  ctrl-%c -> 0x%02x (want 0x%02x, macro '%s') %s\n",
               t[i].c, got, t[i].want, t[i].macro,
               got==t[i].want?"ok":"MISMATCH");
        assert(got == t[i].want);
        /* and the old SDL2 behaviour was the raw control code */
        assert((t[i].c & 0x1f) != t[i].want);
    }
    /* every ctrl letter must land above keyaction()'s 32 floor */
    for (char c='a'; c<='z'; c++) assert(cow_ctrl(c) >= 32);
    printf("CTRL KEY ENCODING OK\n");
    return 0;
}
