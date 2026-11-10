// Package main implements aos2extract, which unpacks the asset files of a
// legally obtained PC copy of Acceleration of SUGURI 2 into standard formats
// (PNG, WAV, Ogg Vorbis, text) as the first stage of the 3DS asset pipeline.
//
// Formats (all reverse-engineered from the data files, see docs/FORMATS.md):
//
//	*.pak            plain ZIP archives (PhysicsFS-style).
//	texture.pak/*.dat PNG files XORed with a repeating 89-byte key (TextureKey).
//	se*.dat          concatenated records: u32 dataLen, name\0, u32 fmtLen,
//	                 WAVEFORMATEX[fmtLen], PCM[dataLen].
//	bgm*.dat         concatenated Ogg Vorbis streams.
package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
)

// TextureKey is the repeating XOR key protecting PNGs inside mdl/texture.pak.
// Recovered by known-plaintext analysis (PNG signature + IHDR, and the fixed
// IEND trailer at varying offsets) and validated by decoding all 4218 files.
var TextureKey = []byte("u73$@STKY%&F#K;;zZTY%JM2@{}}1HsdbtyJU+g2j9ZXSc;32<%&v#&>_vDHYwQWJKIJs67?*e'-wBJ3#!)FVh!)O")

var pngSig = []byte{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}

// XorTexture decrypts (or encrypts — XOR is symmetric) a texture in place.
func XorTexture(b []byte) {
	n := len(TextureKey)
	for i := range b {
		b[i] ^= TextureKey[i%n]
	}
}

// DecodeTexture returns the PNG bytes for an encrypted texture.pak entry, or
// an error if the result is not a PNG (e.g. an unencrypted or unknown file).
func DecodeTexture(enc []byte) ([]byte, error) {
	out := append([]byte(nil), enc...)
	XorTexture(out)
	if !bytes.HasPrefix(out, pngSig) {
		return nil, errors.New("not an encrypted PNG")
	}
	return out, nil
}

// Sound is one record from an se*.dat container.
type Sound struct {
	Name string
	Fmt  []byte // raw WAVEFORMATEX
	PCM  []byte
}

// ParseSoundBank splits an se*.dat container into its records.
func ParseSoundBank(b []byte) ([]Sound, error) {
	var out []Sound
	for off := 0; off < len(b); {
		if len(b)-off < 4 {
			return out, fmt.Errorf("truncated record header at %d", off)
		}
		dataLen := int(binary.LittleEndian.Uint32(b[off:]))
		off += 4
		z := bytes.IndexByte(b[off:], 0)
		if z < 0 || z > 260 {
			return out, fmt.Errorf("bad name at %d", off)
		}
		name := string(b[off : off+z])
		off += z + 1
		if len(b)-off < 4 {
			return out, fmt.Errorf("truncated fmt length at %d", off)
		}
		fmtLen := int(binary.LittleEndian.Uint32(b[off:]))
		off += 4
		if fmtLen < 16 || fmtLen > 64 || len(b)-off < fmtLen+dataLen || dataLen < 0 {
			return out, fmt.Errorf("record %q at %d: bad sizes fmt=%d data=%d", name, off, fmtLen, dataLen)
		}
		s := Sound{Name: name, Fmt: b[off : off+fmtLen], PCM: b[off+fmtLen : off+fmtLen+dataLen]}
		off += fmtLen + dataLen
		out = append(out, s)
	}
	return out, nil
}

// WAV renders a Sound as a RIFF/WAVE file.
func (s Sound) WAV() []byte {
	var w bytes.Buffer
	fmtLen := len(s.Fmt)
	w.WriteString("RIFF")
	binary.Write(&w, binary.LittleEndian, uint32(4+8+fmtLen+8+len(s.PCM)))
	w.WriteString("WAVEfmt ")
	binary.Write(&w, binary.LittleEndian, uint32(fmtLen))
	w.Write(s.Fmt)
	w.WriteString("data")
	binary.Write(&w, binary.LittleEndian, uint32(len(s.PCM)))
	w.Write(s.PCM)
	return w.Bytes()
}

// SplitOgg splits concatenated Ogg streams at beginning-of-stream pages.
// The game's BGM archives contain stray padding bytes between some streams,
// so it resyncs on the next "OggS" capture pattern and reports how many bytes
// it skipped; streams never include those bytes.
func SplitOgg(b []byte) (streams [][]byte, skipped int, err error) {
	var starts, ends []int
	end := 0
	for off := 0; off+27 <= len(b); {
		if !bytes.Equal(b[off:off+4], []byte("OggS")) {
			n := bytes.Index(b[off:], []byte("OggS"))
			if n < 0 {
				skipped += len(b) - off
				break
			}
			skipped += n
			off += n
			continue
		}
		if b[off+5]&0x02 != 0 {
			if len(starts) > 0 {
				ends = append(ends, end)
			}
			starts = append(starts, off)
		}
		nseg := int(b[off+26])
		if off+27+nseg > len(b) {
			return nil, skipped, fmt.Errorf("truncated Ogg page at %d", off)
		}
		size := 27 + nseg
		for _, l := range b[off+27 : off+27+nseg] {
			size += int(l)
		}
		if off+size > len(b) {
			return nil, skipped, fmt.Errorf("truncated Ogg page body at %d", off)
		}
		off += size
		end = off
	}
	if len(starts) > 0 {
		ends = append(ends, end)
	}
	for i, s := range starts {
		streams = append(streams, b[s:ends[i]])
	}
	return streams, skipped, nil
}
