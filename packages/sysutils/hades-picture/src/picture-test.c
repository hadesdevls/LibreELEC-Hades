// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026-present Hades

/*
 * The test of picture.c on the build host (tools/regress/boot, stage picture): not part of the image.
 *
 *   picture-test <splash folder> <theme folder of GRUB> <notice folder> <empty folder>
 *
 * Built with ply-image.c of plymouth-lite with the patches of its recipe (PLY_IMAGE_C, its main() renamed), so that
 * the pictures that picture_draw() makes are compared, pixel by pixel, with those that ply-image itself makes, on
 * screens of many sizes; and with the boot.png of each set of the theme of GRUB, which make-boot-theme.py computes as
 * ply-image draws the picture on a screen of that size. Exit 0 when every check passed.
 */

#include <png.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "picture.h"

/* (ply-image as it is: its functions, without its main()) */
#define main ply_image_main
#include PLY_IMAGE_C
#undef main

static int ok, bad;

static void check(int pass, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(int pass, const char *fmt, ...)
{
	va_list ap;

	fputs(pass ? "ok   " : "FAIL ", stdout);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	if (pass)
		ok++;
	else
		bad++;
}

static uint32_t *screen(int w, int h)
{
	return calloc((size_t)w * h, 4);
}

/* how many pixels of a screen differ (in RGB) from what ply-image makes of the same file */
static long against_ply(const char *path, enum picture_mode mode, int sw, int sh)
{
	char err[600];
	struct picture p;
	ply_image_t *img, *made;
	uint32_t *ours;
	long diff = 0, i;

	if (picture_load(path, &p, err, sizeof(err)))
		return -1;
	img = ply_image_new(path);
	if (!ply_image_load(img)) {
		picture_free(&p);
		return -1;
	}
	made = mode == PICTURE_FIT ? ply_image_fit(img, sw, sh) : ply_image_resize(img, sw, sh);
	ours = screen(sw, sh);
	picture_draw(&p, mode, (uint8_t *)ours, sw, sh, (size_t)sw * 4, 0);
	for (i = 0; i < (long)sw * sh; i++)
		if ((made->layout.as_pixels[i] & 0xffffff) != (ours[i] & 0xffffff))
			diff++;
	free(ours);
	if (made != img)
		ply_image_free(made);
	ply_image_free(img);
	picture_free(&p);
	return diff;
}

static int write_png(const char *path, int w, int h, uint32_t argb, int alpha)
{
	png_image img;
	uint8_t *px = malloc((size_t)w * h * 4);
	int i, r;

	for (i = 0; i < w * h; i++) {
		px[i * 4 + 0] = (argb >> 16) & 0xff;
		px[i * 4 + 1] = (argb >> 8) & 0xff;
		px[i * 4 + 2] = argb & 0xff;
		px[i * 4 + 3] = argb >> 24;
	}
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	img.width = (png_uint_32)w;
	img.height = (png_uint_32)h;
	img.format = alpha ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;
	if (!alpha)
		for (i = 0; i < w * h; i++)
			memmove(px + i * 3, px + i * 4, 3);
	r = png_image_write_to_file(&img, path, 0, px, 0, NULL);
	free(px);
	return r ? 0 : -1;
}

int main(int argc, char **argv)
{
	static const int heights[] = { 720, 768, 800, 900, 1024, 1050, 1080, 1200, 1440, 1600, 2160 };
	static const int screens[][2] = { { 1920, 1080 }, { 3840, 2160 }, { 2560, 1440 }, { 1024, 768 }, { 1366, 768 },
					  { 1280, 1024 }, { 3440, 1440 }, { 1280, 720 }, { 2560, 1600 }, { 1920, 1200 },
					  { 5120, 2880 }, { 1024, 600 }, { 1152, 864 }, { 2560, 1080 }, { 800, 480 },
					  { 1680, 1050 }, { 7680, 4320 } };
	char path[600], err[600], what[256], dir[600];
	struct picture p, q;
	const char *splash, *theme, *notice, *tmp;
	long nw, nh, left, top, d;
	unsigned i, j;
	int sw, sh;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc != 5) {
		fprintf(stderr, "usage: %s <splash folder> <theme folder of GRUB> <notice folder> <empty folder>\n", argv[0]);
		return 2;
	}
	splash = argv[1];
	theme = argv[2];
	notice = argv[3];
	tmp = argv[4];
	snprintf(dir, sizeof(dir), "%s/splash/splash", splash);

	/* where a picture goes: the numbers of plymouth-lite-9000 */
	picture_place(1350, 1080, PICTURE_FIT, 1920, 1080, &nw, &nh, &left, &top);
	check(nw == 1350 && nh == 1080 && left == 285 && top == 0, "1350x1080 on 1920x1080: %ldx%ld at %ld,%ld", nw, nh,
	      left, top);
	picture_place(1350, 1080, PICTURE_FIT, 2560, 1440, &nw, &nh, &left, &top);
	check(nw == 1800 && nh == 1440 && left == 380 && top == 0, "1350x1080 on 2560x1440: %ldx%ld at %ld,%ld", nw, nh,
	      left, top);
	picture_place(1350, 1080, PICTURE_FIT, 1024, 600, &nw, &nh, &left, &top);
	check(nw == 750 && nh == 600 && left == 137 && top == 0, "1350x1080 on 1024x600: %ldx%ld at %ld,%ld", nw, nh,
	      left, top);
	picture_place(2700, 2160, PICTURE_FIT, 1280, 1024, &nw, &nh, &left, &top);
	check(nw == 1280 && nh == 1024 && left == 0 && top == 0, "2700x2160 on 1280x1024 (the same proportions): %ldx%ld "
	      "at %ld,%ld", nw, nh, left, top);
	picture_place(1350, 1080, PICTURE_FIT, 800, 1280, &nw, &nh, &left, &top);
	check(nw == 800 && nh == 640 && left == 0 && top == 320, "1350x1080 on 800x1280 (upright): %ldx%ld at %ld,%ld",
	      nw, nh, left, top);
	picture_place(1350, 1080, PICTURE_STRETCH, 3440, 1440, &nw, &nh, &left, &top);
	check(nw == 3440 && nh == 1440 && left == 0 && top == 0, "stretch on 3440x1440: %ldx%ld at %ld,%ld", nw, nh, left,
	      top);

	/* which file a height takes */
	check(!picture_choose(dir, 1440, path, sizeof(path)) && strstr(path, "splash-1440.png"), "1440 lines: %s", path);
	check(!picture_choose(dir, 1152, path, sizeof(path)) && strstr(path, "splash-1080.png"),
	      "1152 lines, which has no picture: %s", path);
	snprintf(path, sizeof(path), "%s/oem.png", tmp);
	write_png(path, 64, 48, 0xff336699, 0);
	snprintf(dir, sizeof(dir), "%s/oem", tmp);
	check(!picture_choose(dir, 2160, path, sizeof(path)) && strstr(path, "/oem.png"),
	      "a picture of the user without a height: %s", path);
	snprintf(dir, sizeof(dir), "%s/none", tmp);
	check(picture_choose(dir, 1080, path, sizeof(path)) == -1, "no picture at all: none chosen");

	/* the pixels of ply-image, on screens of every kind, from the picture that each takes */
	snprintf(dir, sizeof(dir), "%s/splash/splash", splash);
	for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
		sw = screens[i][0];
		sh = screens[i][1];
		picture_choose(dir, sh, path, sizeof(path));
		d = against_ply(path, PICTURE_FIT, sw, sh);
		check(d == 0, "fit on %dx%d (%s): %ld pixels differ from ply-image", sw, sh, strrchr(path, '/') + 1, d);
	}
	/* (every picture made larger and smaller, and stretched) */
	for (i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
		snprintf(path, sizeof(path), "%s/splash/splash-%d.png", splash, heights[i]);
		d = against_ply(path, PICTURE_FIT, 1366, 768) + against_ply(path, PICTURE_FIT, 3840, 2160);
		d += against_ply(path, PICTURE_STRETCH, 1920, 1200);
		check(d == 0, "splash-%d.png fit on 1366x768 and 3840x2160, and stretched on 1920x1200: %ld pixels differ "
		      "from ply-image", heights[i], d);
	}

	/* the picture of the start of GRUB (boot.png), which is the init's picture on a screen of that size */
	for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
		uint32_t *ours;
		long diff = 0, k;

		sw = screens[i][0];
		sh = screens[i][1];
		snprintf(path, sizeof(path), "%s/%dx%d/boot.png", theme, sw, sh);
		if (picture_load(path, &q, err, sizeof(err)))
			continue;
		check(!picture_make(dir, NULL, NULL, sh, &p, what, sizeof(what), err, sizeof(err)), "the picture for %d lines: %s",
		      sh, what);
		ours = screen(sw, sh);
		picture_draw(&p, PICTURE_FIT, (uint8_t *)ours, sw, sh, (size_t)sw * 4, 0);
		for (k = 0; k < (long)sw * sh && q.w == sw && q.h == sh; k++)
			if (q.px[k] != (ours[k] & 0xffffff))
				diff++;
		check(q.w == sw && q.h == sh && diff == 0, "the boot.png of GRUB of %dx%d: %ld pixels differ", sw, sh, diff);
		free(ours);
		picture_free(&p);
		picture_free(&q);
	}

	/* a card is laid over the bottom of the picture of its height, and only there */
	for (i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
		const char *names[] = { "display", "failed-off", "failed-on" };

		for (j = 0; j < 3; j++) {
			char card[600];
			struct picture s, c;
			long y, diff = 0, x;

			snprintf(card, sizeof(card), "%s/%s", notice, names[j]);
			if (picture_make(dir, card, NULL, heights[i], &p, what, sizeof(what), err, sizeof(err))) {
				check(0, "%s on %d lines: %s", names[j], heights[i], err);
				continue;
			}
			snprintf(path, sizeof(path), "%s/splash/splash-%d.png", splash, heights[i]);
			picture_load(path, &s, err, sizeof(err));
			snprintf(path, sizeof(path), "%s-%d.png", card, heights[i]);
			picture_load(path, &c, err, sizeof(err));
			for (y = 0; y < p.h; y++)
				for (x = 0; x < p.w; x++) {
					uint32_t want = y < p.h - c.h ? s.px[y * s.w + x] : c.px[(y - (p.h - c.h)) * c.w + x];

					if (p.px[y * p.w + x] != want)
						diff++;
				}
			/* the splash screen is black where the card goes: the card is all that changes */
			for (y = p.h - c.h; y < p.h; y++)
				for (x = 0; x < s.w; x++)
					if (s.px[y * s.w + x])
						diff++;
			check(diff == 0 && c.w == s.w && c.h < s.h / 3, "%s on %d lines (%s): %ld pixels not as they must be",
			      names[j], heights[i], what, diff);
			picture_free(&p);
			picture_free(&s);
			picture_free(&c);
		}
	}
	snprintf(path, sizeof(path), "%s/wrong-1080.png", tmp);
	write_png(path, 1000, 100, 0xffffffff, 0);
	snprintf(path, sizeof(path), "%s/wrong", tmp);
	check(picture_make(dir, path, NULL, 1080, &p, what, sizeof(what), err, sizeof(err)) == -1 && strstr(err, "is not a card"),
	      "a card of another width is refused: %s", err);

	/* the version of the system on the card of the notice that Kodi could not start, at every height: only the line
	 * that the card has for it changes, every character is written; the one of 1080 lines goes to <empty folder> for
	 * the check against the typeface itself (tools/regress/boot/version_check.py) */
	for (i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
		static const char v[] = "LibreELEC (Hades 1.1): devel-20261118180313-3ea7eec";
		char card[600], want[64];
		struct picture a = { 0 }, c = { 0 };
		long changed = 0, y, x, top = -1, bottom = -1, outside = 0, strip;

		snprintf(card, sizeof(card), "%s/failed-off", notice);
		if (picture_make(dir, card, NULL, heights[i], &a, what, sizeof(what), err, sizeof(err)) ||
		    picture_make(dir, card, v, heights[i], &p, what, sizeof(what), err, sizeof(err))) {
			check(0, "the version on %d lines: %s", heights[i], err);
			continue;
		}
		snprintf(path, sizeof(path), "%s-%d.png", card, heights[i]);
		picture_load(path, &c, err, sizeof(err));
		strip = p.h - c.h;
		for (y = 0; y < p.h; y++)
			for (x = 0; x < p.w; x++)
				if (p.px[y * p.w + x] != a.px[y * p.w + x]) {
					changed++;
					if (top < 0)
						top = y;
					bottom = y;
					if (y < strip)
						outside++;
				}
		snprintf(want, sizeof(want), "%zu of %zu characters", strlen(v), strlen(v));
		check(strstr(what, want) && changed > 100 && outside == 0 && bottom - top < 40 * heights[i] / 1080 + 4,
		      "the version on %d lines (%s): %ld pixels in rows %ld to %ld, %ld outside the card", heights[i], what,
		      changed, top, bottom, outside);
		if (heights[i] == 1080) {
			uint8_t *rgb = malloc((size_t)p.w * p.h * 3);
			png_image img;
			long k;

			for (k = 0; k < (long)p.w * p.h; k++) {
				rgb[k * 3] = p.px[k] >> 16;
				rgb[k * 3 + 1] = p.px[k] >> 8;
				rgb[k * 3 + 2] = p.px[k];
			}
			memset(&img, 0, sizeof(img));
			img.version = PNG_IMAGE_VERSION;
			img.width = (png_uint_32)p.w;
			img.height = (png_uint_32)p.h;
			img.format = PNG_FORMAT_RGB;
			snprintf(path, sizeof(path), "%s/version-1080.png", tmp);
			check(png_image_write_to_file(&img, path, 0, rgb, 0, NULL) != 0, "the card with the version: %s", path);
			free(rgb);
		}
		picture_free(&a);
		picture_free(&c);
		picture_free(&p);
	}
	{
		char card[600];

		snprintf(card, sizeof(card), "%s/failed-on", notice);
		check(!picture_make(dir, card, "a\x01\xc3\xa9" "b", 1080, &p, what, sizeof(what), err, sizeof(err)) &&
		      strstr(what, "5 of 5 characters"), "a byte that is not printable ASCII is written as a question mark: %s",
		      what);
		picture_free(&p);
		snprintf(card, sizeof(card), "%s/display", notice);
		check(picture_make(dir, card, "x", 1080, &p, what, sizeof(what), err, sizeof(err)) == -1 &&
		      strstr(err, "not on this one"), "a card without the line: no picture (%s)", err);
		picture_free(&p);
	}

	/* a picture with transparency, over black */
	snprintf(path, sizeof(path), "%s/half.png", tmp);
	write_png(path, 4, 4, 0x80ff4000, 1);
	check(!picture_load(path, &p, err, sizeof(err)) && p.px[0] == 0x802000, "half transparent orange over black: %06x",
	      p.px ? p.px[0] : 0);
	picture_free(&p);

	printf("picture: %d checks passed, %d failed\n", ok, bad);
	return bad ? 1 : 0;
}
