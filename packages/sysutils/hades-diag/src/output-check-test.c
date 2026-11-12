// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2026-present Hades */
/*
 * Host test of output-check-analysis.c: the test pictures, and the judgement of the CRCs of pipes and CRCs
 * modelled here (the identity, curves, range conversion, truncation, dither in time and in space, swapped
 * components, an inversion; a CRC-32 over the whole output, three CRC-16 one per component, a sum that is no
 * CRC, a CRC that never changes). Each case says what the check must answer. Exit status 0: all as expected.
 */
#include "output-check.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48
#define SEED 0x4861646573ULL

static int checks, failures;

#define CHECK(cond, ...)                                                                           \
	do {                                                                                       \
		checks++;                                                                          \
		if (!(cond)) {                                                                     \
			failures++;                                                                \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                \
			printf(__VA_ARGS__);                                                       \
			printf("\n");                                                              \
		}                                                                                  \
	} while (0)

/* ---- pipes: what a display pipe does to the 0x00RRGGBB of pixel (x, y) of frame n ---- */
enum pipe_kind {
	P_IDENTITY,
	P_GAMMA,	/* a curve on each component */
	P_LIMITED,	/* full range to limited range */
	P_TRUNC6,	/* the two low bits dropped */
	P_TEMPORAL,	/* a dither that changes from frame to frame */
	P_SPATIAL,	/* a fixed dither by position, of a value scaled by 257/256 (a prescale) */
	P_SWAP_RB,	/* red and blue exchanged */
	P_INVERT,	/* every bit inverted */
};

static unsigned int frame_no;

static uint32_t hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static uint32_t per_component(uint32_t rgb, unsigned int (*f)(unsigned int v, unsigned int c, uint32_t pos),
			      uint32_t pos)
{
	unsigned int r = f((rgb >> 16) & 255, 0, pos), g = f((rgb >> 8) & 255, 1, pos), b = f(rgb & 255, 2, pos);

	return (r << 16) | (g << 8) | b;
}

static unsigned int f_gamma(unsigned int v, unsigned int c, uint32_t pos)
{
	(void)c;
	(void)pos;
	return (unsigned int)lround(255.0 * pow(v / 255.0, 2.2));
}

static unsigned int f_limited(unsigned int v, unsigned int c, uint32_t pos)
{
	(void)c;
	(void)pos;
	return (16 * 255 + v * 219 + 127) / 255;
}

static unsigned int f_trunc6(unsigned int v, unsigned int c, uint32_t pos)
{
	(void)c;
	(void)pos;
	return v & 0xfc;
}

static unsigned int f_temporal(unsigned int v, unsigned int c, uint32_t pos)
{
	unsigned int d = hash32(pos * 3 + c + 0x10000u * frame_no) & 1;

	return v + d > 255 ? 255 : v + d;
}

static unsigned int f_spatial(unsigned int v, unsigned int c, uint32_t pos)
{
	/* v * 257 / 256 at 10 bits, a fixed threshold by position, back to 8 bits */
	unsigned int ten = v * 257 * 4 / 256;
	unsigned int threshold = hash32(pos * 3 + c) & 3;
	unsigned int out = (ten + threshold) >> 2;

	return out > 255 ? 255 : out;
}

static uint32_t pipe_apply(enum pipe_kind kind, uint32_t rgb, uint32_t pos)
{
	switch (kind) {
	case P_IDENTITY:
		return rgb;
	case P_GAMMA:
		return per_component(rgb, f_gamma, pos);
	case P_LIMITED:
		return per_component(rgb, f_limited, pos);
	case P_TRUNC6:
		return per_component(rgb, f_trunc6, pos);
	case P_TEMPORAL:
		return per_component(rgb, f_temporal, pos);
	case P_SPATIAL:
		return per_component(rgb, f_spatial, pos);
	case P_SWAP_RB:
		return ((rgb & 255) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 255);
	case P_INVERT:
		return rgb ^ 0xffffff;
	}
	return rgb;
}

/* ---- CRCs of a whole output ---- */
enum crc_kind {
	C_CRC32,	/* one CRC-32 (reflected, start and end inverted) over R, G, B of each pixel in raster order */
	C_CRC16X3,	/* three CRC-16, one over each component (as amdgpu gives three) */
	C_SUM,		/* one sum of the bytes: not a CRC */
	C_CONSTANT,	/* always the same */
};

static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
	int k;

	crc ^= byte;
	for (k = 0; k < 8; k++)
		crc = (crc >> 1) ^ (0xedb88320U & (0u - (crc & 1)));
	return crc;
}

static uint16_t crc16_byte(uint16_t crc, uint8_t byte)
{
	int k;

	crc ^= (uint16_t)(byte << 8);
	for (k = 0; k < 8; k++)
		crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1);
	return crc;
}

static unsigned int crc_values(enum crc_kind kind)
{
	return kind == C_CRC16X3 ? 3 : 1;
}

/* The CRC of one frame of a picture through a pipe; corrupt: a byte of pixel 7 changed after the pipe */
static void frame_crc(enum crc_kind ck, enum pipe_kind pk, const struct oc_picture *p, bool corrupt,
		      struct oc_crc *out)
{
	uint32_t c32 = 0xffffffffU, sum = 0;
	uint16_t c16[3] = {0xffff, 0xffff, 0xffff};
	uint32_t i;

	for (i = 0; i < W * H; i++) {
		uint32_t px = pipe_apply(pk, oc_pixel(p, SEED, i), i);
		uint8_t r, g, b;

		if (corrupt && i == 7)
			px ^= 0x5a0000;
		r = (uint8_t)(px >> 16);
		g = (uint8_t)(px >> 8);
		b = (uint8_t)px;
		c32 = crc32_byte(crc32_byte(crc32_byte(c32, r), g), b);
		c16[0] = crc16_byte(c16[0], r);
		c16[1] = crc16_byte(c16[1], g);
		c16[2] = crc16_byte(c16[2], b);
		sum += (uint32_t)r + g + b;
	}
	memset(out, 0, sizeof(*out));
	out->measured = true;
	switch (ck) {
	case C_CRC32:
		out->v[0] = ~c32;
		break;
	case C_CRC16X3:
		out->v[0] = c16[0];
		out->v[1] = c16[1];
		out->v[2] = c16[2];
		break;
	case C_SUM:
		out->v[0] = sum;
		break;
	case C_CONSTANT:
		out->v[0] = 0x1234;
		break;
	}
	frame_no++;
}

/* Measure all pictures as output-check.c does: Z held for 6 frames, each picture held for 2. */
struct options {
	bool corrupt_c0;	/* a byte of C of trial 0 changed after the pipe */
	bool skip_bit;		/* one bit picture left unmeasured */
	bool force_retry;	/* variant 0 of bit 0 of green made equal to Z: the second set must decide */
};

static void measure(enum crc_kind ck, enum pipe_kind pk, const struct options *o, struct oc_measure *m)
{
	struct oc_picture p = {.kind = OC_Z};
	struct oc_crc x, y, prev;
	unsigned int i, t, c, b;

	memset(m, 0, sizeof(*m));
	m->values = crc_values(ck);
	frame_no = 0;
	frame_crc(ck, pk, &p, false, &m->z);
	prev = m->z;
	for (i = 1; i < 6; i++) {
		frame_crc(ck, pk, &p, false, &x);
		m->z_entries++;
		if (memcmp(x.v, prev.v, sizeof(x.v)))
			m->z_changes++;
		prev = x;
	}
	m->z_entries++;

#define HOLD(pic, corrupt, dest)                                                                   \
	do {                                                                                       \
		frame_crc(ck, pk, (pic), (corrupt), &x);                                           \
		frame_crc(ck, pk, (pic), (corrupt), &y);                                           \
		m->held++;                                                                         \
		if (memcmp(x.v, y.v, sizeof(x.v)))                                                 \
			m->held_changes++;                                                         \
		*(dest) = x;                                                                       \
	} while (0)

	for (t = 0; t < OC_TRIALS; t++) {
		struct oc_picture a = {.kind = OC_A, .trial = t}, bb = {.kind = OC_B, .trial = t},
				  cc = {.kind = OC_C, .trial = t};

		HOLD(&a, false, &m->a[t]);
		HOLD(&bb, false, &m->b[t]);
		HOLD(&cc, o && o->corrupt_c0 && t == 0, &m->c[t]);
	}
	{
		struct oc_picture d = {.kind = OC_D}, e = {.kind = OC_E}, f = {.kind = OC_F};

		HOLD(&d, false, &m->d);
		HOLD(&e, false, &m->e);
		HOLD(&f, false, &m->f);
	}
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++) {
			struct oc_picture q = {.kind = OC_BIT, .channel = c, .bit = b};

			if (o && o->skip_bit && c == 1 && b == 3)
				continue;
			HOLD(&q, false, &m->bits[c][b][0]);
			if (o && o->force_retry && c == 1 && b == 0)
				m->bits[c][b][0] = m->z;
			if (!memcmp(m->bits[c][b][0].v, m->z.v, sizeof(m->z.v))) {
				q.variant = 1;
				HOLD(&q, false, &m->bits[c][b][1]);
			}
		}
	HOLD(&p, false, &m->z_end);
#undef HOLD
}

static void judge(const char *name, enum crc_kind ck, enum pipe_kind pk, const struct options *o,
		  enum oc_verdict want, unsigned int want_reasons, uint32_t want_lost, struct oc_result *out)
{
	struct oc_measure m;
	struct oc_result r;

	measure(ck, pk, o, &m);
	oc_analyse(&m, &r);
	CHECK(r.verdict == want, "%s: verdict %d, expected %d (reasons 0x%x)", name, r.verdict, want, r.reasons);
	CHECK(r.reasons == want_reasons, "%s: reasons 0x%x, expected 0x%x", name, r.reasons, want_reasons);
	CHECK(r.lost_bits == want_lost, "%s: lost bits 0x%06x, expected 0x%06x", name, r.lost_bits, want_lost);
	printf("%-44s verdict %d reasons 0x%02x lost 0x%06x trials failed %u values changed r/g/b %x/%x/%x\n",
	       name, r.verdict, r.reasons, r.lost_bits, r.trials_failed, r.changed_values[0],
	       r.changed_values[1], r.changed_values[2]);
	if (out)
		*out = r;
}

static void test_pictures(void)
{
	unsigned int t, c, b, v;
	uint32_t i;
	unsigned int half = 0, both = 0;

	for (i = 0; i < W * H; i++) {
		for (t = 0; t < OC_TRIALS; t++) {
			struct oc_picture z = {.kind = OC_Z}, a = {.kind = OC_A, .trial = t},
					  bb = {.kind = OC_B, .trial = t}, cc = {.kind = OC_C, .trial = t};
			uint32_t x = oc_pixel(&z, SEED, i) ^ oc_pixel(&a, SEED, i) ^ oc_pixel(&bb, SEED, i) ^
				     oc_pixel(&cc, SEED, i);

			CHECK(x == 0, "pixel %u trial %u: Z ^ A ^ B ^ C = 0x%06x", i, t, x);
			CHECK(oc_pixel(&a, SEED, i) <= 0xffffff, "pixel %u: A beyond 24 bits", i);
		}
		{
			struct oc_picture z = {.kind = OC_Z}, d = {.kind = OC_D}, e = {.kind = OC_E},
					  f = {.kind = OC_F};
			uint32_t zz = oc_pixel(&z, SEED, i), dd = oc_pixel(&d, SEED, i) ^ zz,
				 ee = oc_pixel(&e, SEED, i) ^ zz, ff = oc_pixel(&f, SEED, i) ^ zz;

			CHECK(!(dd && ee), "pixel %u: the halves of the calibration overlap", i);
			CHECK((dd ^ ee) == ff, "pixel %u: F is not D ^ E ^ Z", i);
			if (dd)
				half++;
			if (dd || ee)
				both++;
		}
		for (c = 0; c < 3; c++)
			for (b = 0; b < 8; b++)
				for (v = 0; v < 2; v++) {
					struct oc_picture z = {.kind = OC_Z},
							  q = {.kind = OC_BIT, .channel = c, .bit = b, .variant = v};
					uint32_t d = oc_pixel(&q, SEED, i) ^ oc_pixel(&z, SEED, i);

					CHECK(d == 0 || d == 1u << (8 * (2 - c) + b),
					      "pixel %u: the picture of bit %u of %s changes 0x%06x", i, b,
					      oc_channel_name(c), d);
				}
	}
	/* random halves: within a few per cent of one half */
	CHECK(half > W * H * 2 / 5 && half < W * H * 3 / 5, "calibration half has %u of %u pixels", half, W * H);
	CHECK(both > W * H * 9 / 10, "calibration changes %u of %u pixels", both, W * H);
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++) {
			struct oc_picture z = {.kind = OC_Z}, q = {.kind = OC_BIT, .channel = c, .bit = b};
			unsigned int n = 0;

			for (i = 0; i < W * H; i++)
				n += oc_pixel(&q, SEED, i) != oc_pixel(&z, SEED, i);
			CHECK(n > W * H * 2 / 5 && n < W * H * 3 / 5, "bit %u of %s changes %u of %u pixels", b,
			      oc_channel_name(c), n, W * H);
		}
	/* another seed, other pictures */
	{
		struct oc_picture z = {.kind = OC_Z};
		unsigned int differ = 0;

		for (i = 0; i < W * H; i++)
			differ += oc_pixel(&z, SEED, i) != oc_pixel(&z, SEED + 1, i);
		CHECK(differ > W * H * 9 / 10, "two seeds share %u of %u pixels", W * H - differ, W * H);
	}
}

/* The limits of the analysis, on measures changed by hand from one of the identity through a CRC-32 */
static void expect(const struct oc_measure *m, enum oc_verdict want, unsigned int want_reasons, const char *name)
{
	struct oc_result r;

	oc_analyse(m, &r);
	CHECK(r.verdict == want && r.reasons == want_reasons, "%s: verdict %d reasons 0x%x, expected %d 0x%x",
	      name, r.verdict, r.reasons, want, want_reasons);
}

static void test_edges(void)
{
	struct oc_measure m0, m;
	struct oc_picture z = {.kind = OC_Z};
	uint32_t i;
	unsigned int t, c, b, k;

	/* parameters out of range give Z; names */
	for (i = 0; i < 64; i++) {
		struct oc_picture q1 = {.kind = OC_BIT, .channel = 3}, q2 = {.kind = OC_BIT, .bit = 8},
				  q3 = {.kind = OC_BIT, .variant = 2};

		CHECK(oc_pixel(&q1, SEED, i) == oc_pixel(&z, SEED, i), "channel 3 changes pixel %u", i);
		CHECK(oc_pixel(&q2, SEED, i) == oc_pixel(&z, SEED, i), "bit 8 changes pixel %u", i);
		CHECK(oc_pixel(&q3, SEED, i) == oc_pixel(&z, SEED, i), "variant 2 changes pixel %u", i);
	}
	CHECK(!strcmp(oc_channel_name(0), "red") && !strcmp(oc_channel_name(1), "green") &&
	      !strcmp(oc_channel_name(2), "blue") && !strcmp(oc_channel_name(3), "?"), "names of the components");

	measure(C_CRC32, P_IDENTITY, NULL, &m0);
	expect(&m0, OC_EXACT, 0, "the measure the edges start from");

	/* each picture unmeasured on its own */
	{
		struct oc_crc *fields[] = {&m.z, &m.z_end, &m.d, &m.e, &m.f, &m.a[0], &m.a[1], &m.b[0], &m.b[1],
					   &m.c[0], &m.c[1], &m.bits[0][0][0], &m.bits[2][7][0], &m.bits[1][4][0]};

		for (k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
			m = m0;
			fields[k]->measured = false;
			expect(&m, OC_NOT_JUDGED, OC_R_INCOMPLETE, "one picture unmeasured");
		}
	}

	/* values beyond the driver's count are not read; the most values a driver may give are */
	m = m0;
	for (k = 1; k < OC_MAX_VALUES; k++) {
		m.z.v[k] = 0x11111111u * k;
		m.z_end.v[k] = 0x01010101u * k;
		m.d.v[k] = k;
		m.f.v[k] = 3u * k;
		for (t = 0; t < OC_TRIALS; t++)
			m.c[t].v[k] = 7u * k + t;
		for (c = 0; c < 3; c++)
			for (b = 0; b < 8; b++)
				m.bits[c][b][0].v[k] = m.z.v[k];
	}
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++)
			m.bits[c][b][0].v[1] = ~m.z.v[1];
	expect(&m, OC_EXACT, 0, "values past the count of the driver");
	{
		struct oc_result r;

		oc_analyse(&m, &r);
		CHECK(r.changed_values[0] == 1 && r.changed_values[1] == 1 && r.changed_values[2] == 1,
		      "values past the count read for the bits (%x %x %x)", r.changed_values[0],
		      r.changed_values[1], r.changed_values[2]);
	}
	m = m0;
	m.values = OC_MAX_VALUES;
	expect(&m, OC_EXACT, 0, "the most values of an entry, the others equal");

	/* Z held for two entries is enough */
	m = m0;
	m.z_entries = 2;
	expect(&m, OC_EXACT, 0, "Z held for two entries");

	/* a CRC constant only in part is not constant */
	m = m0;
	m.d = m.z;
	m.e = m.z;
	m.f = m.z;
	for (t = 0; t < OC_TRIALS; t++) {
		m.b[t] = m.z;
		m.c[t] = m.z;
	}
	expect(&m, OC_NOT_EXACT, OC_R_TRANSFORMED, "only A unlike Z");
	m = m0;
	m.a[0] = m.z;
	m.b[0] = m.z;
	m.c[0] = m.z;
	expect(&m, OC_EXACT, 0, "trial 0 all like Z");
	m = m0;
	m.d = m.z;
	m.f = m.e;
	for (t = 0; t < OC_TRIALS; t++) {
		m.a[t] = m.z;
		m.b[t] = m.z;
		m.c[t] = m.z;
	}
	expect(&m, OC_EXACT, 0, "D and every trial like Z");
	m = m0;
	m.e = m.d;
	m.f = m.z;
	for (t = 0; t < OC_TRIALS; t++) {
		m.a[t] = m.z;
		m.b[t] = m.z;
		m.c[t] = m.z;
	}
	expect(&m, OC_EXACT, 0, "F and every trial like Z");
	m = m0;
	m.d = m.z;
	m.e = m.z;
	m.f = m.z;
	m.a[1] = m.z;
	m.b[1] = m.z;
	m.c[1] = m.z;
	m.b[0] = m.a[0];
	m.c[0] = m.z;
	expect(&m, OC_EXACT, 0, "all like Z but trial 0, which keeps the relation");

	/* the second set of pixels is asked only where the first gave the CRC of Z */
	m = m0;
	m.bits[2][3][1] = m.z;
	expect(&m, OC_EXACT, 0, "a second set measured though the first differed");
	m = m0;
	m.bits[2][3][0] = m.z;
	m.bits[2][3][1] = m.z;
	{
		struct oc_result r;

		oc_analyse(&m, &r);
		CHECK(r.verdict == OC_NOT_EXACT && r.reasons == OC_R_BIT_LOST && r.lost_bits == 1u << 19,
		      "bit 3 of blue lost in both sets: verdict %d reasons 0x%x lost 0x%06x", r.verdict,
		      r.reasons, r.lost_bits);
	}
}

int main(void)
{
	struct options corrupt = {.corrupt_c0 = true}, skip = {.skip_bit = true}, retry = {.force_retry = true};
	struct oc_result r;
	const uint32_t low2 = 0x030303;

	test_pictures();
	test_edges();

	/* One CRC-32 over the output */
	judge("crc32 identity", C_CRC32, P_IDENTITY, NULL, OC_EXACT, 0, 0, NULL);
	judge("crc32 curve", C_CRC32, P_GAMMA, NULL, OC_NOT_EXACT, OC_R_TRANSFORMED, 0, &r);
	CHECK(r.trials_failed == OC_TRIALS, "curve: %u of the trials failed", r.trials_failed);
	judge("crc32 limited range", C_CRC32, P_LIMITED, NULL, OC_NOT_EXACT, OC_R_TRANSFORMED, 0, NULL);
	judge("crc32 two low bits dropped", C_CRC32, P_TRUNC6, NULL, OC_NOT_EXACT, OC_R_BIT_LOST, low2, NULL);
	judge("crc32 dither in time", C_CRC32, P_TEMPORAL, NULL, OC_NOT_EXACT,
	      OC_R_UNSTABLE | OC_R_DRIFT | OC_R_TRANSFORMED | OC_R_CRC_NOT_LINEAR, 0, NULL);
	judge("crc32 prescale and dither by position", C_CRC32, P_SPATIAL, NULL, OC_NOT_EXACT,
	      OC_R_TRANSFORMED, 0, NULL);
	/* limits of the measure, said in output-check.h: a swap or an inversion keeps every bit and is linear */
	judge("crc32 red and blue swapped (a limit)", C_CRC32, P_SWAP_RB, NULL, OC_EXACT, 0, 0, NULL);
	judge("crc32 every bit inverted (a limit)", C_CRC32, P_INVERT, NULL, OC_EXACT, 0, 0, NULL);
	/* a byte changed in one picture after the pipe: the relation of its trial fails */
	judge("crc32 identity, one byte of C changed", C_CRC32, P_IDENTITY, &corrupt, OC_NOT_EXACT,
	      OC_R_TRANSFORMED, 0, &r);
	CHECK(r.trials_failed == 1, "one byte changed: %u trials failed", r.trials_failed);

	/* Three CRC-16, one per component */
	judge("crc16x3 identity", C_CRC16X3, P_IDENTITY, NULL, OC_EXACT, 0, 0, &r);
	CHECK(r.changed_values[0] == 1 && r.changed_values[1] == 2 && r.changed_values[2] == 4,
	      "identity: each component should change its own value (%x %x %x)", r.changed_values[0],
	      r.changed_values[1], r.changed_values[2]);
	judge("crc16x3 red and blue swapped", C_CRC16X3, P_SWAP_RB, NULL, OC_EXACT, 0, 0, &r);
	CHECK(r.changed_values[0] == 4 && r.changed_values[2] == 1,
	      "swap: red should change the third value and blue the first (%x %x)", r.changed_values[0],
	      r.changed_values[2]);
	judge("crc16x3 curve", C_CRC16X3, P_GAMMA, NULL, OC_NOT_EXACT, OC_R_TRANSFORMED, 0, NULL);
	judge("crc16x3 two low bits dropped", C_CRC16X3, P_TRUNC6, NULL, OC_NOT_EXACT, OC_R_BIT_LOST, low2,
	      NULL);
	judge("crc16x3 dither in time", C_CRC16X3, P_TEMPORAL, NULL, OC_NOT_EXACT,
	      OC_R_UNSTABLE | OC_R_DRIFT | OC_R_TRANSFORMED | OC_R_CRC_NOT_LINEAR, 0, NULL);

	/* What is not a CRC, or measures nothing */
	judge("sum of the bytes, identity", C_SUM, P_IDENTITY, NULL, OC_NOT_JUDGED,
	      OC_R_CRC_NOT_LINEAR | OC_R_TRANSFORMED, 0, NULL);
	judge("constant, identity", C_CONSTANT, P_IDENTITY, NULL, OC_NOT_JUDGED, OC_R_CONSTANT | OC_R_BIT_LOST,
	      0xffffff, NULL);

	/* A picture not measured: nothing is judged */
	judge("crc32 identity, one bit picture missing", C_CRC32, P_IDENTITY, &skip, OC_NOT_JUDGED,
	      OC_R_INCOMPLETE, 0, NULL);
	/* The second set of pixels decides for a bit whose first gave the CRC of Z */
	judge("crc32 identity, a second set of pixels", C_CRC32, P_IDENTITY, &retry, OC_EXACT, 0, 0, NULL);

	/* values out of range */
	{
		struct oc_measure m;
		struct oc_result q;

		measure(C_CRC32, P_IDENTITY, NULL, &m);
		m.values = 0;
		oc_analyse(&m, &q);
		CHECK(q.verdict == OC_NOT_JUDGED && q.reasons == OC_R_INCOMPLETE, "0 values judged");
		m.values = OC_MAX_VALUES + 1;
		oc_analyse(&m, &q);
		CHECK(q.verdict == OC_NOT_JUDGED && q.reasons == OC_R_INCOMPLETE, "11 values judged");
		measure(C_CRC32, P_IDENTITY, NULL, &m);
		m.held_changes = 1;
		oc_analyse(&m, &q);
		CHECK(q.verdict == OC_NOT_EXACT && q.reasons == OC_R_UNSTABLE, "a held picture that changed");
		measure(C_CRC32, P_IDENTITY, NULL, &m);
		m.z_entries = 1;
		oc_analyse(&m, &q);
		CHECK(q.verdict == OC_NOT_EXACT && q.reasons == OC_R_UNSTABLE, "Z held for one entry only");
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
