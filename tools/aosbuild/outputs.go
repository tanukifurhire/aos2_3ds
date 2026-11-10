package main

import "path/filepath"

// outputs lists the files a target produces in out.
func outputs(out, target string) []string {
	var fs []string
	if target != "cia" {
		fs = append(fs, filepath.Join(out, "aos2_3ds.3dsx"))
	}
	if target != "3dsx" {
		fs = append(fs, filepath.Join(out, "aos2_3ds.cia"))
	}
	return fs
}
