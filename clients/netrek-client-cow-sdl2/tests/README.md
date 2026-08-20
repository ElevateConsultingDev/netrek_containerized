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
