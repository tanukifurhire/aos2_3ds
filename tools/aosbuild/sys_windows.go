package main

import (
	"os"
	"os/exec"
	"syscall"

	"golang.org/x/sys/windows"
)

// hideWindow keeps console tools (go, gcc, tex3ds, makerom...) from opening a console window each when they are
// started from the GUI; their children inherit the hidden console.
func hideWindow(c *exec.Cmd) {
	c.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: windows.CREATE_NO_WINDOW}
}

// attachConsole gives the GUI-subsystem exe a stdout for command-line use: inherited handles (Git Bash pipes)
// are kept, otherwise it attaches to the console of the shell that started it (cmd, PowerShell).
func attachConsole() {
	if h, err := windows.GetStdHandle(windows.STD_OUTPUT_HANDLE); err == nil && h != 0 && h != windows.InvalidHandle {
		return
	}
	k := windows.NewLazySystemDLL("kernel32.dll").NewProc("AttachConsole")
	if r, _, _ := k.Call(uintptr(^uint32(0))); r == 0 { // ATTACH_PARENT_PROCESS
		return
	}
	if f, err := os.OpenFile("CONOUT$", os.O_WRONLY, 0); err == nil {
		os.Stdout, os.Stderr = f, f
	}
}

func openFolder(p string) error {
	c := exec.Command("explorer", p)
	return c.Start()
}
