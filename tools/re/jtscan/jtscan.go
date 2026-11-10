// jtscan: find every indirect jump table (switch) in aos2.exe and dump its cases.
//
// Usage (from tools/re):
//
//	go build -o jtscan.exe ./jtscan
//	./jtscan.exe [-only list.txt] [-preview N] [-tsv] "$EXE" dec/all.c
//	./jtscan.exe -fcmp -only list.txt "$EXE" dec/all.c   # float-compare idioms instead
//
// Function starts come from the corpus headers `// ===== NAME @ addr` in dec/all.c;
// a function's code range ends at the next header (MSVC places its jump tables
// right after the function body, so this range includes them).
// Each function is swept linearly and every
//
//	jmp dword ptr [4*reg+imm]                      (dword table)
//	movzx r, byte ptr [r+imm] ; jmp [4*r+imm]      (MSVC two-level byte->dword table)
//
// is reported with its bound (`cmp x, imm ; ja`), its case base (`sub/add/lea` before
// the cmp), and each case value -> target with the first N instructions of the target.
// The number of `switch` statements in the corpus text is printed next to the number of
// tables; fewer switches than tables ("NOSWITCH") means Ghidra collapsed at least one
// switch into if/ternary code -- check those by hand. FEWLABELS = fewer `case N:`
// lines in the corpus than non-default table entries (merged or dropped cases).
// Full-exe summary: jtscan/jumptables.tsv (regenerate with -tsv).
package main

import (
	"bufio"
	"debug/pe"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/arch/x86/x86asm"
)

var (
	f      *pe.File
	text   []byte
	textVA uint32
	base   uint32
)

func load(p string) {
	var err error
	f, err = pe.Open(p)
	if err != nil {
		panic(err)
	}
	base = f.OptionalHeader.(*pe.OptionalHeader32).ImageBase
	for _, s := range f.Sections {
		if s.Name == ".text" {
			text, _ = s.Data()
			textVA = base + s.VirtualAddress
		}
	}
}

func readVA(va uint32, n int) []byte {
	for _, s := range f.Sections {
		lo := base + s.VirtualAddress
		if va >= lo && va+uint32(n) <= lo+s.Size {
			d, _ := s.Data()
			return d[va-lo : va-lo+uint32(n)]
		}
	}
	return nil
}

func rd32(va uint32) uint32 {
	b := readVA(va, 4)
	if b == nil {
		return 0
	}
	return binary.LittleEndian.Uint32(b)
}

func rd8(va uint32) byte {
	b := readVA(va, 1)
	if b == nil {
		return 0
	}
	return b[0]
}

type fn struct {
	start, end uint32
	name       string
	nswitch    int
	labels     map[int64]bool // `case N:` values in the corpus text
	nlabels    int            // number of `case N:` occurrences in the corpus text
	warn       map[string]bool
}

type insn struct {
	va uint32
	in x86asm.Inst
	s  string
}

type table struct {
	jmpVA, tblVA, byteTbl uint32
	bound                 int
	caseBase              int64
	window                []string
	deflt                 uint32 // target of the bound check's ja (default case)
	cases                 []int64
	targets               []uint32
	entries               int
}

func decode(va uint32) (x86asm.Inst, error) {
	off := int(va - textVA)
	if off < 0 || off >= len(text) {
		return x86asm.Inst{}, fmt.Errorf("oob")
	}
	return x86asm.Decode(text[off:], 32)
}

func syn(in x86asm.Inst, va uint32) string { return x86asm.IntelSyntax(in, uint64(va), nil) }

func jmpTable(in x86asm.Inst) (uint32, bool) {
	if in.Op != x86asm.JMP {
		return 0, false
	}
	m, ok := in.Args[0].(x86asm.Mem)
	if !ok || m.Scale != 4 || m.Index == 0 || m.Base != 0 {
		return 0, false
	}
	return uint32(m.Disp), true
}

func scanFn(fu *fn, skip map[uint32]uint32) []*table {
	var out []*table
	var hist []insn
	for va := fu.start; va < fu.end; {
		if e, ok := skip[va]; ok && e > va {
			va = e
			hist = nil
			continue
		}
		in, err := decode(va)
		if err != nil {
			va++
			continue
		}
		hist = append(hist, insn{va, in, syn(in, va)})
		if len(hist) > 16 {
			hist = hist[1:]
		}
		if tva, ok := jmpTable(in); ok {
			t := analyse(fu, hist, tva)
			out = append(out, t)
			skip[t.tblVA] = t.tblVA + uint32(4*t.entries)
			if t.byteTbl != 0 && t.bound >= 0 {
				skip[t.byteTbl] = t.byteTbl + uint32(t.bound+1)
			}
		}
		va += uint32(in.Len)
	}
	return out
}

func analyse(fu *fn, hist []insn, tva uint32) *table {
	n := len(hist)
	t := &table{jmpVA: hist[n-1].va, tblVA: tva, bound: -1}
	for i := n - 2; i >= 0 && i >= n-8; i-- {
		in := hist[i].in
		if in.Op == x86asm.MOVZX {
			if m, ok := in.Args[1].(x86asm.Mem); ok && uint32(m.Disp) > 0x400000 && m.Index == 0 {
				t.byteTbl = uint32(m.Disp)
				break
			}
		}
	}
	jaAt := -1
	for i := n - 2; i >= 0; i-- {
		if hist[i].in.Op == x86asm.JA {
			jaAt = i
			break
		}
	}
	lo := n - 6
	if jaAt >= 0 {
		if r, ok := hist[jaAt].in.Args[0].(x86asm.Rel); ok {
			t.deflt = uint32(int64(hist[jaAt].va) + int64(hist[jaAt].in.Len) + int64(r))
		}
	}
	if jaAt > 0 {
		if c := hist[jaAt-1].in; c.Op == x86asm.CMP {
			if im, ok := c.Args[1].(x86asm.Imm); ok {
				if v := int(int32(im)); v >= 0 && v < 4096 {
					t.bound = v
				}
			}
		}
		// case base: follow the compared operand backwards through movs to a
		// sub/add/lea with an immediate applied to it.
		track := ""
		if c := hist[jaAt-1].in; c.Op == x86asm.CMP {
			track = c.Args[0].String()
		}
	scanBase:
		for i := jaAt - 2; track != "" && i >= 0 && i >= jaAt-8; i-- {
			in := hist[i].in
			if in.Args[0] == nil || in.Args[0].String() != track {
				if in.Op == x86asm.CALL || in.Op == x86asm.JMP || in.Op == x86asm.RET {
					break
				}
				continue
			}
			switch in.Op {
			case x86asm.MOV:
				track = in.Args[1].String()
				if _, isMem := in.Args[1].(x86asm.Mem); isMem {
					// loaded from memory: a stack slot may have been adjusted earlier
					continue
				}
			case x86asm.SUB:
				if im, ok := in.Args[1].(x86asm.Imm); ok {
					t.caseBase = int64(int32(im))
				}
				break scanBase
			case x86asm.ADD:
				if im, ok := in.Args[1].(x86asm.Imm); ok {
					t.caseBase = -int64(int32(im))
				}
				break scanBase
			case x86asm.LEA:
				if m, ok := in.Args[1].(x86asm.Mem); ok && m.Index == 0 && m.Base != 0 {
					t.caseBase = -int64(int32(m.Disp))
				}
				break scanBase
			case x86asm.DEC:
				t.caseBase = 1
				break scanBase
			case x86asm.INC:
				t.caseBase = -1
				break scanBase
			default:
				break scanBase
			}
		}
		lo = jaAt - 6
	}
	if lo < 0 {
		lo = 0
	}
	for i := lo; i < n; i++ {
		t.window = append(t.window, fmt.Sprintf("%08x %s", hist[i].va, hist[i].s))
	}
	if t.byteTbl != 0 && t.bound >= 0 {
		maxb := 0
		for i := 0; i <= t.bound; i++ {
			b := int(rd8(t.byteTbl + uint32(i)))
			if b > maxb {
				maxb = b
			}
			t.cases = append(t.cases, int64(i)+t.caseBase)
			t.targets = append(t.targets, rd32(t.tblVA+4*uint32(b)))
		}
		t.entries = maxb + 1
		return t
	}
	cnt := t.bound + 1
	if t.bound < 0 { // unknown bound: read while entries point into this function
		for cnt = 0; cnt < 1024; cnt++ {
			v := rd32(tva + 4*uint32(cnt))
			if v < fu.start || v >= fu.end {
				break
			}
		}
	}
	t.entries = cnt
	for i := 0; i < cnt; i++ {
		t.cases = append(t.cases, int64(i)+t.caseBase)
		t.targets = append(t.targets, rd32(tva+4*uint32(i)))
	}
	return t
}

func preview(va uint32, n int) string {
	var parts []string
	for c := 0; c < n; c++ {
		in, err := decode(va)
		if err != nil {
			break
		}
		parts = append(parts, syn(in, va))
		if in.Op == x86asm.JMP || in.Op == x86asm.RET {
			break
		}
		va += uint32(in.Len)
	}
	return strings.Join(parts, " ; ")
}

var caseRE = regexp.MustCompile(`case (-?0x[0-9a-fA-F]+|-?[0-9]+):`)

// missing lists case values that jump somewhere other than the default target
// but never appear as a `case N:` label in the corpus text of the function
// (either raw or rebased, since Ghidra often renders `switch(x - base)`).
func missing(fu *fn, t *table) []int64 {
	var out []int64
	for i, tg := range t.targets {
		if tg != t.deflt && !fu.labels[t.cases[i]] && !fu.labels[t.cases[i]-t.caseBase] {
			out = append(out, t.cases[i])
		}
	}
	return out
}

func short(v []int64) string {
	if len(v) == 0 {
		return ""
	}
	s := fmt.Sprint(v)
	if len(s) > 80 {
		s = s[:77] + "..."
	}
	return s
}

func keys(m map[string]bool) string {
	var s []string
	for k := range m {
		s = append(s, k)
	}
	sort.Strings(s)
	return strings.Join(s, ",")
}

func main() {
	only := flag.String("only", "", "file with hex function addresses (FUN_ / 0x prefixes ok) to restrict to")
	pv := flag.Int("preview", 6, "instructions to show per case target")
	tsv := flag.Bool("tsv", false, "one summary line per table")
	fcmp := flag.Bool("fcmp", false, "instead of tables: list float-compare idioms (x87 fnstsw/sahf/fcomi, SSE comiss) with the flag test and branch")
	flag.Parse()
	if flag.NArg() < 2 {
		fmt.Fprintln(os.Stderr, "usage: jtscan [-only list] [-preview N] [-tsv] <aos2.exe> <dec/all.c>")
		os.Exit(2)
	}
	load(flag.Arg(0))
	cf, err := os.Open(flag.Arg(1))
	if err != nil {
		panic(err)
	}
	var fns []*fn
	sc := bufio.NewScanner(cf)
	sc.Buffer(make([]byte, 1<<20), 1<<24)
	var cur *fn
	for sc.Scan() {
		l := sc.Text()
		if strings.HasPrefix(l, "// ===== ") {
			i := strings.LastIndex(l, " @ ")
			a, _ := strconv.ParseUint(strings.TrimSpace(l[i+3:]), 16, 32)
			cur = &fn{start: uint32(a), name: strings.TrimSpace(l[9:i]), warn: map[string]bool{}, labels: map[int64]bool{}}
			fns = append(fns, cur)
			continue
		}
		if cur == nil {
			continue
		}
		if strings.Contains(l, "switch(") || strings.Contains(l, "switch (") {
			cur.nswitch++
		}
		for _, m := range caseRE.FindAllStringSubmatch(l, -1) {
			if v, err := strconv.ParseInt(m[1], 0, 64); err == nil {
				cur.labels[v] = true
				cur.nlabels++
			}
		}
		if strings.Contains(l, "Could not recover jumptable") {
			cur.warn["unrecovered-jumptable"] = true
		}
		if strings.Contains(l, "halt_baddata") {
			cur.warn["halt_baddata"] = true
		}
	}
	sort.Slice(fns, func(i, j int) bool { return fns[i].start < fns[j].start })
	for i, fu := range fns {
		fu.end = textVA + uint32(len(text))
		if i+1 < len(fns) {
			fu.end = fns[i+1].start
		}
	}
	want := map[uint32]bool{}
	if *only != "" {
		b, err := os.ReadFile(*only)
		if err != nil {
			panic(err)
		}
		for _, w := range strings.Fields(string(b)) {
			w = strings.TrimPrefix(strings.TrimPrefix(w, "FUN_"), "0x")
			if a, err := strconv.ParseUint(w, 16, 32); err == nil {
				want[uint32(a)] = true
			}
		}
	}
	skip := map[uint32]uint32{}
	if *tsv {
		fmt.Println("func\tname\tjmp\ttable\tbytetable\tbase\tncases\tntargets\tfn_tables\tcorpus_switches\tflag\twarn\tmissing_case_labels")
	}
	for _, fu := range fns {
		if *only != "" && !want[fu.start] {
			continue
		}
		if fu.start < textVA || fu.start >= textVA+uint32(len(text)) {
			continue
		}
		if *fcmp {
			fcmpScan(fu)
			continue
		}
		ts := scanFn(fu, skip)
		flagSw := ""
		if len(ts) > fu.nswitch {
			flagSw = "NOSWITCH"
		}
		// every non-default table entry needs its own `case N:` line; fewer
		// lines than that means some cases were merged or dropped.
		need := 0
		for _, t := range ts {
			for _, tg := range t.targets {
				if tg != t.deflt {
					need++
				}
			}
		}
		if fu.nlabels < need {
			flagSw = strings.TrimSpace(flagSw + " FEWLABELS")
		}
		w := keys(fu.warn)
		if *tsv {
			for _, t := range ts {
				u := map[uint32]bool{}
				for _, x := range t.targets {
					u[x] = true
				}
				fmt.Printf("%08x\t%s\t%08x\t%08x\t%08x\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", fu.start, fu.name, t.jmpVA, t.tblVA, t.byteTbl, t.caseBase, len(t.cases), len(u), len(ts), fu.nswitch, flagSw, w, short(missing(fu, t)))
			}
			if len(ts) == 0 && w != "" {
				fmt.Printf("%08x\t%s\t-\t-\t-\t-\t0\t0\t0\t%d\t\t%s\n", fu.start, fu.name, fu.nswitch, w)
			}
			continue
		}
		if len(ts) == 0 {
			if w != "" && *only != "" {
				fmt.Printf("== %s @ %08x  (no tables) warnings: %s\n\n", fu.name, fu.start, w)
			}
			continue
		}
		fmt.Printf("== %s @ %08x  tables=%d corpus-switches=%d %s %s\n", fu.name, fu.start, len(ts), fu.nswitch, flagSw, w)
		for _, t := range ts {
			kind := "dword"
			if t.byteTbl != 0 {
				kind = fmt.Sprintf("byte@%08x->dword", t.byteTbl)
			}
			fmt.Printf("  jmp %08x table %08x (%s) bound=%d base=%d default=%08x\n", t.jmpVA, t.tblVA, kind, t.bound, t.caseBase, t.deflt)
			if m := missing(fu, t); len(m) > 0 {
				fmt.Printf("    !! non-default cases with no `case N:` label in corpus: %v\n", m)
			}
			for _, l := range t.window {
				fmt.Printf("    | %s\n", l)
			}
			var order []uint32
			byT := map[uint32][]int64{}
			for i, tg := range t.targets {
				if _, ok := byT[tg]; !ok {
					order = append(order, tg)
				}
				byT[tg] = append(byT[tg], t.cases[i])
			}
			for _, tg := range order {
				d := ""
				if tg == t.deflt {
					d = " (=default)"
				}
				fmt.Printf("    case %v -> %08x%s: %s\n", byT[tg], tg, d, preview(tg, *pv))
			}
		}
		fmt.Println()
	}
}

// fcmpScan prints one line per float compare: the compare instruction, then the
// flag-test idiom up to and including the first conditional branch, e.g.
//
//	FUN_x @ va: fcomp dword ptr [..] | fnstsw ax | test ah, 0x41 | jnz ..
//
// x87 status bits after fcom: C0=ah&1 (st0<src), C2=ah&4 (unordered), C3=ah&0x40 (equal).
func fcmpScan(fu *fn) {
	isCmp := func(op x86asm.Op) bool {
		switch op {
		case x86asm.FCOM, x86asm.FCOMP, x86asm.FCOMPP, x86asm.FUCOM, x86asm.FUCOMP, x86asm.FUCOMPP,
			x86asm.FICOM, x86asm.FICOMP, x86asm.FTST, x86asm.FCOMI, x86asm.FCOMIP, x86asm.FUCOMI,
			x86asm.FUCOMIP, x86asm.COMISS, x86asm.UCOMISS, x86asm.COMISD, x86asm.UCOMISD:
			return true
		}
		return false
	}
	for va := fu.start; va < fu.end; {
		in, err := decode(va)
		if err != nil {
			va++
			continue
		}
		if isCmp(in.Op) {
			parts := []string{syn(in, va)}
			v := va + uint32(in.Len)
			for k := 0; k < 6 && v < fu.end; k++ {
				n, err := decode(v)
				if err != nil {
					break
				}
				parts = append(parts, syn(n, v))
				v += uint32(n.Len)
				if o := n.Op.String(); (o[0] == 'J' && o != "JMP") || strings.HasPrefix(o, "SET") {
					break
				}
			}
			fmt.Printf("%s\t%08x\t%s\n", fu.name, va, strings.Join(parts, " | "))
		}
		va += uint32(in.Len)
	}
}
