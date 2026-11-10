#include "wav.h"

#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int aos_wav_parse(const uint8_t *buf, size_t len, aos_wav *out)
{
	memset(out, 0, sizeof *out);
	if (len < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
		return -1;
	int have_fmt = 0;
	uint16_t bits = 0;
	size_t off = 12;
	while (off + 8 <= len) {
		uint32_t sz = le32(buf + off + 4);
		const uint8_t *body = buf + off + 8;
		if (sz > len - off - 8)
			return -2; /* chunk runs past end of buffer */
		if (!memcmp(buf + off, "fmt ", 4)) {
			if (sz < 16)
				return -3;
			if (le16(body) != 1)
				return -4; /* not PCM */
			out->channels = le16(body + 2);
			out->sample_rate = le32(body + 4);
			bits = le16(body + 14);
			have_fmt = 1;
		} else if (!memcmp(buf + off, "data", 4)) {
			if (!have_fmt)
				return -5;
			if ((bits != 8 && bits != 16) || out->channels < 1 || out->channels > 2 || out->sample_rate == 0)
				return -6;
			out->bits = bits;
			uint32_t frame_bytes = (bits / 8u) * out->channels;
			out->pcm = body;
			out->pcm_bytes = sz - sz % frame_bytes;
			out->frames = out->pcm_bytes / frame_bytes;
			return out->frames ? 0 : -7;
		}
		off += 8 + (size_t)sz + (sz & 1); /* chunks are word aligned */
	}
	return -8;
}
