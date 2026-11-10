/* Minimal RIFF/WAVE PCM8/PCM16 parser (portable, host-testable). */
#ifndef AOS_WAV_H
#define AOS_WAV_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint16_t channels;    /* 1 or 2 */
	uint32_t sample_rate;
	uint16_t bits;        /* 8 (unsigned PCM) or 16 (signed LE PCM) */
	const uint8_t *pcm;   /* interleaved samples, points into input */
	uint32_t pcm_bytes;
	uint32_t frames;      /* samples per channel */
} aos_wav;

/* Returns 0 on success, negative on malformed/unsupported input. */
int aos_wav_parse(const uint8_t *buf, size_t len, aos_wav *out);

#endif
