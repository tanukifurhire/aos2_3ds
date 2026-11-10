# Music selection, BGM flow and SE rules

Sources: `tools/re/dec/all.c` (Ghidra), `xref.exe dis/rd` on aos2.exe and Ayame.dll, `extracted/script/en/bgm.txt`.
Port: `src/app/music.c` (director), `src/platform/bgm.c` (stream + Ayame volume law), `src/app/audio.c` (SE rules).

## 1. BGM manager (FUN_004bdc80 init … FUN_004bf470)

Globals:

| addr | meaning |
|---|---|
| `DAT_0087a528` | current id (0 = none) — `FUN_004bf470` returns it |
| `DAT_0087a52c` | requested id |
| `DAT_0087a530` | requested loop flag (byte) |
| `DAT_0087a534` | fade counter, 100 = full volume |
| `DAT_0087a538` | suspended (window inactive); requests are parked in `DAT_0087a53c/540/541` |
| `DAT_0087a544` | Ayame stream object (vtable: +4 play once, +8 play looped (count −1, loop start), +0xc stop, +0x14 volume, +0x1c release, +0x24 ?) |
| `DAT_0087a558 + 12*(id%1000)` | archive offset/size; `+0x560` loop start (table built by FUN_004bdc80 → `bgm_table.tsv`) |
| `DAT_00881ea8` | BGM volume level 0..10 (default 5, ConfigData Init FUN_004db370) |
| `DAT_0086c000` | level → scale `{0, .3, .5, .6, .65, .7, .75, .85, .9, .95, 1.0}` |
| `DAT_00814408` | 1000.0 (volume clamp, never reached) |

API:
- **`FUN_004bee00(id, restart, loop)`** — request. If `restart && cur == id` → `cur = 0` (forces an immediate restart). Then `req = id, loop = loop` (or parked while suspended).
- **`FUN_004bee60(id, loop)`** — `cur = 0; req = id` → switch on the next update **without fade**.
- **`FUN_004beeb0()`** — once per main-loop frame (called from FUN_006325f0 after the scene update and after the SE update FUN_007bcc00):
  ```
  if (req != cur) {
    if (fade > 0) { fade--; if (stream) stream.volume(fade * scale[lvl]); }
    if (fade < 1 || cur == 0) {
      cur = req; stop+release old stream;
      if (cur != 0) {
        open stream from bgm.dat / bgm_add (0x15..0x18) / bgm_realism (0x1a..0x1c) /
          bgm_suguri (0x1d..0x27) / bgm_hoshi (0x28..0x2f) / bgm_trancenation (0x30..0x40);
        if (lvl != 0) { play once (+4) or looped from loop start (+8); fade = 100;
                        stream.volume(clamp(scale[lvl] * 100, 0, 1000)); }
      }
    }
  }
  ```
  So a change of track = **linear fade-out over 100 frames** (counter 100→0, volume = counter·scale) and then the new track starts **at full volume with no fade-in**. Requesting the id that is already current stops a running fade where it is (quirk, kept). With BGM level 0 the stream is opened but never played.
- **`FUN_004bf2e0()`** — apply volume now: `counter * scale[lvl]` (called by the config menu FUN_00647fd0 on BGM ± and by resume).
- `FUN_004bf360(c)` sets the counter directly — no callers.
- `FUN_004bf3c0` / `FUN_004bf420` — mute + park requests / restore + replay parked request; called only by the window focus callbacks FUN_00633580 / FUN_006335b0 when "play audio when inactive" (`DAT_00881f5c`) is 1 or 3. Not a game pause.

**Ayame volume law** (Ayame.dll, vtable `0x1002a884`, +0x14 = `0x100224d0` posts message 4, applied by `0x10022950` → `IDirectSoundBuffer::SetVolume` (+0x3c)): `v >= 100 → 0`, `v <= 0 → −10000`, else `−pow(100, log10(100 − v)) = −(100 − v)²` hundredths of dB (`fldlg2` → log10; `100238b0` = pow). Linear gain = `10^(−(100−v)²/2000)`.

| BGM level | 0 | 1 | 2 | 3 | 4 | **5** | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| v at full | 0 | 30 | 50 | 60 | 65 | 70 | 75 | 85 | 90 | 95 | 100 |
| dB | off | −49 | −25 | −16 | −12.25 | **−9** | −6.25 | −2.25 | −1 | −0.25 | 0 |

## 2. VS-mode BGM select (stage select screen)

Help text `ANNOUNCE_CHARACTERSELECT_STAGE`: "Choose a stage (left/right) and BGM (up/down)."

**List** `DAT_009b8dd4` (std::vector<int>, online mode 3 uses the received copy `DAT_009b8e04`), built in Select Init **FUN_0079acc0** (disasm 0x79b713..0x79bb..):
1. `10` — "????" (`BGM_RAMDOM`), always first.
2. Base tracks if unlocked (`byte 0x87b753 + id`): `0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x14, 0x13, 0x15` (note 0x14 before 0x13).
   Unlocks: save-data init FUN_007c4480 sets 11,12,13,15,16,17,18; `0x0e` (b761) = arcade cleared with ≥5 characters (or play counter `DAT_0087b76c` > 0x59), `0x14` (b767) ≥10 characters (or > 0x77) — both in FUN_004a8390 (arcade end); `0x13` (b766) story cleared with ≥7, `0x15` (b768) ≥10 — FUN_007c4480.
3. DLC packs if owned (`FUN_007c4b10(appid)`): Realism `0x78515` → 0x1a..0x1c; SUGURI the Best `0x78516` → 0x1d..0x27; Hoshi no Kakera `0x17c924` → 0x28..0x2f; TRANCENATION `0x17c91a` → 0x30..0x40.

**Cursor** `DAT_009b6adc`: set to the index of entry 10 (= 0) when Select Init comes from the menu (`DAT_00885874 == 4`), else kept (rematch keeps the choice). Stage-select substate 200 (FUN_0079c110): up/down = −1/+1 with wrap (`<0 → n−1`, `>=n → 0`), each plays SE 2 (cursor5). Confirm → substate 300, SE 0, `FUN_004bee00(0,1,1)` (fade the select music).

**Name shown**: text id `0x208 + (id <= 0x19 ? id − 10 : id − 14)` (FUN_007aabb0 draw; same formula in Game Init FUN_005f4650 for the in-battle "now playing" caption). Text ids 0x208..0x23a ↔ `BGM_*` keys (strings 0x823c7c.., registered at 0x63d8a9..).

**Choice** at Select Exit **FUN_0079bfc0**: `bgm = list[sel] == 10 ? FUN_007aaed0() : list[sel]`, stored in `DAT_0087fae0` (stage in `DAT_0087fadc`), then `FUN_004bee00(0,1,1)`.

**Random** FUN_007aaed0: `r = FUN_00459f80(0, 100)` (rand_int inclusive) called outside the tick, i.e. on the **cosmetic** MT `0x875b68` (random_device-seeded) — the game RNG `0x876ef0` is untouched. `bgm = list[(uint)r % (count − 1) + 1]` (never "????"). Rolled again at every Select Exit (each new match after the select screen).

Not in VS: there is **no per-stage or per-character default** for VS. Arcade/story (FUN_004a7910 case 0): `bgm = script entry +0x18`, or if 0 the opponent's record `DAT_00882258 + char*0x2dc` (character table +0x2d8, 0 in the shipped data per core.md). Online lobby music player FUN_005c54f0/FUN_005d0d10 cycles the same list (1..n−1) while 0x17 plays.

## 3. Scene music (ids → `bgm_table.tsv`)

| id | file | where | call |
|---|---|---|---|
| 0x18 (24) | bgm_add_02 | main menu: Menu Init FUN_00642190 (3 paths) and title→menu (FUN_00643150 state 10, 0x643451) | `FUN_004bee00(0x18,1,1)` |
| — | — | title screen (Menu state 10) | silent |
| 0x16 (22) | bgm_add_00 (loop 942005) | VS Select Init FUN_0079acc0, ArcSelect Init FUN_0079df00, net/story selects (0x79bf8a, 0x7b9fdd, 0x7ba1b5, 0x7bb05b, 0x5cfb98, 0x5d105f) | `FUN_004bee60(0x16,1)` (restart, no fade) |
| 0x17 (23) | bgm_add_01 (loop 2722597) | online lobby FUN_007b6630 (0x7b673b); NetSelect exit 0x5a107f | `FUN_004bee60(0x17,1)` / `bee00(0x17,1,1)` |
| 2 | bgm_01 | "VS" screen: stage select substate 300 after 100 frames (+0.01/frame on `DAT_009b8ca4` to 1.0) → substate 0x136; also FUN_00597f40, FUN_007aefa0 | `FUN_004bee00(2,1,0)` **once** |
| battle | `DAT_0087fae0` | Game Init FUN_005f4650 **only for a new match** (`DAT_00885879`), 0x5f5677; online: FUN_006124b0 when peers are ready | `FUN_004bee00(bgm,1,1)` |
| 1 | bgm_00 | result screen Result Init FUN_00796030 (0x79658b) | `FUN_004bee60(1,0)` **once** |
| 0x15 / 4 | bgm_add_03 / bgm_03 | ending roll FUN_00519ce0 (0x15 if `DAT_0087faa8==1 && DAT_0087fac4==7`) | `bee00(..,1,1)` |

VS timeline after confirming the stage (frames at 60 Hz): t0 request 0 → select music fades 100 f → t100 jingle 2 starts (once) → VS screen 101 f (0x136, `DAT_009b6a34 > 100`) → transition 2 (rate 0.01) → Select Exit requests 0 (jingle fades 100 f) → loading → Game Init requests the battle track, which starts at full volume as soon as the jingle fade is over.

## 4. Battle: rounds, KO, pause, end

- **Rounds / KO / round end**: no BGM calls; the track keeps playing across rounds (Game Init requests it only when `DAT_00885879` = new match). No KO or victory jingle in battle, **no announcer voices** (the `ANNOUNCE_*` strings are menu help lines; se*.dat contain no voices).
- **Pause menu**: no BGM change.
- **Match end** FUN_00608a00 (0x608ad4): when the round-end banner counter `DAT_0087fb5c >= 0x12d`, alpha 0, and FUN_00619c60 (match decided): `FUN_00561fc0(0x12)` wipe + `FUN_004bee00(0,1,1)` → 100-frame fade-out, then the result screen forces jingle 1.
- **Game Exit** FUN_005f5780 (0x5f583c): `FUN_004bee00(0,1,0)` fade-out. Online disconnect FUN_006072d0 (0x607e2e): `(0,1,1)`.
- Result screen confirm (FUN_00796ef0): `(0,1,1)` fade, then back to select (0x16 restart).

## 5. Port implementation (music.c) and approximations

- `music_request/force/update/set_level` = FUN_004bee00 / FUN_004bee60 / FUN_004beeb0 / FUN_004bf2e0, same counters; `bgm_set_ayame_volume` applies the Ayame law; `bgm_play_ex(id, loop)` = Ayame +4/+8.
- Menu = the VS select screen: `music_scene_select()` = force 0x16 on start-up and whenever the menu is re-entered (after a match, netplay, input lab). The main-menu track 0x18 has no screen in the port.
- BGM select in the port menu: **L/R** = the original's up/down (wrap, SE 2); default "????". Circle pad up/down = BGM level, left/right = SE level (0..10, default 5, SE 2 on change; not saved).
- List: all base tracks treated as unlocked (no save data in the port); a DLC pack is listed when its first track exists in romfs (stands in for Steam ownership).
- Random: own MT19937 seeded from the system tick (stand-in for the random_device-seeded cosmetic engine); never the game RNG.
- **Approximated**: the port has no stage-select/VS/result screens. Match start forces the battle track (same audible result as the original, where the jingle has already faded out by Game Init); jingles 1 and 2 are not played. At match end the fade-out starts with the 0x12 wipe exactly as FUN_00608a00 (`music_watch` watches `w->flow`), and when the app starts the next match it restarts the battle track (local: re-rolled if "????", like a new Select Exit).
- **Netplay**: like the original online mode the host decides: START (`rb_start.bgm`, packet byte 46, `RB_PROTO_VERSION` 2) carries the host's resolved id; both consoles play it and restart it each new match (no re-roll in a session).

## 6. Sound effects (FUN_007bc9c0 / FUN_007bcaf0 / FUN_007bcc00)

| addr | meaning |
|---|---|
| `DAT_009b99a0[61]` | sample handle per id (−1 = missing → nothing happens, no lockout) |
| `DAT_009b9ad4[61]` | lockout counter (frames) |
| `DAT_009b9a94[61]` | "allowed this frame" byte |
| `DAT_00885a8c / DAT_00893eac[61]`, `DAT_00893ea8` | netplay rollback replay suppression (play counts, last frame); the port's rb_take_sounds does this |
| `DAT_00881eaa` | SE level 0..10, default 5; 0 = SE off |

- **FUN_007bc9c0(id)**: plays if level ≠ 0, handle ≠ −1, (window active or audio-when-inactive) and `lockout[id] < 1`; then `allowed = 0, lockout = 2`.
- **FUN_007bcaf0(id, n)**: plays if `allowed[id]` and `lockout[id] == 0`; then `allowed = 0, lockout = n`. Call-site lockouts range 1..0x14 (e.g. 0x13 hit with 3/4/5, 0x14 with 0x10, 0x1d with 0x14); they vary per call site, so the sim now records them (`aos_world.se_cd`, `AOS_SE_CD_DEFAULT` for FUN_007bc9c0).
- **FUN_007bcc00** (main loop, after the scene update): `allowed[] = 1`, `lockout[i]--` if > 0. Net effect: a FUN_007bc9c0 sound can retrigger every 2nd frame; a FUN_007bcaf0(id, n) sound every n-th frame.
- **Voices** (FUN_00515800 / FUN_00517420): each id has `voices` (se_table.h, ≤ 10) DirectSound buffers; a play stops and restarts the *next* buffer of that id (round robin `DAT_00899ae8[id]`), so an id never exceeds its voice count and the oldest instance of that id is cut.
- **Volume** (FUN_007bc800 / FUN_007bc8a0 → FUN_00518f80 reset + FUN_00518ce0 add): `−(int)(50 − 50·t[lvl])` dB (SSE single precision, `DAT_008140ec` = 50, `t` = `DAT_0086c890` = same values as §1):

| SE level | 0 | 1 | 2 | 3 | 4 | **5** | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| dB | off | −35 | −25 | −19 | −17 | **−15** | −12 | −7 | −5 | −2 | 0 |

Port (audio.c): all of the above; the only extra rule is the 22-channel NDSP limit (a free channel, else the oldest-started voice is stolen). Known difference: the sim keeps one request per id per frame (first wins), so FUN_007bcaf0(id, 0) followed by FUN_007bc9c0(id) in the same frame (would play twice in the original) plays once — no call site uses lockout 0.
