#!/bin/sh
# Builds and runs host-side unit tests with the bundled portable Zig/clang.
set -e
cd "$(dirname "$0")/.."
ZIG=tools/zig/zig-x86_64-windows-0.14.1/zig.exe
mkdir -p build/tests
"$ZIG" cc -std=c99 -Wall -Wextra -Werror -O2 -fsanitize=undefined -fno-sanitize-recover=all \
  src/input/aos_input.c tests/test_input.c -o build/tests/test_input.exe
build/tests/test_input.exe
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
