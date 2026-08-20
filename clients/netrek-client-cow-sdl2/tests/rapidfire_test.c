/* Mirrors the rapidFire gate in synth_held_button(): given a hold duration
 * and interval, how many shots come out, and does release stop it. */
#include <assert.h>
#include <stdio.h>
static int rapidFire, held_button; static unsigned held_next, now;
static int due(void) {                    /* the gate under test */
    if (!rapidFire || !held_button) return 0;
    if (now < held_next) return 0;
    held_next = now + (unsigned) rapidFire;
    return 1;
}
static int hold_ms(int ms, int interval) {
    int shots = 0; rapidFire = interval; held_button = 1;
    now = 0; held_next = now + interval;          /* set on BUTTONDOWN */
    for (now = 0; now <= (unsigned) ms; now++) if (due()) shots++;
    return shots;
}
int main(void) {
    /* off by default: a held button must fire exactly once (the real event) */
    rapidFire = 0; held_button = 1; now = 100; held_next = 0;
    assert(!due());
    printf("rapidFire=0: no repeats\n");

    /* 100ms interval over a 1s hold = ~10 extra shots */
    int n = hold_ms(1000, 100);
    printf("held 1000ms at 100ms interval -> %d repeats\n", n);
    assert(n >= 9 && n <= 11);

    /* faster interval, more shots, still bounded */
    n = hold_ms(1000, 50);
    printf("held 1000ms at  50ms interval -> %d repeats\n", n);
    assert(n >= 19 && n <= 21);

    /* release stops it immediately */
    rapidFire = 100; held_button = 0; now = 99999; held_next = 0;
    assert(!due());
    printf("released: no repeats\n");

    printf("RAPIDFIRE GATE OK\n");
    return 0;
}
