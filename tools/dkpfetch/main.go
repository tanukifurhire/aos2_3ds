// dkpfetch: minimal, portable devkitPro package fetcher. Reads the pacman sync
// databases from pkg.devkitpro.org, resolves dependencies, verifies sha256 and
// unpacks packages under -root (default ../devkitpro) without any system
// install. Only used to get a 3DS toolchain without the GUI installer.
package main

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/klauspost/compress/zstd"
	"github.com/ulikunitz/xz"
)

type pkg struct {
	name, filename, sha256, repoURL string
	depends, provides               []string
}

var repos = []string{
	"https://pkg.devkitpro.org/packages/windows/x86_64/dkp-windows.db",
	"https://pkg.devkitpro.org/packages/dkp-libs.db",
}

func fetch(u string) ([]byte, error) {
	req, _ := http.NewRequest("GET", u, nil)
	req.Header.Set("User-Agent", "pacman/6.0.2 (MSYS_NT x86_64) libalpm/13.0.2")
	r, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer r.Body.Close()
	if r.StatusCode != 200 {
		return nil, fmt.Errorf("%s: %s", u, r.Status)
	}
	return io.ReadAll(r.Body)
}

func decompress(b []byte) (io.Reader, error) {
	switch {
	case bytes.HasPrefix(b, []byte{0x28, 0xb5, 0x2f, 0xfd}):
		d, err := zstd.NewReader(bytes.NewReader(b))
		if err != nil {
			return nil, err
		}
		return d.IOReadCloser(), nil
	case bytes.HasPrefix(b, []byte{0x1f, 0x8b}):
		return gzip.NewReader(bytes.NewReader(b))
	case bytes.HasPrefix(b, []byte{0xfd, '7', 'z', 'X', 'Z'}):
		return xz.NewReader(bytes.NewReader(b))
	}
	return bytes.NewReader(b), nil
}

var verRe = regexp.MustCompile(`[<>=].*$`)

func loadDB(u string, into map[string]*pkg, prov map[string]string) error {
	b, err := fetch(u)
	if err != nil {
		return err
	}
	r, err := decompress(b)
	if err != nil {
		return err
	}
	base := u[:strings.LastIndex(u, "/")+1]
	tr := tar.NewReader(r)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if !strings.HasSuffix(h.Name, "/desc") {
			continue
		}
		p := &pkg{repoURL: base}
		var key string
		sc := bufio.NewScanner(tr)
		for sc.Scan() {
			l := sc.Text()
			if strings.HasPrefix(l, "%") {
				key = l
				continue
			}
			if l == "" {
				continue
			}
			switch key {
			case "%NAME%":
				p.name = l
			case "%FILENAME%":
				p.filename = l
			case "%SHA256SUM%":
				p.sha256 = l
			case "%DEPENDS%":
				p.depends = append(p.depends, verRe.ReplaceAllString(l, ""))
			case "%PROVIDES%":
				p.provides = append(p.provides, verRe.ReplaceAllString(l, ""))
			}
		}
		if _, dup := into[p.name]; !dup {
			into[p.name] = p
			for _, pr := range p.provides {
				prov[pr] = p.name
			}
		}
	}
}

func main() {
	root := flag.String("root", "../devkitpro", "install root (gets opt/devkitpro/... inside)")
	list := flag.Bool("list", false, "list matching package names and exit")
	flag.Parse()
	pkgs, prov := map[string]*pkg{}, map[string]string{}
	for _, u := range repos {
		if err := loadDB(u, pkgs, prov); err != nil {
			log.Fatal(err)
		}
	}
	if *list {
		for n := range pkgs {
			for _, a := range flag.Args() {
				if strings.Contains(n, a) {
					fmt.Println(n, pkgs[n].filename)
				}
			}
		}
		return
	}
	want := map[string]bool{}
	var order []string
	var visit func(n string)
	visit = func(n string) {
		if p, ok := prov[n]; ok && pkgs[n] == nil {
			n = p
		}
		if want[n] {
			return
		}
		p := pkgs[n]
		if p == nil {
			log.Printf("skip unresolved dependency %q (not in devkitPro repos)", n)
			return
		}
		want[n] = true
		for _, d := range p.depends {
			visit(d)
		}
		order = append(order, n)
	}
	for _, a := range flag.Args() {
		visit(a)
	}
	for _, n := range order {
		p := pkgs[n]
		b, err := fetch(p.repoURL + p.filename)
		if err != nil {
			log.Fatal(err)
		}
		sum := sha256.Sum256(b)
		if p.sha256 != "" && hex.EncodeToString(sum[:]) != p.sha256 {
			log.Fatalf("%s: sha256 mismatch", p.filename)
		}
		if err := extract(b, *root); err != nil {
			log.Fatalf("%s: %v", p.filename, err)
		}
		log.Printf("installed %s", p.filename)
	}
}

func extract(b []byte, root string) error {
	r, err := decompress(b)
	if err != nil {
		return err
	}
	tr := tar.NewReader(r)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if strings.HasPrefix(h.Name, ".") { // .PKGINFO, .MTREE, .INSTALL
			continue
		}
		clean := filepath.Clean(filepath.FromSlash(h.Name))
		if strings.HasPrefix(clean, "..") || filepath.IsAbs(clean) {
			return fmt.Errorf("unsafe path %q", h.Name)
		}
		dst := filepath.Join(root, clean)
		switch h.Typeflag {
		case tar.TypeDir:
			os.MkdirAll(dst, 0o755)
		case tar.TypeReg:
			os.MkdirAll(filepath.Dir(dst), 0o755)
			f, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o755)
			if err != nil {
				return err
			}
			_, err = io.Copy(f, tr)
			f.Close()
			if err != nil {
				return err
			}
		case tar.TypeSymlink, tar.TypeLink:
			// Windows: materialise links as copies once targets exist.
			tgt := h.Linkname
			if h.Typeflag == tar.TypeSymlink {
				tgt = filepath.Join(filepath.Dir(clean), filepath.FromSlash(h.Linkname))
			}
			if data, err := os.ReadFile(filepath.Join(root, filepath.Clean(filepath.FromSlash(tgt)))); err == nil {
				os.MkdirAll(filepath.Dir(dst), 0o755)
				os.WriteFile(dst, data, 0o755)
			}
		}
	}
}
