//go:build !windows

package main

import "os/exec"

func hideWindow(*exec.Cmd)      {}
func attachConsole()            {}
func openFolder(p string) error { return exec.Command("xdg-open", p).Start() }
