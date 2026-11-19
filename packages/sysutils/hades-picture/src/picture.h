/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026-present Hades */

/*
 * The pictures of hades-picture: which file a screen of a height takes, the card of a notice laid over it, and the
 * picture drawn on a screen as ply-image draws it (plymouth-lite, with plymouth-lite-9000 for "fit"). Nothing here
 * touches a device: the test on the build host (picture-test.c) runs it as it is.
 */
#ifndef HADES_PICTURE_H
#define HADES_PICTURE_H

#include <stddef.h>
#include <stdint.h>

/* no picture or screen larger than this on either side */
#define PICTURE_MAX 16384

struct picture {
	uint32_t *px; /* 0x00RRGGBB, row after row, w by h */
	int w, h;
};

enum picture_mode {
	PICTURE_FIT,     /* its own proportions, as large as fits, in the middle, on black */
	PICTURE_STRETCH, /* as wide and as high as the screen */
};

/* A PNG file, laid over black where it is not opaque. 0, or -1 with why in err. */
int picture_load(const char *path, struct picture *p, char *err, size_t errlen);
void picture_free(struct picture *p);

/* The file for a screen of h lines: <prefix>-<h>.png, else <prefix>-1080.png, else <prefix>.png (the order in which
 * the init picks the splash screen). 0 and its path, or -1. */
int picture_choose(const char *prefix, int h, char *path, size_t len);

/* The picture for a screen of sh lines, with the card of <card>-<height of the picture>.png laid over its bottom rows
 * when card is not NULL (a card is as wide as the picture: brand/notice), and text, when it is not NULL, written on
 * the line that the card has for it with the letters of font-<height>.png and font-<height>.txt in the folder of the
 * card (picture_text()). what: the names of the files, for the log. 0, or -1 with why in err. */
int picture_make(const char *prefix, const char *card, const char *text, int sh, struct picture *out, char *what,
		 size_t whatlen, char *err, size_t errlen);

/* Writes text on a picture with the letters of an atlas (<font>.png, <font>.txt: make-brand-images.py, font()), at
 * the place that <font>.txt gives in the rows of a card whose first row is row top of the picture (refused for a
 * card of another height than the one it was made for). A character that
 * is not printable ASCII is written as "?", and the text ends where it would pass the end of the line. 0 and how many
 * characters were written in *n, or -1 with why in err. */
int picture_text(struct picture *p, int top, const char *font, const char *text, int *n, char *err, size_t errlen);

/* Draws p on a screen of sw by sh pixels of 32 bits (XRGB8888) at dst, pitch bytes from one row to the next, with
 * alpha ORed into every pixel (0 for XRGB; the bits of an alpha channel of a frame buffer that has one): the picture
 * at the size and in the place where ply-image puts it, with the pixels that ply-image picks, and black around it. */
void picture_draw(const struct picture *p, enum picture_mode mode, uint8_t *dst, int sw, int sh, size_t pitch,
		  uint32_t alpha);

/* Where picture_draw() puts a picture of w by h on a screen of sw by sh: its size and its upper left corner. */
void picture_place(int w, int h, enum picture_mode mode, int sw, int sh, long *nw, long *nh, long *left, long *top);

#endif
