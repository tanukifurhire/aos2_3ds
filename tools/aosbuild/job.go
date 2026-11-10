package main

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// Job carries one run (setup, build, copy): cancellation, the log sink, overall progress and the environment
// for child processes.
type Job struct {
	ctx      context.Context
	line     func(string)                     // one log line
	progress func(frac float64, label string) // overall 0..1
	env      []string
}

func newJob(ctx context.Context, tc Toolchain, line func(string), progress func(float64, string)) *Job {
	env := os.Environ()
	if tc.DKP != "" {
		env = append(env, "DEVKITPRO="+filepath.ToSlash(tc.DKP), "DEVKITARM="+filepath.ToSlash(filepath.Join(tc.DKP, "devkitARM")))
	}
	return &Job{ctx: ctx, line: line, progress: progress, env: env}
}

func (j *Job) logf(format string, a ...any) { j.line(fmt.Sprintf(format, a...)) }

// stage is one named step of a pipeline; weight sets its share of the progress bar, and run may report its
// own 0..1 progress through sub.
type stage struct {
	name   string
	weight float64
	run    func(j *Job, sub func(float64)) error
}

func (j *Job) runStages(st []stage) error {
	total := 0.0
	for _, s := range st {
		total += s.weight
	}
	if total == 0 {
		total = 1
	}
	done := 0.0
	start := time.Now()
	for _, s := range st {
		if err := j.ctx.Err(); err != nil {
			return err
		}
		j.progress(done/total, s.name)
		j.line("== " + s.name)
		base, w := done, s.weight
		sub := func(f float64) { j.progress((base+min(max(f, 0), 1)*w)/total, s.name) }
		t0 := time.Now()
		if err := s.run(j, sub); err != nil {
			if j.ctx.Err() != nil {
				return j.ctx.Err()
			}
			return fmt.Errorf("%s: %w", s.name, err)
		}
		if d := time.Since(t0); d > 2*time.Second {
			j.logf("   (%s)", d.Round(100*time.Millisecond))
		}
		done += s.weight
	}
	j.progress(1, "Done")
	j.logf("finished in %s", time.Since(start).Round(time.Second))
	return nil
}

func (j *Job) command(dir, name string, args ...string) *exec.Cmd {
	c := exec.CommandContext(j.ctx, name, args...)
	c.Dir = dir
	c.Env = j.env
	c.WaitDelay = 3 * time.Second
	hideWindow(c)
	return c
}

// run executes a tool and streams its stdout+stderr into the log line by line.
func (j *Job) run(dir, name string, args ...string) error {
	c := j.command(dir, name, args...)
	pr, pw := io.Pipe()
	c.Stdout, c.Stderr = pw, pw
	done := make(chan struct{})
	go func() {
		sc := bufio.NewScanner(pr)
		sc.Buffer(make([]byte, 64*1024), 1024*1024)
		for sc.Scan() {
			if l := strings.TrimRight(sc.Text(), "\r"); l != "" {
				j.line(l)
			}
		}
		io.Copy(io.Discard, pr)
		close(done)
	}()
	err := c.Run()
	pw.Close()
	<-done
	return j.wrapErr(name, err)
}

// output executes a tool and returns its combined output instead of logging it.
func (j *Job) output(dir, name string, args ...string) (string, error) {
	c := j.command(dir, name, args...)
	var b bytes.Buffer
	c.Stdout, c.Stderr = &b, &b
	err := c.Run()
	return strings.TrimSpace(b.String()), j.wrapErr(name, err)
}

func (j *Job) wrapErr(name string, err error) error {
	if err == nil {
		return nil
	}
	if j.ctx.Err() != nil {
		return j.ctx.Err()
	}
	if errors.Is(err, exec.ErrNotFound) {
		return fmt.Errorf("%s not found", name)
	}
	return fmt.Errorf("%s: %w", filepath.Base(name), err)
}

// goBuild builds the Go tool in dir to dir/<out> (as build.sh does: the real exe is then run directly, so a
// cancel kills the tool itself and not just a `go run` wrapper).
func (j *Job) goBuild(dir, out string) error {
	return j.run(dir, "go", "build", "-o", out, ".")
}
