// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026-present Hades

/*
 * hades-picture: the picture of the start, and the notices that the start of Kodi lays over it, on every display at
 * the size of that display.
 *
 * The console's frame buffer (fbdev) is one picture for all the displays of a graphics card: the kernel makes it as
 * large as the smallest of them (drm_fb_helper.c, __drm_fb_helper_find_sizes()), and every display shows it from its
 * upper left corner. A picture drawn there fills the smallest display and sits small in a corner of the others. With
 * several displays of different sizes, this program gives each display (each CRTC of a DRM device that is on) a
 * picture of its own size, in a buffer of its own:
 *   - only where the display is on already, in the mode it has: the primary plane of its CRTC gets the new buffer in
 *     an atomic commit without DRM_MODE_ATOMIC_ALLOW_MODESET, which the kernel refuses rather than change a mode. No
 *     mode, connector, colour or other property is changed, and nothing else is written;
 *   - it holds the DRM master of the device only while it shows its pictures, so that nothing else (the console)
 *     draws over them; "release", before Kodi starts, lets go of it, and the pictures stay on the screen until Kodi has
 *     put its own in their place: then the buffers are freed and the device is closed, so that the console is as it
 *     was and comes back when Kodi closes the device. "stop" puts back what each plane showed before (the console's
 *     frame buffer) and closes the device.
 * Where every display that is on shows the console's frame buffer at its full size (one display, or several of one
 * size), the picture goes into that frame buffer as ply-image draws it, and nothing is held.
 *
 *   hades-picture serve                                       the service (hades-picture.service, through its socket)
 *   hades-picture show <fit|stretch> <boot|kodi> <picture> [<card>]   asks the service to show a picture
 *   hades-picture release                                     Kodi starts: the service lets go of the displays
 *   hades-picture stop                                        the service gives the displays back to the console
 *   hades-picture fb <fit|stretch> <picture> [<card>]         draws into the console's frame buffer (fb0) and shows it
 *   hades-picture fb black                                    the same, black
 *   hades-picture init <fit|stretch> <picture>                the init of the initramfs: the picture of the start, as
 *                                                             "show", without the service (see init_mode())
 * "show" and "fb" with a card take text=<a line>, last: the line written on the card where it has a place for it (the
 * version of the system on the notice that Kodi could not start: picture_text()).
 *
 * <picture> and <card> name files without their ending (picture.h, picture_choose()). "fit" keeps the proportions of
 * the picture, "stretch" makes it as large as the screen (a picture of the user: oemsplash). "boot" is the picture of
 * the start, which is not drawn once Kodi was started ("release" leaves /run/hades-picture.kodi); "kodi", a notice of
 * the start of Kodi. Each command prints one line that starts with "ok", "none" (no display is on: status 3) or "no"
 * (status 1; 2 where the service did not answer).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <linux/fb.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "picture.h"

#define SOCK_PATH "/run/hades-picture.sock"
#define HELD_FLAG "/run/hades-picture.held"
#define KODI_FLAG "/run/hades-picture.kodi"
#define INIT_FILE "/run/hades-picture.init"
#define INIT_WAIT_MS 120000
#define MAX_CARDS 8
#define MAX_OUT 16
#define LINE_MAX_LEN 1024
#define REPLY_LEN 4096

enum { P_FB_ID, P_CRTC_ID, P_SRC_X, P_SRC_Y, P_SRC_W, P_SRC_H, P_CRTC_X, P_CRTC_Y, P_CRTC_W, P_CRTC_H, P_N };
static const char *const plane_prop_names[P_N] = { "FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",  "SRC_W",
						   "SRC_H",  "CRTC_X",  "CRTC_Y", "CRTC_W", "CRTC_H" };

struct buffer {
	uint32_t fb, handle;
	void *map;
	uint64_t size;
};

/* a display that is on: a CRTC with its mode, and the primary plane that shows on it */
struct output {
	uint32_t crtc, plane;
	uint32_t prop[P_N];
	uint64_t saved[P_N]; /* the plane as it was before this program put a picture on it */
	int w, h;
	char name[40];
	struct buffer buf; /* the picture of this program on it (fb 0: none) */
};

struct card {
	char node[24];
	int fd;
	int master;
	int n;
	struct output out[MAX_OUT];
};

/* the devices whose displays show pictures of this program */
static struct card cards[MAX_CARDS];
static int ncards;
static int released;
static char reply[REPLY_LEN];

static void logline(const char *fmt, ...)
{
	char line[2048];
	va_list ap;
	int n;

	n = snprintf(line, sizeof(line), "hades-picture: ");
	va_start(ap, fmt);
	vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, ap);
	va_end(ap);
	n = (int)strlen(line);
	line[n++] = '\n';
	if (write(2, line, (size_t)n) < 0)
		return;
}

static void say(const char *fmt, ...)
{
	size_t used = strlen(reply);
	va_list ap;

	if (used >= sizeof(reply) - 1)
		return;
	va_start(ap, fmt);
	vsnprintf(reply + used, sizeof(reply) - used, fmt, ap);
	va_end(ap);
}

static long long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

static void touch(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);

	if (fd >= 0)
		close(fd);
}

/* ---------------------------------------------------------------- the console's frame buffer */

static void fb_name(char *name, size_t len)
{
	FILE *f = fopen("/sys/class/graphics/fb0/name", "r");

	name[0] = 0;
	if (f) {
		if (fgets(name, (int)len, f))
			name[strcspn(name, "\n")] = 0;
		fclose(f);
	}
}

/* Draws into fb0 at its visible size (a picture with its card), or makes all of it black, and with pan pans to where
 * it is, which makes the driver of a DRM device show it (plymouth-lite-9001 says why); refused while a program owns
 * the display, and by a frame buffer that is the screen itself (efifb), which shows it as it is. 0, or -1; what was
 * done in msg. */
static int fb_draw(enum picture_mode mode, const char *prefix, const char *card, const char *text, int black, int pan,
		   char *msg, size_t len)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct picture p = { 0 };
	char what[256] = "black", err[600], name[64];
	uint8_t *map, *base;
	uint32_t alpha = 0;
	size_t need;
	int fd, y, x;

	fb_name(name, sizeof(name));
	fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		snprintf(msg, len, "no fb0: %s", strerror(errno));
		return -1;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
		snprintf(msg, len, "no fb0 (%s): %s", name, strerror(errno));
		close(fd);
		return -1;
	}
	if (var.bits_per_pixel != 32 || var.red.offset != 16 || var.red.length != 8 || var.green.offset != 8 ||
	    var.green.length != 8 || var.blue.offset != 0 || var.blue.length != 8 || var.xres < 1 || var.yres < 1 ||
	    var.xres > PICTURE_MAX || var.yres > PICTURE_MAX) {
		snprintf(msg, len, "no fb0 (%s): %ux%u at %u bits is not a frame buffer of 32-bit RGB", name, var.xres,
			 var.yres, var.bits_per_pixel);
		close(fd);
		return -1;
	}
	if (var.transp.length == 8 && var.transp.offset == 24)
		alpha = 0xffu << 24;
	need = ((size_t)var.yoffset + var.yres) * fix.line_length;
	if (fix.line_length < var.xres * 4 || need > fix.smem_len || (size_t)var.xoffset * 4 >= fix.line_length) {
		snprintf(msg, len, "no fb0 (%s): its memory does not hold %ux%u", name, var.xres, var.yres);
		close(fd);
		return -1;
	}
	if (!black && picture_make(prefix, card, text, (int)var.yres, &p, what, sizeof(what), err, sizeof(err))) {
		snprintf(msg, len, "nothing drawn on fb0 (%s): %s", name, err);
		close(fd);
		return -1;
	}
	map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		snprintf(msg, len, "nothing drawn on fb0 (%s): %s", name, strerror(errno));
		picture_free(&p);
		close(fd);
		return -1;
	}
	base = map + (size_t)var.yoffset * fix.line_length + (size_t)var.xoffset * 4;
	if (black) {
		/* (all of it: a display larger than the visible part shows more of it from its upper left corner) */
		for (y = 0; y < (int)(fix.smem_len / fix.line_length); y++)
			for (x = 0; x < (int)(fix.line_length / 4); x++)
				((uint32_t *)(map + (size_t)y * fix.line_length))[x] = alpha;
	} else {
		picture_draw(&p, mode, base, (int)var.xres, (int)var.yres, fix.line_length, alpha);
	}
	munmap(map, fix.smem_len);
	picture_free(&p);
	if (!pan)
		snprintf(msg, len, "fb0 (%s, %ux%u): %s", name, var.xres, var.yres, what);
	else if (ioctl(fd, FBIOPAN_DISPLAY, &var))
		snprintf(msg, len, "fb0 (%s, %ux%u): %s; it was not panned: %s", name, var.xres, var.yres, what,
			 strerror(errno));
	else
		snprintf(msg, len, "fb0 (%s, %ux%u): %s; it is shown", name, var.xres, var.yres, what);
	close(fd);
	return 0;
}

/* ---------------------------------------------------------------- the displays of a DRM device */

static int object_prop(int fd, uint32_t obj, uint32_t type, const char *name, uint32_t *id, uint64_t *value)
{
	drmModeObjectPropertiesPtr p = drmModeObjectGetProperties(fd, obj, type);
	int found = -1;
	uint32_t i;

	if (!p)
		return -1;
	for (i = 0; i < p->count_props && found; i++) {
		drmModePropertyPtr q = drmModeGetProperty(fd, p->props[i]);

		if (!q)
			continue;
		if (!strcmp(q->name, name)) {
			if (id)
				*id = q->prop_id;
			if (value)
				*value = p->prop_values[i];
			found = 0;
		}
		drmModeFreeProperty(q);
	}
	drmModeFreeObjectProperties(p);
	return found;
}

static int plane_props(int fd, uint32_t plane, uint32_t *ids, uint64_t *values)
{
	int k;

	for (k = 0; k < P_N; k++)
		if (object_prop(fd, plane, DRM_MODE_OBJECT_PLANE, plane_prop_names[k], &ids[k], &values[k]))
			return -1;
	return 0;
}

/* the connector that the CRTC drives, as the kernel names it (HDMI-A-1), without asking the display again */
static void output_name(int fd, drmModeResPtr res, uint32_t crtc, char *name, size_t len)
{
	int i;

	snprintf(name, len, "CRTC %u", crtc);
	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr c = drmModeGetConnectorCurrent(fd, res->connectors[i]);
		drmModeEncoderPtr e;

		if (!c)
			continue;
		e = c->encoder_id ? drmModeGetEncoder(fd, c->encoder_id) : NULL;
		if (e && e->crtc_id == crtc) {
			const char *type = drmModeGetConnectorTypeName(c->connector_type);

			snprintf(name, len, "%s-%u", type ? type : "Unknown", c->connector_type_id);
		}
		drmModeFreeEncoder(e);
		drmModeFreeConnector(c);
	}
}

static int takes_xrgb(int fd, uint32_t plane)
{
	drmModePlanePtr p = drmModeGetPlane(fd, plane);
	uint32_t i;
	int ok = 0;

	if (!p)
		return 0;
	for (i = 0; i < p->count_formats; i++)
		if (p->formats[i] == DRM_FORMAT_XRGB8888)
			ok = 1;
	drmModeFreePlane(p);
	return ok;
}

/* The displays of a device that are on: an active CRTC with a mode, and its primary plane, which takes XRGB8888. */
static int outputs_of(int fd, struct output *o, int max)
{
	drmModeResPtr res = drmModeGetResources(fd);
	drmModePlaneResPtr pres = drmModeGetPlaneResources(fd);
	int n = 0, c;
	uint32_t i;

	if (!res || !pres) {
		drmModeFreeResources(res);
		drmModeFreePlaneResources(pres);
		return 0;
	}
	for (c = 0; c < res->count_crtcs && n < max; c++) {
		struct output *x = &o[n];
		uint64_t active = 0;
		drmModeCrtcPtr cr;

		if (object_prop(fd, res->crtcs[c], DRM_MODE_OBJECT_CRTC, "ACTIVE", NULL, &active) || !active)
			continue;
		cr = drmModeGetCrtc(fd, res->crtcs[c]);
		if (!cr)
			continue;
		memset(x, 0, sizeof(*x));
		x->crtc = cr->crtc_id;
		x->w = cr->mode_valid ? cr->mode.hdisplay : 0;
		x->h = cr->mode_valid ? cr->mode.vdisplay : 0;
		drmModeFreeCrtc(cr);
		if (x->w < 1 || x->h < 1 || x->w > PICTURE_MAX || x->h > PICTURE_MAX)
			continue;
		for (i = 0; i < pres->count_planes && !x->plane; i++) {
			uint64_t type = 0, on = 0;

			if (object_prop(fd, pres->planes[i], DRM_MODE_OBJECT_PLANE, "type", NULL, &type) ||
			    type != DRM_PLANE_TYPE_PRIMARY)
				continue;
			if (object_prop(fd, pres->planes[i], DRM_MODE_OBJECT_PLANE, "CRTC_ID", NULL, &on) ||
			    on != x->crtc)
				continue;
			if (takes_xrgb(fd, pres->planes[i]) && !plane_props(fd, pres->planes[i], x->prop, x->saved))
				x->plane = pres->planes[i];
		}
		if (!x->plane)
			continue;
		output_name(fd, res, x->crtc, x->name, sizeof(x->name));
		n++;
	}
	drmModeFreeResources(res);
	drmModeFreePlaneResources(pres);
	return n;
}

static void buffer_free(int fd, struct buffer *b)
{
	if (b->fb)
		drmModeRmFB(fd, b->fb);
	if (b->map)
		munmap(b->map, b->size);
	if (b->handle) {
		struct drm_mode_destroy_dumb d = { .handle = b->handle };

		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	}
	memset(b, 0, sizeof(*b));
}

static int buffer_make(int fd, int w, int h, struct buffer *b, uint32_t *pitch)
{
	struct drm_mode_create_dumb cr = { .width = (uint32_t)w, .height = (uint32_t)h, .bpp = 32 };
	struct drm_mode_map_dumb mp = { 0 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };

	memset(b, 0, sizeof(*b));
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cr))
		return -1;
	b->handle = cr.handle;
	b->size = cr.size;
	*pitch = cr.pitch;
	mp.handle = cr.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mp))
		goto fail;
	b->map = mmap(NULL, cr.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)mp.offset);
	if (b->map == MAP_FAILED) {
		b->map = NULL;
		goto fail;
	}
	handles[0] = cr.handle;
	pitches[0] = cr.pitch;
	if (drmModeAddFB2(fd, (uint32_t)w, (uint32_t)h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->fb, 0))
		goto fail;
	return 0;
fail:
	buffer_free(fd, b);
	return -1;
}

static int held_count(void)
{
	int i, n = 0;

	for (i = 0; i < ncards; i++)
		n += cards[i].n;
	return n;
}

/* (not in the process that holds the pictures of the init: the flag is the service's) */
static int no_flag;

static void held_flag(void)
{
	if (no_flag)
		return;
	if (held_count())
		touch(HELD_FLAG);
	else
		unlink(HELD_FLAG);
}

static struct card *card_find(const char *node)
{
	int i;

	for (i = 0; i < ncards; i++)
		if (!strcmp(cards[i].node, node))
			return &cards[i];
	return NULL;
}

/* closes a device that this program holds: its last close gives the displays back to the console (drm_lastclose()) */
static void card_drop(struct card *c)
{
	int i;

	for (i = 0; i < c->n; i++)
		buffer_free(c->fd, &c->out[i].buf);
	close(c->fd);
	*c = cards[--ncards];
	held_flag();
}

static int card_caps(int fd)
{
	return drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) || drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
}

/* the DRM master of the device: what a device opened when none had it is; else asked for during 2 s, while another
 * program lets go of it (never taken from one: the kernel gives it only where no one has it) */
static int card_master(struct card *c)
{
	int i;

	for (i = 0; !c->master && i < 20; i++) {
		if (drmIsMaster(c->fd) || drmSetMaster(c->fd) == 0)
			c->master = 1;
		else
			usleep(100000);
	}
	return c->master ? 0 : -1;
}

static void plane_set(drmModeAtomicReqPtr req, const struct output *o, const uint64_t *v)
{
	int k;

	for (k = 0; k < P_N; k++)
		drmModeAtomicAddProperty(req, o->plane, o->prop[k], v[k]);
}

/* every plane of the device as it was before this program, without a change of mode; 0 where it was done */
static int card_restore(struct card *c)
{
	drmModeAtomicReqPtr req;
	int i, r;

	if (!c->master && drmSetMaster(c->fd) == 0)
		c->master = 1;
	if (!c->master)
		return -1;
	req = drmModeAtomicAlloc();
	if (!req)
		return -1;
	for (i = 0; i < c->n; i++) {
		uint64_t v[P_N];

		memcpy(v, c->out[i].saved, sizeof(v));
		if (!v[P_FB_ID])
			v[P_CRTC_ID] = 0;
		plane_set(req, &c->out[i], v);
	}
	r = drmModeAtomicCommit(c->fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	if (!r)
		r = drmModeAtomicCommit(c->fd, req, 0, NULL);
	drmModeAtomicFree(req);
	return r;
}

/* "stop": every display back to what it showed before, and the devices closed */
static void stop_all(const char *why)
{
	while (ncards) {
		struct card *c = &cards[0];
		int n = c->n;

		if (card_restore(c))
			logline("%s: %s: the planes could not be put back as they were (%s): the kernel takes the pictures "
				"away with the device", why, c->node, strerror(errno));
		else
			logline("%s: %s: %d display(s) given back to the console", why, c->node, n);
		card_drop(c);
	}
	released = 0;
	held_flag();
}

/* after "release": a picture that Kodi (or anything else) has put its own in place of is freed; a device that shows
 * none of them any more is closed */
static void check_released(void)
{
	int i, j;

	for (i = 0; i < ncards; i++) {
		struct card *c = &cards[i];

		for (j = 0; j < c->n; j++) {
			uint64_t fb = 0;

			if (!object_prop(c->fd, c->out[j].plane, DRM_MODE_OBJECT_PLANE, "FB_ID", NULL, &fb) &&
			    fb == c->out[j].buf.fb)
				continue;
			logline("%s %s: its picture is replaced: freed", c->node, c->out[j].name);
			buffer_free(c->fd, &c->out[j].buf);
			c->out[j] = c->out[--c->n];
			j--;
		}
		if (!c->n) {
			logline("%s: shows nothing of this program any more: closed", c->node);
			card_drop(c);
			i--;
		}
	}
}

/* ---------------------------------------------------------------- show */

struct found {
	char node[24];
	int fd;
	int mine; /* opened for this show */
	int n;
	struct output out[MAX_OUT];
};

struct made {
	int sh;
	int ok;
	struct picture p;
	char what[256];
	char err[600];
};

static const struct picture *made_for(struct made *m, int *nm, int sh, const char *prefix, const char *card,
				      const char *text, const char **what)
{
	int i;

	for (i = 0; i < *nm; i++)
		if (m[i].sh == sh)
			break;
	if (i == *nm) {
		if (*nm >= MAX_OUT) {
			*what = "too many sizes of display";
			return NULL;
		}
		m[i].sh = sh;
		m[i].ok = !picture_make(prefix, card, text, sh, &m[i].p, m[i].what, sizeof(m[i].what), m[i].err,
					sizeof(m[i].err));
		(*nm)++;
	}
	*what = m[i].ok ? m[i].what : m[i].err;
	return m[i].ok ? &m[i].p : NULL;
}

/* does fb0 belong to the device at node? (the same device in sysfs) */
static int fb0_on(const char *node)
{
	char a[PATH_MAX], b[PATH_MAX], path[64];

	snprintf(path, sizeof(path), "/sys/class/drm/%s/device", strrchr(node, '/') + 1);
	return realpath("/sys/class/graphics/fb0/device", a) && realpath(path, b) && !strcmp(a, b);
}

static int fb0_visible(int *w, int *h)
{
	struct fb_var_screeninfo var;
	char name[64];
	size_t l;
	int fd, r;

	fb_name(name, sizeof(name));
	l = strlen(name);
	if (l < 5 || strcmp(name + l - 5, "drmfb"))
		return -1;
	fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	r = ioctl(fd, FBIOGET_VSCREENINFO, &var);
	close(fd);
	if (r)
		return -1;
	*w = (int)var.xres;
	*h = (int)var.yres;
	return 0;
}

/* The pictures that the init showed (init_mode()) are held by a process of its own until something puts its own in their
 * place: INIT_FILE names it and, for each display, the plane as it was before the init drew on it. A display that
 * shows such a picture now keeps what it showed before that for "stop", not the picture of the init. */
static void init_saved(const char *node, struct output *o, int n)
{
	FILE *f = fopen(INIT_FILE, "r");
	char line[512], dev[32];
	unsigned crtc, plane, fb;
	uint64_t v[P_N];
	int i, k;

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%31s %u %u %u %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", dev, &crtc, &plane, &fb, &v[0], &v[1],
			   &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) != 14 || strcmp(dev, node))
			continue;
		for (i = 0; i < n; i++)
			if (o[i].crtc == crtc && o[i].plane == plane && o[i].saved[P_FB_ID] == fb) {
				for (k = 0; k < P_N; k++)
					o[i].saved[k] = v[k];
				logline("%s %s: shows the picture of the init, which it takes over", node, o[i].name);
			}
	}
	fclose(f);
}

static int show(enum picture_mode mode, int boot, const char *prefix, const char *card, const char *text)
{
	static struct found f[MAX_CARDS];
	static struct made m[MAX_OUT];
	int nf = 0, nm = 0, i, j, total = 0, fbw = 0, fbh = 0, fbpath, failed = 0, code = 0;
	char node[24], msg[1024], desc[REPLY_LEN] = "";
	size_t used;

	if (boot && access(KODI_FLAG, F_OK) == 0) {
		say("no Kodi was started: the picture of the start is not drawn again");
		return 1;
	}
	/* Kodi has gone (it stopped before it had taken every display): the display that it had is off now, and comes on
	 * again only with the console (drm_lastclose()), so everything goes back to it first */
	if (released && held_count())
		stop_all("a picture is asked for after Kodi");

	/* the displays that are on, on every device */
	for (i = 0; i < 16 && nf < MAX_CARDS; i++) {
		struct card *held;
		struct found *x = &f[nf];

		snprintf(node, sizeof(node), "/dev/dri/card%d", i);
		if (access(node, F_OK))
			continue;
		memset(x, 0, sizeof(*x));
		snprintf(x->node, sizeof(x->node), "%s", node);
		held = card_find(node);
		x->fd = held ? held->fd : open(node, O_RDWR | O_CLOEXEC);
		x->mine = !held;
		if (x->fd < 0)
			continue;
		if (card_caps(x->fd)) {
			logline("%s: no atomic mode setting: its displays are left as they are", node);
			if (x->mine)
				close(x->fd);
			continue;
		}
		x->n = outputs_of(x->fd, x->out, MAX_OUT);
		if (!held)
			init_saved(node, x->out, x->n);
		total += x->n;
		nf++;
	}

	if (!total) {
		say("none no display is on");
		code = 3;
		goto out;
	}

	/* every display that is on shows the console's frame buffer at its full size: drawn there, nothing held */
	fbpath = !held_count() && !fb0_visible(&fbw, &fbh);
	for (i = 0; i < nf && fbpath; i++)
		for (j = 0; j < f[i].n; j++)
			if (!fb0_on(f[i].node) || f[i].out[j].w != fbw || f[i].out[j].h != fbh)
				fbpath = 0;
	if (fbpath) {
		for (i = 0; i < nf; i++)
			if (f[i].mine)
				close(f[i].fd);
		nf = 0;
		if (fb_draw(mode, prefix, card, text, 0, 1, msg, sizeof(msg))) {
			say("no %s", msg);
			return 1;
		}
		say("ok fb %d display(s) of %dx%d show the console's frame buffer: %s", total, fbw, fbh, msg);
		return 0;
	}

	/* a picture of its own size on each display, device by device */
	for (i = 0; i < nf && !failed; i++) {
		struct found *x = &f[i];
		struct buffer nb[MAX_OUT];
		drmModeAtomicReqPtr req;
		struct card *c;
		int r;

		if (!x->n)
			continue;
		c = card_find(x->node);
		if (!c) {
			if (ncards >= MAX_CARDS) {
				failed = 1;
				say("no too many devices");
				break;
			}
			c = &cards[ncards++];
			memset(c, 0, sizeof(*c));
			memcpy(c->node, x->node, sizeof(c->node));
			c->fd = x->fd;
			x->mine = 0;
		}
		if (card_master(c)) {
			failed = 1;
			say("no %s: another program shows on its displays (it has their DRM master)", x->node);
			break;
		}
		memset(nb, 0, sizeof(nb));
		req = drmModeAtomicAlloc();
		for (j = 0; j < x->n && req; j++) {
			struct output *o = &x->out[j];
			const struct picture *p;
			const char *what;
			uint32_t pitch;
			uint64_t v[P_N];

			p = made_for(m, &nm, o->h, prefix, card, text, &what);
			if (!p) {
				failed = 1;
				say("no %s %s %dx%d: %s", x->node, o->name, o->w, o->h, what);
				break;
			}
			if (buffer_make(c->fd, o->w, o->h, &nb[j], &pitch)) {
				failed = 1;
				say("no %s %s: no buffer of %dx%d: %s", x->node, o->name, o->w, o->h, strerror(errno));
				break;
			}
			picture_draw(p, mode, nb[j].map, o->w, o->h, pitch, 0);
			used = strlen(desc);
			snprintf(desc + used, sizeof(desc) - used, "%s%s %dx%d: %s", used ? ", " : "", o->name, o->w,
				 o->h, what);
			v[P_FB_ID] = nb[j].fb;
			v[P_CRTC_ID] = o->crtc;
			v[P_SRC_X] = 0;
			v[P_SRC_Y] = 0;
			v[P_SRC_W] = (uint64_t)o->w << 16;
			v[P_SRC_H] = (uint64_t)o->h << 16;
			v[P_CRTC_X] = 0;
			v[P_CRTC_Y] = 0;
			v[P_CRTC_W] = (uint64_t)o->w;
			v[P_CRTC_H] = (uint64_t)o->h;
			plane_set(req, o, v);
		}
		if (!req) {
			failed = 1;
			say("no memory");
		}
		/* no ALLOW_MODESET: a commit that would change a mode is refused, and nothing changes */
		r = failed ? -1 : drmModeAtomicCommit(c->fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
		if (!r)
			r = drmModeAtomicCommit(c->fd, req, 0, NULL);
		drmModeAtomicFree(req);
		if (r) {
			if (!failed)
				say("no %s: the driver did not take the pictures without a change of mode: %s", x->node,
				    strerror(errno));
			failed = 1;
			for (j = 0; j < x->n; j++)
				buffer_free(c->fd, &nb[j]);
			if (!c->n)
				card_drop(c);
			break;
		}
		/* shown: the pictures of before are off the screen; each display keeps what it showed before this
		 * program first drew on it, for "stop" */
		for (j = 0; j < c->n; j++)
			buffer_free(c->fd, &c->out[j].buf);
		for (j = 0; j < x->n; j++) {
			int k;

			for (k = 0; k < c->n; k++)
				if (c->out[k].crtc == x->out[j].crtc && c->out[k].plane == x->out[j].plane)
					memcpy(x->out[j].saved, c->out[k].saved, sizeof(x->out[j].saved));
			x->out[j].buf = nb[j];
		}
		memcpy(c->out, x->out, sizeof(c->out));
		c->n = x->n;
	}
	released = 0;
	held_flag();
	if (failed) {
		/* nothing half shown: what is held goes back to the console, and the caller draws where it can */
		stop_all("a picture could not be shown everywhere");
		code = 1;
		goto out;
	}
	say("ok kms %d display(s), each its own picture: %s", held_count(), desc);
	/* the console's frame buffer, which the displays show again when the last program lets go of the device (Kodi
	 * at its end), is made black: a picture in it would sit small in a corner of the larger displays. Not shown now:
	 * the displays show the pictures of this program */
	for (i = 0; i < ncards; i++)
		if (fb0_on(cards[i].node) && !fb_draw(PICTURE_FIT, NULL, NULL, NULL, 1, 0, msg, sizeof(msg)))
			logline("%s, under the pictures", msg);
out:
	for (i = 0; i < nf; i++)
		if (f[i].mine)
			close(f[i].fd);
	for (i = 0; i < nm; i++)
		picture_free(&m[i].p);
	return code;
}

/* ---------------------------------------------------------------- the service */

static int release(void)
{
	int i, n = held_count();

	touch(KODI_FLAG);
	for (i = 0; i < ncards; i++)
		if (cards[i].master) {
			drmDropMaster(cards[i].fd);
			cards[i].master = 0;
		}
	released = n > 0;
	say("ok released: %d picture(s) stay until Kodi shows its own in their place", n);
	return 0;
}

static int valid_path(const char *s)
{
	return s && (!strncmp(s, "/usr/", 5) || !strncmp(s, "/flash/", 7)) && !strstr(s, "..") && strlen(s) < 400;
}

/* a line of text for a card: at most 160 characters, printable ASCII as it is, any other byte as "?" */
static char *text_of(char *s)
{
	char *c;

	if (!s)
		return NULL;
	if (strlen(s) > 160)
		s[160] = 0;
	for (c = s; *c; c++)
		if ((unsigned char)*c < 32 || (unsigned char)*c > 126)
			*c = '?';
	return s;
}

static int dispatch(char *line)
{
	char *arg[8], *save = NULL, *t, *text = strstr(line, " text=");
	int n = 0;

	/* (the text is the rest of the line, spaces and all) */
	if (text) {
		*text = 0;
		text += 6;
		text[strcspn(text, "\r\n")] = 0;
	}
	for (t = strtok_r(line, " \t\r\n", &save); t && n < 8; t = strtok_r(NULL, " \t\r\n", &save))
		arg[n++] = t;
	if (n >= 4 && n <= 5 && !strcmp(arg[0], "show") && (!strcmp(arg[1], "fit") || !strcmp(arg[1], "stretch")) &&
	    (!strcmp(arg[2], "boot") || !strcmp(arg[2], "kodi")) && valid_path(arg[3]) && (n == 4 || valid_path(arg[4])))
		return show(!strcmp(arg[1], "fit") ? PICTURE_FIT : PICTURE_STRETCH, !strcmp(arg[2], "boot"), arg[3],
			    n == 5 ? arg[4] : NULL, n == 5 ? text_of(text) : NULL);
	if (n == 1 && !strcmp(arg[0], "release"))
		return release();
	if (n == 1 && !strcmp(arg[0], "stop")) {
		int held = held_count();

		stop_all("asked to stop");
		say("ok stopped: %d display(s) given back to the console", held);
		return 0;
	}
	say("no not a command");
	return 1;
}

static void handle(int cfd)
{
	struct timeval tv = { .tv_sec = 5 };
	char line[LINE_MAX_LEN];
	size_t used = 0;
	ssize_t r;
	int code;

	setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	while (used < sizeof(line) - 1 && (r = read(cfd, line + used, sizeof(line) - 1 - used)) > 0) {
		used += (size_t)r;
		if (memchr(line, '\n', used))
			break;
	}
	line[used] = 0;
	reply[0] = 0;
	{
		char asked[LINE_MAX_LEN];

		snprintf(asked, sizeof(asked), "%s", line);
		asked[strcspn(asked, "\n")] = 0;
		code = dispatch(line);
		logline("%s -> %s", asked, reply);
	}
	(void)code;
	say("\n");
	if (write(cfd, reply, strlen(reply)) < 0)
		logline("the answer was not sent: %s", strerror(errno));
	close(cfd);
}

static int listen_socket(void)
{
	const char *pid = getenv("LISTEN_PID"), *fds = getenv("LISTEN_FDS");
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	int s;

	if (pid && fds && atol(pid) == (long)getpid() && atoi(fds) >= 1) {
		fcntl(3, F_SETFD, FD_CLOEXEC);
		return 3;
	}
	/* (started by hand: its own socket) */
	s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s < 0)
		return -1;
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", SOCK_PATH);
	unlink(SOCK_PATH);
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) || chmod(SOCK_PATH, 0600) || listen(s, 4)) {
		close(s);
		return -1;
	}
	return s;
}

static int serve(void)
{
	long long idle_until = now_ms() + 10000;
	sigset_t set;
	int lfd, sfd;

	lfd = listen_socket();
	if (lfd < 0) {
		logline("no socket: %s", strerror(errno));
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_BLOCK, &set, NULL);
	sfd = signalfd(-1, &set, SFD_CLOEXEC);

	for (;;) {
		struct pollfd p[2] = { { .fd = lfd, .events = POLLIN }, { .fd = sfd, .events = POLLIN } };
		int holding = held_count() > 0, timeout;

		if (holding)
			timeout = released ? 200 : -1;
		else
			timeout = idle_until > now_ms() ? (int)(idle_until - now_ms()) : 0;
		if (poll(p, sfd >= 0 ? 2 : 1, timeout) < 0 && errno != EINTR)
			break;
		if (sfd >= 0 && (p[1].revents & POLLIN)) {
			struct signalfd_siginfo si;

			if (read(sfd, &si, sizeof(si)) > 0)
				logline("signal %u", si.ssi_signo);
			stop_all("the service ends");
			return 0;
		}
		if (p[0].revents & POLLIN) {
			int c = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);

			if (c >= 0)
				handle(c);
			/* (a command that leaves nothing held: the service ends a moment later, and the socket starts it
			 * again when it is asked for) */
			idle_until = now_ms() + 1000;
		}
		if (released && held_count())
			check_released();
		if (!held_count() && now_ms() >= idle_until)
			break;
	}
	stop_all("the service ends");
	return 0;
}

/* ---------------------------------------------------------------- the commands */

/* the process that holds the pictures of the init (init_mode()) is asked to give the displays back and to end */
static void end_init_holder(void)
{
	char line[64], comm[32] = "", path[64];
	FILE *f = fopen(INIT_FILE, "r");
	long pid = 0;

	if (!f)
		return;
	if (fgets(line, sizeof(line), f))
		sscanf(line, "pid %ld", &pid);
	fclose(f);
	snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
	f = pid > 1 ? fopen(path, "r") : NULL;
	if (f) {
		if (fgets(comm, sizeof(comm), f))
			comm[strcspn(comm, "\n")] = 0;
		fclose(f);
	}
	if (!strcmp(comm, "hades-pic-init") && kill((pid_t)pid, SIGTERM) == 0)
		printf("the pictures of the init (process %ld) go back to the console; ", pid);
}

static int client(int argc, char **argv)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	struct timeval tv = { .tv_sec = 30 };
	char line[LINE_MAX_LEN] = "", answer[REPLY_LEN];
	size_t used = 0;
	ssize_t r;
	int i, s;

	if (!strcmp(argv[1], "release")) {
		/* (also where nothing is held: the picture of the start is not drawn once Kodi was started) */
		touch(KODI_FLAG);
		if (access(HELD_FLAG, F_OK)) {
			puts("ok nothing is held: the displays are the console's");
			return 0;
		}
	}
	if (!strcmp(argv[1], "stop")) {
		end_init_holder();
		if (access(HELD_FLAG, F_OK)) {
			puts("ok nothing is held");
			return 0;
		}
	}
	for (i = 1; i < argc; i++) {
		if (strlen(line) + strlen(argv[i]) + 2 >= sizeof(line)) {
			puts("no the command is too long");
			return 1;
		}
		strcat(line, argv[i]);
		strcat(line, i + 1 < argc ? " " : "\n");
	}
	s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", SOCK_PATH);
	if (s < 0 || connect(s, (struct sockaddr *)&a, sizeof(a))) {
		printf("no the service did not answer: %s\n", strerror(errno));
		if (s >= 0)
			close(s);
		return 2;
	}
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (write(s, line, strlen(line)) < 0) {
		printf("no the service did not answer: %s\n", strerror(errno));
		close(s);
		return 2;
	}
	shutdown(s, SHUT_WR);
	while (used < sizeof(answer) - 1 && (r = read(s, answer + used, sizeof(answer) - 1 - used)) > 0)
		used += (size_t)r;
	close(s);
	answer[used] = 0;
	if (!used) {
		puts("no the service did not answer");
		return 2;
	}
	fputs(answer, stdout);
	if (!strncmp(answer, "ok", 2))
		return 0;
	return !strncmp(answer, "none", 4) ? 3 : 1;
}

/* How many displays are connected, on every device (as kodi-display-notice counts them). */
static int connected(void)
{
	DIR *d = opendir("/sys/class/drm");
	struct dirent *e;
	char path[300], st[32];
	int n = 0;
	FILE *f;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "card", 4) || !strchr(e->d_name, '-'))
			continue;
		snprintf(path, sizeof(path), "/sys/class/drm/%s/status", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(st, sizeof(st), f) && !strncmp(st, "connected", 9))
			n++;
		fclose(f);
	}
	closedir(d);
	return n;
}

static volatile sig_atomic_t init_end;

static void init_end_signal(int sig)
{
	(void)sig;
	init_end = 1;
}

/* The init draws the picture of the start (packages/sysutils/busybox/scripts/init, load_splash). As "show" does: into
 * the console's frame buffer where every display shows it at its full size, else each display its own picture (and,
 * where no display is on yet, the console lights them black first). Pictures of its own on the displays are then held
 * by a process of this program that the init leaves behind, without the DRM master, so that the console takes the
 * screen as soon as it has text to show and the service takes them over without a change (init_saved()): it ends as
 * soon as none of its pictures is on a display any more, or when "stop" asks it to (Kodi's end, or a shutdown) after
 * it has put back what each display showed before; or, after 120 s, as soon as it can put that back. */
static int init_mode(enum picture_mode mode, const char *prefix)
{
	char msg[1024];
	int r, i, j, kmsg;
	pid_t pid;
	FILE *f;

	long long t0 = now_ms();

	no_flag = 1;
	/* (its lines into the kernel log: what the init reads of it is the answer alone) */
	kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (kmsg >= 0)
		dup2(kmsg, 2);
	r = show(mode, 0, prefix, NULL, NULL);
	if (r == 3) {
		/* (no display is on: the console lit them in the meantime, drm_lastclose()) */
		if (connected() >= 2) {
			fb_draw(mode, NULL, NULL, NULL, 1, 1, msg, sizeof(msg));
			logline("init: no display was on: lit black after %lld ms (%s)", now_ms() - t0, msg);
			reply[0] = 0;
			r = show(mode, 0, prefix, NULL, NULL);
		} else {
			reply[0] = 0;
			r = fb_draw(mode, prefix, NULL, NULL, 0, 1, msg, sizeof(msg)) ? 1 : 0;
			say("%s %s", r ? "no" : "ok fb", msg);
		}
	}
	logline("init: %s after %lld ms", r ? "nothing drawn" : "drawn", now_ms() - t0);
	if (r || !held_count()) {
		puts(reply);
		return r;
	}
	/* held: without the master from now on */
	for (i = 0; i < ncards; i++)
		if (cards[i].master) {
			drmDropMaster(cards[i].fd);
			cards[i].master = 0;
		}
	released = 1;
	fflush(stdout);
	pid = fork();
	if (pid < 0) {
		stop_all("no process to hold the pictures");
		puts("no no process to hold the pictures");
		return 1;
	}
	if (pid > 0) {
		f = fopen(INIT_FILE, "w");
		if (f) {
			fprintf(f, "pid %ld\n", (long)pid);
			for (i = 0; i < ncards; i++)
				for (j = 0; j < cards[i].n; j++) {
					const struct output *o = &cards[i].out[j];

					fprintf(f, "%s %u %u %u %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", cards[i].node, o->crtc,
						o->plane, o->buf.fb, o->saved[0], o->saved[1], o->saved[2], o->saved[3],
						o->saved[4], o->saved[5], o->saved[6], o->saved[7], o->saved[8], o->saved[9]);
				}
			fclose(f);
		}
		printf("%s; held by process %ld\n", reply, (long)pid);
		return 0;
	}
	/* the holder: nothing on the console (text there would take the screen), its lines into the kernel log */
	r = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (r >= 0) {
		dup2(r, 0);
		dup2(r, 1);
	}
	if (kmsg < 0)
		dup2(r, 2);
	setsid();
	prctl(PR_SET_NAME, "hades-pic-init");
	if (chdir("/"))
		_exit(0);
	signal(SIGTERM, init_end_signal);
	signal(SIGINT, init_end_signal);
	signal(SIGHUP, SIG_IGN);
	{
		long long until = now_ms() + INIT_WAIT_MS;
		struct timespec t = { 0, 200000000 };
		int n = held_count();

		logline("init: %d picture(s) of the start held until something shows its own in their place", n);
		while (held_count() && !init_end) {
			nanosleep(&t, NULL);
			check_released();
			if (now_ms() >= until && held_count()) {
				/* each device that can be given back is, and closed; the master of one that cannot is let go
				 * of again at once (another program has a display there: wait on) */
				for (i = 0; i < ncards;) {
					if (card_restore(&cards[i]) == 0) {
						logline("init: %s: nothing took its pictures in %d s: its displays go back to the "
							"console", cards[i].node, INIT_WAIT_MS / 1000);
						card_drop(&cards[i]);
						continue;
					}
					if (cards[i].master) {
						drmDropMaster(cards[i].fd);
						cards[i].master = 0;
					}
					i++;
				}
				until = now_ms() + 5000;
			}
		}
		if (init_end && held_count())
			stop_all("init: asked to end");
		else
			while (ncards)
				card_drop(&cards[0]);
		/* (after the start of systemd, its /run is the one of the system: /proc/1/root) */
		unlink(INIT_FILE);
		unlink("/proc/1/root" INIT_FILE);
		logline("init: ends");
	}
	_exit(0);
}

static int usage(void)
{
	fputs("usage: hades-picture serve | show <fit|stretch> <boot|kodi> <picture> [<card> [text=<line>]] | release |\n"
	      "       stop | fb <fit|stretch> <picture> [<card> [text=<line>]] | fb black | init <fit|stretch> <picture>\n",
	      stderr);
	return 2;
}

int main(int argc, char **argv)
{
	char msg[1024];

	if (argc < 2)
		return usage();
	if (!strcmp(argv[1], "serve") && argc == 2)
		return serve();
	if (!strcmp(argv[1], "init") && argc == 4 && (!strcmp(argv[2], "fit") || !strcmp(argv[2], "stretch")))
		return init_mode(!strcmp(argv[2], "fit") ? PICTURE_FIT : PICTURE_STRETCH, argv[3]);
	if (!strcmp(argv[1], "fb") && argc == 3 && !strcmp(argv[2], "black")) {
		int r = fb_draw(PICTURE_FIT, NULL, NULL, NULL, 1, 1, msg, sizeof(msg));

		printf("%s %s\n", r ? "no" : "ok", msg);
		return r ? 1 : 0;
	}
	if (!strcmp(argv[1], "fb") && argc >= 4 && argc <= 6 &&
	    (!strcmp(argv[2], "fit") || !strcmp(argv[2], "stretch"))) {
		char *text = argc == 6 && !strncmp(argv[5], "text=", 5) ? text_of(argv[5] + 5) : NULL;
		int r;

		if (argc == 6 && !text)
			return usage();
		r = fb_draw(!strcmp(argv[2], "fit") ? PICTURE_FIT : PICTURE_STRETCH, argv[3], argc >= 5 ? argv[4] : NULL,
			    text, 0, 1, msg, sizeof(msg));

		printf("%s %s\n", r ? "no" : "ok", msg);
		return r ? 1 : 0;
	}
	if ((!strcmp(argv[1], "show") && argc >= 5 && argc <= 7 && (argc < 7 || !strncmp(argv[6], "text=", 5))) ||
	    ((!strcmp(argv[1], "release") || !strcmp(argv[1], "stop")) && argc == 2))
		return client(argc, argv);
	return usage();
}
