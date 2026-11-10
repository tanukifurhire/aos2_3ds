# Menus, story mode and arcade mode (modes.md)

Everything outside the fight that a 1:1 port of the **offline** PC game needs: scene machine, title/menus,
selects, VS/result/continue, arcade ladder, story sequencer + conversation scene, endings/staff roll, text.
Source: `aos2.exe` (Steam build), decompile corpus `tools/re/dec/all.c`; addresses are VAs. Anything not proven
is marked **UNVERIFIED**. Companion tables (TSV, all in `docs/re/`): see §0.6.

Implementation split (one agent each; §0 is shared and must be read by all):

| Part | Sections | Scope |
|---|---|---|
| A | §0, §A | text/font renderer, menu framework + input, logo, title, main menu, sub-menus, config |
| B | §0, §B | arcade/story character select, VS select (chars/colours/stage/BGM), VS screen, loading, result, continue |
| C | §0, §C | story conversation scene: script loader (exe resources), sequencer hooks, portraits, text box, typewriter |
| D | §0, §D | arcade + story ladders/sequencer, CPU difficulty, scoring/unlocks, endings, staff roll, mode effects on the sim |

## 0. Shared framework

### 0.1 Scene machine

Main loop `FUN_006325f0`: one scene function per frame, selected by **`DAT_00885870`**, then (always, in this
order) `FUN_00495040` (announce/help line update), `FUN_007bcc00` (SE lockouts), `FUN_004beeb0` (BGM manager),
`FUN_00561c70` (screen-transition update). Each scene function calls its Init when **`DAT_00885878`** ("init
pending") is set and calls its Exit itself when done; Exit writes the next scene id into `DAT_00885870`, sets
`DAT_00885878 = 1` and stores its own id in **`DAT_00885874`** (previous scene; several Inits branch on it, e.g.
Select Init resets the BGM cursor only when coming from the menu, `DAT_00885874 == 4`).

| id | Scene | update | Init / Exit | Port owner |
|---:|---|---|---|---|
| 1 | Logo | `FUN_006321a0` | `FUN_00632120` / `FUN_00632160` (→ 4) | A |
| 2 | Battle | `FUN_006124b0` | Game Init `FUN_005f4650` / Game Exit `FUN_005f5780` | existing (src/sim/flow.c) + D |
| 3 | Result (+ continue) | `FUN_00796ef0` | `FUN_00796030` / `FUN_00796ca0` | B |
| 4 | Menu (title + main menu + sub-menus + config) | `FUN_00643150` | `FUN_00642190` / `FUN_00642880` | A |
| 5 | Conversation (story) | `FUN_004e3a20` | `FUN_004e35e0(scriptId)` / `FUN_004e3730` | C |
| 6 | VS Select (match/training) | `FUN_0079c110` | `FUN_0079acc0` / `FUN_0079bfc0` | B |
| 7 | NetSelect (online) | `FUN_007aefa0` | `FUN_007affe0` / `FUN_007b0d60` | — (port has its own UDS lobby) |
| 8 | ArcSelect (arcade/story char select) | `FUN_0079e590` | `FUN_0079df00` / `FUN_0079e4b0` | B |
| 9 | Transit | `FUN_007c4c90` | `FUN_007c4c20` | — (UNVERIFIED role; online) |
| 10 | Memory (replays) | `FUN_006338b0` | `FUN_00633640` / `FUN_00633720` | — |
| 11 | Endroll (ending + staff roll) | `FUN_0051a060` | `FUN_00519ce0` / `FUN_00519f60` | D |
| 12 | quit | `FUN_0045f3c0` | — | A (return to HOME/exit) |
| 13 | online select | `FUN_00597f40` | `FUN_0059a180` / `FUN_005a1130` | — |

Offline flow (what the port needs):

```
Logo(1) ──> Menu(4: title → main menu → sub-menu)
  Arcade / Story ─> ArcSelect(8) ─confirm─> sequencer step ─> Battle(2) / Conversation(5) / Result(3) / Endroll(11)
                                  └cancel─> Menu(4)
  Match / Training ─> VS Select(6) ─> (stage+BGM, VS screen) ─> Battle(2) ─> Result(3) ─> VS Select(6) …
  Config ─ inside Menu(4)
Battle(2) end ─> Result(3) (versus) or sequencer step (arcade/story; see §0.4)
```

### 0.1.1 Port API (src/app/scene.h, src/app/text.h — Part A, stable)

- **Scenes**: `scene_register(id, &def)` with `scene_def {init, update, draw_top, draw_bottom, exit, run,
  clear_top, clear_bottom}`; ids = the original ids above (+ port-only `SCENE_LAB`, `SCENE_DEV`,
  `SCENE_COMING_SOON`). `scene_goto(next)` from `update()` = the scene's Exit: the framework runs `exit()` after
  this frame's draw, sets `scene_prev()` (= `DAT_00885874`) and runs next's `init()` at the next frame start.
  Unregistered ids show "coming soon" and return to `scene_prev()`. `run()` = blocking legacy scene (own loop;
  bottom screen handed to the text console). Register new scenes in `scenes_register_all()` (main.c).
- **Per-frame post-scene calls** (framework, original order): announce update → `audio_se_frame()` →
  `music_update()` → transition update. Scenes only call `audio_se(id, AUDIO_SE_CD_DEFAULT)` /
  `music_request()` / `music_force()`.
- **Transitions**: `scene_fade(type)` (types 1–16 of FUN_00561fc0), `scene_fade_busy()` (= `DAT_0087e7c0`),
  drawn over both screens.
- **Help line**: `announce_show(id)` (ANNOUNCE order index), `announce_show_text()`, `announce_hide()`; drawn on
  the **bottom** screen (system font, typewriter).
- **Menu input**: `g_min` fed once per frame; `min_pressed(MI_*)` with the 20/5 direction repeat,
  `min_pressed_nr()` without, `min_confirm()` (A, or START), `min_cancel()` (B).
- **Match setup**: `g_setup` (`match_setup`, one field per §0.2 global, same names/values) — the Battle scene
  (main.c `battle_init`) reads characters, colours, handicap, stage, BGM, difficulty, P2 input, rounds, timer.
- **Config**: `g_config` (`app_config`, the 3DS-meaningful rows of `modes_menu_config.tsv`), `config_save()`.
- **Text** (`text.h`): `tr("KEY")`, `tr_id(textId)`, `announce_by_id()`, `kv_load()` for other script files;
  `text_draw(FONT_*, s, x, y, align, rgba, scale, maxw)` with y = vertical centre of the first line
  (FUN_005931d0), `text_width()`, `text_wrap()`. Fonts packed: system, menu, menubig, menulabel, menusubtitle,
  winquote, dialogue (English glyph set, also covers ptbr/sp).

### 0.2 Match-setup globals (the interface between menus and the fight)

Written by Menu Exit `FUN_00642880` (per main-menu choice `DAT_009b1618`), the select scenes and the sequencer
steppers; read by Game Init `FUN_005f4650`.

| Global | Meaning | Arcade (`DAT_009b1618`=0) | Story (1) | Match (3) | Training (4) |
|---|---|---|---|---|---|
| `DAT_0087faa8` | mode | 0 | 1 | 2 | 3 |
| `DAT_0087faac` | sub-mode (0 CPU match, 1 2P, 3 online, 4 replay) | 0 | 0 | kept (0/1) | 0 |
| `DAT_0087fab0` | CPU difficulty 0..2 (`DAT_009b163c`, menu choice) | choice | choice | choice / 1 (2P) | 1 |
| `DAT_0087fab4/fab8` | P1/P2 input source (10 = CPU) | 0 / 10 | 0 / 10 | 0 / 10 (2P: 1) | 0 / 10 |
| `DAT_0087fabc/fac0` | P1/P2 hyper control mode (0 Standard, 1 Advanced) | ArcSelect | ArcSelect | select | select |
| `DAT_0087fac4/fac8` | P1/P2 character (engine id 3..18) | ArcSelect / ladder | ArcSelect / ladder | select | select |
| `DAT_0087facc/fad0` | P1/P2 colour (3 = auto) | ladder entry (0/0) | ladder entry (0/0) | select | select |
| `DAT_0087fad4/fad8` | = 2 (UNVERIFIED role) | 2/2 | 2/2 | 2/2 | 2/2 |
| `DAT_0087fadc` | stage id (stages.md §1) | ladder | ladder | stage select | stage select |
| `DAT_0087fae0` | BGM id (music.md) | ladder | ladder | BGM select | BGM select |
| `DAT_0087fae8/faec` | P1/P2 controller id | ArcSelect | ArcSelect | select | select |
| `DAT_0087faf0` | rounds to win | 1 | 1 | config `DAT_00881f60`+1 | 1 |
| `DAT_00885974` | round timer (Game Init) | 180 | 180; **999 on stage 0x13** | config | 999 |

Engine character ids: 3 Sora, 4 Alte, 5 Tsih, 6 Mira, 7 Sham, 8 Nath, 9 Star Breaker, 10 Suguri, 11 Saki, 12 Iru,
13 Nanako, 14 Kae, 15 Kyoko, 16 Hime, 17 Sumika, 18 Sumika 2nd form (HANDBOOK, +3 table offset).

### 0.3 Sequencer (arcade and story share it)

A `std::vector` of 0x1c-byte entries (base `DAT_008940xx`, accessor `FUN_004a9a90(i)` = `begin + i*0x1c`,
size `FUN_004aa1a0` = `(end−begin)/0x1c`, push `FUN_004aa150`), cursor **`DAT_008940ac`**, plus an int vector
"opponent pool" (push `FUN_004aa170`, size `FUN_004aa1d0`, at `FUN_004a9ab0`).

| Off | Field | Notes |
|---|---|---|
| +0x00 | type | 0 fight, 1 conversation, 2 result screen, 3 ending (Endroll) |
| +0x04 | fight: P1 char; conversation: script id | script id → resource `TEXT_CONVERSATION0<id>` (§C) |
| +0x08 | fight: opponent (1 = random from pool, replaced by the pick); conversation: backdrop stage id | conversation: `FUN_004bfc10(id)` (stage init) |
| +0x0c | P1 colour | always 0 in the shipped ladders |
| +0x10 | P2 colour | always 0 |
| +0x14 | stage id | arcade remap: 12→13 and 17→18 when no continue was used |
| +0x18 | BGM id | 0 → opponent's stats record +0x2d8 (`DAT_00882258 + c*0x2dc`; 0 in shipped data) |

Builders (clear vectors, `DAT_008940a8 = char`, `DAT_008940c8 = 0` continues, `DAT_008940cc = 0`,
`DAT_008940d0 = 10`): arcade **`FUN_004a7cc0(char)`**, story **`FUN_007befa0(char)`**; called by ArcSelect Exit
`FUN_0079e4b0` (state 900 = confirmed; story if `DAT_0087faa8 == 1`), which then runs the first step.

Steppers: arcade **`FUN_004a7910(param)`**, story **`FUN_007bec90(param)`** (identical except the arcade stage
remap and the end handler). If `cursor < size`:
- `param == 3` (continue): `DAT_008940c8++`, `DAT_008940cc = 0`, `cursor−−` (replay the fight just lost).
- `param == 1 && DAT_009b6930 == 1` → scene 3 (Result in continue mode) without advancing. `param 1` = "match
  decided" from Game Exit `FUN_005f5780` (`DAT_00885970 == 1`); `DAT_009b6930` = winning side,
  `p0.hp(+0xd0) <= p1.hp` (0x5f5780; a tie is a CPU win). `param 3` comes from pause-menu Retry
  (`DAT_00885970 == 3`) and from the continue screen; pause Quit (`== 2`) returns to ArcSelect. §D.1.5.
- else by entry type: 0 → copy fields to the setup globals (opponent 1 → random pick: `r = FUN_00459f80(0,
  n*10) % n` — cosmetic MT `0x875b68` because it runs outside the tick — `pool[r]` taken and erased, written back
  into the entry so a continue replays the same opponent), `DAT_00885879 = 1` (new match) → scene 2; 1 →
  `FUN_004e35e0(+4)` + `FUN_004bfc10(+8)` → scene 5; 2 → scene 3; 3 → scene 11. Then `cursor++`.
If `cursor >= size` (ladder finished): scene 4 (menu); arcade: `DAT_008940d0 = FUN_00792f40(DAT_0087fab0,
DAT_008940cc, DAT_0087fac4)` and `FUN_004a8390()` (arcade clear: unlocks/ranking, §D); story:
`DAT_008940d0 = 0xb`, `FUN_007c3e80()` (story clear, §D).

Callers of the steppers: Conversation Exit `FUN_004e3730`, Result Exit `FUN_00796ca0`, Endroll Exit
`FUN_00519f60`, Game Exit `FUN_005f5780` (§D gives the params).

### 0.4 Ladders (full per-character table: `modes_ladders.tsv`, §D)

**Arcade** `FUN_004a7cc0(c)`: pool = {4,5,6,7,8,11,12,13,14,15} minus `c` (Sora 3, Star Breaker 9, Suguri 10,
Hime 16, Sumika 17 never appear as random opponents). Then 7 fights, each followed by a result entry, then an
ending entry:

| # | opponent | stage | BGM |
|---|---|---|---|
| 1 | random | 15 Nature 10,000 Years After | 0x10 |
| 2 | random | 6 War 10,000 Years Ago | 0x0c |
| 3 | random | 7 Canyon of Wind | 0x0d |
| 4 | random | 8 Dust Storm | 0x0b |
| 5 | random | 9 Rain and Sunset | 0x11 |
| 6 | random | 11 Big Bridge | 0x0f |
| 7 (boss) | c ∈ 3..9: Star Breaker (9), but Sora (3) if c = 9; c ∈ 10..17: Hime (16), but Suguri (10) if c ∈ {16,17} | c ∈ 3..9: 12 Capital in Flames (→ 13 Whirlpool of Malice if no continue); else 17 Guardian's Chamber (→ 18 Moonlight Dance Hall if no continue) | 0x0e |

No conversations in arcade. No Sumika fight in arcade.

**Story** `FUN_007befa0(c)` (c = 3..16; Sumika is not a story character): per character a 5-entry pool and

`conv(c*10−20)` on stage 15 → fight 1 (fixed rival or random) stage 15 BGM 0x10 → fights 2–5 random, stage 15,
BGM 0x0c, 0x0d, 0x0b, 0x11 → `conv(+1)` on stage 16 → fight vs fixed boss, stage 16 Crashed Spaceship, BGM 0x12 →
`conv(+2)` on stage 19 → fight vs **Sumika 2nd form (18)**, stage 19 Sumika's Hideout, BGM 0x13 (timer infinite) →
`conv(+3)` on stage 19 → ending. Each fight is followed by a result entry.

Script ids are `10*k + n` with k = 1..14 in story order Sora, Alte, Tsih, Mira, Sham, Nath, Star Breaker, Suguri,
Saki, Iru, Nanako, Kae, Kyoko, Hime and n = 0..3 (file `story_<name>_0<n+1>.txt`).

### 0.5 Corrections to existing docs/code found by this work

| Where | Was | Is | Source |
|---|---|---|---|
| core.md §6.1, `src/sim/core.c:787` | maxHp 4500 for everyone | `DAT_0087fad4[i]` (handicap 0..5, default **2** on every menu path) → 4500/6750/**9000**/13500/18000/27000 via jump table `0x69d038` (`jmp [4*ecx+0x69d038]` at 0x69ce04, cases 0x69ce0b..0x69ce47); Ghidra collapsed the switch. Sumika 2nd still 12000/15000/18000 (redHp/`local_a8` keeps the table value). **Fixed** (Part D): `aos_match_params.handicap` (default 2), `aos_handicap_hp()`, netplay proto 4; golden re-recorded. | FUN_0069cc40, verified by the lead with `xref dis 69cde0` |
| `src/sim/sim.h:214` | gameMode 3 = training-like | `DAT_0087faac == 3` is **online**; Sumika 2nd input fn FUN_00771d10 returns early then | §D.3 |
| stages.md §1 | stage 4 = menu/title background | menu/title backdrop = stage **0x13** (Menu Init; Menu Exit `FUN_004bfca0(0x13)`); stage 4 is the select-screen backdrop | §A.4.1 |
| stages.md §2.2 | VS stage cursor = first entry | from the menu the cursor lands on stage **15** (0x79beb2 overwrites the 0) | §B.2 |
| music.md §2 | — | conversations play **no** BGM (act field 1 change → `FUN_004bee00(0,1,1)` fade-out) | §C |
| FORMATS.md | script.pak = all text | speaker/portrait/staging scripts are **Win32 resources (type `TXT`) in aos2.exe**, 56 × `TEXT_CONVERSATION0<id>`; extracted to `extracted/script_res/` by a throwaway PE-resource reader — add this to `tools/aos2extract` (build step) | §C.1 |

### 0.6 Data tables (docs/re/)

| File | Content | Section |
|---|---|---|
| `modes_ladders.tsv` | arcade + story ladder per character (step, type, opponent/pool, stage, BGM, script id, backdrop) | D |
| `modes_story_scripts.tsv` | all 1146 ACT records of the 56 conversation scripts (names, portrait, band, text refs, backdrop, SE) | C |
| `modes_menu_fonts.tsv` | 22 font families × sizes × language folders | A |
| `modes_menu_textids.tsv` | 429 text ids → key, font family, scale | A |
| `modes_menu_config.tsv` | 27 config rows: storage, range, default, help id, 3DS relevance | A |
| `modes_select_pools.tsv` | character list order per select mode | B |
| `modes_select_charstats.tsv` | select-screen ratings (difficulty, melee/projectile/mobility/meter) | B |
| `modes_select_sprites.tsv` | select-screen sprite src/dst rects | B |
| `modes_result_sprites.tsv` | result/continue sprite rects | B |
| `modes_result_winquotes.tsv` | win-quote rules incl. rival X-quotes | B |

### 0.7 Input on 3DS (all menus)

PC: confirm = OK key or the bind `DAT_00881edc`, cancel = Cancel key or `DAT_00881ee8`; directions repeat after
20 frames then every 5 (§A.2.1). 3DS: **A = confirm, B = cancel**, D-pad/circle pad = directions with the same
repeat, START = confirm on title. The conversation scene also accepts the Attack button and fast-forwards while
Special is held (§C.2) — map to A / hold X.

### 0.8 Determinism / netplay

Arcade and story are offline. Everything mode-specific (opponent pick, BGM "????" pick, win-quote pick, continue
counting, score, ranking, unlocks) runs outside the tick on the cosmetic RNG or in the app layer. The sim receives
only plain match parameters (characters, colours, handicap → HP, difficulty, P2 = CPU, rounds = 1, timer 180/999,
stage) through `aos_match_init`, and must expose the end-of-round facts the result/score screens need (§D.3.1).
No `gameMode` value is needed for arcade/story.

# A. Text renderer, menu framework, Logo / Title / Main menu / sub-menus / Config

Notation as hud.md: `W,H = DAT_009b0fe4/fe8` (back buffer), `Ww,Hw = DAT_009b0fdc/fe0` (window, equal to W,H),
`s = _DAT_009b1000 = min(W/800, H/600)`, every offset `(int)(c*s)`. Values "PC" = 800x600 (s = 1), "3DS" = 400x240
(s = 0.4). Constants: value (address). Frames at 60 Hz. Tables: `docs/re/modes_menu_fonts.tsv`,
`docs/re/modes_menu_textids.tsv`, `docs/re/modes_menu_config.tsv`.

## A.1 Text: strings, fonts, renderer

### A.1.1 Strings and ids
- Text files `extracted/script/<lang>/*.txt`: blocks `< KEY >` + text lines (UTF-8). Lang dir by `DAT_00881ec0`
  (FUN_00597910): 0 en, 1 jp, 2 chs, 3 cht, 4 ptbr, 5 sp, 6 ko, 7 ru. Default language = Steam country
  (FUN_00597570: JP→1, CN→2, HK/TW/MO→3, BR→4, Spanish-speaking→5, KO→6, RU→7, else 0); stored in config.
- **Menu text ids**: FUN_00633e50 (called from FUN_00596630 at start-up) registers 429 `id → KEY` pairs with
  FUN_00498ca0 (full list `modes_menu_textids.tsv`; e.g. 0x8b..0x93 MENU_MAINMENU_*, 0x94 VERSION_PREFIX,
  0x99..0xa0 match/training rows, 0xab..0xdf config, 0xf6..0xfb difficulty, 0x1f8..0x207 CHARACTER_*,
  0x208..0x23a BGM_* (music.md), 0x24a..0x261 MENULABEL_*). Raw strings → map `DAT_009b202c`; FUN_0063edb0 then
  builds a **text object per id** into map `DAT_009b2034` (font + scale fixed at build time; rebuilt on
  resolution/language change by FUN_00647d20). Lookup at draw time: `FUN_0049ad40(&it,&id)` on `ecx = 0x9b2034`,
  `FUN_0049a210` (found?), `FUN_0049a250` → text object (e.g. 0x653a80).
- **Announce (help line) ids** = 0-based order of keys in `announce.txt` (FUN_00495740 registers ANNOUNCE_* 0..138).
- Parser of the `< KEY >` files: not traced (UNVERIFIED where; behaviour visible in the files is enough).

### A.1.2 Fonts (BMFont v3 binary, AngelCode loader `CFontLoaderBinaryFormat` FUN_004944a0)
- FUN_00593660 (start-up, and again after a language change in FUN_00647d20) loads 22 families (table
  `modes_menu_fonts.tsv`). Per family FUN_005946c0 tries sizes {1080, 960, 768} (0x438/0x3c0/0x300) as
  `font/<langdir>/<name>_<size>.fnt` (langdir FUN_00597a50: en/jp/cn(2,3)/ko/ru, ptbr/sp → en) or `font/<name>_<size>.fnt`
  for language-neutral families, and keeps the (size, font id) pairs that exist (en: menu/menubig/menulabel/
  menusubtitle/system/winquote/dialogue have all 3 sizes; jp/cn/ko/ru mostly only 1080).
- **Size pick + scale** FUN_00594570(family, Hw): walking the list in load order 1080, 960, 768 it keeps the
  last size ≥ Hw (i.e. the smallest size ≥ the window height; 1080 if Hw > 1080). Text scale = `DAT_00881eb8 / size`
  (DAT_00881eb8 = configured height = H). PC 600: 768 fonts at 0.78125; 3DS 240: 768 fonts at **0.3125**.
- Font record (0x38 bytes, vector `DAT_00d93ea8`, FUN_0047b3e0(id)): +0 and +4 = common.lineHeight, +8 = base,
  +0xc float extra line spacing (0, FUN_0047ad00), +0x10/+0x14 = scaleW/H, +0x18 page textures, +0x28 hasOutline,
  +0x2c = info.outline (px), +0x30 glyph map (id → {x,y,w,h,xoff,yoff,xadv,page}, shorts).
- Colours/outlines are **baked into the glyph PNGs** (menu/menubig: white glyphs + black edge; menulabel: black;
  menusubtitle: black + white edge; system: white). Vertex colour multiplies (ARGB arg), blend 1 = alpha.

### A.1.3 Text object and draw
- Ctor FUN_00593430(out, fontId, string, scale, maxW, maxH) (stack args; −1 = no limit). Layout: +0 std::string,
  +0x18 font id, +0x1c lineHeight, +0x20 width px, +0x24 height px, +0x28 maxW, +0x2c maxH, +0x30 sx, +0x34 sy.
  If `maxW > 0` and width(scale) > maxW: `sx = 1.0 · scale · maxW / width` (horizontal squeeze only). If `maxH > 0` and
  `(int)(base·scale + 0.5) > maxH`: `sy = scale − (1 − maxH/h)` (literal: subtracts, not multiplies). +0x20/+0x24 =
  `(int)(FUN_0047cb50(sx)+0.5)`, `(int)(FUN_0047cd50(sy)+0.5)` (FUN_005930f0 / FUN_00593160 recompute).
- **Draw** `FUN_005931d0(this=textobj; x, y, align, ARGB, blend)` (707 call sites):
  `FUN_0047dbd0(font, text, align, x, y − (int)(lineHeight·sy/2 + 0.5), ARGB, sx, sy, blend)` → **y is the vertical
  centre** of the first line. Align (per line): 0 left at x; 1 centre: `x' = (int)(x + 0.5) + (int)(lineW · −0.5)`
  (−0.5 0x853bb0); 2 right: `x' = (int)(x+0.5) − (int)lineW` (lineW = FUN_0047ca00, widest-line measure).
- Per glyph (FUN_0047d130): dst = `(int)(penX + xoff·sx)`, `(int)(penY + yoff·sy)`, size `w·sx`, `h·sy` (each
  edge truncated), src = glyph rect (u2 = x+w, not +1); drawn with the page texture vtable +0x74 (dst, 0, src, ARGB,
  blend). Advance `xadv·sx` (+ `(int)(outline·sx)` when the font has an outline) + kerning (FUN_0047c240).
  Quirk: if any glyph has `frac(yoff·sy) > 0.1` (0x853b10) all bottom edges get +0.999 (0x853b30).
- **Line breaks only at '\n'** (the draw itself never wraps; conversation text is pre-wrapped by FUN_00593340 ->
  FUN_0047e2c0, see C.6; the only other caller is FUN_00566440, online chat). Line advance =
  `(int)((base + extra)·sy + 0.5)` (record +8 and +0xc). Hanging indent: a text starting with `"` or `「` (0x300c)
  indents continuation lines by the width of that quote (until the closing `"`/`」`, UNVERIFIED exact rule).
- Fonts per text id (FUN_0063edb0, scale factor × H/size): main-menu items and most texts **menu ×1.0**; sub-menu row
  titles **menubig ×1.0**; `*_SUBTITLE` **menusubtitle ×1.0**; MENULABEL_* **menulabel ×1.0**; config labels
  **menu ×0.9 (0x814178), maxW (int)(260 s) (0x81cda0)**; config values menu ×0.9; BGM names menu ×0.9 maxW 400s
  (0x8145f4); key names menu ×0.8 (0x8145e8); memory/memory_small for Memory rows.

### A.1.4 Generic typewriter
Only the announce line (A.2.2) types: 1 UTF-8 code point per frame once its band is fully open (FUN_00495040:
`DAT_0087a1cc` < length → advance, text object string = prefix). The conversation typewriter is section C.

## A.2 Menu framework

### A.2.1 Input (all menus)
- Each frame: `FUN_0061b2d0()` (poll), `pad = FUN_0061ae20()` (first active controller, cached `DAT_009b1224`;
  FUN_0061aed0 is the same search into a caller variable), `FUN_0061b2e0(pad, &bits, 1, 0)` builds a 16-bit mask
  from the keyboard binds `DAT_00881ec4..f04` (defaults FUN_004dc6f0) or the pad:

| bit | bind slot (default key) | menu meaning |
|---|---|---|
| 0x1 | ec4 (Up) + pad POV 0x14 / axis 10000 | Up |
| 0x2 | ec8 (Down) + 0x15 / 10001 | Down |
| 0x4 | ecc (Left) + 0x16 / 10002 | Left |
| 0x8 | ed0 (Right) + 0x17 / 10003 | Right |
| 0x800 | ed4 OK ('Z') or Enter | confirm |
| 0x1000 | ed8 Cancel ('X') or Esc | cancel |
| 0x10 | edc ('Z', = Primary weapon by the label order of config KEY_*) | confirm alt |
| 0x80 | ee8 ('V', = Dash by label order, UNVERIFIED) | cancel alt |
| 0x20/0x40/0x100/0x200 | ee0 'X' / ee4 'A' / eec 'C' / ef0 'S' (sub/special/hyper/guard) | – |
| 0x400 | ef4 'T' or Esc (pause) | – |
| 0x2000/0x4000/0x8000 | efc Tab (chat) / f00 PgUp / f04 PgDn | – |

- `FUN_0061c100(&bits, state[16], idx)` → per-button state 0 up, 1 released, 2 held, **3 = pressed this frame**.
  Slots: [0] Right, [1] Left, [2] Up, [3] Down, [4] OK, [5] Cancel, [6] bit 0x80, [7] bit 0x10, [8] 0x20, [9] 0x40,
  [10] 0x100, [11] 0x200, [12] 0x400, [13..15] chat/scroll. **Key repeat for the 4 directions only**: counters
  `DAT_009b1234..`; first frame → 3, held 1..19 frames → 2, from frame 20 every 5th frame (`n % 5 == 0`) → 3.
  FUN_0061bc70 = same without repeat (logo, Memory scene).
- **Confirm** FUN_0061c7b0: `[4]==3 || ([7]==3 && [5]∉{2,3})`. **Cancel** FUN_0061c7e0: `[5]==3 || ([6]==3 && [4]∉{2,3})`.
- Menu state block `DAT_009b16d8` (16 ints): `d8` Right, `dc` Left, `e0` Up, `e4` Down. Zeroed by Menu Init.
  (States 0x28a/0x294 = key-config rebind force all to 2 while the pad being rebound is active.)
- **3DS mapping**: A = confirm (OK slot), B = cancel, D-pad/circle pad = directions (keep the 20/5 repeat).

### A.2.2 Announce / help line (FUN_004953f0 show, FUN_00495040 update, FUN_00495170 draw, FUN_00495030 hide)
- `FUN_004953f0(id, opt*, fmt)`: opt = {y, ?, align, duration} — every menu call passes `{0x118, 1, 2, −1}`
  (y 280, centre, infinite); opt = NULL → {0x114, 1, 1(left), −1}. Text = ANNOUNCE_<id> in **system** font
  (family `DAT_009b0e58`, scale H/size), maxW `Ww − (int)(156 s)` (0x813a60), maxH `(int)(60 s)` (0x813a58).
- Update: shown → alpha `DAT_0087a1a4` += 0.1/frame (0x853b10) to 1, then typewriter (A.1.4), duration counts down
  (−1 = forever); hidden → alpha −= 0.1. Draw (when shown or alpha > 0), after every scene (main loop):
  centre `yc = H/2 + (int)(y·s)`; band rect `(W/2 − Ww/2, yc − hh, W/2 + Ww/2, yc + hh)`, `hh = (int)(14·s·alpha)`
  (0x813a54), vertical-gradient fan TL 0x80000000, TR 0xe1000000, BR 0xe1000000, BL 0xff000000 (FUN_00457a00), blend 1.
  Text only at alpha ≥ 1: x by align (1: `(int)(132 s)` 0x813a5c; 2: `W/2 − textW/2`; 3: `W − 132s − textW`),
  y = `yc − (int)(3 s)` (+ `(int)(2 s)` for languages 1..3); 4 shadows at (±1, ±1) ARGB 0x20000000, then white.
- PC: band 566..594 (28 px), text y 577. 3DS: yc 232, band 227..237, system_768 at 0.3125 (10 px line) — too small,
  see A.7.

### A.2.3 Common sub-menu visuals (draw fns below)
- Every panel has a header **pill**: solid strip `(0, yh − 8s, W, yh + 8s)` colour `(int)(128a)<<24 | RGB`
  (blend 1) + label text (menulabel) on a coloured pill from `systemAddMdl/ttl_menu.png` (`DAT_009b1fc4`): body = a
  1-px column src stretched over `[W/2 − Ww/2, +textW + (int)(44 s)]`, cap src 24x32 drawn `(int)(12 s)` wide after it;
  label text left-aligned at `x = (int)(12 s)`, y = yh, white·a. Pills in ttl_menu: main menu blue
  (0x2df,8,0x2e0,0x27)+(0x2e0,8,0x2f7,0x27) strip 0x97cbff; difficulty yellow (0x32d,0x38..)+(0x32e,0x38,0x345,0x57)
  strip 0xffed97; match/training/command green (0x2ef,0x68..)+(0x2f0,0x68,0x307,0x87) strip 0x9cffa5;
  config orange (0x2ef,0x98..)+(0x2f0,0x98,0x307,0xb7) strip 0xffd09c.
- Row band: `(0, y − 32s, W, y + 32s)` (0x8140dc) white (selected) or black, alpha `(int)(144 a)` (0x814138),
  1-px black lines (alpha 255a) at both edges (FUN_00457ae0); title (menubig) centred at `y − (int)(12 s)`
  (0x813f54), subtitle (menusubtitle) at `y + (int)(16 s)` (0x8140d8); colour white (selected) / 0xa4a4a4.
- Panel alpha `a` per state (Menu update tail): +0.05/frame (0x813ef4) while its state is active, −0.05 otherwise,
  frozen during state 900 (exit). Slide-in: panels are offset down by `(int)(480·s·(1 − a))` (0x815884) → 20-frame
  slide from below; the main band rises by `(int)(Σ 490·s·a_i)` (0x824494) over all open sub-panels.

## A.3 Logo scene (scene 1, FUN_006321a0; Init FUN_00632120; textures FUN_00631fc0)
- Textures `systemAddMdl/daidai.png` (`DAT_009b1530`) and `systemAddMdl/shinden.png` (`DAT_009b1534`), 2048².
- Draw FUN_00632340: clear **white** 0xffffffff; states 10/0xf: daidai, states 0x14/0x19: shinden, src (0,0,0x7ff,0x7ff)
  centred, half `(int)(300 s)` (PC (100,0)-(700,600); 3DS (80,0)-(320,240)), white, blend 1; then transition.
- State machine (`DAT_009b1538`, counter `DAT_009b153c` ++ each frame, input FUN_0061bc70 no-repeat into
  `DAT_009b1540`): 0: after counter > 50 → transition 5 (fade in from black, 0.05/frame = 20 f), state 10, c = 0.
  10: confirm or c > 199 → transition 1 (to black, 20 f), state 0xf. 0xf: when the transition ends
  (`DAT_0087e7c0 == 0`) → transition 5, state 0x14, c = 0. 0x14: confirm or c > 199 → transition 1, 0x19.
  0x19: when done → Logo Exit FUN_00632160: scene 4 (Menu), prev = 1. Total ≈ 51 + 200 + 20 + 200 + 20 = 491 f
  without skipping; each logo skippable with confirm (A). No sound/BGM. (What shows during the first 51 frames
  before the first fade: white clear, UNVERIFIED whether a start-up transition covers it.)

## A.4 Menu scene 4 (FUN_00643150; Init FUN_00642190; Exit FUN_00642880; draw FUN_00645080)

### A.4.1 Init / resources / background
- Textures (FUN_006413c0, loaded once at start-up by FUN_005dff20, freed FUN_00641e60): `menuMdl/ttl_logo` (fc0),
  `systemAddMdl/ttl_menu` (fc4), `ttl_smenu` (fc8), `menuMdl/ttl_config` (fcc), `pad_button` (fd0),
  `systemAddMdl/ttl_ranking` (fd4), `menuMdl/ttl_face` (fd8), `ver/ver` (fdc), `ttl_tournament` (fe0),
  selMdl particle_cash_wBorder/sel_buy/titleunlockpopup/ticktitle/aos2_heatup/aos2_mizuumi/aos2_discord*/
  aos2_twitter/aos2_facebook/dlc_icon/majorTop8Badges (fe4..2010). Only **ttl_logo and ttl_menu are referenced by the
  menu draw fns** found; ttl_smenu/config/face/ranking/tournament/ver are only loaded/freed (unused in this build).
  `systemAddMdl/menuBG` (`DAT_00893e30`, FUN_005f3510) is also unused (only load/free refs).
- **Background = stage 0x13 (19, "sh")**, not stage 4: Menu Init `FUN_004bfc10(0x13)` (0x642824), Menu Exit
  `FUN_004bfca0(0x13)`; drawn by `FUN_004bfd80(1)` after clearing to 0xff0055aa (FUN_00645080), updated by
  `FUN_004bfd30()` each menu frame. (Stage 4 "sl" is the VS/arcade select background, FUN_007aef30; online select uses
  0x13 too.) → stages.md §1 label for id 4 "menu / title background" should read "select-screen background".
- Init paths (by previous scene `DAT_00885874`): from Result/Endroll with arcade (`DAT_0087faa8==0`) → state 0x19a
  (ranking name entry if `DAT_008940d0 < 10`, announce 0x36), BGM 0x18; story → state 10 (title), no BGM; replay
  (`DAT_0087faac==4`) → state 500 (Memory), BGM 0x18; otherwise (from Logo) **state 10, everything zeroed, no BGM**.
  If unlock popups are queued (FUN_0047bc80 ≠ 0) → state 0x1a4 + SE 5. Always: counter `DAT_009b1614 = 0`,
  input block cleared, transition 5 (fade in 20 f).

### A.4.2 State machine (`DAT_009b1610`)
| state | what | input → result (SE = FUN_007bc9c0 id) |
|---|---|---|
| 10 | **title**: only the 3D background (no logo, no text, silent) | confirm or counter > 299 (300 f) → 100: white flash `DAT_009b1660 = 1.0`, SE 0, BGM `FUN_004bee00(0x18,1,1)`, help ANNOUNCE 0, Steam achievements |
| 100 | **main menu** (cursor `DAT_009b1618`, 9 items) | Right +1 / Left −1 (wrap 0..8), SE 2, carousel kick `DAT_009b16d0 = ±1`, help line = ANNOUNCE_MENU_<item> (FUN_006536f0); cancel → cursor 8 (Exit), SE 2; confirm → table below + SE 0 |
| 300 | difficulty (Easy/Normal/Hard, `DAT_009b163c` 0..2) | Down +1 / Up −1 wrap, SE 2; cancel → 100, SE 1; confirm → 900, SE 0, transition 1 |
| 200 | match (`DAT_009b161c`: 0 2P Game, 1 CPU Game) | Down/Up wrap SE 2; cancel → 100 SE 1; confirm: 0 → `faac=1`, 900 + transition 1; 1 → `faac=0`, 300 (difficulty, ANNOUNCE 9); SE 0 |
| 0x44c | training (`DAT_009b1620`: 0 How to Play, 1 Practice) | Down/Up wrap SE 2; cancel → 100 SE 1; confirm 0 → 0x456 manual (SE 0, ANNOUNCE 12); 1 → `faa8=3, faac=0`, 900 + transition 1, SE 0 |
| 0x456 | How to Play pages (`DAT_009b1624` 0..8) | Left/Right page −/+ (SE 2; SE 3 at an end); confirm = next page, on the last → 0x44c (SE 0); cancel → 0x44c (SE 1, ANNOUNCE 11) |
| 400 | ranking (FUN_0065c670) | Left/Right page `DAT_009b164c` 0..8 wrap SE 2; confirm/cancel → 100 SE 1 |
| 500 | Memory (replay list, FUN_006571e0) | Down/Up wrap SE 2; cancel → 100 SE 1; confirm → 900 SE 0 transition 1 (→ battle scene with the replay) |
| 600 | config (FUN_00647fd0) | A.5 |
| 0x280..0x294 | key config (FUN_0064fed0, draw FUN_00651260) | out of scope (3DS: own remap screen) |
| 0x19a / 0x1a4 | arcade ranking name entry (FUN_00658550 / draw FUN_00658970) / unlock popup (FUN_00646ee0 / draw FUN_006471d0) | popup (confirm per page, `DAT_009b165c`) → 10 after story / 0x19a after arcade (FUN_00646ee0); name entry → 400 ranking (FUN_00658550) |
| 0xfa | command Standard/Advanced (`DAT_009b1638`, draw FUN_006563f0 draws no row texts) | **dead**: nothing sets 0x1610 = 0xfa; command choice is in character select (section B) |
| 900 | leaving | waits for the transition (`DAT_0087e7c0 == 0`) → Menu Exit |

Main-menu confirm (0x643...): 0 Arcade → 300 (`faac=0`, ANNOUNCE 9 SMENU_DIF); 1 Story → 300 (same);
2 Online → `faac=3`, 900 + transition 1 (Exit → FUN_00645530 → scene 13); 3 Match → `faa8=2`, 200 (ANNOUNCE 10);
4 Training → 0x44c (ANNOUNCE 11); 5 Memory → 500 (FUN_00656950, ANNOUNCE 0x37); 6 Ranking → 400 (FUN_006593b0/
FUN_006597b0, rows `DAT_009b1694[k] = (10−k)·0.25 + 1`, ANNOUNCE 0x35); 7 Config → 600; 8 Exit → 900 + transition 1
(Exit → scene 12). **Arcade/story difficulty → `DAT_009b163c` → Menu Exit `DAT_0087fab0`** (0 Easy, 1 Normal, 2 Hard).
Menu Exit (FUN_00642880) per item: 0/1 → scene 8 ArcSelect (faa8 0/1, P2 = CPU 10, rounds 1); 3 → scene 6 VS select
(faac 0 CPU: fab0 = difficulty, fab8 10; faac 1 2P: fab8 1; rounds = `DAT_00881f60 + 1`); 4 (Practice) → scene 6,
faa8 3; 5 → scene 2 directly (replay header copy FUN_00793e30); 8 → scene 12.

Fades: transition 1 = to black at 0.05/frame (20 f) on every "leave the menu" confirm; Menu Init fades in (type 5).
White flash `DAT_009b1660`: −0.025/frame (0x813edc) → 40 f, drawn as full-screen white `(int)(255 f)` alpha with
blend 2 (additive) at the end of FUN_00645080.

### A.4.3 Main menu = title-logo screen (draw FUN_00645080 / FUN_00653810)
Order: stage 0x13 → [config 600/0x28x: FUN_00651260] → logo (alpha `DAT_009b1664`, +0.05 in state 100 only) →
main band FUN_00653810 (alpha `a = DAT_009b1668`, +0.05 in all menu states except 10) → sub panels → white flash →
announce FUN_00495170 → transition FUN_005622a0.
- **Logo**: `ttl_logo` src (0,0,0x3ff,0x3ff), centre (W/2, H/2), half `(int)(256 s)`, colour `(int)(255·a1664)<<24|0xffffff`,
  blend 1. PC (144,44)-(656,556); 3DS (98,18)-(302,222). Version text id 0x94 ("ver" + number, number UNVERIFIED),
  right-aligned at `(W/2 + (int)(380 s), H/2 + (int)(180 s))` (0x824448, 0x815258), same alpha. PC (780,480), 3DS (352,192).
- **Band** `yB = H/2 + (int)(240 s) − (int)(Σ 490·s·a_sub)` (0x815584, 0x824494). Header strip + blue pill
  "MAIN MENU" (0x24a) at `yB − (int)(30 s)` (0x8140e8), half `(int)(8 s)` (0x8140d4), strip alpha 128a (0x814134).
  Item band `(0, yB − 15s, W, yB + 15s)` (0x853b88) colour `(int)(144a)<<24 | v·0x010101`, `v = (int)(255a)`, + black
  1-px edge lines. PC (closed) band 525..555, label 510; 3DS 210..222, label 204. With a sub-menu open the band sits
  at PC yB 50 / 3DS 20.
- **Items** (carousel, horizontal): for k = −4..5, `idx = (cursor + k) mod 9`, text id `0x8b + idx`
  (Arcade, Story, Online, Match, Training, Memory, Ranking, Config, Exit), centred at
  `x = W/2 + (int)(200 s)·k + (int)(200·s·d)`, `y = yB − (int)(1·s)`; `d = DAT_009b16d0` (±1 on a move, −/+0.1 per
  frame to 0 → 10-frame glide). Colour: k = 0 in state 100 → white, others 0xa4a4a4 (tournament mode
  `DAT_00881f70` on: Match orange 0xff8c00 / 0xa45a00); alpha `(int)(255a)`. PC spacing 200 px, 3DS 80 px.

### A.4.4 Sub-menu panels (layout, PC / 3DS values with a = 1)
| panel (draw fn, alpha) | header y | rows | text ids |
|---|---|---|---|
| Difficulty (FUN_00655c10, `DAT_009b166c`) | `H/2 − 80s + slide − 50s` (0x814164, 0x8140ec): 170 / 68 | 3, `y = H/2 − 80s + (int)(80 s)·k + slide`: 220/300/380; 3DS 88/120/152 | label 0x24f; rows 0xf6/0xf8/0xfa + subtitles 0xf7/0xf9/0xfb |
| Match (FUN_00654570, `DAT_009b1674`) | `H/2 − 40s + slide − 50s` (0x8140e0): 210 / 84 | 2: 260/340; 3DS 104/136 | 0x24c; 0x99/0x9b (+0x9a/0x9c) |
| Training (FUN_00654d20, `DAT_009b1678`) | same as Match | 2 | 0x24d; 0x9d/0x9f (+0x9e/0xa0) |
| How to Play (FUN_006554b0, `DAT_009b167c`) | yellow pill 0x24e | page = `manual/<eng|jp|chs|cht|ptbr|sp|ko|ru>_0N.png` (N = 1..9, loaded at 0x456 entry with `selMdl/sel_system` `DAT_009b1634`), src (0,0,0x3ff,0x3ff); 3 page-arrow frames from sel_system src (0x334+0x3c k,0x118,..,0x153) (UNVERIFIED placement) | |
| Config (FUN_00649210, `DAT_009b1680`) | A.5 | | |
| Ranking (FUN_0065c7c0), Memory (FUN_00657300), name entry (FUN_00658970), unlock popup (FUN_006471d0) | not detailed | | |
Row band half `(int)(32 s)` = 32 / 12 px. Selection colours as A.2.3.

### A.4.5 Sounds / music summary
SE: 0 decide, 1 back, 2 cursor, 3 blocked (page end), 4 config value change, 5 unlock popup. BGM: title silent;
0x18 from the title→menu step (and the return paths of Menu Init); unchanged across sub-menus; leaving to a select
screen the select scene forces 0x16 (music.md). No idle/attract demo: the title only auto-advances to the main menu
after 300 f; the main menu never times out.

## A.5 Config menu (state 600)
- Entry (main item 7): cursor `DAT_009b1640 = 0`, help FUN_00647a40 (ANNOUNCE 0xd + row mapping, see TSV),
  temp copies `DAT_009b2044 = language`, `DAT_009b2048 = screen mode`, `DAT_009b204c = resolution index`;
  FUN_006474d0 / FUN_00647860 rebuild value text objects.
- 27 rows, order and storage in **`modes_menu_config.tsv`** (defaults FUN_004db370). Up/Down wrap 0..26 (SE 2,
  help line updated); Left/Right change the value (SE 4; bools: Left = ENABLED); cancel on a non-Exit row jumps to
  Exit (SE 1); confirm or cancel on Exit → FUN_00647d20: apply resolution/window mode (rebuilds all text objects),
  apply language (reloads fonts FUN_00593660 and texts FUN_00596630), save (FUN_004dbce0), back to 100 (SE 0);
  confirm on Controls → key config 0x280 (SE 0, ANNOUNCE 0x28).
- Draw (FUN_00649210): orange header pill 0x251 at `H/2 − 180s + slide − 30s` (0x815258, 0x8140e8; PC 90, 3DS 36);
  panel `(W/2 − 410s, H/2 − 200s + slide, W/2 + 410s, H/2 + 253s + slide)` 0xa0000000 (0x824468, 0x8147cc, 0x824464);
  **14 visible rows** from `top = clamp(cursor − 7, 0, 13)`, row k at `y = H/2 − 180s + (int)(32 s)·k + slide`
  (PC 120..536, 3DS 48..204 step 12); selected row highlight `(W/2 ± 410s, y ± 16s)` 0x78bebebe; label left-aligned at
  `W/2 − (int)(360 s)` (0x817894; PC 40, 3DS 56), white (selected) / 0xa4a4a4; values centred around
  `W/2 + (int)(180 s)` (PC 580, 3DS 272) as the full option list separated by MENU_OPTIONSEPARATOR "/" (0xa9) with
  gaps `(int)(10 s)` (0x813f4c): current option white, others 0xa4a4a4 (volumes as numbers, UNVERIFIED format).
  Scroll arrows `systemAddMdl/arrows.png` (`DAT_00893e2c`), 3 frames src (0, 0x5e+0x3c k, 0x3b, 0x99+0x3c k)
  (up) / (0x3c, …, 0x77, …) (down), centre x `W/2 − (int)(380 s)`, y `H/2 ∓ (int)(100 s)` ± bob
  `(int)(sin((t%100)/100·2π)·(3k+5) + 5k + 5)·s`, half `(int)(15 s)`; shown when rows are hidden above/below.
- 3DS-meaningful: required wins, time limit, SFX/BGM volume, controls (remap), hitstun meter, high contrast + P1/P2
  colours, grey background, armor points (+%), attack indicator (+mirror), pause hold, pause menu on start,
  tournament mode, language (if the text/font assets are shipped). Not meaningful: screen mode, resolution, 32-bit
  mode/textures, play audio when inactive; advanced effects only as an optional performance toggle; autosave replay /
  replay menu on start only if replays are implemented.

## A.6 Other entries (one paragraph each)
- **Training**: sub-menu "How to Play" (9 pre-rendered manual pages per language, `extracted/texture/manual/`) and
  "Practice" (Menu Exit → VS select scene 6 with `DAT_0087faa8 = 3`, rounds 1, timer 999 in Game Init; the
  training pause menu/options belong to the battle scene).
- **Memory**: in-menu replay browser (state 500, list FUN_00658240 count, entry FUN_00656950); confirm starts the
  battle scene (2) directly from the replay header (`DAT_00894268..`, sub-mode `faac = 4`); after the replay the
  menu reopens at state 500. Scene 10 (FUN_006338b0, "Memory Init") is a separate list screen with no writer of
  `DAT_00885870 = 10` found → probably legacy/unreachable (UNVERIFIED).
- **Ranking**: state 400, local arcade score tables, 9 pages (`DAT_009b164c` 0..8, Left/Right; page meaning
  UNVERIFIED), rows slide in staggered (`DAT_009b1694`). Arcade clears enter names via state 0x19a.
- **Online**: main item 2 → scene 13 (FUN_00597f40, Steam lobbies). Not portable as is (the 3DS port has its own netplay).
- **AOS2 NEWS**: text ids 0x95..0x98 and label 0x258 and the social/news textures are registered/loaded but no draw
  reference was found → not shown in this build (UNVERIFIED).
- **Exit**: item 8 → scene 12. On 3DS: return to HOME / close app (or omit the item).

## A.7 Proposed 3DS layout (keep the original look; A = confirm, B = cancel)
Top screen 400x240 uses the original formulas with s = 0.4 (all numbers above). Problem: font scale H/768 = 0.3125
gives 8.75 px menu text, 7.5 px subtitles, ≈8 px config rows, 10 px help line — too small. Proposal:
- **Title (state 10) / logo scene**: top = original (stage 0x13 + white flash; logo 204 px, daidai/shinden 240 px).
  Bottom: black or a dimmed static copy of ttl_logo; "PRESS A" not in the original — omit.
- **Main menu**: top = logo + band + carousel exactly as A.4.3 but draw the item texts with menu_1080 at scale 0.5
  (20 px line; or menu_768 at 0.6) — still spaced 80 px, so 5 items visible like the PC. Bottom (320x240): the help line
  (ANNOUNCE text, system font, scale ≥ 0.45, typewriter kept) in the original gradient band at the bottom of the lower
  screen, plus optional tappable list of the 9 items (selected white, others 0xa4a4a4) on the blue-pill header.
- **Sub-menus (difficulty/match/training)**: top = header pill + row bands (32 px pitch, 24 px bands) with menubig
  titles at its 0.3125 scale (16 px, legible); subtitles move to the bottom screen (menusubtitle at 0.6) together with
  the help line. Same slide/fade timings (20 f).
- **How to Play**: page image on the top screen scaled to 240x240 (pages are 1024², text-heavy → legibility to be
  checked; alternative: bottom screen 240x240 zoomable).
- **Config**: move the whole panel to the **bottom screen** (320x240): orange pill header 20 px, 9 visible rows of
  22 px (menu font at 0.5), label column x 8, value column centred at 230, same colours/arrows/scroll rule
  (`top = clamp(cursor − 4, 0, rows − 9)`), only the 3DS-meaningful rows (A.5); top screen keeps the stage, the logo
  hidden (as the PC: logo alpha only in state 100) and the main band at its raised position with "Config" selected.
- Input: A = confirm (OK slot), B = cancel (Cancel slot); keep the alt pairs (Primary-weapon button = confirm,
  Dash button = cancel) only if the in-game mapping keeps them distinct from A/B; repeat 20/5 frames.

## A.8 Port implementation (src/app/menu.c, menu_draw.c, text_c2d.c; deviations)
- Fonts pre-scaled offline to on-screen line heights (system 16, menu 18, menubig 20, menulabel 12,
  menusubtitle 14, winquote/dialogue 16 px) and drawn 1:1 (A.7's 0.5–0.6 proposal, but without runtime
  minification blur). Carousel pitch 120 px instead of (int)(200 s) = 80 (item width up to 101 px); side items
  at 0.85 scale; band 22 px (PC 12 px at s), raised by 184 px per open sub-panel instead of 196.
- Sub-panel subtitles, the config panel and the help line are on the bottom screen (A.7). Config keeps 18 of
  27 rows + 2 port rows (City sprites, wireless input delay); dropped: screen mode, resolution, 32-bit mode,
  32-bit textures, advanced effects, autosave replay, play audio when inactive, replay menu on start. Values
  stored only (not yet read by the match): hitstun meter, high contrast + colours, tournament, pause options,
  grey background, armor points, attack indicator. Language: en/ptbr/sp (glyph set packed); jp/chs/cht/ko/ru
  need their font folders. Language value names are hard-coded native names (0xae..0xb5 not in the text table).
- Port-only return paths (coming soon, lobby, input lab, port select) resume the main menu instead of the title
  (`menu_resume_next`); original return paths (select cancel, story end) go to the title as Menu Init does.
- Match → 2P Game and Online both open the local-wireless lobby; Memory/Ranking show "coming soon".
- How to Play: 512² RGB565 page on the top screen fitted to 400 px width, Up/Down scrolls (PC: 1024² at s).

## Open questions / UNVERIFIED
- Exact display of numeric config values (volumes/time) and the text after "ver" (0x94).
- Ranking page meaning (9 pages); How to Play arrow placement; first 51 logo frames.
- Menu-text bind semantics of slots edc/ee8 (Primary/Dash) inferred from KEY_* label order.

# B. Character select, stage/BGM select, VS sequence, loading, result, continue

Conventions: `s = _DAT_009b1000 = min(W/800,H/600)`, f = 60 Hz frames. Big tables:
`docs/re/modes_select_pools.tsv` (pool order, unlock flags, default cursors),
`docs/re/modes_select_charstats.tsv` (stats panel values), `docs/re/modes_select_sprites.tsv`
(every 2D element of ArcSelect/VS/loading with src/dst/colour), `docs/re/modes_result_sprites.tsv`
(result + continue), `docs/re/modes_result_winquotes.tsv`.

SE ids (`src/sim/se_table.h`): 0 sh_decide, 1 sh_cancel, 2 cursor5, 3 error3 (buzzer), 4 pi38
(option value change), 5 decide11 (result rank stamp). Transitions (FUN_00561fc0, hud.md §5):
1 = to black +0.05/f (20 f), 2 = to black +0.01/f (100 f), 5 = from black 0.05/f, 0x11 = 20x15 black
tiles, tile (c,r) starts after c+r frames, +0.02/f (DAT_00813ed8; covered after 83 f), 0x13 = tiles
shrinking (open). "Wait for the transition" = `DAT_0087e7c0 == 0`.

## B.0 Menu input layer (shared by all three scenes)

`FUN_0061b2e0(dev, &mask, ...)` (core.md §2.1 bit table) → `FUN_0061c100(mask, st, i)` fills a
0x40-byte per-controller struct `DAT_009b6a44 + 0x40*i` (int per button):
+0x00 Right (bit 8), +0x04 Left (4), +0x08 Up (1), +0x0c Down (2), +0x10 menu OK (0x800),
+0x14 menu cancel (0x1000), +0x18 D (0x80), +0x1c A (0x10), +0x20 B, +0x24 S, +0x28 H, +0x2c Shield,
+0x30 Pause. Value 3 = "fire this frame". **Directions auto-repeat**: counter `DAT_009b1234/123c/1244/124c`
per direction; fire on the first frame, then nothing for frames 1..19, then every 5th frame (≥20, %5==0).
Buttons fire only on the press edge.
- confirm `FUN_0061c7b0` = OK==3 || (A==3 && cancel not pressed/held)
- cancel `FUN_0061c7e0` = cancel==3 || (D==3 && OK not pressed/held)
3DS: A = confirm (OK/A), B = cancel (cancel/D) — matches.

## B.1 ArcSelect (scene 8): arcade / story character select

Init `FUN_0079df00`: substate `DAT_009b6a30 = 100`, timers `DAT_009b6a34` (substate) /
`DAT_009b6a38` (scene frame) = 0, all anim floats 0; menu stage 4 + camera (`FUN_007aef30`);
pool per player `0x9b8de0+12p` (order in pools.tsv: SB, Nath, Sham, Mira, Tsih, Alte, Sora, Suguri,
Saki, Iru, Nanako, Kae, Kyoko, Hime, + Sumika only in arcade `DAT_0087faa8==0`; no random entry,
Sumika 2nd never). Cursor `DAT_009b6ac8` = index of Suguri when coming from the menu
(`DAT_00885874==4`), else kept. Help line ANNOUNCE 0x34 (SELECT_COMMAND), transition 5 (fade in),
BGM `FUN_004bee60(0x16,1)`.

Update `FUN_0079e590` (controller 0 only, local_34 = 0):

| substate | input | effect |
|---|---|---|
| 100 command mode | Up/Down | `DAT_009b6ae8` −1/+1, wraps 0..1, SE 2 |
| | confirm | `DAT_0087fabc = (6ae8 != 0)` (0 Standard, 1 Advanced), → 200, SE 0, ANNOUNCE 0x2b (CHARACTER) |
| | cancel | → 0x3b6, transition 1, SE 1, announce off |
| 200 character | Right | cursor −1 (wrap to n−1), SE 2, slide `DAT_009b8d74 = +1.0`, ring/portrait `DAT_009b8cc0 = 0` |
| | Left | cursor +1 (wrap 0), SE 2, `d74 = −1.0`, `cc0 = 0` |
| | (every frame) | `DAT_0087fac4 = pool[cursor]` |
| | confirm | → 900, transition 2, SE 0, `DAT_0087fae8 = controller` |
| | cancel | → 100, SE 1, ANNOUNCE 0x34 |
| 900 / 0x3b6 | — | when the transition is done → Exit |

Exit `FUN_0079e4b0`: BGM `FUN_004bee00(0,1,1)` (fade); 900: story → `FUN_007befa0(char)` +
`FUN_007bec90(0)`, arcade → `FUN_004a7cc0(char)` + `FUN_004a7910(0)`; 0x3b6 → scene 4 (menu).
`DAT_00885874 = 6`.

Animation floats (all clamp 0..1): `cb8` command panel +0.05/f in 100, else −0.05 (frozen in
900/0x3b6 until the transition ends); `cd0` character UI +0.05 in 200 else −0.05; `cc0` portrait/ring
+0.1 in 200 (reset to 0 on each move → 10-frame fade-in); `ca8` +0.1 in 200 else −0.05; `d74`
slide → 0 at 0.1/f (10 f). `ca4` (white flash) is never raised here.

**No colour, handicap or random choice here.** The arcade builder writes colour 0 into both
+0xc/+0x10 of every fight entry (`FUN_004a7cc0`, `uStack_18/uStack_14 = 0`) and Game Init uses
that directly (3 would be "auto") → arcade/story always use palette A (folder `chara<Name>A`). There
are no voices anywhere in these screens (only SE 0–5).

Draw `FUN_0079ed60` (all coordinates in sprites.tsv): sl_bgp backdrop → menu stage 4 3D → white
flash quad → black name band at H/2+50s ±24s → portrait column at (80s, H/2) 160×512 (animated grey
noise `sel_chara02` (f/4)%8 under the portrait `sel_chara0[(c+4)/12]` cell (c+4)%12) → name plate
`sel_name0` 256×64 at (W/2, H/2+50s) → **icon carousel** `FUN_0079fb30` (11 icons on y = H/2+150s,
spacing 90s, x = W/2 − k·90s + 90s·d74, so higher pool index is further left; alpha
(255−55|k|)·cd0, non-selected tinted 0x808080; cleared-star `simplestar2`; rotating double ring
`lockbb`+`lockb` 2°/frame) → **stats panel** `FUN_007a49d0` at (W−125s, H/2) (charstatbase,
DIFFICULTY Easy/Medium/Hard + 4 bars MELEE/PROJECTILE/MOBILITY/METER GAIN with rank letter D..S,
values in charstats.tsv) → **command-mode panel** `FUN_0079f410` (green header band "SELECT COMMAND
MODE" y = H/2−120s, two full-width bands Standard/Advanced at H/2−60s and H/2+12s with title +
subtitle text from menu_header/menu_subtitle) → white letterbox bars (y < H/2−250s, > H/2+250s) →
title `sel_system (0,0x200,0x3ff,0x27f)` at (W/2, H/2−275s) → transition → help line.

## B.2 VS Select (scene 6)

Init `FUN_0079acc0`: substate 100. Coming from the menu (`DAT_00885874==4`) it resets per player
handicap `DAT_009b8c58 = 2`, colour `DAT_009b8c60 = 0` (Auto), hyper control `DAT_009b8c68 = 0`,
input delay `DAT_009b8c70 = 4`; cursors P1 = Suguri, P2 = Sora, **stage cursor `DAT_009b6ad8` = index of
stage 15 (Nature 10,000 Years After)** (0x79beb2 — stages.md §2.2 says "first entry"; the code
overwrites the 0 with the index of 15), BGM cursor `DAT_009b6adc` = index of 10 ("????"). Otherwise
(rematch from the result screen) everything is kept. Option-enable flags: handicap `8c82` and colour
`8c84` = 1, hyper-control `8c86` = 0 for a CPU slot (`DAT_0087fab4[p]==10`) else 1; handicap disabled
when config `DAT_00881f70==1`. Slot state `DAT_009b8c40[p]`: 0 choosing, 1 options, 2 ready,
−1 not human (CPU slot); `DAT_009b8c48[ctrl]` = slot driven by controller ctrl (= `DAT_0087fab4[ctrl]`
if < 10, else −1). Pool = arcade pool + Sumika (if unlocked) + **1 = Random** last. Stage/BGM
lists: stages.md §2.2 / music.md §2. ANNOUNCE 0x2b, transition 5, BGM `FUN_004bee60(0x16,1)`.

Update `FUN_0079c110`:

| substate | slot state | input (of the controller driving that slot) | effect |
|---|---|---|---|
| 100 | 0 | Up / Down | char cursor −1/+1 (wrap), `d74 = −1/+1`, `cc0 = 0`, SE 2; `DAT_0087fac4[p] = pool[cur]` |
| | 0 | confirm | → 1, option row `DAT_009b6ad0[p] = 4` (Accept), SE 0, flash `ce0 = 1`, `DAT_0087fae8[p] = ctrl` |
| | 0 | cancel | 2P / online / own slot: → 0x3b6 (to menu), transition 1; CPU mode while picking the CPU slot: back to P1's options (P2 → −1, P1 → 1). SE 1 |
| | 1 | `FUN_007a2a20`: Up/Down row 1..4 wrap (online 0..4), SE 2, help line per row (0x2e HANDICAP, 0x2f COLOR, 0x30 INPUT, 0x31 ACCEPT); Left/Right change the row's value, SE 4 (SE 3 if the option is disabled) | handicap 0..5 (texts 0x1d8..0x1dd, labels UNVERIFIED); colour 0 Auto / 1 P1 / 2 P2 / 3 SP (SP only for Sora, StarBreaker, Suguri, Saki, Nanako, Kae, Hime, Sumika); input 0 Standard / 1 Advanced |
| | 1 | confirm (any row) | SP colour of a DLC character not owned (Hime/Sumika app 0x16ece8, Kae/Nanako 0x196a18, Saki/SB 0x38f1da) → SE 3; else → 2 (ready), flash, SE 0; in CPU mode the controller moves to the other slot (set to 0 unless already 2) |
| | 1 | cancel | → 0, SE 1 |
| | 2 | cancel | → 1, SE 1 |
| | both 2 | — | → 200, ANNOUNCE 0x33 (STAGE) |
| 200 | — | controller 0 only: Left/Right stage −1/+1 (`d7c = −1/+1`), Up/Down BGM −1/+1, all wrap, SE 2 | |
| | | confirm | → 300, `ca4 = 0`, stage resolved (Random → `FUN_007aaf90`), SE 0, announce off, BGM fade `FUN_004bee00(0,1,1)` |
| | | cancel | → 100 (2P: both slots → 1; CPU: slot 0 → 1), SE 1, ANNOUNCE 0x2b |
| 300 | | — | `ca4 += 0.01`/f (screen fades to white, 100 f); at 1.0: remove stage 4, init chosen stage, → 0x136, jingle `FUN_004bee00(2,1,0)`, Random characters resolved `FUN_007ab0c0(p)` (cosmetic-RNG pick from the pool without 1) + flash |
| 0x136 "VS" | | — | `ca4 −= 0.025`/f (white → the chosen stage behind the two portraits, 40 f); after 100 f → 900, transition 2 (100 f) |
| 900 / 0x3b6 | | — | transition done → Exit |

There is **no separate VS splash**: the "VS screen" is the select screen itself after the stage
swap (portraits, names, stats stay; stage-select UI fades with `ca0`), white flash + jingle 2.

Exit `FUN_0079bfc0` (900): `DAT_0087fabc[p] = 8c68[p]`, colour `DAT_0087facc[p] = FUN_007a2880(p)`
(P1→0, P2→1, SP→2 if allowed, **Auto→0**: the opponent-aware Auto code at 0x7a290e is unreachable in
this build; Game Init only maps the value 3 to "P2 gets colour 1 on a mirror match", which VS never
produces → a local Auto/Auto mirror match uses colour 0 for both (UNVERIFIED in game; the online path
FUN_007b6ac0 does flip P2)), handicap `DAT_0087fad4[p] = 8c58[p]` (→ max HP 4500/6750/9000/13500/18000/27000 for 0..5, jump table 0x69d038 in FUN_0069cc40; the decompile's `< 6 ? 4500` is a Ghidra switch-collapse artefact, see §D.3), BGM resolved (`FUN_007aaed0`), scene 2,
`DAT_00885879 = 1` (new match). 0x3b6 → scene 4.

Animation floats VS: `cd0[p]` +0.1 while slot state 0 else −0.1; `cd8[p]` (options) and `cc8[p]`
(standing sprite) +0.1 while state 1/2 else −0.1; `cc0[p]` +0.1 unless state −1; `ce0[p]` −0.025;
`ca8` +0.1 while substate < 400; `ca0` (stage UI) and `d80` (portrait slide-out) +0.1 in 200 else −0.1
(d80 frozen from 300 on); `d7c` → 0 at 0.1/f; `cbc` +0.025 (< 400).

Draw `FUN_0079d420`: sl_bgp → stage (4, later the chosen one) → 3D stage cards `FUN_007a91f0` →
white flash → stage name band `FUN_007aa670` → BGM band `FUN_007aabb0` → portraits `FUN_007a6710`
(P1 left, mirrored; dimmed black silhouette while choosing — the bright art is the wheel icon) →
stats panels `FUN_007a49d0` (P1 at 125s, P2 at W−125s) → icon **wheels** `FUN_007a3600` (one
semicircle per player hugging the screen edge, 9 icons, k/6·π spacing) → option rows `FUN_007a6f90`
→ standing sprite in the chosen colour `FUN_007a9910` (`<xx>_std00..02`, (f/8)%4 ping-pong) → letterbox
→ 2P controller prompts `FUN_007a40d0` → PLAYER1/PLAYER2/CPU labels `FUN_007a9c90` at H/2−235s →
title. Details: sprites.tsv.

## B.3 Loading screen

Game Init `FUN_005f4650`, new match only (`DAT_00885879`): `FUN_005f95c0` presents one frame: clear
black, `systemMdl/loading.png` row 0 "NOW LOADING..." 256×32 at (W−120s, H−40s); then loads
synchronously (unload all stages + character resources). The screen was already black (transition 2
of the select). Battle fades in with transition 0x13. 3DS: identical (bottom-right of the top screen
at s=0.4: 102×13 at (352,224)); also usable for our own romfs loads.

## B.4 Result screen (scene 3)

Who goes there: Game Exit `FUN_005f5780` computes `DAT_009b6930 = (P1.roundsWon(+0xd0) <= P2.roundsWon)`
(1 = P1 lost), winner index = that value, `DAT_009b6940 = winner char`, sums per-round records into
`DAT_009b6948` time bonus, `694c` fight time (s), `6950` armor bonus, `6954` attack bonus, `6958` max
hit, `695c` total; `DAT_009b6960` = rank 0..4 (S..D) `FUN_00617740`; win quote `FUN_00617110`
(winquotes.tsv: random 01..03 on the cosmetic RNG, forced X quote vs. specific opponents); arcade wins
add the total to the run score `DAT_008940cc`. Then the stepper: VS → scene 3; arcade/story
`FUN_004a7910(1)` / `FUN_007bec90(1)`: **loss (`param==1 && DAT_009b6930==1`) → scene 3 directly**;
win → next sequencer entry (each fight is followed by a type-2 entry = scene 3).

Init `FUN_00796030`: substate `DAT_009b68e0` = 0x6e (110) for an arcade/story loss, else 100; loads
`<xx>_res_bgp`, winner palette `res_chr`/`res_sys`; prepares the quote text (wrap W−250s if W/H ≥ 1.5,
else W−100s); transition 0x13; BGM jingle 1 `FUN_004bee60(1,0)` (no loop); Steam stats/achievements
(skip); autosave replay (skip).

Update `FUN_00796ef0` (controller from `FUN_0061ae20`):
- 100/0x6e: confirm while t (`DAT_009b68e8`) ≤ 199 → t = 200 (skips the intro); confirm later →
  BGM fade `(0,1,1)` and: 0x6e → continue (B.5); 100 → 900 + transition 0x11. At t == 350
  ANNOUNCE 0x38 "Press Hyper to save a replay" (`DAT_009b6914 == 3` = H pressed → `FUN_0079a650`)
  — **skip on 3DS**.
- 900 + transition done → Exit `FUN_00796ca0`.

Timeline (draw details in result_sprites.tsv): f0 char art slides in from the left for 50 f (+
optional rotation from record +0x2c8); f50 bands/strips appear (0.05/f, 20 f) with the scrolling
"winner's report" text and the win quote on the lower rotated band; f200 score rows slide in
staggered (row k complete at 220+10k); f300 SE 5 + rank emblem zooms in (40 f). The score panel is
drawn in every mode (arcade, story, versus) — `FUN_00797ea0` has no mode test.

Exit `FUN_00796ca0` (`DAT_00885874 = 3`, all stages exited):

| mode | flag | next |
|---|---|---|
| arcade | normal (win) | play counter `DAT_0087b76c++`, save `FUN_007c47f0`, `FUN_004a7910(0)` → next entry |
| arcade | continue `DAT_009b692e` | `DAT_008940c8++` (continues), score `DAT_008940cc = 0`, index −1, `FUN_004a7910(0)` → same fight again (a "random" opponent is **not** re-rolled: the stepper wrote the pick back into the entry's +8, so the same opponent returns — §0.3) |
| arcade | game over `DAT_009b692d` | ranking `DAT_008940d0 = FUN_00792f40(difficulty, score, char)` (inserts into the top-10 table `DAT_0087b7d0 + diff*0x168`, entry 0x24 bytes, name "-------"), `FUN_004a8390` (arcade-end unlocks), scene 4 → Menu Init sees `DAT_008940d0 < 10` → ranking name entry (menu section) |
| story | win / continue / game over | as arcade with `FUN_007bec90`; game over: `DAT_008940d0 = 0xb` (no ranking), `FUN_007c3e80`, scene 4 |
| versus | — | scene 6 (VS select, all cursors/options kept = "rematch" via character select) |
| online / replay | — | scene 0xd / scene 4 |

## B.5 Continue screen (arcade/story loss)

Same scene, substate 0x122 entered from 0x6e on confirm: `DAT_009b68e4 = 0`, countdown
`DAT_009b6988 = 9`, ANNOUNCE 0x3d RESULT_CONTINUE. Each frame: if `e4 % 100 == 99` → countdown−1 + SE 2
(first tick 98 f after entering, then every 100 f: digits 9..0, ≈1000 f ≈ 16.7 s); **cancel also
decrements** (+SE 2, skips ahead); countdown < 0 → game over (`692d = 1`), transition 0x11; confirm →
continue (`692e = 1`), SE 0, transition 0x11. Overlay `FUN_0079a240` fades in with `cont` (0.05/f,
20 f): screen dim 164·cont, white band at H/2−110s ±32s with "GameOver" (res_continue 800×100 → 400×50),
"Continue?" at H/2−50s, big digit 128×72 at H/2+30s (res_continue cells 256×144: col d%4, row d/4,
y offset 100). Result screen keeps drawing underneath.

## B.6 3DS layout proposals (top 400×240 = the original at s = 0.4 unless noted; bottom 320×240)

**ArcSelect** — top: the original layout at s = 0.4 except the letterbox (H/2∓250s exceeds 240 →
bars of 20 px: keep) and fonts. Portrait 64×205 at (32,120) would be clipped by the 20-px bars (the
original also overlaps them: it is drawn before the bars) — keep. Command-mode bands at y 72..108 /
101..137 with the original text in the system font. Bottom: the 14/15-icon pool as a touch grid
(5 columns × 3 rows of 56×56 `sel_icon` cells at 0.22 scale, in the original pool order left→right =
carousel right→left), the stats panel (charstatbase at 0.6, 156×156) on the right and the
Standard/Advanced choice as two touch buttons during substate 100. D-pad Left/Right keep the
original carousel semantics (Right = previous index), A/B = confirm/cancel, touch = jump cursor +
confirm on second tap.

**VS select** — top: both portraits + wheels + stage cards + name bands as the original at s = 0.4
(wheel radius 72..144 px, stage name 122×13, BGM band y 176..200). Bottom: per-player option panel
(handicap / colour with the standing sprite preview `<xx>_std` at 1:1 / input / accept) side by side
(160 px each) during substate 100, then the stage list (thumbnails 64×64 from selMdl/stage<xx>) and
BGM list during 200. The 3D stage-card carousel can be a 2D projection of the same positions
(UNVERIFIED projection; ortho assumption: x' = W/2 + x·s, scale by depth not needed if ortho).

**Loading** — top only, as B.3. **Result** — top: the original at s = 0.4 (char art 410 px square,
mostly off-screen as on PC; score rows at x 40, digits 19×19 at s=0.4 → legible); win quote text on
the lower band in the system font; bottom: the score table re-laid at 0.6 (labels res_tag, digits
res_system) + rank emblem + "A: continue". **Continue** — top as original (GameOver 160×20, digit
51×29); bottom: big digit + "A: Continue / B: speed up" touch buttons.

## B.7 What must change in `src/app/main.c`

The current dev menu (one console loop: D-pad rows P1/P2/stage/city-LOD, L/R BGM, C-pad volumes,
A = local vs CPU, Y/B wireless, X lab) has to become a scene machine like `FUN_006325f0`
(scene id + "init next frame" flag + previous scene):
1. Roster: replace `roster[]` (Suguri-first, includes 18) with the pool order of pools.tsv per mode
   (no Sumika 2nd; Sumika only arcade/VS; Random only VS), gated by unlock flags (save data).
2. Character select must collect per player: colour (0..2 + SP gating; Auto = 0), handicap (0..5,
   default 2), hyper control (Standard/Advanced → `aos` equivalent of `DAT_0087fabc/fac0`, today
   not selectable), and for arcade/story the command-mode step before the character.
3. Stage default = index of stage 15, BGM default "????"; stage Left/Right and BGM Up/Down live on
   the stage-select step (the current L/R BGM mapping becomes Up/Down there).
4. Add the post-select sequence: SE 0, BGM fade, 100 f white, stage swap + jingle 2, 100 f, fade 2,
   NOW LOADING frame, battle with transition 0x13; and after `flow.matchEnd` the result scene (jingle
   1, confirm → VS select with previous choices instead of returning to the menu).
5. Arcade/story: the sequencer (lead) + continue screen + game over → ranking/menu; CPU difficulty
   from the menu (`DAT_0087fab0`).
6. Keep input lab / wireless / volume options as menu entries (they are port additions).

## Open questions / UNVERIFIED
- Handicap labels 0x1d8..0x1dd (the HP effect is settled, §D.3)
  (registered outside FUN_00633e50 — literal strings?).
- Auto/Auto mirror match in local VS: both colour 0? (dead code at 0x7a290e).
- Projection of the stage-card camera (FUN_007a3510: eye (0,0,−500), far 1000).
- Result background compositing via `DAT_009b6a20/6a24` render targets; whether the win quote is
  typed out progressively (`DAT_009b69d8` counter in FUN_0079a7d0).
- `sel_icon0[id/16]` index expression in FUN_0079fb30/FUN_007a3600 (texture chosen by `[4*eax+0x9b8e68]`).

# C. Story conversation scene (scene 5)

Coordinates: W,H = `DAT_009b0fe4/fe8`, s = `_DAT_009b1000` (hud.md §2). Screen rects are y-down, origin
top-left; the noise band uses the centred y-up vertex space (hud.md). "800x600" = value at s = 1.

## C.1 Loading (once, at start-up)

| Step | Fn | Notes |
|---|---|---|
| Load all scripts | `FUN_004e2600` (called from `FUN_00596630`) | Builds the id list 10..13, 20..23, ..., 140..143 (56 ids). For each id it calls `FUN_004e3d20(id)` and stores the record vector in the std::map `DAT_00894140` (id → vector<ACT>) |
| Resource name | `FUN_004e4fa0(id)` | `"TEXT_CONVERSATION"` + id zero-padded to 3 digits (prefix `DAT_00816f78` / `f7c` for id < 10 / < 100) → `TEXT_CONVERSATION010` … `TEXT_CONVERSATION099`, `TEXT_CONVERSATION100` … `TEXT_CONVERSATION143` |
| Parse | `FUN_005d6430` | `FindResourceA(type "TXT")`. Fails with the log "Failed to read script file." |
| Text file | `FUN_004e49c0(id, lang DAT_00881ec0)` → `FUN_005966e0` (lang path) → `FUN_005d7620` | `story_` + char + `_` + `0`(`DAT_00816ecc`) + (id%10+1). Char by id/10: 1 sora, 2 alte, 3 tsih, 4 mira, 5 sham, 6 nath, 7 starbreaker, 8 suguri, 9 saki, 10 iru (`DAT_00816f20`), 11 nanako, 12 kae (`DAT_00816f30`), 13 kyoko, 14 hime. Lines have the form `<NNNN> text`, and a literal `\n` in the text is a line break (the font layout handles it, section A) |
| Ref substitution | `FUN_004e3d20` loop (stride 0x88) | If a record's text (+8 left, +0x20 right) starts with `'0'`, it looks up that 4-digit key in the text map and replaces the text with the line |
| Textures | `FUN_004e2910` (called from `FUN_005dff20`) | Loads `noise0..7` (`FUN_005df500(0x38,…)` = systemMdl) into `DAT_0089415c..178` and 8 vertex buffers `DAT_0089417c..198` (`FUN_004789e0`), with buffer k bound to noise k (+0x20). Freed by `FUN_004e3100` |

Parser grammar (`FUN_005d6430`): blank lines and lines starting with `/` (0x2f) are skipped. State 0 waits for `[ACT]` (memsets a 0x88-byte record) or `[EOF]` (ends parsing). Then come 5 lines in a fixed order:
1. `< name >< band >` (left slot)
2. left text
3. `< name >< band >` (right slot)
4. right text
5. `< f0 >< f1 >< f2 >< int >`

The record is pushed with `FUN_005da440`. Tokens are the text between `<` and `>`, trimmed of `" \t"`. A missing `>` still works: TEXT_CONVERSATION123 has `< カエ >< 帯あり ` and 101:28 has `< ナート >< `.

Text line semantics: the first code point (`FUN_005d7d20` decodes UTF-8) decides.
- U+FF04 `＄` → the trimmed line is stored, i.e. `"＄"`.
- U+3000 `　` → `" "` (one space).
- `[text NNNN]` → `"NNNN"` (substr 6,4).
- Anything else → empty.

**ACT record (0x88 bytes)**, vector at `DAT_00894150` (copied from the map in Init), accessor `FUN_004e7930(i)`, count `FUN_004e8040`:

| Off | Type | Source | Meaning |
|---|---|---|---|
| +0x00 / +0x04 | int | left/right name | Name id from the std::map `DAT_009b1054` (name → id, static init `FUN_00456820`); not found → 0 |
| +0x08 / +0x20 | std::string | left/right text | Dialogue line, `"＄"`, or `" "` |
| +0x38 / +0x39 | byte | left/right band token | `token != "帯なし"` (`DAT_0081f274`) → band on. `帯あり` or an empty token both count as on |
| +0x3c | string | field 2 (3rd token) | SE wav name, or `なし` |
| +0x54 | string | field 0 (1st token) | Backdrop: `なし`, `黒地` (black) or `白地` (white, never used) |
| +0x6c | string | field 1 (2nd token) | "BGM" token, always `なし` (see C.2) |
| +0x84 | int | field 3, `FUN_007e4d5d` (atoi) | **Unused.** No conversation function (`FUN_004e2600..004e5ee0`) reads +0x84. Always −1, except script 143 act 13 = 200 |

Name map `DAT_009b1054` → id → convMdl texture name (`FUN_004e43a0`, path `FUN_005df500(0x2e,…)` = `/convMdl/`):

| Name | Id | Texture | Name | Id | Texture |
|---|---|---|---|---|---|
| ソラ | 3 | sora | スグリ | 10 | suguri |
| アルテ | 4 | alte | サキ | 11 | saki |
| ツィー | 5 | tsih | イル | 12 | iru |
| ミラ | 6 | mira | ナナコ | 13 | nanako |
| シャム | 7 | sham | カエ | 14 | kae |
| ナート | 8 | nath | キョウコ | 15 | kyoko |
| スタブ | 9 | star | ヒメ | 16 | hime |
| スミカ | 17 | sumika | 黒幕 | 18 | sumikab |
| なし / other | 0 | none (512², fully transparent) | | | |

- Character portraits are 2048x512, with the art at x 849..1560 (sora).
- At Init, `FUN_004e4630` collects the unique ids of the script's records and loads each texture into the map `DAT_00894148` (id → texture, released in Exit).
- In case 1 the parser also scans `DAT_009b1054` for value 10 into a local. This has no effect on the record (**UNVERIFIED**; it looks like dead code).

## C.2 Scene flow

**Init `FUN_004e35e0(id)`** is called directly by the stepper (`FUN_007bec90` case 1, followed by `FUN_004bfc10(entry+8)` = stage used as the backdrop):
- Resets: `DAT_008940e0=1`, `DAT_008940e4=id`, state `DAT_008940e8`, frame counters `DAT_008940ec/f0`, act index `DAT_008940f4`.
- Clears the backdrop-texture map `DAT_00894138` (`FUN_004e7e50`; nothing ever inserts into it, so the textured-backdrop path in `FUN_004e5af0` is dead) and the record vector.
- Clears the per-slot strings `0x8941a0/0x8941d8` and the name ids `DAT_00894250/58`.
- Copies the records from `DAT_00894140`, loads the portraits (`FUN_004e4630`), and sets the font table `DAT_009b0e64 = FUN_005946c0("dialogue")` (`FUN_004e4890`; fonts `font/<lang>/dialogue_{768,960,1080}.fnt`).
- Creates a CCamera `DAT_0089419c` (`FUN_00465c30`).
- Sets state 10 and calls `FUN_00561fc0(5)`: fade from black, α += 0.05/frame, 20 frames.

**Frame `FUN_004e3a20`:**
1. If `DAT_00885878`, call `FUN_004e5190` (load act 0) and clear the flag. Act 0 is therefore set up on the first frame, during the fade-in.
2. Input: device `FUN_0061ae20` (the first of the 12 pads with input, cached in `DAT_009b1224`) → mask `FUN_0061b2e0` → button states `FUN_0061bc70` into `DAT_008940f8` (layout = player+0x1c, core.md §2.1). So +0x10 `DAT_00894108` = menu OK, +0x14 = menu cancel, +0x1c `DAT_00894114` = A (attack), +0x24 `DAT_0089411c` = S (special).
3. State 10: waits until the transition is over (`DAT_0087e7c0 == 0`, cleared by `FUN_00561c70`), then goes to state 0x14 with `DAT_008940f0 = 0`. Nothing is typed and portraits do not fade during state 10.
4. State 0x14:
   - **Typewriter.** Per slot with `DAT_00894262[i]` set and `0 ≤ DAT_00894248[i] < DAT_008941d0[i]`, add **one character per frame** (`FUN_00458dd0`, UTF-8 code point). The shown text is `0x8941d8[i]` = substr(0, n) of the full laid-out text `0x8941a0[i]`. Both slots type at the same time.
   - **Confirm** `FUN_0061c7b0`: OK == 3, or (A == 3 and cancel ∉ {2,3}).
   - **Fast-forward**: S held (`DAT_0089411c > 1`) and `DAT_008940f0 % 5 == 0` counts as a confirm every 5 frames. `f0` restarts at 0 on each new act, so held S completes the text at f0 = 5 and advances at f0 = 10, i.e. about 10 frames per act.
   - **On confirm:** any slot whose shown text ≠ full text is completed (`n = len`). Only when nothing needed completing does it call `FUN_004e5190` (next act). Next act exists → stay in state 0x14 with `f0 = 0`. Script ended → state 0x5a and `FUN_00561fc0(1)` (fade to black, 20 frames). It then forces `DAT_00894114 = DAT_00894108 = 3`, which has no effect because they are rewritten next frame.
   - `DAT_00894264` (act fade) += 0.05 (`DAT_00813ef4`) per frame, clamped to 1.0 (`DAT_00853b34`). That is 20 frames.
5. State 0x5a: once the transition is done, run **Exit `FUN_004e3730`**. Exit releases the camera, portraits and backdrops, sets `DAT_00885874 = 5`, `DAT_00885878 = 1`, then calls the stepper: story (`DAT_0087faa8 == 1`) `FUN_007bec90(0)`, else arcade `FUN_004a7910(0)`. If the next scene is Endroll (0xb) it also calls `FUN_004bfbc0()` (exit all stages). Otherwise the frame draws (`FUN_004e38e0`).
6. Every frame: `FUN_004bfd30` (stage update), `DAT_008940ec++`, `DAT_008940f0++`.

**No back key and no skip-all.** Cancel only blocks the A-button confirm path.

**Next act `FUN_004e5190`** (returns 0 when `DAT_008940f4 ≥ count`):
- **BGM:** on act 0, or when +0x6c differs from the previous act, it calls `FUN_004bee00(0,1,1)` (pushes are `0,1,1`). This fades the current BGM out over 100 frames. **Conversations have no music**: they open by fading out whatever was playing, and +0x6c is always `なし`.
- **SE:** if +0x3c ≠ `なし` (`FUN_004e6380`, `DAT_00816cb0`), it runs `FUN_007bccb0(0x38, wav, ".\se.dat")` (`DAT_00816cb8`; loads that named sample from se.dat into SE slot 0x38), then `FUN_007bc8a0(0x38)` (volume) and plays it with `FUN_007bc9c0(0x38)`. It also logs with `DAT_00816cc4`. Used wavs: swing_h.wav ×5, chain.wav ×2, jya05.wav ×1 (extracted/se/se/SWING_H.WAV, CHAIN.WAV, JYA05.WAV). The SE plays when the act **starts**.
- **Per slot i:**
  - Text == `"＄"` (`DAT_00816cd8`): `DAT_00894262[i] = 0`. The slot is "not speaking": its previous text stays (not cleared) and is drawn grey, and its portrait is dimmed.
  - Otherwise: `DAT_00894262[i] = 1`. The text is laid out with `FUN_00593430(text, scale = (float)DAT_00881eb8 / FUN_00594570(DAT_009b0e64, H).first, 9999, 9999)`, then `FUN_00593340(box w = (int)(540·s) [0x81412c], h = (int)(88·s) [0x817000])` on the object `0x8941d8+0x38i`.
  - The result is copied to `0x8941a0[i]`, with `DAT_008941d0[i] = length` and `DAT_00894248[i] = 0`. The shown string is then cleared, so the typewriter starts from empty. `"　"` → `" "`: a new, blank, bright line (that slot is "present but silent").
  - `DAT_00894260[i] = band`, `DAT_00894250[i] = name id`.
- Finally `DAT_00894264 = 0` and `DAT_008940f4++`.

## C.3 Rendering `FUN_004e38e0` (order)

1. Clear to `0xff0055aa`, `FUN_00463a00(1.0)`.
2. `FUN_004bfd80(1)`: the 3D stage = backdrop (stage from the sequencer entry +8).
3. `FUN_007a3510(cam)`: ortho W×H 2D camera.
4. **Backdrop fill `FUN_004e5af0`**. a = (int)(`DAT_00894264`·255) (`DAT_00853b94` = 255), cur = act `DAT_008940f4`−1.
   - If cur ≥ 1 and the previous act's +0x54 differs: draw the previous fill with α = 255 − a.
   - Then the current fill: α = 255 on act 0 or if unchanged, else a.
   - `黒地` (`DAT_00816cdc/cec`) → full-screen rect `α<<24 | 0x202020`. `白地` → `α<<24 | 0xffffff` (`FUN_00457c60`).
   - Effect: a 20-frame cross-fade between the stage and dark grey.
   - Data: chapter-0 scripts (x0) open with `黒地` narration acts (acts 0..4–8, e.g. 010 act 0 only, 020 acts 0–6), while chapters 1–3 start directly over the stage. 143 acts 13 and 43 also use `黒地`.
5. **Letterbox:** `(0, 0, W, H/2 − (int)(220s))` and `(0, H/2 + (int)(220s), W, H)`, `0xff000000` (220 `DAT_00817008`). At 800x600: bars 0..80 and 520..600.
6. **Slot draw `FUN_004e54b0(0)`, then `(1)`.** Slot 0 = left/1st-line character, bottom left. Slot 1 = right character, top right.
   - **Band** (if `DAT_00894260[i]`): `FUN_004e3fc0(yc)` with yc = −140s (slot 0, `DAT_00817010`) or +140s (slot 1, `DAT_00817004` = 140), in y-up centre space.
     - Tiles (int)(256s) wide (`DAT_00814160`) from x = −W/2 across W.
     - Quad y from yc − 50s to yc + 50s (50 `DAT_008140ec`), vertex colour `0xff404040`.
     - uv (0 / tile w, 128s ∓ 50s) (128 `DAT_00814134`).
     - Texture noise[(`DAT_008940ec`/4) % 8].
     - It is the same noise strip as the round banner (hud.md §4.1). **Resolved (Part C):** the 8 buffers are `FUN_004789e0(2 polygons, blend 1, 0)` (0x4e2d61), a CPrimitive3D (vtable 0x84a020) with blend mode 1 = SRCALPHA/INVSRCALPHA (FUN_004637d0). Its quad setter (+0x28 = FUN_00479b80) multiplies every u by the bound texture's +0x3c and every v by +0x40 (CTexture vtable +0x2c/+0x30 = FUN_0045bbe0/FUN_0045bbf0), which the loader FUN_0045a6f0 sets to 1/width and 1/height. So **uv are texels of the 512² noise**: u 0..(int)(256s), v = 128s+50s at the top edge and 128s−50s at the bottom (vertically flipped). +0x38/+0x3c (FUN_0047a360/FUN_0047a3c0) reset the world matrix to identity; they do not set addressing. Vertex colour 0xff404040 times the opaque RGB noise. Screen y: slot 0 band H/2+140s ± 50s (390..490), slot 1 H/2−140s ± 50s (110..210).
   - **Portrait**: `FUN_0045bf30(tex, dst, 1.0, src (0x1b8,0,0x647,0x1ff), colour, blend 1)`. Source 1168x512 drawn at 584x256 (half size). Half sizes 292 (`DAT_0081700c`) and 128.
     - cx = W/2 ∓ (int)(300s) (`DAT_008145f0`).
     - cy = H/2 ± (int)(140s) + (int)(50s) − (int)(128s).
     - Slot 0: dst rect **mirrored** (l = cx+292s, r = cx−292s); at 800x600 cx 100, y 234..490.
     - Slot 1: normal; cx 700, y −46..210, partly over the top bar.
     - Colour: `α<<24 | 0xffffff` if the slot speaks (`DAT_00894262`), `α<<24 | 0x808080` if it is `＄` (dimmed).
     - **Corrected (Part C, disasm 0x4e5728..0x4e57b4):** `it = find(prev id)`; if the ids **differ**, `it = end()` (the jz at 0x4e5783 skips that only when they are equal); the outgoing draw needs `it != end` (FUN_0049a210).
       - Id **unchanged**: the same portrait is drawn twice, α 255 − a then α 255 (only semi-transparent edges change).
       - Id **changed**: no outgoing portrait. The new one fades in with α = a and the old one disappears at once.
       - Act 0 fades in with α = a.
     - Both draws use the current speak flag.
   - **Text** (if the shown string is non-empty, `FUN_00459e20`): `FUN_005931d0(obj 0x8941d8+0x38i, x, y − (int)(30s), 0, colour, 1)` (30 `DAT_008140e8`).
     - Slot 0: x = W/2 − (int)(150s) (`DAT_0081414c`), y = H/2 + 140s → (250, 410).
     - Slot 1: x = W/2 − (int)(350s) (`DAT_00814be0`), y = H/2 − 140s → (50, 130).
     - Colour `0xffffffff` when speaking, `0xff808080` for `＄`.
     - Box 540x88s, so the text sits inside its band, beside the portrait.
     - No name plate and no text-box frame: the speaker is shown only by brightness. Text alpha does not fade.
7. `FUN_005622a0` (transition overlay).

Timeline: frames 0–19 fade in from black (stage, letterbox and act-0 bands visible; portraits α 0; no typing) → portraits fade in over 20 frames while typing runs at 1 char/frame → confirm. Last confirm → fade to black over 20 frames → Exit.

## C.4 Data checks

- **docs/re/modes_story_scripts.tsv**: 1146 ACT rows for the 56 scripts (7–47 acts per script). Columns: Japanese name / id / convMdl texture, the raw band token and its effective value, and the text ref, `＄` or `(space)`.
- **Text refs vs text files**: every script's refs are exactly the key set `<0000>..<n-1>` of its `story_<char>_0<k>.txt`, in all 8 languages (jp en chs cht ko ptbr ru sp). Nothing is missing, unused or duplicated (`$SP/conv_tsv.py`, report `$SP/conv_report.txt`).
- Field statistics:
  - f0: `なし` 1092, `黒地` 54.
  - f1: always `なし`.
  - f2: 8 SEs (011 a16 chain, 031 a3 swing_h, 080 a5, 090 a4/a9, 121 a5 swing_h, 140 a4 jya05, 141 a15 chain).
  - int: −1, except one 200.

## C.5 3DS proposal (top 400x240: s = 0.4)

Evaluated at s = 0.4:
- Letterbox 0..32 / 208..240.
- Slot 0 portrait cx 80, 232x102 at y 94..196, band 156..196, text at (140, 164), box 216x35.
- Slot 1 portrait cx 320, y −18..84, band 64..104, text at (60, 52).

The composition fits unchanged (the 1.667 aspect only adds side margin), but the dialogue font at 0.4 scale gives glyphs of about 10 px — readable for Latin text, marginal for kanji (**UNVERIFIED** until section A gives the .fnt line height).

Proposal:
- **Top screen:** exactly the original (stage, 黒地 fill, letterbox, noise bands, mirrored and dimmed portraits, cross-fades), without the text.
- **Bottom screen (320x240):** both text slots at full font size, keeping the diagonal arrangement:
  - Slot 1 panel: upper half, left-aligned at x 8.
  - Slot 0 panel: lower half, indented about 40 px.
  - Each panel has a noise band behind it when band = 1. White when speaking, grey `0xff808080` for `＄`. Same 1 char/frame typewriter.
  - Wrap width = panel width (the original wraps at 540·s).

Keys:
- **A** = confirm (= menu OK / Weapon A).
- **Hold the Special button** (user mapping: X "Special A") = original fast-forward, every 5 frames.
- **B** = nothing (the original has no back key).
- **START = skip the whole script** would be a port addition; the original has none, so make it optional. If added, use the same exit path: state 0x5a + `FUN_00561fc0(1)`.

Open / UNVERIFIED:
- Noise-band blend and uv addressing: resolved in C.3 (alpha blend, texel uv).
- The text scale `DAT_00881eb8` / mode width live in section A. Line breaking: C.6.
- The parser's id-10 scan (case 1) looks like dead code.

## C.6 Port notes (Part C: `src/modes/conv.c`, `tools/aos2story`, `tests/test_conv.c`)

**Text wrap.** `FUN_00593340(obj, w, h)` calls `FUN_0047e2c0(font; text, maxW, maxH, &sx, &sy, 0)`. It lays out every conversation line in its box and is ported literally as `conv_wrap`. The same rules apply to all 8 languages:
- **Words** are runs between `' '` / U+3000; the separator stays at the front of the next word.
- **At a separator or the last code point**, the pending word goes on the current line if `(int)(wordW + lineW + 0.5) <= maxW`. Otherwise a `'\n'` is inserted first and the word's leading separator is dropped.
- **Runs without separators** (CJK, long words) break inside once `word + c` alone reaches the width (`(int)(w + 0.5) >= maxW`): the word so far ends the line and `c` starts the next one. There are no kinsoku rules.
- **Hanging indent:** for text starting with `"` or `「`, continuation lines are indented by xadvance(quote)·sx. A later occurrence of the **same** code point cancels it (the closing `"` does; `」` does not).
- **Explicit breaks:** a `'\n'` from `\n` in the text files is kept. Consecutive `'\n'` collapse into one (literal behaviour).
- **Shrink to fit:** height = the lowest glyph yoff of line 1 + Σ (int)((base+extra)·sy+0.5) per break + the highest (yoff+h) of the last line. While height > maxH, sx and sy each drop by 0.1 (DAT_00853b10) and the layout is redone. At 800x600 this shrinks 15 ptbr and 20 sp lines (down to scale 0.58) and none in the other languages.
- **Width** = FUN_0047ca00: Σ xadv·sx, plus (int)(outline·sx) per glyph when the font has an outline, plus kerning·sx; the widest line counts.
- **Typewriter positions** are UTF-8 byte offsets, advanced one code point per frame over the laid-out text. Inserted `'\n'` count as characters.

**Data checks (host test):**
- All 56 scripts parse to exactly the 1146 rows of `modes_story_scripts.tsv`.
- In all 8 languages every ref resolves and every text key is used; there are 1253 non-`＄` lines per language.
- Wrapping never loses a character.
- Scene runs (en, jp) give 1 BGM fade per script (act 0), the 8 SEs at their acts, and one fade-out. Held Special takes 10 frames per act (5 when both texts are ≤ 4 code points).

**3DS** (keeps the §C.5 proposal):
- **Top screen:** the original composition at s = 0.4 (`conv_view` from `conv_layout_original(…, 400, 240)`).
- **Bottom screen:** the text, in the app's dialogue font (16 px line, scale 1). Wrap boxes 272 x 96 (slot 0, indented 40) and 304 x 96 (slot 1) give at most 6 lines (sp), 4 (en, jp, ptbr, ru) and 3 (chs, cht, ko), with no shrinking.
- **Portraits:** see FORMATS.md "Story conversations" (232x102 RGBA8, at most 3 per scene).

# D. Arcade / story ladders, scoring, endings, staff roll, mode effects on the match

Sources: `tools/re/dec/all.c`, disassembly via `xref.exe dis/rd` on aos2.exe. Table of every ladder step:
`docs/re/modes_ladders.tsv` (generated from FUN_004a7cc0 and the FUN_007befa0 decompile; 491 rows).

## D.1 Ladder data and stepping

### D.1.1 Data

- Step vector at `0x8940b0` (`ecx` of FUN_004aa150 in the builders), pool vector at `0x8940bc` (FUN_004aa170),
  index `DAT_008940ac`. Entry = 0x1c bytes: `+0 type, +4 P1 char | script id, +8 opponent | backdrop stage,
  +0xc P1 colour, +0x10 P2 colour, +0x14 stage, +0x18 BGM`. In the disassembly +4/+8 are written as an
  int[2] (`[ebp+ecx-0x1c]`, 0x7bf0ee) and +0xc/+0x10 likewise.
- Builders (FUN_004a7cc0 arcade, FUN_007befa0 story) both first set `DAT_008940ac = 0`, clear both vectors
  (FUN_004a9f10 ×2), set `DAT_008940a8 = char` (the run's player character, used by the unlock code),
  `DAT_008940c8 = 0` (continues), `DAT_008940cc = 0` (score), `DAT_008940d0 = 10` (ranking position, 10 = not ranked)
  and clear the unlock-message vector `0x8940d4` (FUN_004a9eb0, 0x4a7d15 / 0x7beff8).
- Called from ArcSelect Exit FUN_0079e4b0 only when the select screen ended with state 900 (confirmed):
  `story ? (FUN_007befa0(DAT_0087fac4), FUN_007bec90(0)) : (FUN_004a7cc0(DAT_0087fac4), FUN_004a7910(0))`; otherwise scene 4 (menu).
- All colours in both ladders are 0/0 (not 3 = auto), so Game Init FUN_005f4650 gives both sides colour 0
  (mirror matches never occur: the player is never in their own pool or boss slot).

### D.1.2 Arcade ladder (FUN_004a7cc0, cases for chars 3..17)

- Pool = `{4,5,6,7,8,11,12,13,14,15}` minus the player (Alte, Tsih, Mira, Sham, Nath, Saki, Iru, Nanako, Kae, Kyoko):
  10 entries for Sora/SB/Suguri/Hime/Sumika, 9 for the others. Sora (3), Star Breaker (9), Suguri (10),
  Hime (16) never appear as random opponents.
- 6 random fights, each followed by a type-2 (result) entry, fixed stage/BGM per slot:
  (15 Nature, BGM 0x10 Expendables), (6 War, 0xc Black Hole), (7 Canyon, 0xd Distant Thunder),
  (8 Dust Storm, 0xb Need for Speed), (9 Rain and Sunset, 0x11 Ribbon), (11 Big Bridge, 0xf Shine).
- Boss (7th fight), BGM 0xe Swordfish:
  - player 3..9 (Sora..Star Breaker): **Star Breaker (9)**, or **Sora (3)** when the player is Star Breaker; stage 12 Capital in Flames.
  - player 10..17 (Suguri..Sumika): **Hime (16)**, or **Suguri (10)** when the player is Hime or Sumika; stage 17 Guardian's Chamber.
  - FUN_004a7910 case 0 rewrites the entry's stage when `DAT_0087fadc == 0xc && DAT_008940c8 == 0` → 0xd
    (Whirlpool of Malice) and `== 0x11 && c8 == 0` → 0x12 (Moonlight Dance Hall), i.e. **a no-continue run fights the
    boss on the "secret" stage**. The rewrite is stored back into the entry, so it sticks for retries in that run.
- Then result (type 2) and ending (type 3 → Endroll, scene 11). **Arcade has no conversation entries** and no
  per-character ending: the staff roll is the ending.

### D.1.3 Story ladder (FUN_007befa0, cases for chars 3..16 — Sumika has none)

Same shape for every character (verified from the decompile and the disassembly at 0x7bf0a9..0x7bf1c0):

| step | type | content |
|---|---|---|
| 0 | conv | `TEXT_CONVERSATION0<10k+0>` (k = char − 2), backdrop stage 15 |
| 1,3,5,7,9 | fight | stage **15** for all five (disasm `mov [ebp-0xc],0xf` 0x7bf122, 0x7bf1b9 — resolves the stages.md UNVERIFIED), BGM 0x10, 0xc, 0xd, 0xb, 0x11; opponent of fight 1 fixed for some chars (below), others RANDOM |
| 2,4,… | result | |
| 11 | conv | `…+1`, backdrop 16 Crashed Spaceship |
| 12 | fight | rival (fixed), stage 16, BGM 0x12 Movin' Out |
| 14 | conv | `…+2`, backdrop 19 Sumika's Hideout |
| 15 | fight | **Sumika 2nd form (18)**, stage 19 (0x13), BGM 0x13 Accelerator, timer 999 (D.3) |
| 17 | conv | `…+3` (the ending conversation), backdrop 19 |
| 18 | ending | Endroll (scene 11) |

| char | pool (random fights) | fixed 1st opponent | rival (fight 12) |
|---|---|---|---|
| 3 Sora | 4 5 6 8 9 (4 picks) | 7 Sham | 16 Hime |
| 4 Alte | 12 11 13 14 16 (5) | — | 15 Kyoko |
| 5 Tsih | 12 11 13 15 16 (5) | — | 14 Kae |
| 6 Mira | 12 11 14 15 16 (5) | — | 13 Nanako |
| 7 Sham | 12 13 14 15 16 (4) | 3 Sora | 11 Saki |
| 8 Nath | 11 13 14 15 16 (5) | — | 12 Iru |
| 9 Star Breaker | 12 11 13 14 15 16 (5 of 6) | — | 10 Suguri |
| 10 Suguri | 4 5 7 8 (4) | 6 Mira | 9 Star Breaker |
| 11 Saki | 4 6 8 9 (4) | 5 Tsih | 7 Sham |
| 12 Iru | 5 6 7 9 (4) | 4 Alte | 8 Nath |
| 13 Nanako | 4 5 7 8 9 (4 of 5) | 14 Kae | 6 Mira |
| 14 Kae | 4 6 7 8 9 (4 of 5) | 13 Nanako | 5 Tsih |
| 15 Kyoko | 5 6 7 9 (4) | 8 Nath | 4 Alte |
| 16 Hime | 4 5 6 7 8 (4 of 5) | 9 Star Breaker | 3 Sora |

Final boss of every story = Sumika 2nd form (18); its HP depends on difficulty (D.2). Sumika (17) is offered
only in arcade: ArcSelect Init FUN_0079df00 pushes 0x11 only if `DAT_0087faa8 == 0 && DAT_0087b73d == 1`.

### D.1.4 Random opponent

FUN_004a7910 / FUN_007bec90 case 0, when entry +8 == 1: `n = pool size (FUN_004aa1d0); i = FUN_00459f80(0, n*10) % n;
opp = pool[i]; erase pool[i]; entry+8 = opp` (written back, so a retry/continue refights the same opponent).
FUN_00459f80 runs outside the tick → cosmetic MT `0x875b68` (same as music.md §2). The pick happens when the
fight is *started*, not when the ladder is built.

### D.1.5 Steppers FUN_004a7910 (arcade) / FUN_007bec90 (story)

Identical except the arcade boss-stage rewrite and the end-of-ladder handler:

```
if (index < size) {
  if (param == 3) { DAT_008940c8++ (continues); DAT_008940cc = 0 (score); index--; }   // refight entry index-1
  if (param == 1 && DAT_009b6930 == 1) scene = 3 (Result, "lost"); index NOT advanced
  else { e = steps[index];
         type 0: fac4=e+4, facc=e+0xc, fac8=e+8 (RANDOM resolved), fad0=e+0x10,
                 fae0 = e+0x18 ?: DAT_00882258[fac8*0x2dc] (char default BGM, unused: all entries ≠ 0),
                 fadc = e+0x14 (+arcade 12→13 / 17→18 rule), scene = 2, DAT_00885879 = 1 (new match)
         type 1: FUN_004e35e0(e+4) (Conversation Init with script id), FUN_004bfc10(e+8) (backdrop stage), scene = 5
         type 2: scene = 3 (Result)        type 3: scene = 0xb (Endroll)
         index++ }
} else {  // ladder finished
  scene = 4 (menu);
  arcade: DAT_008940d0 = FUN_00792f40(DAT_0087fab0, DAT_008940cc, DAT_0087fac4); FUN_004a8390();
  story:  DAT_008940d0 = 0xb; FUN_007c3e80();
}
```

Params: **0** = advance; **1** = "a match just ended normally" (only passed by Game Exit when `DAT_00885970 == 1`);
**3** = retry the current fight counting a continue (Game Exit when `DAT_00885970 == 3`, i.e. pause-menu Retry).
No caller passes 2.

`DAT_009b6930` = **index of the winning side** (0 = P1, 1 = P2), set by Game Exit FUN_005f5780 only when
`DAT_00885970 == 1`: `(p0.hp(+0xd0) <= p1.hp)` — a tie counts as a P2 (CPU) win. Also copied to `_DAT_008942b0`
and used by Result Init (0x6e "lost" vs 100 "won") and Result Exit.

Callers:

| caller | call | when |
|---|---|---|
| ArcSelect Exit FUN_0079e4b0 | builder + stepper(0) | start of run |
| Game Exit FUN_005f5780 (faa8 0/1) | stepper(1) if `DAT_00885970 == 1`; stepper(3) if `== 3`; else scene 8 (ArcSelect) | `DAT_00885970`: 1 match decided, 2 pause **Quit** (FUN_0068f4a0 item 3), 3 pause **Retry** (item 2) |
| Result Exit FUN_00796ca0 | see D.2.4 | after the result/continue screen |
| Conversation Exit FUN_004e3730 | `faa8==1 ? FUN_007bec90(0) : FUN_004a7910(0)`; if next scene is 0xb → FUN_004bfbc0 (unload all stages) | |
| Endroll Exit FUN_00519f60 | `faa8==0 ? FUN_004a7910(0) : FUN_007bec90(0)` (after the clear bookkeeping, D.4.4) | always lands in the end-of-ladder branch |

Game Exit keeps the 3D stage loaded when the next scene is the result (3) or a conversation whose backdrop equals
the current stage `DAT_00885978` (local `local_2d`, 0x5f5ed4).

Run flow per fight: fight → Game Exit stepper(1) → (won) type-2 entry → Result (win quote) → Result Exit → stepper(0)
→ next entry; (lost) Result in continue mode (D.2.4).

## D.2 Difficulty, score, ranking, continue

### D.2.1 CPU difficulty is constant for the whole run

`DAT_0087fab0` is written only by Menu Exit FUN_00642880 (`= DAT_009b163c`, menu choice, for arcade, story and
versus) and by replay playback setup (FUN_00633720 Memory Init, all.c:313451 `= DAT_008942a8`).
Neither the steppers nor Game Init FUN_005f4650 nor Player Init FUN_0069cc40 change it, and no AI reads
`DAT_008940ac`/`DAT_008940cc` (only the modes code references them). **No per-stage ramp.** Story uses the same
menu difficulty. Difficulty effects inside the match (already in the sim):
- CPU input fn `(&DAT_00881400)[diff + charId*4]` (FUN_006116c0) — 3 AI levels per character.
- FUN_006f1720: CPU (input src 10) on Hard (2) gains `+DAT_00814bd8` on +0xe4 every 5th frame (src/sim/core.c:637).
- Sumika 2nd form HP 12000 / 15000 / 18000 for diff 0/1/2 (FUN_0069cc40 case 0x12) and pattern density in
  FUN_00770750 / FUN_0076ecd0 (diff branches).

### D.2.2 Match score (FUN_0061ac80, FUN_00619cb0, Game Exit)

At every round end FUN_00608a00 calls FUN_00619cb0, which appends one 0x18-byte record per player to the vector
`0x885a74 + side*0xc` (FUN_004e8cd0):

| + | value |
|---|---|
| 0 | time bonus, **only for the round winner** (`side == (p0.hp <= p1.hp)`): `clamp((int)(((180 − frames/60) / 120.0f) * 10000.0f), 0, 10000)`; `frames = DAT_008858f8` (int division), `DAT_00814174` = 120.0, `DAT_00853ba0` = 10000.0. 180 is a constant (also in story's 999-s fight). ≤ 60 s → 10000, ≥ 180 s → 0 |
| 4 | seconds elapsed `DAT_008858f8 / 60` |
| 8 | remaining HP (+0xd0) |
| 0xc | best combo damage (+0x124) |
| 0x10 | best combo hits (+0x12c) |
| 0x14 | **round score = HP + best combo damage + time bonus** |

Game Exit sums the winner's records into `DAT_009b694c` (Σ+4 time), `DAT_009b6950` (Σhp), `DAT_009b6954` (Σdmg),
`DAT_009b6958` (max hits), `DAT_009b6948` (Σbonus), `DAT_009b695c` (Σscore) — shown on the result screen
(FUN_007975a0, score up to 99999). **Arcade only** (`DAT_0087faa8 == 0 && DAT_009b6930 == 0`, 0x5f5b9x):
`DAT_008940cc += DAT_009b695c`. Continue/Retry resets `DAT_008940cc = 0`. No penalty other than that.
Story keeps no score.

### D.2.3 Ranking (FUN_00792f40) and name entry

- Table `DAT_0087b7d0`, file `game.rkg` (0xca8 bytes, load all.c:445505 / save FUN_00792bf0): 3 difficulties ×
  10 entries × 0x24: `+0 score, +4..+0x1c name (7 ints, new entries filled with 0x2d '-'), +0x20 character`.
  Default table: score `10000 − 1000*i`, name chars all = i, char 10 (even i) / 3 (odd i) (verified: FUN_00792a30 copies 9 dwords from `local_130`, whose 9th is the adjacent
  `local_110`, the same slot FUN_00792f40 writes the character to at +0x20).
- `FUN_00792f40(diff, score, char)`: first i with `table[diff][i].score < score` (strictly greater wins); shift down,
  insert, return i; 10 if not ranked. Called at **arcade end and at arcade game over** (Result Exit), never story.
  Steam leaderboard upload FUN_006593b0 when the top score > 10000 (port: skip).
- Name entry trigger: Menu Init FUN_00642190, branch `prev scene ∈ {3, 0xb} && faa8 == 0` → menu state 0x19a
  (ranking of difficulty `DAT_009b164c = DAT_0087fab0`) and, if `DAT_008940d0 < 10`, help line 0x36
  ANNOUNCE_RANKING_ENTERNAME + FUN_00658270 (texts: rank `DAT_008940d0+1`, score `DAT_008940cc`) / FUN_00658410;
  state 0x19a runs FUN_00658550 (input) / FUN_00658970 (draw). If unlock messages are pending, state 0x1a4 comes
  first (D.5). Story return: `prev ∈ {3,0xb} && faa8 == 1` → title state 10 (after the unlock popups).

### D.2.4 Continue (Result scene, FUN_00796030 / FUN_00796ef0 / FUN_00796ca0)

- Result Init: `DAT_009b68e0 = (faa8 ∈ {0,1} && DAT_009b6930 == 1) ? 0x6e (lost) : 100 (won)`; BGM jingle 1.
- State 0x6e, confirm (FUN_0061c7b0) → state 0x122 "continue?": `DAT_009b6988 = 9`, help 0x3d
  ANNOUNCE_RESULT_CONTINUE; every 100 frames (`DAT_009b68e4 % 100 == 99`) and on each cancel press
  (FUN_0061c7e0): count −1, SE 2. Count < 0 → `DAT_009b692d = 1` (game over), transition 0x11.
  Confirm → `DAT_009b692e = 1` (continue), SE 0, transition 0x11. Texture `resMdl/res_continue.png` (layout: result section).
- Result Exit FUN_00796ca0:
  - game over (`692d`): scene 4; arcade: `DAT_008940d0 = FUN_00792f40(diff, score, char)`, FUN_004a8390; story:
    `DAT_008940d0 = 0xb`, FUN_007c3e80.
  - continue (`692e`): `c8++, cc = 0, index--`, stepper(0) → same fight, same opponent, same stage
    (the boss stage was already rewritten to the secret one if the run had no continue before).
  - else (won): `DAT_0087b76c++` (lifetime fights won in arcade+story, saved), FUN_007c47f0 (save game.sys), stepper(0).
- Pause **Retry** in arcade/story = stepper(3): also counts as a continue (`c8++`, score 0) without the countdown.
  Pause **Quit** = back to character select (scene 8), run abandoned, nothing saved.

## D.3 What the match itself does differently (arcade/story vs versus)

Set by Menu Exit FUN_00642880 (case 0 arcade / 1 story): `faac = 0, fab4 = 0 (P1 local), fab8 = 10 (P2 CPU),
fabc = fac0 = 0 (Standard hyper control for both), fad4 = fad8 = 2, fae8 = faec = 0, faf0 = 1, fadc = 0, fae4 = 0`.

| item | arcade / story | source |
|---|---|---|
| rounds to win | **1** (`DAT_0087faf0 = 1`) → Game Init calls FUN_006179d0(0) | FUN_00642880, FUN_005f4650 |
| round timer `DAT_00885974` | 180 (overrides config `DAT_00881eac`: 0→90, 1→180, 2→270, else 999); **story && stage 0x13 → 999** (infinite) | FUN_005f4650 |
| HP (`DAT_0087fad4/8` = 2) | jump table 0x69d038: fad4 0..5 → **4500, 6750, 9000, 13500, 18000, 27000**; default 2 → **9000**. Sumika 2nd overrides (12000/15000/18000) | FUN_0069cc40 0x69cde8 |
| P2 input | source 10 → AI `(&DAT_00881400)[diff + id*4]`, mask forced 0 while the round-end banner is up (`0x589 < DAT_0089426c && DAT_0087fb58`) | FUN_006116c0 |
| colours | 0 / 0 from the ladder entry | FUN_005f4650 |
| BGM | ladder entry, requested once per new match (`DAT_00885879`) | FUN_005f4650 0x5f5677 |
| HUD | no difference except versus-only timer tint (`faa8 == 2`) | FUN_006bafd0 l.377/394 |
| pause menu | 4 items: 0 Resume, 1 Command list (help 0x66/0x67), 2 Retry (→ continue), 3 Quit (→ ArcSelect) — same list as local versus, where Retry restarts the match and Quit returns to VS select | FUN_0068f4a0 |
| KO / tick | no `faa8 == 0/1` branches in FUN_006129b0, FUN_00780ab0, FUN_0060f5a0, FUN_0060b310 except Steam achievement/stat eligibility (`faac == 0 && faa8 ∈ {0,1,2} && diff > 0`: ACH_NODAMAGE 0x12 if winner hp == maxHp, ACH_LOWHEAT 0x16 if `DAT_00894068 < 0x65`, combo/breakshield stats); training (faa8 3) branches are separate | |
| replays | arcade/story fights are recorded like versus (Game Init copies faa8/fac4/…/fadc/fae0 to `DAT_00894268..`) and can be saved on the result screen (help 0x38 at frame 0x15e) | FUN_005f4650, FUN_00796ef0 |

**CORRECTION for core.md §6.1 and src/sim/core.c:787:** they say maxHp = 4500 for everyone. Ghidra collapsed the
switch at 0x69ce04 ("2 cases all go to same destination") into `fad4 < 6 ? 4500 : 9000`. The real jump table
(`rd 69d038 6` → 0x69ce0b/17/23/2f/3b/47) gives 4500/6750/9000/13500/18000/27000 for fad4 0..5, and every menu
path sets fad4 = 2 → **9000 HP**. (UNVERIFIED only in the sense of no runtime check; the disassembly is unambiguous.)

**Correction for src/sim/sim.h:214:** Sumika 2nd form's input fn FUN_00771d10 returns immediately when
`DAT_0087faac == 3`, and faac 3 is **online netplay**, not training. Irrelevant in practice (Sumika 2nd is story-only).

### D.3.1 Feeding the mode into src/sim

Arcade/story are offline-only, so nothing goes over the wire, but the sim must stay a pure function of its inputs
(replays). Everything mode-dependent is a match parameter resolved by the app *before* `aos_match_init`:
`ids = {ladder P1, resolved opponent}`, `schemes = {0,0}`, `colors = {0,0}`, `roundSeconds = (story && stage == 19) ? 999 : 180`,
`roundsToWin = 1`, `w->difficulty = menu difficulty`, `p[1].inputSrc = 10`, HP handicap index 2 (once the HP fix
lands, make it a match parameter), stage id for the cosmetic stage layer. Random opponent / BGM picks use the
app's cosmetic RNG (never `w->rng`). The sim needs no `gameMode` for arcade/story; the ladder, continue count,
score accumulation, ranking and unlocks live in the app layer and read only the end-of-match facts
(winner side, per-round records of D.2.2 — the sim must expose hp, best combo dmg/hits per player and the frame
count at the round end, or the record itself as FUN_0061ac80 builds it).


**Port (implemented, Part D).** Verified: every `faa8` reference reachable from the tick (FUN_006129b0,
FUN_006116c0, FUN_00780ab0, FUN_0060f5a0, FUN_0060b310, FUN_006f1720, FUN_005f91d0, FUN_006124b0, the Sumika 2nd
fns FUN_00771d10/007721f0/00772ae0) is either `faa8 == 3` (training), `faac == 3/4` (online/replay) or Steam
achievement/stat eligibility — arcade/story change nothing in the tick. What a fight needs from the mode is
exactly `ladder_match_params()` (src/modes/ladder.c): ids from the stepper, colours 0/0, schemes {ArcSelect
choice, 0}, handicap {2,2}, inputSrc {0,10}, difficulty = run difficulty, roundsToWin 1, roundSeconds 180 or 999
(story && stage 0x13); boss rules need nothing else (Sumika 2nd HP comes from difficulty inside Player Init).
End-of-match facts: `aos_world.roundRec[round][player]` (FUN_0061ac80 records, appended by FUN_00619cb0 at both
call sites of FUN_00608a00, cleared by a new-match Game Init), `aos_hp_winner()` (= `DAT_009b6930`) and
`modes_match_result()` (Game Exit sums). Not ported: the in-match Steam achievements/stats (ACH_NODAMAGE,
LOWHEAT, COMBO, BREAKSHIELD, ALTEACCEL) and replay bookkeeping.

## D.4 Endroll (scene 11)

### D.4.1 Init FUN_00519ce0

- `DAT_009b0920 = 0` (state), `DAT_009b0924 = 0` (counter), clears vector `0x9b0968`, then the **5 FUN_004aa150
  calls push the 5 staff-roll pieces** into `0x9b0968` (same generic 0x1c-byte vector push as the ladder; `ecx = 0x9b0968`
  at 0x519d11). Piece = `{+0 texture index, +4 RECT src (l,t,r,b), +0x14 x float, +0x18 y float}`:

| # | texture | src (l,t,r,b) | x | y (centre) | dst half size (draw) | 800×600 |
|---|---|---|---|---|---|---|
| — logo (draw only) | ed_staff1 | 0,0,0x1ff,0x5f ("PROJECT / Acceleration of Suguri 2") | W/2 | H/2 | (int)(128s) × (int)(24s) | 256×48 at (400,300) |
| 0 | ed_staff1 | 4,0x11c,0x1fb,0x3fc ("PLANNING" … "Megane") | W/2 (`DAT_00813a4c`=2.0) | `y0 = H + 400s` (`DAT_008145f4`) | 176s × 176s (`DAT_00817ab8`) | 352×352, y 1000 |
| 1 | ed_staff2 | 4,4,0x1fb,0x400 | W/2 | y0 + 430s (`DAT_00817ac0`) | 176s × 224s (`DAT_00817abc`) | 352×448, y 1430 |
| 2 | ed_staff3 | 4,4,0x1fb,0x400 | W/2 | + 460s (`DAT_00815dc0`) | 176s × 224s | y 1890 |
| 3 | ed_staff4 | 4,4,0x1fb,0x400 | W/2 | + 460s | 176s × 224s | y 2350 |
| 4 | ed_staff1 | 4,0x94,0x1fb,0xf3 ("PRODUCED BY / Orange Juice") | W/2 | + 784s (`DAT_00817ac4`) | 176s × 24s (`DAT_00853b8c`) | 352×48, y 3134 |

  Note the dst/src ratios (x ≈ 0.70, y ≈ 0.44–0.50): the PC squeezes the 512-wide credit pages vertically
  relative to horizontally. Kept as is for 1:1 (UNVERIFIED visually — a screenshot of the PC staff roll would settle it).
- Transition 5 (fade in from black, 0.05/frame). BGM: `FUN_004bee00(0x15,1,1)` (bgm_add_03, "Magical Girl of Miracles")
  if `DAT_0087faa8 == 1 && DAT_0087fac4 == 7` (**Sham's story**), else `FUN_004bee00(4,1,1)` (bgm_03, the ending theme) —
  music.md verified.
- Textures loaded by FUN_00519980 (start-up resource loader FUN_005dff20, and Game Exit when `DAT_009b1078` is set (reload flag, UNVERIFIED meaning)): `resMdl/ed_staff1..4` → `DAT_009b0974..980`.
  PNGs are 1024×1024; only the left 512×1024 is used (white text on opaque black).

### D.4.2 Update FUN_0051a060

- Input: FUN_0061ae20(…,1,0) → FUN_0061bc70(…, `DAT_009b0928`) (any local pad), confirm = FUN_0061c7b0.
- State 0: wait until counter > 50 (51 frames) → state 100, counter = 0.
- State 100: **skip allowed**: confirm, or counter > 10000 (166.7 s) → state 900 + transition 2 (fade to black, 0.01/frame, 100 frames).
- State 900: when the transition is off (`DAT_0087e7c0 == 0`) → Endroll Exit.
- Scroll: while state ∈ {100, 900} and counter < 0x1608 (5640 frames = 94 s): every piece `y -= 0.5 * s`
  (`DAT_00853b1c`), i.e. 30 px/s at 800×600, 2820 px total. Final position of piece 4 at 800×600: y = 3134 − 2820 = 314
  (≈ screen centre); pieces 0–3 have left the top. Then it holds until 10000 or confirm.
- Counter increments every frame (also in state 0).

### D.4.3 Draw FUN_0051a1d0

Clear `0xff222222`, z 1. Logo: alpha `255 − (counter − 200)` clamped ≥ 0 (full until counter 200 of state 100,
gone at 455; in state 0 alpha 255), ARGB `a<<24 | 0xffffff`, blend 2 (additive). Pieces: dst =
`(x − (int)(176s), y − (int)(h s), x + (int)(176s), y + (int)(h s))` with x, y truncated to int, texture
`DAT_009b0974[entry+0]`, src entry+4, `0xffffffff`, blend 2. Then the transition FUN_005622a0. No character
art, no stage in the background (Conversation Exit unloaded all stages via FUN_004bfbc0 when going to 0xb;
Result Exit always calls FUN_004bfbc0).

### D.4.4 Exit FUN_00519f60

Clears `0x9b0968`, BGM fade `(0,1,1)`, prev scene = 0xb. **Only if the run used no continue** (`DAT_008940c8 == 0`,
`cmp [0x8940c8],0` at 0x519f99):
- arcade: `DAT_0087b770[diff]++` (clears per difficulty), `DAT_0087b780[DAT_008940a8 + diff*0x13] = 1` (char cleared at diff);
- story: `DAT_0087b77c++`, `DAT_0087b7b9[DAT_008940a8] = 1` (story cleared with char);
- achievement 0x11 ACH_NOCONTINUE (FUN_007ca890).
Then save (FUN_007c47f0) and stepper(0) → past the end → menu + ranking insert + FUN_004a8390 (arcade) /
FUN_007c3e80 (story). **So a story or arcade clear with any continue (including pause Retry) records nothing**
except the ranking entry (arcade) and story achievements.

Ending sequence: story = final conversation (chapter 3, backdrop stage 19) → staff roll → menu (unlock popups → title);
arcade = boss result screen → staff roll → menu (unlock popups → ranking / name entry if ranked).

## D.5 Unlocks, save data, achievements

Save `game.sys` = 0xa4 bytes from `DAT_0087b728` (version 0x5a2, FUN_007c4450/FUN_007c4480 load, FUN_007c47f0 save).

| flag | meaning | default (new save, FUN_007c4480) |
|---|---|---|
| `DAT_0087b72c + char` | character unlocked | 3..8, 10..15 = 1; 9 SB, 16 Hime, 17 Sumika locked |
| `DAT_0087b73f + stage` | stage unlocked | 5,6,7,8,9,11,15 |
| `DAT_0087b753 + bgm` | BGM unlocked | 11,12,13,15,16,17,18 |
| `DAT_0087b780 + diff*0x13 + char` | arcade clear without continue at diff (3 × 0x13) | 0 |
| `DAT_0087b7b9 + char` | story clear without continue | 0 |
| `DAT_0087b770[3]`, `DAT_0087b77c` | arcade clears per diff / story clears | 0 |
| `DAT_0087b76c` | fights won in arcade/story (Result Exit) | 0 |

"cleared(c)" = any of `b780/b793/b7a6 [c]`. Unlock checks (message id pushed to `0x8940d4` via FUN_004aa120: 1000+char,
2000+stage, 3000+bgm):

| unlock | condition | where |
|---|---|---|
| char 16 Hime (msg 0x3f8) | arcade cleared with any of 10..15 (any diff), or wins > 29 | FUN_004a8390 |
| char 9 Star Breaker (0x3f1) | arcade cleared with any of 3..8, or wins > 59 | FUN_004a8390 |
| stage 17 (0x7e1) | ≥ 2 of chars 10..16 arcade-cleared, or wins > 14 | FUN_004a8390 |
| stage 12 (0x7dc) | ≥ 2 of 3..9, or wins > 29 | FUN_004a8390 |
| stage 18 (0x7e2) | ≥ 4 of 10..16, or wins > 44 | FUN_004a8390 |
| stage 13 (0x7dd) | ≥ 4 of 3..9, or wins > 59 | FUN_004a8390 |
| BGM 14 (0xbc6) | ≥ 5 of 3..16, or wins > 89 | FUN_004a8390 |
| BGM 20 (0xbcc) | ≥ 10 of 3..16, or wins > 119 | FUN_004a8390 |
| char 17 Sumika (0x3f9) + ACH_UNLOCK_SUMIKA | all 14 stories (3..16) cleared, or (wins > 255 and ≥ 1 story) | FUN_007c3e80 |
| stage 16 (0x7e0) | ≥ 3 stories | FUN_007c3e80 |
| stage 19 (0x7e3) | ≥ 5 stories | FUN_007c3e80 |
| BGM 19 (0xbcb) | ≥ 7 stories | FUN_007c3e80 |
| BGM 21 (0xbcd) | ≥ 10 stories | FUN_007c3e80 |
| ACH_UNLOCK_STAGEBGM (0x10) | all of stages 12,13,16,17,18,19 + BGM 14,19,20,21 | both |

Stage 10 (Equator doldrums, `DAT_0087b749`) has no writer except a save file → never unlocked in a fresh save
(UNVERIFIED: maybe a DLC/legacy flag; grep finds only reads at all.c:244145/449455/458382).
Achievements (table `PTR_s_ACH_AOS2_0086c8c4`, index → name): 1..7 ACH_STORY_SUGURI, SAKI, IRU, NANAKO, KAE, KYOKO,
HIME; 8..14 SORA, ALTE, TSIH, MIRA, SHAM, NATH, STARBREAKER — FUN_007c3e80 at story end when not game over and diff > 0
(char 3..9 → 8..14, 10..16 → 1..7); 15 UNLOCK_SUMIKA; 16 UNLOCK_STAGEBGM; 17 NOCONTINUE (Endroll Exit); 18 NODAMAGE;
19 BREAKSHIELD; 20 COMBO; 21 GAMES; 22 LOWHEAT; 23 ALTEACCEL; 0 ACH_AOS2 when all others are set.

Unlock popup: Menu Init → state 0x1a4 (SE 5) if `0x8940d4` is non-empty; FUN_006471d0 draws a full-width band
`H/2 ± (int)(32s)` (black, alpha `192*a`) with white lines at its edges and the text MENU_UNLOCK_CHARACTER /
STAGE / BGM (text 0x23b/0x23c/0x23d chosen by id < 2000 / < 3000 / else — generic "New … unlocked!", no item name),
centred; confirm advances (FUN_00646ee0, SE 5), after the last → ranking (arcade) or title (story).
Character-select marks: FUN_0079fb30 draws a 9×9 "cleared" icon on a portrait if `b7b9[c]` (story) or
`b780[c + diff*0x13]` (arcade, current difficulty).

3DS save (implemented, `src/modes/save.h`): `sdmc:/3ds/aos2_3ds/progress.sav` = header "A2SV" + format version +
crc32, payload = the 0xa4-byte game.sys image (PC byte layout) + the 0xca8-byte game.rkg image + achievement bitmask.

## D.6 3DS layout proposal — staff roll

At top-screen size (`s = min(400/800, 240/600) = 0.4`) a piece is 140 px wide and the glyphs ≈ 5 px tall —
unreadable. Proposal: lay the roll out in the PC's **800×600 virtual space at s = 1** and show a 400×240 window
of it centred on the virtual screen centre: `screen = (vx − 200, vy − 180)` on the top screen
(virtual centre (400,300) → (200,120)). Pieces are 352 px wide (fit 400), glyphs ≈ 13 px, logo 256×48 centred;
scroll 0.5 px/frame and all timings unchanged (51 f wait, 5640 f scroll, 10000 f / A to skip, logo fade 200..455,
fade-out 100 f), so the final "PRODUCED BY Orange Juice" rests at y = 134 (centre + 14), as on PC.
Bottom screen: plain `0xff222222` (the original shows nothing else); optionally continue the same virtual plane
below the top screen (virtual y 420..660 shifted) so credits enter from the bottom screen — cosmetic choice.
Textures: convert the used 512×1024 left half of `resMdl/ed_staff1..4.png` to A8/L8 (additive white-on-black
→ alpha or luminance only; 512 KB each at 8 bpp, 256 KB at 4 bpp), load only for scene 11.

## Open questions

- Visual aspect of the staff pages (D.4.1) — confirm with a PC screenshot.
- Menu item that sets `DAT_009b163c` for story (is a difficulty asked before story?) — menus section.
