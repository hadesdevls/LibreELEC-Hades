// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2026-present Hades */
/*
 * The test pictures of hades-output-check and the judgement of the CRCs that the card gave for them
 * (output-check.h says what is measured and why).
 */
#include "output-check.h"

#include <string.h>

/* splitmix64: one well mixed 64-bit word from any other */
static uint64_t mix(uint64_t x)
{
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

/* Word number 'stream' of pixel 'index' for a seed: the same every time, independent from one stream to another */
static uint64_t word(uint64_t seed, uint32_t stream, uint32_t index)
{
	return mix(mix(seed ^ ((uint64_t)stream << 32)) ^ index);
}

/* The streams of words: 0 for Z, 1 + 2t and 2 + 2t for dA and dB of trial t, 100 to 102 for the halves of the
 * calibration, 200 + 24 v + 8 c + b for bit b of component c in variant v. */
#define S_Z 0u
#define S_DA(t) (1u + 2u * (t))
#define S_DB(t) (2u + 2u * (t))
#define S_HALF 100u
#define S_DD 101u
#define S_DE 102u
#define S_BIT(v, c, b) (200u + 24u * (v) + 8u * (c) + (b))
#define S_ONE_DA(c) (300u + 2u * (c))
#define S_ONE_DB(c) (301u + 2u * (c))
/* 400 + 10 s for Zs of set s, 401 + 10 s + 2 t and 402 + 10 s + 2 t for dAs and dBs of trial t */
#define S_STEP_Z(s) (400u + 10u * (s))
#define S_STEP_DA(s, t) (401u + 10u * (s) + 2u * (t))
#define S_STEP_DB(s, t) (402u + 10u * (s) + 2u * (t))

/* A pixel whose three components are values of set s (OC_SET_*), k * 0x11 or k * 0x55, k from the bits of w */
static uint32_t step_pixel(uint64_t w, unsigned int s)
{
	const uint32_t step = s == OC_SET_0X55 ? 0x55u : 0x11u, kmask = s == OC_SET_0X55 ? 3u : 15u;
	uint32_t px = 0;
	unsigned int c;

	for (c = 0; c < 3; c++)
		px |= (step * ((uint32_t)(w >> (8 * c)) & kmask)) << (8 * c);
	return px;
}

uint32_t oc_pixel(const struct oc_picture *p, uint64_t seed, uint32_t index)
{
	const uint32_t z = (uint32_t)word(seed, S_Z, index) & 0xffffffu;
	uint32_t da, db, half, mask, zs;

	switch (p->kind) {
	case OC_Z:
		return z;
	case OC_A:
	case OC_B:
	case OC_C:
		da = (uint32_t)word(seed, S_DA(p->trial), index) & 0xffffffu;
		db = (uint32_t)word(seed, S_DB(p->trial), index) & 0xffffffu;
		if (p->kind == OC_A)
			return z ^ da;
		if (p->kind == OC_B)
			return z ^ db;
		return z ^ da ^ db;
	case OC_D:
	case OC_E:
	case OC_F:
		half = (uint32_t)word(seed, S_HALF, index) & 1u;
		da = half ? (uint32_t)word(seed, S_DD, index) & 0xffffffu : 0;
		db = half ? 0 : (uint32_t)word(seed, S_DE, index) & 0xffffffu;
		if (p->kind == OC_D)
			return z ^ da;
		if (p->kind == OC_E)
			return z ^ db;
		return z ^ da ^ db;
	case OC_BIT:
		if (p->channel > 2 || p->bit > 7 || p->variant > 1)
			return z;
		if (word(seed, S_BIT(p->variant, p->channel, p->bit), index) & 1u)
			return z ^ (1u << (8u * (2u - p->channel) + p->bit));
		return z;
	case OC_ONE_A:
	case OC_ONE_B:
	case OC_ONE_C:
		if (p->channel > 2)
			return z;
		mask = 0xffu << (8u * (2u - p->channel));
		da = (uint32_t)word(seed, S_ONE_DA(p->channel), index) & mask;
		db = (uint32_t)word(seed, S_ONE_DB(p->channel), index) & mask;
		if (p->kind == OC_ONE_A)
			return z ^ da;
		if (p->kind == OC_ONE_B)
			return z ^ db;
		return z ^ da ^ db;
	case OC_STEP_Z:
	case OC_STEP_A:
	case OC_STEP_B:
	case OC_STEP_C:
		if (p->set >= OC_SETS || p->trial >= OC_TRIALS)
			return z;
		/* each set is closed under XOR: (k1 * step) ^ (k2 * step) = (k1 ^ k2) * step */
		zs = step_pixel(word(seed, S_STEP_Z(p->set), index), p->set);
		if (p->kind == OC_STEP_Z)
			return zs;
		da = step_pixel(word(seed, S_STEP_DA(p->set, p->trial), index), p->set);
		db = step_pixel(word(seed, S_STEP_DB(p->set, p->trial), index), p->set);
		if (p->kind == OC_STEP_A)
			return zs ^ da;
		if (p->kind == OC_STEP_B)
			return zs ^ db;
		return zs ^ da ^ db;
	}
	return z;
}

/* The words of linux-9889 (amdgpu_dm_crc.c), each time the CRC is set for a Dolby Vision tunnel on DCE */
#define OC_POINT_WORDS "Dolby Vision tunnel, DCE: its CRC is taken "

enum oc_point oc_crc_point_line(const char *line, enum oc_point before)
{
	const char *p = strstr(line, OC_POINT_WORDS);

	if (!p)
		return before;
	p += strlen(OC_POINT_WORDS);
	if (!strncmp(p, "after the formatter", strlen("after the formatter")))
		return OC_POINT_DCE_FORMATTER;
	if (!strncmp(p, "in the CRTC", strlen("in the CRTC")))
		return OC_POINT_DCE_CRTC;
	return before;
}

const char *oc_channel_name(unsigned int channel)
{
	static const char *const names[] = {"red", "green", "blue"};

	return channel < 3 ? names[channel] : "?";
}

static bool same(const struct oc_crc *x, const struct oc_crc *y, unsigned int values)
{
	return memcmp(x->v, y->v, values * sizeof(x->v[0])) == 0;
}

/* Whether crc(Z) ^ crc(A) ^ crc(B) ^ crc(C) is 0 in every value */
static bool relation(const struct oc_crc *z, const struct oc_crc *a, const struct oc_crc *b,
		     const struct oc_crc *c, unsigned int values)
{
	unsigned int i;

	for (i = 0; i < values; i++)
		if (z->v[i] ^ a->v[i] ^ b->v[i] ^ c->v[i])
			return false;
	return true;
}

/* The values (bit i: value i) where crc(Z) ^ crc(A) ^ crc(B) ^ crc(C) is not 0 */
static uint32_t broken(const struct oc_crc *z, const struct oc_crc *a, const struct oc_crc *b,
		       const struct oc_crc *c, unsigned int values)
{
	uint32_t d = 0;
	unsigned int i;

	for (i = 0; i < values; i++)
		if (z->v[i] ^ a->v[i] ^ b->v[i] ^ c->v[i])
			d |= 1u << i;
	return d;
}

/* The values (bit i: value i) that differ between x and y */
static uint32_t differing(const struct oc_crc *x, const struct oc_crc *y, unsigned int values)
{
	uint32_t d = 0;
	unsigned int i;

	for (i = 0; i < values; i++)
		if (x->v[i] != y->v[i])
			d |= 1u << i;
	return d;
}

void oc_analyse(const struct oc_measure *m, struct oc_result *r)
{
	const unsigned int n = m->values;
	bool all_measured = m->z.measured && m->z_end.measured && m->d.measured && m->e.measured &&
			    m->f.measured;
	bool constant;
	unsigned int t, c, b, s;

	memset(r, 0, sizeof(*r));
	if (n == 0 || n > OC_MAX_VALUES) {
		r->verdict = OC_NOT_JUDGED;
		r->reasons = OC_R_INCOMPLETE;
		return;
	}
	for (t = 0; t < OC_TRIALS; t++)
		all_measured = all_measured && m->a[t].measured && m->b[t].measured && m->c[t].measured;
	for (c = 0; c < 3; c++) {
		for (b = 0; b < 8; b++)
			all_measured = all_measured && m->bits[c][b][0].measured;
		all_measured = all_measured && m->one_a[c].measured && m->one_b[c].measured &&
			       m->one_c[c].measured;
	}
	if (!all_measured) {
		r->verdict = OC_NOT_JUDGED;
		r->reasons = OC_R_INCOMPLETE;
		return;
	}

	/* Stability: Z held for several frames, every picture over two frames, Z again at the end. */
	if (m->z_entries < 2 || m->z_changes || m->held_changes)
		r->reasons |= OC_R_UNSTABLE;
	if (!same(&m->z, &m->z_end, n))
		r->reasons |= OC_R_DRIFT;

	/* A CRC that never changes with the picture measures nothing. */
	constant = same(&m->z, &m->d, n) && same(&m->z, &m->e, n) && same(&m->z, &m->f, n);
	for (t = 0; t < OC_TRIALS; t++)
		constant = constant && same(&m->z, &m->a[t], n) && same(&m->z, &m->b[t], n) &&
			   same(&m->z, &m->c[t], n);
	if (constant)
		r->reasons |= OC_R_CONSTANT;

	/* Calibration: disjoint changes keep the relation through any pipe that works pixel by pixel. */
	if (!relation(&m->z, &m->d, &m->e, &m->f, n))
		r->reasons |= OC_R_CRC_NOT_LINEAR;

	/* Exactness: overlapping changes keep it only through a pipe linear over the bits. */
	for (t = 0; t < OC_TRIALS; t++)
		if (!relation(&m->z, &m->a[t], &m->b[t], &m->c[t], n)) {
			r->trials_failed++;
			r->values_broken |= broken(&m->z, &m->a[t], &m->b[t], &m->c[t], n);
		}
	if (r->trials_failed)
		r->reasons |= OC_R_TRANSFORMED;

	/* Where: one component of the input changed at a time (output-check.h). */
	for (c = 0; c < 3; c++) {
		r->channel_broken[c] = broken(&m->z, &m->one_a[c], &m->one_b[c], &m->one_c[c], n);
		if (r->channel_broken[c])
			r->reasons |= OC_R_TRANSFORMED;
	}

	/* Every bit of every component reaches the CRC (a second set of pixels, where measured, rules out a
	 * CRC that happened to be equal). */
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++) {
			uint32_t d = differing(&m->bits[c][b][0], &m->z, n);

			if (!d && m->bits[c][b][1].measured)
				d = differing(&m->bits[c][b][1], &m->z, n);
			if (!d)
				r->lost_bits |= 1u << (8 * c + b);
			r->changed_values[c] |= d;
		}
	if (r->lost_bits)
		r->reasons |= OC_R_BIT_LOST;

	if (r->reasons & OC_R_CONSTANT)
		r->verdict = OC_NOT_JUDGED;
	else if (r->reasons & (OC_R_UNSTABLE | OC_R_DRIFT))
		/* the same bytes gave different outputs: they are not passed through as they are */
		r->verdict = OC_NOT_EXACT;
	else if (r->reasons & OC_R_CRC_NOT_LINEAR)
		r->verdict = OC_NOT_JUDGED;
	else if (r->reasons & (OC_R_TRANSFORMED | OC_R_BIT_LOST))
		r->verdict = OC_NOT_EXACT;
	else
		r->verdict = OC_EXACT;

	/*
	 * The sets that a widening keeps linear (output-check.h): read whenever measured, and a fit only for a change
	 * that the exactness relation found and nothing else did (stable, linear CRC, every bit reaching it).
	 */
	r->steps_measured = true;
	for (s = 0; s < OC_SETS; s++) {
		r->steps_measured = r->steps_measured && m->step_z[s].measured;
		for (t = 0; t < OC_TRIALS; t++)
			r->steps_measured = r->steps_measured && m->step_a[s][t].measured &&
					    m->step_b[s][t].measured && m->step_c[s][t].measured;
	}
	if (!r->steps_measured)
		return;
	for (s = 0; s < OC_SETS; s++)
		for (t = 0; t < OC_TRIALS; t++)
			if (!relation(&m->step_z[s], &m->step_a[s][t], &m->step_b[s][t], &m->step_c[s][t], n)) {
				r->steps_failed[s]++;
				r->steps_broken[s] |= broken(&m->step_z[s], &m->step_a[s][t], &m->step_b[s][t],
							     &m->step_c[s][t], n);
			}
	if (r->verdict != OC_NOT_EXACT || r->reasons != OC_R_TRANSFORMED)
		return;
	if (r->steps_failed[OC_SET_0X55])
		r->fit = OC_FIT_VALUES;
	else if (r->steps_failed[OC_SET_0X11])
		r->fit = OC_FIT_WIDENING_10;
	else
		r->fit = OC_FIT_WIDENING_12;
}
