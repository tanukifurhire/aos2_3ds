/* Host tests for the WAV parser: synthetic edge cases + every extracted SE. */
#include "../src/app/wav.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static size_t mk(uint8_t *b, uint16_t fmt, uint16_t ch, uint16_t bits, uint32_t data_len, int junk_odd)
{
	size_t o = 0;
#define P32(v) do { uint32_t _v = (v); memcpy(b + o, &_v, 4); o += 4; } while (0)
#define P16(v) do { uint16_t _v = (v); memcpy(b + o, &_v, 2); o += 2; } while (0)
	memcpy(b, "RIFF", 4); o = 4; P32(0); memcpy(b + o, "WAVE", 4); o += 4;
	if (junk_odd) { memcpy(b + o, "junk", 4); o += 4; P32(3); b[o++] = 1; b[o++] = 2; b[o++] = 3; b[o++] = 0; }
	memcpy(b + o, "fmt ", 4); o += 4; P32(16); P16(fmt); P16(ch); P32(22050); P32(22050 * 2 * ch); P16(2 * ch); P16(bits);
	memcpy(b + o, "data", 4); o += 4; P32(data_len);
	for (uint32_t i = 0; i < data_len; i++) b[o++] = (uint8_t)i;
	return o;
}

static void synthetic(void)
{
	uint8_t b[512];
	aos_wav w;
	size_t n = mk(b, 1, 1, 16, 100, 0);
	CHECK(aos_wav_parse(b, n, &w) == 0 && w.frames == 50 && w.channels == 1 && w.sample_rate == 22050, "mono");
	n = mk(b, 1, 2, 16, 100, 1);
	CHECK(aos_wav_parse(b, n, &w) == 0 && w.frames == 25, "stereo + odd junk chunk padding");
	n = mk(b, 1, 2, 16, 102, 0);
	CHECK(aos_wav_parse(b, n, &w) == 0 && w.frames == 25 && w.pcm_bytes == 100, "partial frame trimmed");
	n = mk(b, 3, 1, 16, 100, 0);
	CHECK(aos_wav_parse(b, n, &w) < 0, "float rejected");
	n = mk(b, 1, 1, 8, 100, 0);
	CHECK(aos_wav_parse(b, n, &w) == 0 && w.bits == 8 && w.frames == 100, "8-bit mono");
	n = mk(b, 1, 2, 8, 101, 0);
	CHECK(aos_wav_parse(b, n, &w) == 0 && w.frames == 50, "8-bit stereo trims partial frame");
	n = mk(b, 1, 1, 24, 99, 0);
	CHECK(aos_wav_parse(b, n, &w) < 0, "24-bit rejected");
	n = mk(b, 1, 1, 16, 100, 0);
	CHECK(aos_wav_parse(b, n - 1, &w) < 0, "truncated data rejected");
	for (size_t cut = 0; cut < n; cut++)
		(void)aos_wav_parse(b, cut, &w); /* must not crash on any truncation (ASan/UBSan) */
	b[40] = 0xff; b[41] = 0xff; b[42] = 0xff; b[43] = 0x7f; /* huge data size */
	CHECK(aos_wav_parse(b, n, &w) < 0, "oversized chunk rejected");
	n = mk(b, 1, 1, 16, 0, 0);
	CHECK(aos_wav_parse(b, n, &w) < 0, "empty data rejected");
}

static void real_files(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d) { printf("skip real files (%s missing)\n", dir); return; }
	struct dirent *e;
	int n = 0;
	while ((e = readdir(d))) {
		if (!strstr(e->d_name, ".WAV")) continue;
		char p[1024];
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		FILE *f = fopen(p, "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
		uint8_t *b = malloc((size_t)len);
		size_t got = fread(b, 1, (size_t)len, f);
		fclose(f);
		aos_wav w;
		int r = aos_wav_parse(b, got, &w);
		CHECK(r == 0, "%s: parse error %d", e->d_name, r);
		free(b);
		n++;
	}
	closedir(d);
	CHECK(n > 0, "found real WAVs");
	printf("parsed %d real SE files in %s\n", n, dir);
}

int main(void)
{
	synthetic();
	real_files("extracted/se/se");
	real_files("extracted/se/se2");
	real_files("extracted/se/se3");
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
