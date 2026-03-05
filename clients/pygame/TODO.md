# Netrek Pygame Client -- Missing Features

Current state: ~75% feature parity with COW (the reference C client).
Core gameplay, network transport (TCP+UDP), rendering, sound, message input,
ship selection, tractor/pressor beams, login flow, refit, short packets,
war declarations, info windows, and player stats are working.

---

## Done (removed from backlog)

- Message input system (smessage two-phase addressing, matches COW)
- Ship selection at outfit (keyboard s/d/c/b/a/g/o/X, remembered across deaths)
- Tractor/pressor beams (toggle, retarget, visual rendering)
- Sound effects (shield, cloak, red alert, explosion, phaser, torp, etc.)
- UDP transport with fallback to TCP
- Short packet decoding (SP_S_*)
- Interactive login flow (name/password/query/makepass, matches COW getname.c)
- Refit during gameplay ('r' then ship key, sends CP_REFIT)
- SP_SHIP_CAP — dashboard uses ship_cap values
- CP_COUP — bound to '^' key
- CP_PRACTR — bound to 'e' key
- War declarations ('w' key, click-to-toggle menu, sends CP_WAR)
- Player/planet info windows ('i' quick info, 'I' extended stats, auto-dismiss)
- SP_STATS / SP_S_STATS storage (13 stats fields on Player, used by info window)

---

## Medium Priority (useful but not blocking)

### Help Window
No built-in keybinding reference. Should render a toggleable overlay listing
all bound keys and their actions. ~100 lines.

### Observer Mode
POBSERV status is tracked but no UI flow to enter observer mode, no special
rendering (follow player, cycle targets).

### RSA Encryption
No encrypted login. Most public servers require RSA; currently only works
against servers with RSA disabled. Blocks connecting to most public servers.

### Metaserver Browser
No server discovery. User must pass --server on command line. Should query
metaserver.netrek.org for active servers.

---

## Low Priority (polish)

### Visual Effects
- Cloak shimmer/distortion (currently just alpha fade)
- Damage glow on hull
- Weapon overheat halo
- Scanner range circle on tactical

### Sound Enhancements
- No volume/mute controls
- No low-fuel or weapon-overheat warning sounds
- No dock/undock sounds

### Army Beaming Display
CP_BEAM works but no visual indicator of armies being transferred.

### Macro System
COW supports KEY## macro definitions in .xtrekrc for multi-action sequences.
No macro recording, playback, or rc parsing for macros.

### Config Gaps
- No window geometry persistence
- No fullscreen toggle
- No server favorites/history
- No sound volume in rc file

### Replay/Demo System
No game recording or playback.

### Screenshot Support
No built-in screenshot key.

### Accessibility
No colorblind modes, high-contrast themes, or font size options.

---

## Protocol Stubs (handlers that are `pass`)

| Packet | What it could do |
|--------|-----------------|
| SP_STATUS | Display tournament mode, army bomb counts, game clock |
| SP_STATS | ~~Done~~ — stored on Player, shown in info window |
| SP_FEATURE | Negotiate feature flags with server |
| SP_GENERIC_32 | Extended server messages |
| SP_RESERVED | Challenge-response (needed for some servers) |
| SP_QUEUE | Display queue position when server is full |

## Client Packets Not Wired to UI

| Packet | What's missing |
|--------|---------------|
| CP_WAR | ~~Done~~ — war toggle menu on 'w' key |
| CP_DOCKPERM | Dock permission response for starbases |
| CP_COPILOT | Co-pilot mode |
| CP_OPTIONS | Client options negotiation |
| CP_SCAN | Scan request |
| CP_RESERVED | Challenge-response reply |
