# Packet handler harness

Drives the real `short.c` packet handlers with crafted packets under
AddressSanitizer, including the malformed inputs a cooperating server never
sends. Packets are placed in exact-size heap allocations so ASan red-zones
catch a read or write off either end.

Build the client with ASan into a separate directory, then link the harness
against the instrumented objects:

    SDL2_CFLAGS=$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer)
    make BUILD=build-asan \
      CFLAGS="-g -O1 -fsanitize=address -fno-omit-frame-pointer -Wall \
        -Wno-implicit-function-declaration -Wno-parentheses -Wno-format \
        -Wno-unused-variable -Wno-pointer-sign -I. -I../netrek-client-cow \
        $SDL2_CFLAGS" \
      LDFLAGS="-g -fsanitize=address"

    cc -g -O1 -fsanitize=address -Wno-implicit-function-declaration \
       -Wno-pointer-sign -Wno-format -I. -I../netrek-client-cow \
       $(pkg-config --cflags sdl2) -c tests/packet_harness.c -o /tmp/h.o
    cc -g -fsanitize=address -o /tmp/harness /tmp/h.o \
       build-asan/short.o build-asan/data.o build-asan/sintab.o -lm
    ASAN_OPTIONS=detect_leaks=0 /tmp/harness      # or pass 1, 2, 3

Tests, and what each caught before it was fixed:

1. `handleSMessage` with a maximum declared length and no terminator
   (stack-buffer-overflow: sprintf of a ~250 byte message into `char buf[100]`)
2. `handleVTorp` naming player slot 255
   (heap-buffer-overflow: indexed `torps[255*8]` against a 288 entry array)
3. `handleSWarning` over every subtype crossed with every declared packet
   length (heap-buffer-overflow: STEXTE_STRING sized its allocation from
   `pad3 - 4` and then `strcpy`d an unrelated length into it)

All three abort on the pre-fix code and are clean after.


## Extrapolation test (tests/extrap_test.c)

Calls the real `extrap_apply()` / `extrap_restore()` from `redraw.c`, linked
against the client's own objects. Build the normal client first, then:

    cc -g -O0 -Wno-implicit-function-declaration -Wno-pointer-sign -Wno-format \
       -I. -I../netrek-client-cow $(pkg-config --cflags sdl2) \
       -c tests/extrap_test.c -o /tmp/e.o
    OBJS=$(ls build/*.o | grep -v /main.o)     # bash; zsh does not word-split
    cc -g -o /tmp/extrap_test /tmp/e.o $OBJS \
       $(pkg-config --libs sdl2 SDL2_ttf SDL2_image SDL2_mixer) -lm
    /tmp/extrap_test

It checks the direction convention (dir 0 is north, dir 64 is east), that
torps are left alone (see below), that objects which are not alive stay put,
that restore is exact, that 500 apply/restore cycles accumulate no drift,
that a late packet clamps to one update instead of flinging things away, and
that `extrapolate: off` disables it.

Ships are reckoned but torps are not. Short packets carry a ship's direction
and speed (`handleVPlayer` sets `p_dir` and `p_speed`, direction quantised to
16 steps), but the short torp handler `handleVTorp` sends position deltas
only and never sets `t_dir`. A torp's stored direction is therefore stale, or
belongs to whatever previously occupied that slot, and reckoning along it
sends torps off at visibly wrong angles. Smoothing torps properly means
deriving velocity from successive reported positions instead.


## Font advance test (tests/font_advance_test.c)

    cc -o /tmp/ft tests/font_advance_test.c $(pkg-config --cflags --libs sdl2 SDL2_ttf)
    /tmp/ft

Bold must have the same advance as regular. The player list draws some rows
bold and some not, so if the two disagree the columns drift apart across the
line. SDL_ttf's TTF_STYLE_BOLD is synthetic emboldening and widens every
glyph: measured on Menlo, 20 characters come to 240px regular and 280px
synthetic bold, 2px of drift per character. The real bold face in the
collection measures 240px, the same as regular, so that is what the client
opens.
