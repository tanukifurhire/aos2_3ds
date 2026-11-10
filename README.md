# aos2_3ds

Personal-research port of a legally owned PC copy of *Acceleration of SUGURI 2* to the Nintendo 3DS.

- Status, decisions and roadmap: [docs/STATUS.md](docs/STATUS.md)
- Reverse-engineered data formats: [docs/FORMATS.md](docs/FORMATS.md)

```
src/input/   aos_input — button mapping (L Dash, R Hyper, A/B weapons, X/Y specials,
             L+R Shield, L,L Hyper Dash). Pure C, host-tested.
src/app/     3DS input-lab app (citro2d), WAV loader, NDSP audio
tools/       aos2extract (unpack/decrypt game data), aos2pack (3DS asset prep),
             dkpfetch (portable devkitPro), re/ (disassembly helper),
             devkitpro/, zig/, azahar/ (project-local toolchains, not installed system-wide)
tests/       run_tests.sh, host C tests
build.sh     -> build/aos2_3ds.3dsx
```

Run in an emulator: open `build/aos2_3ds.3dsx`. For sound, the emulated SD needs
`3ds/dspfirm.cdc` (any file works under HLE DSP).
