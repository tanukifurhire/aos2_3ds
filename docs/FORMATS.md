# Acceleration of SUGURI 2 (PC, Steam build 2024-08) — data formats

All findings verified against every file of the install; tool: `tools/aos2extract`.

| File | Format | Notes |
|---|---|---|
| `script.pak` | ZIP | 632 files. **Localisation text only** (menus, story, quotes) in 8 languages. No gameplay data. |
| `font.pak` | ZIP | `.fnt` (BMFont-style) + PNG pages; CJK chat fonts at 1080p. |
| `shader.pak`, `mdl/shader.pak` | ZIP | D3D9 compiled shader blobs (`*VS.dat`, `ColorAdd.dat`, …). Must be re-done for PICA200. |
| `mdl/texture.pak` | ZIP of `.dat` | 4218 sprites/textures. Each `.dat` is a **PNG XORed with a repeating 89-byte key** (below). |
| `se.dat`, `se2.dat`, `se3.dat` | concatenated records | `u32 dataLen; char name[]\0; u32 fmtLen; WAVEFORMATEX[fmtLen]; PCM[dataLen]`. 58 sounds: PCM 8-bit unsigned (27) and 16-bit, mono/stereo, 8–44.1 kHz. |
| `bgm*.dat` | concatenated Ogg Vorbis | 58 tracks total, 44.1 kHz. Exactly one padding byte between consecutive streams. Track names/order: `script/*/bgm.txt`. |
| `mdl/*.x` | DirectX `.x` text meshes | Stage geometry (city, bridge, tower, ship). |
| `mdl/3dtex.dat`, `mdl/win_font_*.dat` | `LAG\0` raw textures | `LAG\0`, u32 0, char name[16], u32 w, u32 h, … then 16-bit-per-channel RGBA. Decoder TODO. |
| `mdl/win_font_*.lfd` | `LFD\0` | Glyph layout for the win-screen fonts. TODO. |

## Texture key

```
u73$@STKY%&F#K;;zZTY%JM2@{}}1HsdbtyJU+g2j9ZXSc;32<%&v#&>_vDHYwQWJKIJs67?*e'-wBJ3#!)FVh!)O
```
`plain[i] = enc[i] ^ key[i % 89]`. Recovered by known-plaintext (PNG signature/IHDR at the start,
the fixed 12-byte IEND chunk at the end of every file, so 4218 different offsets → period 89, zero
conflicts). All 4218 decrypted files decode as valid PNGs.

## Where the game logic is

Not in the data. `aos2.exe` (32-bit x86, MSVC, ~4.2 MB `.text`, no SteamStub/packer) contains
character moves, bullet patterns, physics, heat/hyper rules, AI, menus and netcode. Libraries
statically linked: zlib 1.2.3, a PhysicsFS-style ZIP reader, libjpeg. Online play uses **Steam
lobbies + Steam P2P networking** (`CFBFSteamMultiplayer`, `P2PSessionRequest_t`).

## Story conversations (docs/re/modes.md §C)

Source: the story texts are in `script.pak` (`script/<lang>/story_<char>_0<n>.txt`, `<NNNN> text` lines,
literal `
` = line break), but *who speaks, which portrait, bands, backdrop and SE* are 56 Win32 resources of
`aos2.exe` (type `TXT`, names `TEXT_CONVERSATION010` … `TEXT_CONVERSATION143`, FUN_004e4fa0 zero-pads the id to
3 digits). Script id = `10*k + n`: k = 1..14 story character (sora alte tsih mira sham nath starbreaker suguri
saki iru nanako kae kyoko hime), n = chapter 0..3 → text file `story_<char>_0<n+1>.txt`. Script grammar
(FUN_005d6430): blank and `/` lines skipped; `[ACT]` then 5 lines `< name >< band >`, left text, `< name ><
band >`, right text, `< bg >< bgm >< se >< int >`; `[EOF]`. Text lines: `＄` (keep the previous text,
dimmed), `　` (U+3000 → `" "`), `[text NNNN]` (key into the text file). Full record layout: modes.md §C.1;
all 1146 records: `docs/re/modes_story_scripts.tsv`.

**Port data** (`tools/aos2story`, run by build.sh → `romfs:/story/`, 2.5 MB):

| Path | Content |
|---|---|
| `story/res/TEXT_CONVERSATION<id>.txt` | the 56 resources, verbatim (134 KB total, ≤ 5.5 KB each) |
| `story/<lang>/story_<char>_0<n>.txt` | the story texts of all 8 languages (en jp chs cht ptbr sp ko ru), verbatim (≤ 3.8 KB each) |
| `story/portrait/<name>.t3x` | 16 portraits (sora alte tsih mira sham nath star suguri saki iru nanako kae kyoko hime sumika sumikab) |

Scripts and texts stay text: `src/modes/conv.c` parses them on the device with the original parsers
(FUN_005d6430 / FUN_005d7620), so there is no second format to keep in sync; one conversation = one script
+ one text file, ≤ 9 KB read, ≤ 7 KB heap after parsing (host test numbers).

Portraits: convMdl textures are 2048x512 RGBA of which the PC draws src rect (0x1b8,0)-(0x647,0x1ff)
(1167x511) at half size, 584x256 at s = 1 (FUN_004e54b0). The port crops that rect and resamples it
(Catmull-Rom, premultiplied alpha) to the exact on-screen size on the top screen (s = 0.4): **232x102**,
tex3ds RGBA8 padded to 256x128 (one sub-texture, `C2D_SpriteSheetLoad`) = **128 KB linear memory each**,
11–38 KB on romfs (`-z auto`). `none.png` (512², fully transparent, name id 0) is not shipped: id 0 draws
nothing. A script uses at most 3 non-empty portraits (49 scripts use 2, 7 use 3), so a conversation holds
≤ 384 KB of portraits, loaded at scene Init (`conv_script_portraits`, FUN_004e4630) and freed at Exit.
The noise band textures are `systemMdl/noise0..7` (already in the HUD atlases).
