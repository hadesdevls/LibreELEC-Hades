/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026-present Hades */
/*
 * The check of the output of a graphics card for Dolby Vision (hades-output-check): the test pictures and the
 * judgement of what the card's own CRC of its output made of them. No hardware here: output-check.c shows the
 * pictures and reads the CRCs; output-check-test.c tests this part on the computer that builds it.
 *
 * What is measured. A display pipe that passes the bytes of the Dolby Vision transport through unchanged is the
 * identity on the 24 bits of each pixel. The driver computes a CRC of what leaves the pipe (debugfs
 * crtc-N/crc/data); its algorithm is not documented, but a CRC is linear over GF(2): CRC(x ^ y) =
 * CRC(x) ^ CRC(y) ^ CRC(0) for pictures of one size. So, for any four pictures with Z ^ A ^ B ^ C = 0,
 *
 *     crc(P(Z)) ^ crc(P(A)) ^ crc(P(B)) ^ crc(P(C)) = 0
 *
 * whenever the pipe P is itself linear over the bits of each pixel (the identity is), whatever the CRC is.
 * A colour conversion, a curve, a range conversion, a rounding or a dither is not linear, and breaks it.
 *
 * Three measures, each on several pictures of random bytes (made again from a seed, never stored):
 * - stability: the same picture held for several frames gives the same CRC each frame (temporal dither or any
 *   change from one frame to the next breaks it);
 * - calibration: A and B differ from Z on two disjoint sets of pixels. For any pipe that works on each pixel
 *   by itself (also one that changes the bytes) the four outputs keep the relation exactly, so it tests the
 *   CRC, not the pipe: where it fails, the CRC of the driver is not a CRC over the bits of the picture (or the
 *   pipe mixes neighbouring pixels), and the check cannot judge the pipe;
 * - exactness: A and B differ from Z on the same pixels, in random bits: the relation holds only if the pipe
 *   is linear over the bits; and each of the 24 bits of a pixel, changed alone in half of the pixels, must
 *   change the CRC, so that no bit is dropped (dropping a bit, as a truncation does, is linear).
 *
 * What it cannot tell: a pipe that swaps bits or always inverts some (linear and keeping every bit) looks
 * exact to it; no display pipe is known to do that. It measures the pipe up to where the driver takes its CRC,
 * not the HDMI encoder, the cable or the display. amdgpu takes it in the output timing generator, after the
 * formatter: amdgpu_dm_crc.c offers the source "crtc dither" beside "crtc", and turns the dither of a stream off
 * for "crtc" so that its CRC repeats, which a CRC taken before the dither would not need (a Dolby Vision tunnel
 * keeps its truncation there whatever the source). i915 and xe take it at the end of the pipe.
 */
#ifndef HADES_OUTPUT_CHECK_H
#define HADES_OUTPUT_CHECK_H

#include <stdbool.h>
#include <stdint.h>

/* As DRM_MAX_CRC_NR: the most values that one CRC entry of debugfs carries */
#define OC_MAX_VALUES 10
/* Pairs of pictures that differ from Z on the same pixels */
#define OC_TRIALS 2

enum oc_kind {
	OC_Z,		/* the base picture: random bytes */
	OC_A,		/* Z ^ dA(trial) */
	OC_B,		/* Z ^ dB(trial) */
	OC_C,		/* Z ^ dA(trial) ^ dB(trial) */
	OC_D,		/* Z ^ dD: random bits on the pixels of one half (a random set) */
	OC_E,		/* Z ^ dE: random bits on the pixels of the other half */
	OC_F,		/* Z ^ dD ^ dE */
	OC_BIT,		/* Z with one bit of one component changed in a random half of the pixels */
};

struct oc_picture {
	enum oc_kind kind;
	unsigned int trial;	/* OC_A, OC_B, OC_C: 0 .. OC_TRIALS - 1 */
	unsigned int channel;	/* OC_BIT: 0 red, 1 green, 2 blue */
	unsigned int bit;	/* OC_BIT: 0 (least significant) .. 7 */
	unsigned int variant;	/* OC_BIT: 0, or 1 for a second set of pixels */
};

/* The colour of pixel index (y * width + x) of a picture: 0x00RRGGBB. */
uint32_t oc_pixel(const struct oc_picture *picture, uint64_t seed, uint32_t index);

/* What the card's CRC said of one picture: its values (as many as the driver gives). */
struct oc_crc {
	bool measured;
	uint32_t v[OC_MAX_VALUES];
};

struct oc_measure {
	unsigned int values;		/* values in each CRC entry (1 .. OC_MAX_VALUES) */
	struct oc_crc z;		/* Z, first */
	unsigned int z_entries;		/* entries read while Z was held */
	unsigned int z_changes;		/* of them, those unlike the one before */
	struct oc_crc z_end;		/* Z shown again at the end */
	struct oc_crc a[OC_TRIALS], b[OC_TRIALS], c[OC_TRIALS];
	struct oc_crc d, e, f;
	struct oc_crc bits[3][8][2];
	unsigned int held;		/* pictures held for two frames, each compared with itself */
	unsigned int held_changes;	/* of them, those whose two frames gave two CRCs */
};

enum oc_verdict {
	OC_EXACT,		/* the pipe passes the bytes unchanged, as far as a CRC can tell */
	OC_NOT_EXACT,		/* it changes them */
	OC_NOT_JUDGED,		/* the measure cannot tell */
};

/* Why (enum oc_verdict): bits of oc_result.reasons */
#define OC_R_UNSTABLE		(1u << 0)	/* one picture gave different CRCs on different frames */
#define OC_R_DRIFT		(1u << 1)	/* Z at the end gave another CRC than at the start */
#define OC_R_CRC_NOT_LINEAR	(1u << 2)	/* the calibration failed */
#define OC_R_TRANSFORMED	(1u << 3)	/* the exactness relation failed */
#define OC_R_BIT_LOST		(1u << 4)	/* a bit of a component did not change the CRC */
#define OC_R_CONSTANT		(1u << 5)	/* the CRC did not change with the picture */
#define OC_R_INCOMPLETE		(1u << 6)	/* a picture was not measured */

struct oc_result {
	enum oc_verdict verdict;
	unsigned int reasons;
	uint32_t lost_bits;		/* bit 8 * channel + bit: a bit that did not change the CRC */
	unsigned int trials_failed;	/* of the OC_TRIALS exactness relations */
	/* for each component, the CRC values (bit i: value i) that its bits changed */
	uint32_t changed_values[3];
};

void oc_analyse(const struct oc_measure *m, struct oc_result *r);

/* The names of the components, for the report */
const char *oc_channel_name(unsigned int channel);

#endif
