/* Operation counters for the host-side cost proxy (tests/perf_proxy.c).
 * Compiled to nothing unless AOS_PERF is defined, so shipped code is unaffected.
 * The counters are machine independent: they say how much work a tick does
 * (pairs tested, bytes moved, strings compared), not how long it takes. */
#ifndef AOS_PERF_H
#define AOS_PERF_H

#ifdef AOS_PERF
#include <stdint.h>
typedef struct {
	uint64_t shape_calls;  /* aos_shape_hit calls (collision pairs tested) */
	uint64_t shape_full;   /* ... that got past the exact distance pre-check */
	uint64_t trig;         /* aos_cos/aos_sin/aos_atan2 calls (double libm on the 3DS) */
	uint64_t trig_miss;    /* ... cos/sin calls that missed the memo and ran libm */
	uint64_t erase_bytes;  /* bytes moved by order-preserving pool erases */
	uint64_t erases;
	uint64_t tex_calls;    /* aos_tex lookups (draw build) */
	uint64_t tex_cmp;      /* string compares done by those lookups */
} aos_perf_counters;
extern aos_perf_counters aos_perf;
#define AOS_PERF_ADD(field, n) (aos_perf.field += (uint64_t)(n))
#else
#define AOS_PERF_ADD(field, n) ((void)0)
#endif

#endif
