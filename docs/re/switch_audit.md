# Switch / jump-table audit of the ported functions (2026-11-10)

Why: Ghidra collapsed the handicap switch in `FUN_0069cc40` into `fad4 < 6 ? 4500 : 9000`, and the
port copied it, so every match ran at half HP. This audit checks every jump table in every function
the port cites, plus other known decompiler failure modes.

## Bottom line

- **Only one gameplay-affecting decompiler error was found: the HP switch in FUN_0069cc40.** It is
  already fixed in `src/sim/core.c` (`aos_handicap_hp`, lines 787-818). I re-checked that fix here
  against the machine code (details below). Nothing else in the sim was built on a collapsed or
  merged switch.
- Ghidra rendered the other 253 jump tables in ported functions correctly. For each table, every
  non-default case value has its own `case N:` label in the corpus, so the corpus text the port was
  written from is accurate.
- A separate check, case values against the port's C bodies, found **two cosmetic port-translation
  bugs**. Neither comes from the decompiler. Both have proposed patches below: the
  `aos_status` refresh SE and Nath's missile-300 draw.
- **x87 compare misreads cannot happen here.** The ported code has no `fnstsw` or `sahf` compares.
  It is SSE (`comiss`/`ucomiss`). I spot-checked the NaN-aware equality idiom in 5 functions and
  Ghidra renders it correctly.

## Method and tool

`tools/re/jtscan/jtscan.go` (build: `cd tools/re && go build -o jtscan.exe ./jtscan`):

- Function ranges come from the corpus headers (`// ===== NAME @ addr` → next header). MSVC puts the
  tables right after the body, so the range includes them. The tool sweeps each function linearly.
- It detects `jmp dword ptr [4*reg+imm]` and the MSVC two-level form
  `movzx r, byte ptr [r+imm]; jmp [4*r+imm]`. The bound comes from `cmp x, imm; ja` and the default
  target from the `ja` target. The case base comes from following the compared operand back through
  `mov`s to `sub`/`add`/`lea`/`dec`/`inc` (debug-style stack-slot code included).
- For each case value it prints the target and the target's first N instructions.
- Corpus cross-checks per function:
  - `NOSWITCH`: fewer `switch(` statements than tables.
  - `FEWLABELS`: fewer `case N:` lines than non-default table entries.
  - `missing_case_labels`: non-default case values with no `case N:` label, raw or rebased, since
    Ghidra often writes `switch(x - base)`.
  - `unrecovered-jumptable` / `halt_baddata` warnings.
- `-tsv` writes the whole-exe summary. Currently `tools/re/jtscan/jumptables.tsv`: 452 tables in 355
  functions, 241 of them two-level.
- `-fcmp` lists float-compare idioms: compare → flag test → first branch.

Inputs: 1,387 functions cited in `src/sim/**` and `src/stage/*` (`FUN_xxxxxxxx`, `0x…` and bare
6-digit addresses that are function starts). Docs-only citations in `docs/re/*.md` add 498 more,
for 1,885 total. Of the src-cited functions, 200 contain 254 jump tables; with the docs-cited ones,
236 functions contain 311 tables.

Then a second script (scratch only, not kept) checked each port function body that cites a table
function. It looked for at least one value of every non-default case group in that body. 97 hits
were bodies split across helpers and needed no follow-up. The 20 flagged bodies were checked by
hand.

## Findings table

| Function | Address (jmp → table) | Decompile says | Machine code does | Port does | Severity | Proposed patch |
|---|---|---|---|---|---|---|
| FUN_0069cc40 Player Init (HP) | 0x69ce04 → 0x69d038 | `local_ac = 9000; if (fad4[i] < 6) local_ac = 0x1194;` (collapsed: one constant, `NOSWITCH FEWLABELS`, labels 0..4 missing) | pre-set 9000 (0x69cdd8); cases 0..5 → 4500 / 6750 / 9000 / 13500 / 18000 / 27000 (0x69ce0b..0x69ce47); `ja` (unsigned) → keep 9000 | **Fixed** (other agent): `aos_handicap_hp()` table {4500,6750,9000,13500,18000,27000}, `(uint32_t)h < 6 ? … : 9000`; default handicap 2 → 9000 | gameplay (fixed) | none, matches |
| FUN_0069cc40 per-char init | 0x69cf52 → 0x69d050, byte table 0x69d068, base 5 | `switch(engine id)` cases 5, 8, 0xd, 0x11, 0x12 | same 5 targets; 6, 7, 9..12, 14..16 → default. Case 0x12 (Sumika 2nd) writes hp/maxHp/recovHp (-0xac/-0xa8/-0xa0), not redHp (-0xa4), with 18000/15000/12000 by `DAT_0087fab0` | `initialState` 0xd2/0x197/0x21d/0x241, Sumika-2nd HP block leaves redHp at the table value. Cases 5/8 only zero fields already zeroed by memset | none | none |
| FUN_00633e50 menu text ids | 0x63d35e → 0x63ed50, base 3 | **no decompile** (`FAILED: … timeout`) | char e = 3..17 → text object id 0x1f8 + (e−3) (`CHARACTER_SORA` … `CHARACTER_SUMIKA`) | `modes_menu_textids.tsv` / `src/app/text_ids.h` have 0x1f8..0x206 in the same order | none (UI) | none |
| FUN_006b1a40 `aos_status` | 0x6b1add → 0x6b1b20, base 1 | switch is **after** the if/else: 1, 2, 7 → SE 0x26 on every call; 3 → SE 0x15 only if newly set | same (refresh path `jmp 0x6b1ac5` lands on the switch) | returns early when the status is already on, so refreshing a status 1/2/7 plays **no** SE 0x26 | cosmetic (audio; changes the SE queue, so a golden trace that records SEs will differ) | patch A |
| FUN_00671b90 Nath missile draw | 0x671bce → 0x6732dc, byte table, base 100 | case 300: sprite 0xee frame 4, then frames 5/6 additive with α = sin·0.15+0.85 / sin·0.25+0.75 | same | `missile_draw` has no `case 300` (spawned at nath.c:1644), so the missile is **invisible** | cosmetic | patch B |
| FUN_0067b5e0 / FUN_0067b9b0 StarBreaker missile hit/update | byte tables, base 100 | case 200 present | case 200 present | no `case 200` | none: missile 200 is never spawned (starbreaker.md §5.8) | none |
| FUN_00796030 Result init | 0x79680c → 0x796c68, base 4 | `switch(DAT_009b693c)` lists 5..0x11 only (`FEWLABELS`) | case 4 → 0x26/0x27 exists | (app side) | none: case 4 is unreachable (`if (… && 9b693c != 4)`) | none |
| FUN_004944a0 BMFont loader | 0x494576, `dec eax` base 1 | cases 1..5 | same | n/a | none (false positive before `dec` handling) | none |
| FUN_00414ea8, FUN_00415994, FUN_0046f4f0 | n/a | `WARNING: Could not recover jumptable` | import / vtable thunks (D3DX plane / matrix, mesh recreate) | render side only (stages.md mirror, render.md) | none | none |
| FUN_005f4650 / FUN_005f5780 Game Init stage load/unload | 0x5f4769 / 0x5f6068, base 5 | `switch(stage − 5)` 0..0xe | stages 5..19 → 4d6500, 4bffe0, 4c90e0, 4c7110, 4d1110, 4d2090, 4c1ff0, **4c3dc0 (12 and 14)**, 4cd0e0, 4c86f0, 4ca360, 4cbfa0, 4cef60, 4d3d60 | `stage_defs[5..19]` loaders match one-for-one, including stage 14 (`stage_ct2`) sharing the `ct` loader | none | none |

Hand-checked by both routes with no mismatch: FUN_006b1b40 (barrier kinds 0/1/2/10/20 → type
1/2/5/3/4, time 60/120/60/100/100, radius 120/200/160/160/160), FUN_006af390 / FUN_006b0110 (per-char
T2 hooks: engine ids 4/5/6/7/8/13/15 → 0x34/0x5d/0x7e/0xa9/0xca/0x18f/0x1d3), FUN_00561fc0 /
00561c70 / 005622a0 (fade types 1..20), FUN_006c6450 (Hime Paradise x/y table and missile kinds per
timer, byte table 0x6c6af0), FUN_006f7020 (Mira caltrop angle table, ver > 0x5a0), FUN_004ae960 (Mira
beam 100/110 = a no-op team-colour load), FUN_0068c500 (particle kinds 1000/1100/1110/1200/1210),
Saki FUN_004b2260 / FUN_00675060, Nath FUN_006249a0, Sumika FUN_007c8ab0.

### Patch A: `src/sim/core.c` `aos_status` (FUN_006b1a40)

core.c is being edited by another agent, so apply this after that change lands.

```diff
 void aos_status(aos_world *w, aos_player *p, int k, int32_t frames) /* FUN_006b1a40 */
 {
 	if (p->state == AOS_ST_KO || !p->hittable || k < 0 || k >= AOS_STATUS_COUNT)
 		return;
-	if (p->statusOn[k]) {
-		if (frames > p->statusTime[k])
-			p->statusTime[k] = frames;
-		return;
-	}
-	p->statusOn[k] = 1;
-	p->statusTime[k] = frames;
-	if (k == 1 || k == 2 || k == 7)
-		aos_se(w, 0x26);
-	if (k == 3)
-		aos_se(w, 0x15);
+	bool fresh = !p->statusOn[k];
+	if (!fresh) {
+		if (frames > p->statusTime[k])
+			p->statusTime[k] = frames;
+	} else {
+		p->statusOn[k] = 1;
+		p->statusTime[k] = frames;
+	}
+	/* switch at 0x6b1add runs on both paths: 1/2/7 always SE 0x26, 3 only when newly set */
+	if (k == 1 || k == 2 || k == 7)
+		aos_se(w, 0x26);
+	if (k == 3 && fresh)
+		aos_se(w, 0x15);
 }
```

### Patch B: `src/sim/chars/nath.c` `missile_draw` (FUN_00671b90 case 300)

Sprite 0xee is `nt_fuse` (nath.c:1867). Frame index 4 comes from `DAT_009b35bc = 4`; position is
+0x28/+0x2c, rotation is +0x6c. Scale is (1,1,1) (`DAT_00853b34`). Frame 4 is drawn with the default
blend, then `FUN_004637d0(2)` (additive) is used for frames 5/6 with colour (α,1,1,1), then the blend
is reset to 1. The 74.0f size follows the enemy draw of the same sprite and is **UNVERIFIED**:
check the 0xee sprite record.

```diff
 	case 200: case 0xc9: { /* 0xf7 frames 0..2 */
 		...
 		break;
 	}
+	case 300: { /* 0xee (nt_fuse) frame 4, then 5/6 additive, pulsing alpha (FUN_00671b90 case 300) */
+		float sn = aos_sin(((float)(m->age % 0x32) / 50.0f) * TWO_PI); /* DAT_008140ec, DAT_00853b78 */
+		float a[2] = {sn * 0.15f + 0.85f, sn * 0.25f + 0.75f};         /* DAT_00813f20/814120, 813f24/813f3c */
+		aos_dcmd *c = model("nt_fuse%02d", 4, bank, m->pos[0], m->pos[1], 74.0f, 1.0f, 1.0f);
+		c->rotZ = aos_rad2bam(m->angle);
+		for (int k = 0; k < 2; k++) {
+			c = model("nt_fuse%02d", 5 + k, bank, m->pos[0], m->pos[1], 74.0f, 1.0f, 1.0f);
+			c->rotZ = aos_rad2bam(m->angle);
+			c->rgba = argb(a[k], 1, 1, 1);
+			c->blend = AOS_BLEND_ADD_ALPHA;
+		}
+		break;
+	}
 	default:
 		break;
```

## Other decompiler failure patterns (step 4)

- **x87 compares.** `jtscan -fcmp` over the 1,387 src-cited functions found 2,056 float compares.
  There are **0** `fnstsw`/`sahf`-based x87 compares; a few apparent hits are data decoded as code.
  The compares are SSE: `comiss`/`ucomiss` + `jbe` (1,435), `jb` (490), `jnbe` (58) and `jnb` (18).
  The other ~50 are the MSVC equality idiom `ucomiss; lahf; test ah,0x44; jp/jnp`: `jp` jumps on
  not-equal or NaN, `jnp` on equal. I spot-checked 5 functions (67e1a0, 6b5b50, 5336b0, 671b90,
  5f1820): Ghidra writes `if (a == b) { … }` around the `jp`-skipped block, which is correct.
  CF/ZF-based SSE compares do not have the C0/C3 misread problem.
- **`Could not recover jumptable` / `halt_baddata`.** These warnings appear in 67 functions
  exe-wide. Only 3 are cited (FUN_00414ea8, FUN_00415994, FUN_0046f4f0), and all are render-side
  thunks (see table).
- **Failed decompiles.** Exactly one function in the corpus failed: FUN_00633e50 (timeout). It is
  cited only for menu text ids and checked above.

## Exe-wide leftovers (not ported, for future work)

These appear in `jtscan/jumptables.tsv` with `missing_case_labels`/`NOSWITCH`. None is cited by
the port, and most are CRT/library code:

- 004061f0, 0040cb40, 00415e93, 00416020, 00435593, 0044574a, 004458d2, 00445af8, 004500bb,
  007d09e0.
- 00476b00, 00481750, 00481830, 00481930, 00486640: unidentified, likely library. Any of these
  that gets ported later must be compared against `jtscan -only` output first.
