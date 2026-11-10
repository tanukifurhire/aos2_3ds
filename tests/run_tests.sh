#!/bin/sh
# Builds and runs host-side unit tests with the bundled portable Zig/clang.
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
mkdir -p build/tests
"$ZIG" cc -std=c99 -Wall -Wextra -Werror -O2 -fsanitize=undefined -fno-sanitize-recover=all \
  src/input/aos_input.c tests/test_input.c -o build/tests/test_input.exe
build/tests/test_input.exe
# touch hyper buttons -> input masks, every slot of every character through the real dispatch (hyper_touch.h)
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c src/input/hyper_touch.c tests/test_hyper_touch.c -lm -o build/tests/test_hyper_touch.exe
build/tests/test_hyper_touch.exe
"$ZIG" cc -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/app/wav.c tests/test_wav.c -o build/tests/test_wav.exe
build/tests/test_wav.exe
(cd tools/aos2extract && go test ./...)
"$ZIG" cc -std=c99 -Wall -Wextra -Werror -O1 -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/rng.c tests/test_rng.c -o build/tests/test_rng.exe
build/tests/test_rng.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_nath.c -lm -o build/tests/test_nath.exe
build/tests/test_nath.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_tsih.c -lm -o build/tests/test_tsih.exe
build/tests/test_tsih.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_kae.c -lm -o build/tests/test_kae.exe
build/tests/test_kae.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_saki.c -lm -o build/tests/test_saki.exe
build/tests/test_saki.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_starbreaker.c -lm -o build/tests/test_starbreaker.exe
build/tests/test_starbreaker.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_iru.c -lm -o build/tests/test_iru.exe
build/tests/test_iru.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_alte.c -lm -o build/tests/test_alte.exe
build/tests/test_alte.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_sora.c -lm -o build/tests/test_sora.exe
build/tests/test_sora.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_suguri.c -lm -o build/tests/test_suguri.exe
build/tests/test_suguri.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_kyoko.c -lm -o build/tests/test_kyoko.exe
build/tests/test_kyoko.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_sham.c -lm -o build/tests/test_sham.exe
build/tests/test_sham.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_hime.c -lm -o build/tests/test_hime.exe
build/tests/test_hime.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_sumika.c -lm -o build/tests/test_sumika.exe
build/tests/test_sumika.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_mira.c -lm -o build/tests/test_mira.exe
build/tests/test_mira.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_nanako.c -lm -o build/tests/test_nanako.exe
build/tests/test_nanako.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/sim/*.c src/sim/chars/*.c tests/test_flow.c -lm -o build/tests/test_flow.exe
build/tests/test_flow.exe
# effect pool one-pass compaction == the original per-record erase (default and low compaction threshold)
for t in "" "-DAOS_EFFECT_COMPACT_AT=64 -DAOS_BEAM_COMPACT_AT=16"; do
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all $t \
  src/sim/*.c src/sim/chars/*.c tests/test_effect_pool.c -lm -o build/tests/test_effect_pool.exe
build/tests/test_effect_pool.exe
done
# story conversations: 56 scripts x 8 languages through loader, wrap and scene (src/modes/conv.c)
(cd tools/aos2story && go test ./...)
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/modes/conv.c src/app/bmfont.c tests/test_conv.c -lm -o build/tests/test_conv.exe
build/tests/test_conv.exe
# dialogue fonts packed for the 3DS conversation scene vs the story texts (glyph coverage, no shrink in
# the bottom-screen boxes; src/app/conv_scene.c, tools/aos2pack fonts_dialogue.go); unbuilt fonts are skipped
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/modes/conv.c src/app/bmfont.c tests/test_conv_font.c -lm -o build/tests/test_conv_font.exe
build/tests/test_conv_font.exe build/romfs extracted
# arcade/story ladders vs docs/re/modes_ladders.tsv, round records/score, ranking, unlocks, save (src/modes)
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/modes/ladder.c src/modes/score.c src/modes/save.c src/sim/*.c src/sim/chars/*.c tests/test_modes.c -lm \
  -o build/tests/test_modes.exe
build/tests/test_modes.exe docs/re/modes_ladders.tsv build/tests
# arcade/story flow glue (src/app/modes_flow.c, Part D): ladder -> g_setup + scenes, continue/retry/quit, save
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all   src/app/modes_flow.c src/app/scene_core.c src/app/strings.c src/app/bmfont.c src/modes/ladder.c src/modes/score.c   src/modes/save.c src/sim/*.c src/sim/chars/*.c tests/test_flow_glue.c -lm -o build/tests/test_flow_glue.exe
build/tests/test_flow_glue.exe build/tests
# menu text (key-value scripts, BMFont parser/layout/wrap), scene core, Logo/Menu state machines (src/app, Part A)
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/app/strings.c src/app/bmfont.c src/app/scene_core.c tests/test_text.c -lm -o build/tests/test_text.exe
build/tests/test_text.exe
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/app/menu.c src/app/strings.c src/app/bmfont.c src/app/scene_core.c src/app/ranking.c src/app/modes_flow.c src/modes/ladder.c src/modes/score.c src/modes/save.c src/sim/*.c src/sim/chars/*.c tests/test_menu.c -lm -o build/tests/test_menu.exe
build/tests/test_menu.exe
# Part B scenes: ArcSelect / VS Select / Result + continue / pause menu logic (src/app/select.c)
"$ZIG" cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  src/app/select.c src/app/modes_flow.c src/app/scene_core.c src/app/strings.c src/app/bmfont.c src/modes/ladder.c \
  src/modes/score.c src/modes/save.c src/sim/*.c src/sim/chars/*.c tests/test_select.c -lm -o build/tests/test_select.exe
build/tests/test_select.exe
