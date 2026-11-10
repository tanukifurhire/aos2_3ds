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
