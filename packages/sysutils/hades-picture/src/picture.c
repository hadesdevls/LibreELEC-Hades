// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026-present Hades

/*
 * The pictures of hades-picture (picture.h). The size, the place and the pixels of a picture on a screen are those of
 * ply-image (plymouth-lite: ply_image_resize(), and ply_image_fit() of plymouth-lite-9000), written again here: the
 * picture of the start is drawn by ply-image in the init and by this program after it, and must not move or change by
 * a pixel when one takes over from the other.
 */

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "picture.h"

void picture_free(struct picture *p)
{
	free(p->px);
	p->px = NULL;
	p->w = p->h = 0;
}

int picture_load(const char *path, struct picture *p, char *err, size_t errlen)
{
	png_image img;
	uint32_t *px;
	size_t i, n;

	memset(p, 0, sizeof(*p));
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&img, path)) {
		snprintf(err, errlen, "%s: %s", path, img.message);
		png_image_free(&img);
		return -1;
	}
	if (img.width < 1 || img.height < 1 || img.width > PICTURE_MAX || img.height > PICTURE_MAX) {
		snprintf(err, errlen, "%s: a picture of %ux%u", path, img.width, img.height);
		png_image_free(&img);
		return -1;
	}
	/* (B, G, R, A in memory: 0xAARRGGBB as a 32-bit number on this little-endian machine) */
	img.format = PNG_FORMAT_BGRA;
	n = (size_t)img.width * img.height;
	px = malloc(n * 4);
	if (!px) {
		snprintf(err, errlen, "%s: no memory for %ux%u", path, img.width, img.height);
		png_image_free(&img);
		return -1;
	}
	if (!png_image_finish_read(&img, NULL, px, 0, NULL)) {
		snprintf(err, errlen, "%s: %s", path, img.message);
		png_image_free(&img);
		free(px);
		return -1;
	}
	/* laid over black where it is not opaque: the screen has no alpha */
	for (i = 0; i < n; i++) {
		uint32_t v = px[i], a = v >> 24;

		if (a == 255) {
			px[i] = v & 0xffffff;
		} else {
			uint32_t r = ((v >> 16) & 0xff) * a, g = ((v >> 8) & 0xff) * a, b = (v & 0xff) * a;

			px[i] = (r + 127) / 255 << 16 | (g + 127) / 255 << 8 | (b + 127) / 255;
		}
	}
	p->px = px;
	p->w = (int)img.width;
	p->h = (int)img.height;
	png_image_free(&img);
	return 0;
}

int picture_choose(const char *prefix, int h, char *path, size_t len)
{
	int n;

	n = snprintf(path, len, "%s-%d.png", prefix, h);
	if (n > 0 && (size_t)n < len && access(path, R_OK) == 0)
		return 0;
	n = snprintf(path, len, "%s-1080.png", prefix);
	if (n > 0 && (size_t)n < len && access(path, R_OK) == 0)
		return 0;
	n = snprintf(path, len, "%s.png", prefix);
	if (n > 0 && (size_t)n < len && access(path, R_OK) == 0)
		return 0;
	return -1;
}

static const char *base_name(const char *path)
{
	const char *s = strrchr(path, '/');

	return s ? s + 1 : path;
}

int picture_text(struct picture *p, int top, const char *font, const char *text, int *n, char *err, size_t errlen)
{
	int cw, ch, base, pad, phases, first, count, strip, x256, line, right, r, g, b, i, k, adv[128];
	char path[512];
	struct picture atlas;
	FILE *f;
	int ok = 0;
	long pen;

	*n = 0;
	snprintf(path, sizeof(path), "%s.txt", font);
	f = fopen(path, "r");
	if (!f) {
		snprintf(err, errlen, "%s: no letters", path);
		return -1;
	}
	ok = fscanf(f, "hades-font 1 cell %d %d baseline %d pad %d phases %d first %d count %d strip %d line %d %d %d "
		    "colour %d %d %d advance", &cw, &ch, &base, &pad, &phases, &first, &count, &strip, &x256, &line, &right,
		    &r, &g, &b) == 14 &&
	     cw > 0 && cw < 512 && ch > 0 && ch < 512 && base >= 0 && base < ch && pad >= 0 && pad < cw && phases > 0 &&
	     phases <= 16 && first == 32 && count == 95 && r >= 0 && r < 256 && g >= 0 && g < 256 && b >= 0 && b < 256;
	for (i = 0; ok && i < count; i++)
		ok = fscanf(f, "%d", &adv[i]) == 1 && adv[i] >= 0 && adv[i] < cw * 256;
	fclose(f);
	if (!ok) {
		snprintf(err, errlen, "%s: not a file of letters of this program", path);
		return -1;
	}
	/* (the place of the line is in the rows of the cards that have it) */
	if (strip != p->h - top) {
		snprintf(err, errlen, "%s: its line is on cards of %d rows, not on this one of %d", path, strip, p->h - top);
		return -1;
	}
	snprintf(path, sizeof(path), "%s.png", font);
	if (picture_load(path, &atlas, err, errlen))
		return -1;
	if (atlas.w != count * cw || atlas.h != phases * ch) {
		snprintf(err, errlen, "%s: %dx%d, not %dx%d", path, atlas.w, atlas.h, count * cw, phases * ch);
		picture_free(&atlas);
		return -1;
	}
	pen = x256;
	for (k = 0; text[k]; k++) {
		int c = (unsigned char)text[k], ph, ix, yy, xx;

		if (c < first || c >= first + count)
			c = '?';
		c -= first;
		ix = (int)(pen >> 8);
		ph = (int)((pen & 255) * phases >> 8);
		if (ix + (adv[c] >> 8) > right)
			break;
		for (yy = 0; yy < ch; yy++) {
			int y = top + line - base + yy;

			if (y < 0 || y >= p->h)
				continue;
			for (xx = 0; xx < cw; xx++) {
				int x = ix - pad + xx;
				uint32_t a = atlas.px[(size_t)(ph * ch + yy) * atlas.w + (size_t)c * cw + xx] & 0xff, v;

				if (!a || x < 0 || x >= p->w)
					continue;
				v = p->px[(size_t)y * p->w + x];
				/* over what is there: the plate of the card, one flat colour */
				p->px[(size_t)y * p->w + x] =
					((((v >> 16) & 0xff) * (255 - a) + (uint32_t)r * a + 127) / 255) << 16 |
					((((v >> 8) & 0xff) * (255 - a) + (uint32_t)g * a + 127) / 255) << 8 |
					(((v & 0xff) * (255 - a) + (uint32_t)b * a + 127) / 255);
			}
		}
		pen += adv[c];
		(*n)++;
	}
	picture_free(&atlas);
	return 0;
}

int picture_make(const char *prefix, const char *card, const char *text, int sh, struct picture *out, char *what,
		 size_t whatlen, char *err, size_t errlen)
{
	char path[512], cpath[512], font[512];
	struct picture c;
	int y, n;

	memset(out, 0, sizeof(*out));
	if (picture_choose(prefix, sh, path, sizeof(path))) {
		snprintf(err, errlen, "no picture %s-%d.png, %s-1080.png or %s.png", prefix, sh, prefix, prefix);
		return -1;
	}
	if (picture_load(path, out, err, errlen))
		return -1;
	snprintf(what, whatlen, "%s", base_name(path));
	if (!card)
		return 0;

	/* the card made for the picture of that height */
	snprintf(cpath, sizeof(cpath), "%s-%d.png", card, out->h);
	if (picture_load(cpath, &c, err, errlen)) {
		picture_free(out);
		return -1;
	}
	if (c.w != out->w || c.h > out->h) {
		snprintf(err, errlen, "%s (%dx%d) is not a card for %s (%dx%d)", cpath, c.w, c.h, path, out->w, out->h);
		picture_free(&c);
		picture_free(out);
		return -1;
	}
	for (y = 0; y < c.h; y++)
		memcpy(out->px + (size_t)(out->h - c.h + y) * out->w, c.px + (size_t)y * c.w, (size_t)c.w * 4);
	snprintf(what + strlen(what), whatlen - strlen(what), " + %s", base_name(cpath));
	if (text) {
		const char *slash = strrchr(card, '/');

		snprintf(font, sizeof(font), "%.*sfont-%d", slash ? (int)(slash - card + 1) : 0, card, out->h);
		if (picture_text(out, out->h - c.h, font, text, &n, err, errlen)) {
			picture_free(&c);
			picture_free(out);
			return -1;
		}
		snprintf(what + strlen(what), whatlen - strlen(what), " + %d of %zu characters (%s)", n, strlen(text),
			 base_name(font));
	}
	picture_free(&c);
	return 0;
}

void picture_place(int w, int h, enum picture_mode mode, int sw, int sh, long *nw, long *nh, long *left, long *top)
{
	if (mode == PICTURE_STRETCH) {
		*nw = sw;
		*nh = sh;
	} else if ((long)w * sh > (long)h * sw) {
		/* wider than the screen is: it takes the whole width */
		*nw = sw;
		*nh = (long)h * sw / w;
	} else {
		*nh = sh;
		*nw = (long)w * sh / h;
	}
	if (*nw < 1)
		*nw = 1;
	if (*nh < 1)
		*nh = 1;
	*left = (sw - *nw) / 2;
	*top = (sh - *nh) / 2;
}

void picture_draw(const struct picture *p, enum picture_mode mode, uint8_t *dst, int sw, int sh, size_t pitch,
		  uint32_t alpha)
{
	long nw, nh, left, top, x, y;
	float scale_x, scale_y;
	uint32_t black = alpha;

	if (!p->px || p->w < 1 || p->h < 1) {
		for (y = 0; y < sh; y++)
			for (x = 0; x < sw; x++)
				((uint32_t *)(dst + (size_t)y * pitch))[x] = black;
		return;
	}
	picture_place(p->w, p->h, mode, sw, sh, &nw, &nh, &left, &top);
	/* as ply_image_resize() does: a float from the double of the quotient, and each source pixel the product of
	 * that float and the position, cut to a whole number */
	scale_x = (double)p->w / nw;
	scale_y = (double)p->h / nh;

	for (y = 0; y < sh; y++) {
		uint32_t *row = (uint32_t *)(dst + (size_t)y * pitch);
		const uint32_t *from;
		long oy;

		if (y < top || y >= top + nh) {
			for (x = 0; x < sw; x++)
				row[x] = black;
			continue;
		}
		for (x = 0; x < left; x++)
			row[x] = black;
		for (x = left + nw; x < sw; x++)
			row[x] = black;
		if (nw == p->w && nh == p->h) {
			from = p->px + (size_t)(y - top) * p->w;
			for (x = 0; x < nw; x++)
				row[left + x] = from[x] | alpha;
			continue;
		}
		oy = (long)((y - top) * scale_y);
		if (oy >= p->h)
			oy = p->h - 1;
		from = p->px + (size_t)oy * p->w;
		for (x = 0; x < nw; x++) {
			long ox = (long)(x * scale_x);

			if (ox >= p->w)
				ox = p->w - 1;
			row[left + x] = from[ox] | alpha;
		}
	}
}
