# AoS2 reverse-engineering handbook (read this first)

Goal: a **1:1 reimplementation** of Acceleration of SUGURI 2's gameplay in portable C for a 3DS
port. Everything is derived from the PC binary `aos2.exe` (32-bit x86, MSVC, image base 0x400000).
Never guess a value — every number in a spec must cite where it came from (address).

## Tools (all project-local, Windows + Git Bash)

Paths are relative to `aos2_3ds/tools/re/`.

| Tool | Use |
|---|---|
| `dec/all.c` | Ghidra decompilation of all 12,871 functions (17 MB). Grep it. |
| `./fn.sh <hexaddr>...` | Print one function from the corpus, e.g. `./fn.sh 7571b0`. |
| `./xref.exe "$EXE" rd <hexva> <n>` | Read n dwords at a VA as hex/int/float/double. Constants like `DAT_008145f0` → `rd 8145f0 1`. BSS prints "(unmapped or bss)" = runtime-initialised; find the code that writes it. |
| `./xref.exe "$EXE" refs <hexva>` | Code locations whose bytes contain that imm32 (finds data/func-pointer uses). |
| `./xref.exe "$EXE" dis <hexva> <n>` | Raw disassembly (when the decompiler output is unclear, e.g. x87 float ops). |
| `./jtscan.exe [-only list] "$EXE" dec/all.c` | Every switch jump table of a function (cases → targets, first instructions). **Run it on any function you port that has a switch**: Ghidra sometimes collapses a switch into `x < N ? c0 : dflt` (FUN_0069cc40 HP). `-tsv` = whole-exe summary (`jtscan/jumptables.tsv`), `-fcmp` = float-compare idioms. See `../../docs/re/switch_audit.md`. |
| `powershell -File ghidra.ps1 decomp <listfile> <outfile>` | Re-decompile specific functions (list = hex addresses, one per line). Slow (~1 min startup) — prefer the corpus. |
| `out/functions.tsv`, `out/strings.tsv` | Function index; strings with referencing functions. |
| `dec/registry.c` | `FUN_0049ae40`: registers every per-state / per-character function pointer. |
| `dec/charindex.txt` | Per-character state IDs and handler addresses (see caveat below). |
| `manual.txt`, `../../docs/MOVELISTS.md` | Official move names/inputs per character. |

`EXE="$(head -n1 game_path.txt)/aos2.exe"`

Ghidra float notes: `float10` = x87 80-bit temp; `DAT_xxxxxxxx` used as float = read it with `rd`.
`(&DAT_00881f80 + charId*0x2dc)` = per-character stats record (see below).

## Engine architecture (established)

- **Player object** (`param_1` / `param_2` in handlers), fields known so far:
  | Offset | Meaning |
  |---|---|
  | +0x10 | character id (index into stats array) |
  | +0x1c, +0x20, +0x24, +0x28 | directional input states (+0x1c → x+, +0x20 → x−, +0x24 → y+, +0x28 → y−; confirm screen orientation) |
  | +0x34, +0x38, +0x3c, +0x40, +0x44, +0x48 | button states. Value 3 = pressed this frame, >1 = held (exact encoding TBD). Suguri's input fn suggests +0x38 = A, +0x3c = B, +0x40 = Special, +0x44 = Hyper, +0x34 = Dash, +0x48 = ? — **verify** |
  | +0x6c, +0x70 | position x, y (float) |
  | +0x84 | rotation/angle-ish (set from DAT_00853b58 when not facing) |
  | +0x8c | facing flag (byte) |
  | +0x150 | current state id |
  | +0x154 | frames in current state |
  | +0x158 | byte flag reset on state change |
- **State machine.** Two global tables indexed by state id (0..603):
  - `T2 = 0x8809a0 + 4*id` — per-frame **update** handler `bool f(player*)`.
  - `T1 = 0x880030 + 4*id` — per-frame **draw** handler (picks sprite frame from timer, positions it).
  - Changing state: `p->state = id; p->timer = 0; p->flag158 = 0; T2[id](p);` (the new state's
    update runs immediately in the same frame).
- **Per-character tables** (16 entries, index = character id below):
  - `0x88131c + 4*c` — input/dispatch fn: reads buttons, picks the next state (Suguri: `FUN_0075a030`).
  - `0x88136c + 4*c` — second per-char fn (role TBD).
  - `0x881430 + 0x10*c` — 3 fns (role TBD).
  - `0x8813bc + 4*c` — resource loader (sprites; registers sprite IDs).
  - More 16-entry tables at 0x8816a4, 0x8816f4, 0x881744, 0x881884, 0x8818d4, 0x881924, 0x881974,
    0x8819c4…0x881b40, 0x881ba4, 0x881bf4 (roles TBD — likely AI / UI / story).
- **Per-character stats**: `DAT_00881f80 + c*0x2dc` (0x13 records, BSS — initialised at runtime).
  Offset 0 = movement speed per frame (added to x/y while a direction is held).

### Character ids and state ranges

> **CORRECTION (from suguri.md):** the engine character index stored at player+0x10 is
> **table id + 3** — every per-character table has 3 leading slots. So the real table bases are
> input fn `0x881310 + 4*e`, loader `0x8813b0 + 4*e`, second fn `0x881360 + 4*e`, triple
> `0x881400 + 0x10*e`, stats record `0x881f80 + e*0x2dc`, where **e = id + 3** (Suguri e = 10,
> stats at 0x883c18). The "id" column below is the table position used in this handbook; add 3
> for the engine index. Addresses of the functions listed are unaffected.

| id | Character | prefix | player states | loader | input fn |
|---|---|---|---|---|---|
| — | common | — | 0x00–0x06 | — | — |
| 0 | Sora | sr | 0x08–0x21 | 7333a0 | 73b470 |
| 1 | Alte | at | 0x32–0x4e | 69d5c0 | 6a7440 |
| 2 | Tsih | ts | 0x5b–0x77 | 780f20 | 786e70 |
| 3 | Mira | mr | 0x7c–0x9d | 6f2980 | 6fbb60 |
| 4 | Sham | sm | 0xa7–0xbe | 727e40 | 72e300 |
| 5 | Nath | nt | 0xc8–0xf0 | 70bf00 | 715cb0 |
| 6 | Star Breaker | sb | 0x101–0x11d | 743b30 | 74b900 |
| 7 | Suguri | sg | 0x128–0x143 | 751e40 | 75a030 |
| 8 | Saki | sk | 0x153–0x169 | 71cf70 | 722ce0 |
| 9 | Iru | ir | 0x16e–0x184 | 6cd8b0 | 6d3910 |
| 10 | Nanako | nk | 0x18d–0x1a3 | 7028c0 | 707380 |
| 11 | Kae | ke | 0x1aa–0x1c7 | 6d86a0 | 6e0490 |
| 12 | Kyoko | ky | 0x1d1–0x1e7 | 6e6ba0 | 6ec830 |
| 13 | Hime | hm | 0x1f0–0x207 | 6c1340 | 6c85d0 |
| 14 | Sumika | sa | 0x211–0x22a | 764930 | 77a950 |
| 15 | Sumika 2nd form | sab | 0x235–0x24e | 769460 | 771d10 |

**Caveat on `dec/charindex.txt`:** its "objects:" lists are T1 (draw) handlers, and because each
character's draw handlers are laid out at the *start of the next character's code block*, the
"objects" printed under character X belong to the character whose state ids they carry. Use the
state-id ranges above, and look up handlers by id: T2 = `rd`/registry at 0x8809a0+4*id,
T1 at 0x880030+4*id (or grep `dec/registry.c` for the DAT address).

Known constants: `DAT_008145f0 = 300.0` (Suguri sword close/long range distance threshold),
`DAT_00853b34 = 1.0`, `DAT_00853b38 = 1.1`.

## Output conventions for specs

Write findings to `docs/re/<topic>.md`. For every mechanic: what it does, exact numbers, frame
timings, and the source address(es). Name struct fields you discover and add them to a
"fields" table; keep Ghidra names (`FUN_xxx`) next to your names. Mark anything uncertain
**UNVERIFIED** with what would settle it. Do not modify `dec/`, `out/` or the Ghidra project.
