#!/bin/sh
# Captures the heavy frames used for the render-pipeline comparisons (docs/PERF.md "Render pipeline"):
#   tools/bench/run_caps_perf2.sh <variant> <3dsx> [scene...]   -> build/cap2/<variant>/<scene>/f1XXXXXX.png
# Scenes: sumika tsih sumika2 suguri nanako star (default: all). Compare with tools/bench/sidebyside.py.
set -u
cd "$(dirname "$0")/../.."
V=$1; X=$2; shift 2
S=${*:-"sumika tsih sumika2 suguri nanako star"}
for s in $S; do
	case $s in
	sumika) tools/bench/run_capture.sh "$X" "17 17 5 2140 1 2" "build/cap2/$V/$s" 2070 2130 ;;
	tsih) tools/bench/run_capture.sh "$X" "5 5 5 3545 1 2" "build/cap2/$V/$s" 820 2620 3500 3530 ;;
	sumika2) tools/bench/run_capture.sh "$X" "18 18 5 3480 1 2" "build/cap2/$V/$s" 3440 3470 ;;
	suguri) tools/bench/run_capture.sh "$X" "10 10 5 1990 1 2" "build/cap2/$V/$s" 300 1950 ;;
	nanako) tools/bench/run_capture.sh "$X" "13 13 12 1800 1 2" "build/cap2/$V/$s" 1770 ;;
	star) tools/bench/run_capture.sh "$X" "9 9 12 660 1 2" "build/cap2/$V/$s" 620 650 ;;
	esac
done
echo ALLDONE
