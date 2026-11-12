// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2026-present Hades */
/*
 * hades-output-check: whether the graphics card passes the bytes of the Dolby Vision transport through its
 * display pipe unchanged, measured with the card's own CRC of its output (output-check.h says how). Run only on
 * request (System > Submit Log, "Check the Dolby Vision output"), by hades-output-check.service before Kodi starts:
 * it needs the display for itself for about ten seconds. It sets the Dolby Vision tunnel
 * (DVBRIDGE_DV_STANDARD_LAB) on the HDMI output that has it, in a mode that the driver accepts for it, shows
 * test pictures of random bytes, reads the CRC that the driver computes of each frame (debugfs
 * crtc-N/crc/data, which a kernel locked down still lets read: its default source "auto" is used then), judges
 * them, writes a report and gives the display back (the console takes it, without the tunnel).
 *
 *   hades-output-check [--probe] [--out FILE]
 *     --probe  only say whether the check can be made here (no change of mode, no need of the display)
 * For the tests, on a virtual display without the tunnel (vkms):
 *   --plain [--mode WxH] [--card /dev/dri/cardN] [--seed N] [--test-gamma identity|invert|curve|trunc1|limited]
 *   [--test-corrupt]; --test-hold SECONDS: hold the display (DRM master) of --card and do nothing else.
 *
 * The report: "result: exact|not-exact|not-judged|unavailable|ready", "reason: ...", "summary: ...", "meaning:
 * ..." and the details of the measure. One line of it also goes to the kernel log.
 */
#define _GNU_SOURCE
#include "output-check.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define TUNNEL_PROP "DVBRIDGE_DV_STANDARD_LAB"
#define REFUSED_PROP "DVBRIDGE_REFUSED"
#define DEFAULT_SEED 0x4861646573ULL
#define BUFFERS 3
/* how long one CRC entry, or one flip, may take (a frame of 23.976 Hz is 42 ms; a driver that computes its CRC in
 * software, as vkms does, can take seconds for one frame): a measure that stops is never judged */
#define WAIT_MS 3000
/* the whole measure */
#define LIMIT_S 90

/* ---- the report ---- */
static char report[16384];
static size_t report_len;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	int n;

	if (report_len >= sizeof(report) - 1)
		return;
	va_start(ap, fmt);
	n = vsnprintf(report + report_len, sizeof(report) - report_len, fmt, ap);
	va_end(ap);
	if (n > 0)
		report_len += (size_t)n < sizeof(report) - report_len ? (size_t)n : sizeof(report) - report_len - 1;
}

static const char *out_path;

/* Write the report: to its file (made whole in a file beside it, then renamed), or to stdout */
static int flush_report(void)
{
	char tmp[4096];
	FILE *f;

	if (!out_path) {
		fwrite(report, 1, report_len, stdout);
		return 0;
	}
	snprintf(tmp, sizeof(tmp), "%s.part", out_path);
	f = fopen(tmp, "w");
	if (!f)
		return -1;
	fwrite(report, 1, report_len, f);
	if (fclose(f) || rename(tmp, out_path)) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

/* One line into the kernel log, so that a later diagnostic has the result too */
static void kernel_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void kernel_log(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	int fd, n;

	n = snprintf(line, sizeof(line), "<6>hades-output-check: ");
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
	va_end(ap);
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	if (write(fd, line, strlen(line)) < 0) {
		/* nothing more to do: the report has it */
	}
	close(fd);
}

static void finish(const char *result, const char *reason, const char *summary, const char *meaning)
{
	char head[2048];
	size_t hl;

	hl = (size_t)snprintf(head, sizeof(head), "result: %s\nreason: %s\nsummary: %s\nmeaning: %s\n", result,
			      reason, summary, meaning);
	if (hl >= sizeof(head))
		hl = sizeof(head) - 1;
	/* the four lines first, the details after them */
	if (report_len + hl < sizeof(report)) {
		memmove(report + hl, report, report_len);
		memcpy(report, head, hl);
		report_len += hl;
	}
	kernel_log("%s (%s): %s\n", result, reason, summary);
}

/* ---- properties of a DRM object ---- */
struct props {
	drmModeObjectPropertiesPtr p;
	drmModePropertyPtr *info;
};

static bool props_get(int fd, uint32_t id, uint32_t type, struct props *out)
{
	uint32_t i;

	memset(out, 0, sizeof(*out));
	out->p = drmModeObjectGetProperties(fd, id, type);
	if (!out->p)
		return false;
	out->info = calloc(out->p->count_props ? out->p->count_props : 1, sizeof(*out->info));
	if (!out->info)
		return false;
	for (i = 0; i < out->p->count_props; i++)
		out->info[i] = drmModeGetProperty(fd, out->p->props[i]);
	return true;
}

static void props_free(struct props *pr)
{
	uint32_t i;

	if (pr->p && pr->info)
		for (i = 0; i < pr->p->count_props; i++)
			drmModeFreeProperty(pr->info[i]);
	free(pr->info);
	drmModeFreeObjectProperties(pr->p);
	memset(pr, 0, sizeof(*pr));
}

/* index of the property named name, or -1 */
static int prop_find(const struct props *pr, const char *name)
{
	uint32_t i;

	if (!pr->p)
		return -1;
	for (i = 0; i < pr->p->count_props; i++)
		if (pr->info[i] && !strcmp(pr->info[i]->name, name))
			return (int)i;
	return -1;
}

static bool prop_value(const struct props *pr, const char *name, uint64_t *value)
{
	int i = prop_find(pr, name);

	if (i < 0)
		return false;
	*value = pr->p->prop_values[i];
	return true;
}

/* the value of the enum entry 'entry' of an enum property */
static bool prop_enum(const struct props *pr, const char *name, const char *entry, uint64_t *value)
{
	int i = prop_find(pr, name), k;

	if (i < 0)
		return false;
	for (k = 0; k < pr->info[i]->count_enums; k++)
		if (!strcmp(pr->info[i]->enums[k].name, entry)) {
			*value = pr->info[i]->enums[k].value;
			return true;
		}
	return false;
}

/* add name = value to the request, if the object has that property */
static bool add(drmModeAtomicReqPtr req, uint32_t obj, const struct props *pr, const char *name, uint64_t value)
{
	int i = prop_find(pr, name);

	if (i < 0)
		return false;
	return drmModeAtomicAddProperty(req, obj, pr->info[i]->prop_id, value) >= 0;
}

/* The names of the bits of a bitmask property that are set, into buf */
static void bitmask_names(const struct props *pr, const char *name, uint64_t v, char *buf, size_t size)
{
	int i = prop_find(pr, name), k;
	size_t len = 0;

	buf[0] = '\0';
	if (i < 0)
		return;
	for (k = 0; k < pr->info[i]->count_enums; k++)
		if (pr->info[i]->enums[k].value < 64 && (v & (1ULL << pr->info[i]->enums[k].value)))
			len += (size_t)snprintf(buf + len, size > len ? size - len : 0, "%s%s", len ? "," : "",
						pr->info[i]->enums[k].name);
}

/* ---- the device, its output and the CRC ---- */
struct output {
	int fd;
	char node[64];
	char driver[64];
	unsigned int minor;
	uint32_t connector_id, crtc_id, plane_id;
	int crtc_index;
	char connector_name[64];
	drmModeModeInfo mode;
	struct props cprops, crtcprops, planeprops;
	uint32_t other_planes[32];
	unsigned int n_other_planes;
	uint32_t clones[16];
	unsigned int n_clones;
};

static const char *connector_name(uint32_t type, uint32_t type_id, char *buf, size_t size)
{
	const char *t = drmModeGetConnectorTypeName(type);

	snprintf(buf, size, "%s-%u", t ? t : "Unknown", type_id);
	return buf;
}

static bool driver_name(int fd, char *buf, size_t size)
{
	drmVersionPtr v = drmGetVersion(fd);

	if (!v)
		return false;
	snprintf(buf, size, "%s", v->name);
	drmFreeVersion(v);
	return true;
}

/* The connected connector of a device that this check is for (with the tunnel, or any in --plain), its
 * CRTC and primary plane. Returns 1 found, 0 none, -1 an error. */
static int find_output(int fd, bool plain, struct output *o, bool *any_connected)
{
	drmModeResPtr res = drmModeGetResources(fd);
	drmModePlaneResPtr pres;
	int found = 0, i;
	uint32_t k;

	if (!res)
		return -1;
	for (i = 0; i < res->count_connectors && !found; i++) {
		drmModeConnectorPtr c = drmModeGetConnectorCurrent(fd, res->connectors[i]);
		struct props pr;
		int crtc_index = -1, e;

		if (!c)
			continue;
		if (c->connection != DRM_MODE_CONNECTED || c->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
			drmModeFreeConnector(c);
			continue;
		}
		*any_connected = true;
		if (!props_get(fd, c->connector_id, DRM_MODE_OBJECT_CONNECTOR, &pr)) {
			drmModeFreeConnector(c);
			continue;
		}
		if (!plain && prop_find(&pr, TUNNEL_PROP) < 0) {
			props_free(&pr);
			drmModeFreeConnector(c);
			continue;
		}
		/* its CRTC: the one it has now, or the first that one of its encoders can drive */
		if (c->encoder_id) {
			drmModeEncoderPtr enc = drmModeGetEncoder(fd, c->encoder_id);

			if (enc) {
				for (e = 0; e < res->count_crtcs; e++)
					if (enc->crtc_id && res->crtcs[e] == enc->crtc_id)
						crtc_index = e;
				drmModeFreeEncoder(enc);
			}
		}
		for (e = 0; e < c->count_encoders && crtc_index < 0; e++) {
			drmModeEncoderPtr enc = drmModeGetEncoder(fd, c->encoders[e]);
			int n;

			if (!enc)
				continue;
			for (n = 0; n < res->count_crtcs && crtc_index < 0; n++)
				if (enc->possible_crtcs & (1u << n))
					crtc_index = n;
			drmModeFreeEncoder(enc);
		}
		if (crtc_index < 0) {
			props_free(&pr);
			drmModeFreeConnector(c);
			continue;
		}
		o->connector_id = c->connector_id;
		o->crtc_index = crtc_index;
		o->crtc_id = res->crtcs[crtc_index];
		o->cprops = pr;
		connector_name(c->connector_type, c->connector_type_id, o->connector_name, sizeof(o->connector_name));
		found = 1;
		drmModeFreeConnector(c);
	}
	if (!found) {
		drmModeFreeResources(res);
		return 0;
	}
	/* the other connectors on that CRTC now (clones): taken off it */
	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnectorPtr c = drmModeGetConnectorCurrent(fd, res->connectors[i]);

		if (!c)
			continue;
		if (c->connector_id != o->connector_id && c->encoder_id) {
			drmModeEncoderPtr enc = drmModeGetEncoder(fd, c->encoder_id);

			if (enc && enc->crtc_id == o->crtc_id && o->n_clones < 16)
				o->clones[o->n_clones++] = c->connector_id;
			drmModeFreeEncoder(enc);
		}
		drmModeFreeConnector(c);
	}
	drmModeFreeResources(res);

	pres = drmModeGetPlaneResources(fd);
	if (!pres)
		return -1;
	for (k = 0; k < pres->count_planes; k++) {
		drmModePlanePtr p = drmModeGetPlane(fd, pres->planes[k]);
		struct props pp;
		uint64_t type;

		if (!p)
			continue;
		if (props_get(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, &pp) && prop_value(&pp, "type", &type) &&
		    type == DRM_PLANE_TYPE_PRIMARY && (p->possible_crtcs & (1u << o->crtc_index)) && !o->plane_id) {
			o->plane_id = p->plane_id;
			o->planeprops = pp;
		} else {
			if (p->crtc_id == o->crtc_id && o->n_other_planes < 32)
				o->other_planes[o->n_other_planes++] = p->plane_id;
			props_free(&pp);
		}
		drmModeFreePlane(p);
	}
	drmModeFreePlaneResources(pres);
	if (!o->plane_id)
		return -1;
	if (!props_get(fd, o->crtc_id, DRM_MODE_OBJECT_CRTC, &o->crtcprops))
		return -1;
	return 1;
}

static void crc_dir(const struct output *o, char *buf, size_t size)
{
	snprintf(buf, size, "/sys/kernel/debug/dri/%u/crtc-%d/crc", o->minor, o->crtc_index);
}

static bool exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* The kernel lockdown, as /sys/kernel/security/lockdown says it: the word in brackets */
static void lockdown_state(char *buf, size_t size)
{
	char line[128] = "";
	FILE *f = fopen("/sys/kernel/security/lockdown", "r");
	char *a, *b;

	snprintf(buf, size, "unknown");
	if (!f)
		return;
	if (fgets(line, sizeof(line), f) && (a = strchr(line, '[')) && (b = strchr(a, ']'))) {
		*b = '\0';
		snprintf(buf, size, "%s", a + 1);
	}
	fclose(f);
}

/* Whether amdgpu was loaded with dvbridge_dce=1 */
static bool amdgpu_dce_on(void)
{
	char v[8] = "";
	FILE *f = fopen("/sys/module/amdgpu/parameters/dvbridge_dce", "r");

	if (!f)
		return false;
	if (!fgets(v, sizeof(v), f))
		v[0] = '\0';
	fclose(f);
	return v[0] == 'Y' || v[0] == '1';
}

/* ---- the modes the tunnel takes (CTA 1080p and 2160p), in the order they are tried ---- */
struct cta {
	int w, h, clock, htotal;
	const char *name;
};

static const struct cta tunnel_modes[] = {
	{3840, 2160, 296703, 5500, "3840x2160 23.976 Hz"}, {3840, 2160, 297000, 5500, "3840x2160 24 Hz"},
	{1920, 1080, 74176, 2750, "1920x1080 23.976 Hz"},  {1920, 1080, 74250, 2750, "1920x1080 24 Hz"},
	{3840, 2160, 297000, 5280, "3840x2160 25 Hz"},     {3840, 2160, 296703, 4400, "3840x2160 29.97 Hz"},
	{3840, 2160, 297000, 4400, "3840x2160 30 Hz"},     {1920, 1080, 74250, 2640, "1920x1080 25 Hz"},
	{1920, 1080, 74176, 2200, "1920x1080 29.97 Hz"},   {1920, 1080, 74250, 2200, "1920x1080 30 Hz"},
	{1920, 1080, 148500, 2640, "1920x1080 50 Hz"},     {1920, 1080, 148352, 2200, "1920x1080 59.94 Hz"},
	{1920, 1080, 148500, 2200, "1920x1080 60 Hz"},     {3840, 2160, 594000, 5280, "3840x2160 50 Hz"},
	{3840, 2160, 593407, 4400, "3840x2160 59.94 Hz"},  {3840, 2160, 594000, 4400, "3840x2160 60 Hz"},
};

/* ---- buffers ---- */
struct buffer {
	uint32_t handle, fb, pitch;
	uint64_t size;
	uint32_t *map;
};

static bool buffer_make(int fd, uint32_t w, uint32_t h, struct buffer *b)
{
	struct drm_mode_create_dumb c = {.width = w, .height = h, .bpp = 32};
	struct drm_mode_map_dumb m = {0};
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	void *p;

	memset(b, 0, sizeof(*b));
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c))
		return false;
	b->handle = c.handle;
	b->pitch = c.pitch;
	b->size = c.size;
	handles[0] = c.handle;
	pitches[0] = c.pitch;
	if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->fb, 0))
		return false;
	m.handle = c.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m))
		return false;
	p = mmap(NULL, (size_t)c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
	if (p == MAP_FAILED)
		return false;
	b->map = p;
	return true;
}

static void buffer_free(int fd, struct buffer *b)
{
	struct drm_mode_destroy_dumb d = {.handle = b->handle};

	if (b->map)
		munmap(b->map, (size_t)b->size);
	if (b->fb)
		drmModeRmFB(fd, b->fb);
	if (b->handle)
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	memset(b, 0, sizeof(*b));
}

static uint64_t seed = DEFAULT_SEED;
static bool test_corrupt;

static void buffer_fill(struct buffer *b, uint32_t w, uint32_t h, const struct oc_picture *pic)
{
	uint32_t x, y;

	for (y = 0; y < h; y++) {
		uint32_t *row = b->map + (size_t)y * (b->pitch / 4);

		for (x = 0; x < w; x++)
			row[x] = 0xff000000u | oc_pixel(pic, seed, y * w + x);
	}
	/* a test of the test: one byte of C of the first trial changed, after the picture was made */
	if (test_corrupt && pic->kind == OC_C && pic->trial == 0)
		b->map[7] ^= 0x005a0000u;
}

/* ---- flips ---- */
static uint32_t flip_sequence;
static bool flip_done;

static void on_flip(int fd, unsigned int sequence, unsigned int sec, unsigned int usec, unsigned int crtc_id,
		    void *data)
{
	(void)fd;
	(void)sec;
	(void)usec;
	(void)crtc_id;
	(void)data;
	flip_sequence = sequence;
	flip_done = true;
}

static int wait_flip(int fd)
{
	drmEventContext ev = {.version = 3, .page_flip_handler2 = on_flip};
	struct pollfd p = {.fd = fd, .events = POLLIN};
	int n;

	while (!flip_done) {
		n = poll(&p, 1, WAIT_MS);
		if (n <= 0)
			return n == 0 ? -ETIMEDOUT : -errno;
		if (drmHandleEvent(fd, &ev))
			return -EIO;
	}
	return 0;
}

/* Show the buffer (the same mode and settings): returns the vblank sequence of the flip, or <0 */
static int64_t show(struct output *o, uint32_t fb)
{
	drmModeAtomicReqPtr req = drmModeAtomicAlloc();
	int r, tries = 0;

	if (!req)
		return -ENOMEM;
	add(req, o->plane_id, &o->planeprops, "FB_ID", fb);
	flip_done = false;
	do {
		r = drmModeAtomicCommit(o->fd, req, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK, NULL);
		if (r == -EBUSY)
			usleep(5000);
	} while (r == -EBUSY && ++tries < 200);
	drmModeAtomicFree(req);
	if (r)
		return r;
	r = wait_flip(o->fd);
	if (r)
		return r;
	return flip_sequence;
}

/* ---- the CRC entries ---- */
struct entry {
	bool has_frame;
	uint32_t frame;
	unsigned int n;
	uint32_t v[OC_MAX_VALUES];
};

/* One entry from the data file: 1, 0 none within the wait, <0 an error */
static int crc_read(int fd, struct entry *e)
{
	char line[16 + 11 * OC_MAX_VALUES + 8], *s, *end;
	struct pollfd p = {.fd = fd, .events = POLLIN};
	ssize_t n;

	for (;;) {
		n = read(fd, line, sizeof(line) - 1);
		if (n > 0)
			break;
		if (n == 0)
			return -ENODATA;
		if (errno != EAGAIN)
			return -errno;
		n = poll(&p, 1, WAIT_MS);
		if (n == 0)
			return 0;
		if (n < 0)
			return -errno;
	}
	line[n] = '\0';
	memset(e, 0, sizeof(*e));
	if (!strncmp(line, "XXXXXXXXXX", 10)) {
		s = line + 10;
	} else {
		e->frame = (uint32_t)strtoul(line, &end, 16);
		if (end == line)
			return -EPROTO;
		e->has_frame = true;
		s = end;
	}
	while (e->n < OC_MAX_VALUES) {
		unsigned long v = strtoul(s, &end, 16);

		if (end == s)
			break;
		e->v[e->n++] = (uint32_t)v;
		s = end;
	}
	return e->n ? 1 : -EPROTO;
}

static bool same_values(const struct entry *a, const struct entry *b)
{
	return a->n == b->n && !memcmp(a->v, b->v, a->n * sizeof(a->v[0]));
}

static void to_crc(const struct entry *e, struct oc_crc *c)
{
	memset(c, 0, sizeof(*c));
	memcpy(c->v, e->v, e->n * sizeof(e->v[0]));
	c->measured = true;
}

static time_t started;

static bool over_time(void)
{
	return time(NULL) - started > LIMIT_S;
}

/* ---- the GAMMA_LUT of the tests (on vkms): a pipe that is not the identity ---- */
static int gamma_blob(struct output *o, const char *kind, uint32_t *blob)
{
	uint64_t size = 256;
	struct drm_color_lut *lut;
	uint64_t i, n;
	int r;

	prop_value(&o->crtcprops, "GAMMA_LUT_SIZE", &size);
	n = size ? size : 256;
	lut = calloc(n, sizeof(*lut));
	if (!lut)
		return -ENOMEM;
	for (i = 0; i < n; i++) {
		double x = n > 1 ? (double)i / (double)(n - 1) : 0, y = x;
		unsigned int v8 = (unsigned int)lround(x * 255.0);

		if (!strcmp(kind, "identity"))
			y = x;
		else if (!strcmp(kind, "invert"))
			y = 1.0 - x;
		else if (!strcmp(kind, "curve"))
			y = pow(x, 2.2);
		else if (!strcmp(kind, "trunc1"))
			y = (v8 & ~1u) / 255.0;
		else if (!strcmp(kind, "limited"))
			y = (16.0 + 219.0 * x) / 255.0;
		else {
			free(lut);
			return -EINVAL;
		}
		lut[i].red = lut[i].green = lut[i].blue = (uint16_t)lround(y * 65535.0);
	}
	r = drmModeCreatePropertyBlob(o->fd, lut, n * sizeof(*lut), blob);
	free(lut);
	return r;
}

/* The request that sets the mode, the tunnel and the first buffer. Every setting the tunnel needs to be byte
 * exact (amdgpu_dm_dvbridge_check_scanout(), the i915 rule): "max bpc" 8, the default colorimetry, no HDR
 * metadata, full range, no underscan, no CTM, no LUT, no VRR, the primary plane alone, unscaled. */
static drmModeAtomicReqPtr mode_request(struct output *o, uint32_t mode_blob, uint32_t fb, bool plain,
					uint32_t gamma)
{
	drmModeAtomicReqPtr req = drmModeAtomicAlloc();
	uint64_t v;
	unsigned int i;
	const uint32_t w = o->mode.hdisplay, h = o->mode.vdisplay;

	if (!req)
		return NULL;
	add(req, o->connector_id, &o->cprops, "CRTC_ID", o->crtc_id);
	if (!plain) {
		add(req, o->connector_id, &o->cprops, TUNNEL_PROP, 1);
		add(req, o->connector_id, &o->cprops, "DVBRIDGE_DV_LOW_LATENCY_LAB", 0);
	}
	if (prop_find(&o->cprops, "max bpc") >= 0)
		add(req, o->connector_id, &o->cprops, "max bpc", 8);
	if (prop_enum(&o->cprops, "Colorspace", "Default", &v))
		add(req, o->connector_id, &o->cprops, "Colorspace", v);
	add(req, o->connector_id, &o->cprops, "HDR_OUTPUT_METADATA", 0);
	if (prop_enum(&o->cprops, "Broadcast RGB", "Full", &v))
		add(req, o->connector_id, &o->cprops, "Broadcast RGB", v);
	if (prop_enum(&o->cprops, "underscan", "off", &v))
		add(req, o->connector_id, &o->cprops, "underscan", v);
	add(req, o->connector_id, &o->cprops, "underscan hborder", 0);
	add(req, o->connector_id, &o->cprops, "underscan vborder", 0);
	if (prop_enum(&o->cprops, "scaling mode", "None", &v))
		add(req, o->connector_id, &o->cprops, "scaling mode", v);
	for (i = 0; i < o->n_clones; i++) {
		struct props cp;

		if (props_get(o->fd, o->clones[i], DRM_MODE_OBJECT_CONNECTOR, &cp)) {
			add(req, o->clones[i], &cp, "CRTC_ID", 0);
			props_free(&cp);
		}
	}

	add(req, o->crtc_id, &o->crtcprops, "MODE_ID", mode_blob);
	add(req, o->crtc_id, &o->crtcprops, "ACTIVE", 1);
	add(req, o->crtc_id, &o->crtcprops, "GAMMA_LUT", gamma);
	add(req, o->crtc_id, &o->crtcprops, "DEGAMMA_LUT", 0);
	add(req, o->crtc_id, &o->crtcprops, "CTM", 0);
	add(req, o->crtc_id, &o->crtcprops, "VRR_ENABLED", 0);

	add(req, o->plane_id, &o->planeprops, "FB_ID", fb);
	add(req, o->plane_id, &o->planeprops, "CRTC_ID", o->crtc_id);
	add(req, o->plane_id, &o->planeprops, "SRC_X", 0);
	add(req, o->plane_id, &o->planeprops, "SRC_Y", 0);
	add(req, o->plane_id, &o->planeprops, "SRC_W", (uint64_t)w << 16);
	add(req, o->plane_id, &o->planeprops, "SRC_H", (uint64_t)h << 16);
	add(req, o->plane_id, &o->planeprops, "CRTC_X", 0);
	add(req, o->plane_id, &o->planeprops, "CRTC_Y", 0);
	add(req, o->plane_id, &o->planeprops, "CRTC_W", w);
	add(req, o->plane_id, &o->planeprops, "CRTC_H", h);
	add(req, o->plane_id, &o->planeprops, "rotation", DRM_MODE_ROTATE_0);
	add(req, o->plane_id, &o->planeprops, "alpha", 0xffff);
	add(req, o->plane_id, &o->planeprops, "COLOR_PIPELINE", 0);
	for (i = 0; i < o->n_other_planes; i++) {
		struct props pp;

		if (props_get(o->fd, o->other_planes[i], DRM_MODE_OBJECT_PLANE, &pp)) {
			add(req, o->other_planes[i], &pp, "FB_ID", 0);
			add(req, o->other_planes[i], &pp, "CRTC_ID", 0);
			props_free(&pp);
		}
	}
	return req;
}

/* ---- the measure ---- */
struct run {
	struct output *o;
	struct buffer buf[BUFFERS];
	unsigned int next;
	int crc_fd;
	int offset;		/* frame of the CRC entry of a picture - sequence of its flip */
	unsigned int values;
	unsigned int pictures;
	unsigned int skipped;	/* entries whose frame came after the one expected */
	struct oc_measure m;
	struct entry last;
	char fail[256];
};

/* The CRC of a picture just flipped at sequence s: the first entry from frame s + offset on, and the next one,
 * which must be equal (the picture is held). */
static int measure_after(struct run *r, uint32_t s, struct oc_crc *out)
{
	struct entry e, f;
	uint32_t target = s + (uint32_t)r->offset;
	int k;

	for (k = 0; k < 64; k++) {
		int n = crc_read(r->crc_fd, &e);

		if (n <= 0)
			return n ? n : -ETIMEDOUT;
		if (!e.has_frame || e.n != r->values)
			return -EPROTO;
		if ((int32_t)(e.frame - target) >= 0)
			break;
	}
	if (k == 64)
		return -ETIMEDOUT;
	if (e.frame != target)
		r->skipped++;
	for (k = 0; k < 64; k++) {
		int n = crc_read(r->crc_fd, &f);

		if (n <= 0)
			return n ? n : -ETIMEDOUT;
		if (!f.has_frame || f.n != r->values)
			return -EPROTO;
		if ((int32_t)(f.frame - e.frame) > 0)
			break;
	}
	if (k == 64)
		return -ETIMEDOUT;
	r->m.held++;
	if (!same_values(&e, &f))
		r->m.held_changes++;
	to_crc(&e, out);
	r->last = f;
	return 0;
}

static int picture(struct run *r, const struct oc_picture *pic, struct oc_crc *out)
{
	struct buffer *b = &r->buf[r->next];
	int64_t s;

	if (over_time())
		return -ETIMEDOUT;
	r->next = (r->next + 1) % BUFFERS;
	buffer_fill(b, r->o->mode.hdisplay, r->o->mode.vdisplay, pic);
	s = show(r->o, b->fb);
	if (s < 0)
		return (int)s;
	r->pictures++;
	return measure_after(r, (uint32_t)s, out);
}

static const char *errname(int e)
{
	return strerror(e < 0 ? -e : e);
}

/* Z after the change of mode: entries dropped first (a driver may give one of the frame before), then six
 * read; then the offset between a flip and the entry of its frame, from the first picture that follows. */
static int start_measure(struct run *r)
{
	struct entry e, prev;
	struct oc_picture a0 = {.kind = OC_A, .trial = 0};
	struct buffer *b;
	int64_t s;
	int k, n;

	for (k = 0; k < 4; k++) {
		n = crc_read(r->crc_fd, &e);
		if (n <= 0) {
			snprintf(r->fail, sizeof(r->fail), "no CRC entry after the change of mode (%s)",
				 n ? errname(n) : "none within the wait");
			return n ? n : -ETIMEDOUT;
		}
	}
	if (!e.has_frame) {
		snprintf(r->fail, sizeof(r->fail), "the driver gives its CRC entries without a frame number");
		return -EPROTO;
	}
	r->values = e.n;
	r->m.values = e.n;
	prev = e;
	for (k = 0; k < 6; k++) {
		n = crc_read(r->crc_fd, &e);
		if (n <= 0 || e.n != r->values) {
			snprintf(r->fail, sizeof(r->fail), "Z: CRC entries stopped (%s)",
				 n < 0 ? errname(n) : "none within the wait");
			return n < 0 ? n : -ETIMEDOUT;
		}
		r->m.z_entries++;
		if (!same_values(&e, &prev))
			r->m.z_changes++;
		prev = e;
	}
	to_crc(&e, &r->m.z);

	/* the offset: the first entry unlike Z after A0 is flipped */
	b = &r->buf[r->next];
	r->next = (r->next + 1) % BUFFERS;
	buffer_fill(b, r->o->mode.hdisplay, r->o->mode.vdisplay, &a0);
	s = show(r->o, b->fb);
	if (s < 0) {
		snprintf(r->fail, sizeof(r->fail), "the first flip failed (%s)", errname((int)s));
		return (int)s;
	}
	r->pictures++;
	for (k = 0; k < 16; k++) {
		n = crc_read(r->crc_fd, &e);
		if (n <= 0) {
			snprintf(r->fail, sizeof(r->fail), "no CRC entry after the first flip");
			return n ? n : -ETIMEDOUT;
		}
		if (!same_values(&e, &prev))
			break;
		prev = e;
	}
	if (k == 16) {
		/* the CRC does not follow the picture: every picture is measured all the same, at the frame
		 * of its flip, and the judgement says so (OC_R_CONSTANT) */
		r->offset = 0;
		n = measure_after(r, (uint32_t)s, &r->m.a[0]);
		if (n)
			snprintf(r->fail, sizeof(r->fail), "no CRC entry after the first flip");
		return n;
	}
	r->offset = (int)(int32_t)(e.frame - (uint32_t)s);
	if (r->offset < -1 || r->offset > 4) {
		snprintf(r->fail, sizeof(r->fail), "the CRC of a flip came %d frames after it", r->offset);
		return -EPROTO;
	}
	/* that entry is A0's; the next one must be equal */
	{
		struct entry f;

		for (k = 0; k < 64; k++) {
			n = crc_read(r->crc_fd, &f);
			if (n <= 0)
				return n ? n : -ETIMEDOUT;
			if (!f.has_frame || f.n != r->values)
				return -EPROTO;
			if ((int32_t)(f.frame - e.frame) > 0)
				break;
		}
		if (k == 64)
			return -ETIMEDOUT;
		r->m.held++;
		if (!same_values(&e, &f))
			r->m.held_changes++;
		to_crc(&e, &r->m.a[0]);
	}
	return 0;
}

static int all_pictures(struct run *r)
{
	unsigned int t, c, b;
	int n;

	for (t = 0; t < OC_TRIALS; t++) {
		struct oc_picture a = {.kind = OC_A, .trial = t}, bb = {.kind = OC_B, .trial = t},
				  cc = {.kind = OC_C, .trial = t};

		if (t > 0 && (n = picture(r, &a, &r->m.a[t])))
			return n;
		if ((n = picture(r, &bb, &r->m.b[t])) || (n = picture(r, &cc, &r->m.c[t])))
			return n;
	}
	{
		struct oc_picture d = {.kind = OC_D}, e = {.kind = OC_E}, f = {.kind = OC_F};

		if ((n = picture(r, &d, &r->m.d)) || (n = picture(r, &e, &r->m.e)) || (n = picture(r, &f, &r->m.f)))
			return n;
	}
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++) {
			struct oc_picture q = {.kind = OC_BIT, .channel = c, .bit = b};

			if ((n = picture(r, &q, &r->m.bits[c][b][0])))
				return n;
			if (!memcmp(r->m.bits[c][b][0].v, r->m.z.v, sizeof(r->m.z.v))) {
				q.variant = 1;
				if ((n = picture(r, &q, &r->m.bits[c][b][1])))
					return n;
			}
		}
	{
		struct oc_picture z = {.kind = OC_Z};

		if ((n = picture(r, &z, &r->m.z_end)))
			return n;
	}
	return 0;
}

static void say_crc(const char *name, const struct oc_crc *c, unsigned int values)
{
	unsigned int i;

	say("crc %s:", name);
	if (!c->measured) {
		say(" not measured\n");
		return;
	}
	for (i = 0; i < values; i++)
		say(" %08" PRIx32, c->v[i]);
	say("\n");
}

static void verdict_text(const struct oc_result *res, const struct run *r)
{
	char reason[256] = "", lost[512] = "";
	size_t len = 0, ll = 0;
	unsigned int c, b;
	static const struct {
		unsigned int bit;
		const char *name;
	} names[] = {{OC_R_UNSTABLE, "unstable"},       {OC_R_DRIFT, "drift"},
		     {OC_R_CRC_NOT_LINEAR, "crc-not-linear"}, {OC_R_TRANSFORMED, "transformed"},
		     {OC_R_BIT_LOST, "bit-lost"},       {OC_R_CONSTANT, "constant"},
		     {OC_R_INCOMPLETE, "incomplete"}};

	for (c = 0; c < sizeof(names) / sizeof(names[0]); c++)
		if (res->reasons & names[c].bit)
			len += (size_t)snprintf(reason + len, sizeof(reason) - len, "%s%s", len ? "," : "",
						names[c].name);
	if (!len)
		snprintf(reason, sizeof(reason), "none");
	for (c = 0; c < 3; c++)
		for (b = 0; b < 8; b++)
			if (res->lost_bits & (1u << (8 * c + b)))
				ll += (size_t)snprintf(lost + ll, sizeof(lost) - ll, "%sbit %u of %s", ll ? ", " : "", b,
						       oc_channel_name(c));
	say("pictures: %u shown, each held for two frames or more (CRC entries a frame %d after the flip; %u "
	    "entries came later than expected)\n",
	    r->pictures, r->offset, r->skipped);
	say("stability: Z held for %u entries, %u changes; %u pictures held, %u changed between their two "
	    "frames; Z at the end %s Z at the start\n",
	    r->m.z_entries, r->m.z_changes, r->m.held, r->m.held_changes,
	    memcmp(r->m.z.v, r->m.z_end.v, sizeof(r->m.z.v)) ? "unlike" : "like");
	say("calibration (changes on two separate halves of the pixels): %s\n",
	    res->reasons & OC_R_CRC_NOT_LINEAR ? "the relation does not hold" : "the relation holds");
	say("exactness (changes on the same pixels): %u of %u trials hold\n", OC_TRIALS - res->trials_failed,
	    OC_TRIALS);
	say("bits: %s\n", ll ? lost : "each of the 24 bits of a pixel changes the CRC");
	say("values changed by the bits of red, green and blue: %#x, %#x, %#x\n", res->changed_values[0],
	    res->changed_values[1], res->changed_values[2]);

	switch (res->verdict) {
	case OC_EXACT:
		finish("exact", reason,
		       "the graphics card passed the bytes of the test pictures through its display pipe unchanged, "
		       "as its own CRC of the output measured them",
		       "what Hades draws for Dolby Vision leaves the display pipe of this card as it was drawn. It "
		       "does not test the HDMI encoder, the cable or the display.");
		break;
	case OC_NOT_EXACT:
		if (res->reasons & (OC_R_UNSTABLE | OC_R_DRIFT))
			finish("not-exact", reason,
			       "the output of the card changed between frames of the same picture",
			       "the display pipe does not pass the bytes through as they are (a dither in time, "
			       "for example): a Dolby Vision display cannot read its metadata from them, and shows "
			       "black or wrong colours.");
		else if (res->reasons & OC_R_TRANSFORMED)
			finish("not-exact", reason,
			       "the card changed the bytes of the test pictures on their way out",
			       "the display pipe converts them (a colour conversion, a curve, a range or a "
			       "rounding): a Dolby Vision display cannot read its metadata from them, and shows "
			       "black or wrong colours.");
		else
			finish("not-exact", reason, "some bits of the test pictures did not reach the output",
			       "the display pipe drops them (a truncation): a Dolby Vision display cannot read its "
			       "metadata from what is left, and shows black or wrong colours.");
		break;
	case OC_NOT_JUDGED:
		finish("not-judged", reason,
		       res->reasons & OC_R_CONSTANT ? "the CRC of the card did not change with the picture"
		       : res->reasons & OC_R_INCOMPLETE
			       ? "not every test picture could be measured"
			       : "the CRC of this driver does not behave as a CRC over the bits of the picture",
		       "this check cannot tell whether the output is exact on this card.");
		break;
	}
}

int main(int argc, char **argv)
{
	bool probe = false, plain = false, any_connected = false, any_nvidia = false, any_amdgpu = false;
	const char *card = NULL, *want_mode = NULL, *gamma = NULL;
	int hold = 0;
	struct output o;
	struct run r;
	char dir[256], path[320], lock[32], drivers[256] = "";
	size_t dl = 0;
	int i, n, found = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--probe"))
			probe = true;
		else if (!strcmp(argv[i], "--plain"))
			plain = true;
		else if (!strcmp(argv[i], "--test-corrupt"))
			test_corrupt = true;
		else if (!strcmp(argv[i], "--out") && i + 1 < argc)
			out_path = argv[++i];
		else if (!strcmp(argv[i], "--card") && i + 1 < argc)
			card = argv[++i];
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc)
			want_mode = argv[++i];
		else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
			seed = strtoull(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--test-gamma") && i + 1 < argc)
			gamma = argv[++i];
		else if (!strcmp(argv[i], "--test-hold") && i + 1 < argc)
			hold = atoi(argv[++i]);
		else {
			fprintf(stderr, "usage: %s [--probe] [--out FILE] (tests: --plain [--mode WxH] [--card NODE] "
					"[--seed N] [--test-gamma KIND] [--test-corrupt])\n",
				argv[0]);
			return 2;
		}
	}
	if (hold > 0) {
		/* a test of "busy": another program that holds the display */
		int fd = open(card ? card : "/dev/dri/card0", O_RDWR | O_CLOEXEC);

		if (fd < 0 || (drmSetMaster(fd) && !drmIsMaster(fd)))
			return 1;
		sleep((unsigned int)hold);
		close(fd);
		return 0;
	}
	started = time(NULL);
	memset(&o, 0, sizeof(o));
	o.fd = -1;
	lockdown_state(lock, sizeof(lock));
	say("check: %s, seed %#" PRIx64 ", kernel lockdown %s\n", plain ? "without the Dolby Vision tunnel (a test)" : "with the Dolby Vision tunnel",
	    seed, lock);

	/* the device: the given one, or the first whose connected output takes the tunnel */
	for (i = 0; i < 16 && !found; i++) {
		char node[64];
		struct stat st;
		bool connected = false;
		int fd;

		if (card)
			snprintf(node, sizeof(node), "%s", card);
		else
			snprintf(node, sizeof(node), "/dev/dri/card%d", i);
		if (stat(node, &st))
			goto next;
		fd = open(node, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			goto next;
		memset(&o, 0, sizeof(o));
		o.fd = fd;
		snprintf(o.node, sizeof(o.node), "%s", node);
		o.minor = minor(st.st_rdev);
		if (!driver_name(fd, o.driver, sizeof(o.driver)))
			snprintf(o.driver, sizeof(o.driver), "unknown");
		dl += (size_t)snprintf(drivers + dl, sizeof(drivers) > dl ? sizeof(drivers) - dl : 0, "%s%s",
				       dl ? ", " : "", o.driver);
		if (!strcmp(o.driver, "nvidia-drm"))
			any_nvidia = true;
		if (!strcmp(o.driver, "amdgpu"))
			any_amdgpu = true;
		if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
		    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
			close(fd);
			o.fd = -1;
			goto next;
		}
		n = find_output(fd, plain, &o, &connected);
		any_connected = any_connected || connected;
		if (n == 1) {
			found = 1;
			break;
		}
		close(fd);
		o.fd = -1;
next:
		if (card)
			break;
	}
	if (!found) {
		if (!any_connected)
			finish("unavailable", "no-display", "no display is connected that this check can use",
			       "connect the display that takes Dolby Vision, and try again.");
		else if (any_nvidia)
			finish("unavailable", "nvidia",
			       "NVIDIA's driver does not offer the measure this check needs (the CRC of the output of "
			       "its display pipe)",
			       "on an NVIDIA card the output cannot be checked this way. Dolby Vision is sent there "
			       "with the means of NVIDIA's own driver.");
		else if (any_amdgpu && !amdgpu_dce_on() && exists("/sys/module/amdgpu/parameters/dvbridge_dce"))
			finish("unavailable", "no-tunnel",
			       "the graphics card does not offer Hades' Dolby Vision output",
			       "on Radeon RX 400 and 500 (Polaris) and Vega cards it is offered only with "
			       "amdgpu.dvbridge_dce=1 on the kernel line; on other cards, the display or the card "
			       "does not take it.");
		else
			finish("unavailable", "no-tunnel",
			       "no connected output offers Hades' Dolby Vision output",
			       "the display does not take Dolby Vision, or this graphics card does not offer it.");
		say("drivers: %s\n", dl ? drivers : "none");
		return flush_report() ? 1 : 0;
	}
	say("device: %s, driver %s, output %s, CRTC %d (index %d), primary plane %u\n", o.node, o.driver,
	    o.connector_name, o.crtc_id, o.crtc_index, o.plane_id);

	crc_dir(&o, dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/data", dir);
	if (!exists("/sys/kernel/debug/dri")) {
		finish("unavailable", "no-debugfs", "the kernel's debug file system is not mounted",
		       "the CRC of the output is read there; without it this check cannot be made.");
		return flush_report() ? 1 : 0;
	}
	if (!exists(path)) {
		finish("unavailable", "no-crc", "this graphics driver does not offer the CRC of its output",
		       "the output cannot be checked this way on this card.");
		say("looked for: %s\n", path);
		return flush_report() ? 1 : 0;
	}
	if (probe) {
		finish("ready", "none", "the output can be checked", "the check takes about twenty seconds.");
		return flush_report() ? 1 : 0;
	}

	/* the display for this program alone */
	if (drmSetMaster(o.fd) && !drmIsMaster(o.fd)) {
		finish("unavailable", "busy", "another program shows the picture on this card",
		       "the check runs before Kodi starts; it could not take the display.");
		return flush_report() ? 1 : 0;
	}

	/* the source of the CRC: "auto", the default, which a locked down kernel cannot change */
	{
		char csrc[64] = "auto";
		int cf;

		snprintf(path, sizeof(path), "%s/control", dir);
		cf = open(path, O_RDWR | O_CLOEXEC);
		if (cf < 0) {
			say("crc source: control %s (%s): the default source auto is used\n", path,
			    errno == EPERM ? "not opened, the kernel is locked down" : strerror(errno));
		} else {
			char buf[512] = "", *star, *line;
			ssize_t got = read(cf, buf, sizeof(buf) - 1);

			buf[got > 0 ? got : 0] = '\0';
			star = strchr(buf, '*');
			if (star) {
				*star = '\0';
				line = strrchr(buf, '\n');
				snprintf(csrc, sizeof(csrc), "%s", line ? line + 1 : buf);
			}
			if (strcmp(csrc, "auto") && strcmp(csrc, "crtc") && strcmp(csrc, "pipe")) {
				if (pwrite(cf, "auto", 4, 0) == 4)
					say("crc source: was %s, set to auto\n", csrc);
				else
					say("crc source: %s, which could not be set to auto\n", csrc);
			} else {
				say("crc source: %s\n", csrc);
			}
			close(cf);
		}
	}

	/* the mode: one the driver accepts with the tunnel (or the one asked for, in a test) */
	{
		drmModeConnectorPtr c = drmModeGetConnectorCurrent(o.fd, o.connector_id);
		uint32_t gblob = 0, mblob = 0;
		bool ok = false;
		unsigned int tried = 0;
		unsigned int k;
		int m;

		memset(&r, 0, sizeof(r));
		r.o = &o;
		r.crc_fd = -1;
		if (!c || !c->count_modes) {
			drmModeFreeConnector(c);
			c = drmModeGetConnector(o.fd, o.connector_id);
		}
		if (!c || !c->count_modes) {
			finish("unavailable", "no-mode", "the output lists no modes", "the display may be off.");
			drmModeFreeConnector(c);
			return flush_report() ? 1 : 0;
		}
		if (gamma && gamma_blob(&o, gamma, &gblob)) {
			finish("unavailable", "test", "the GAMMA_LUT of the test could not be made", "a test option.");
			return flush_report() ? 1 : 0;
		}
		/* the first buffer has to exist before a request can be tested: made at the size of the
		 * largest mode tried, then again at the size chosen */
		for (k = 0; k < (plain ? 1u : sizeof(tunnel_modes) / sizeof(tunnel_modes[0])) && !ok; k++) {
			for (m = 0; m < c->count_modes && !ok; m++) {
				const drmModeModeInfo *mi = &c->modes[m];
				drmModeAtomicReqPtr req;
				struct buffer tb;
				int w, h;

				if (mi->flags & DRM_MODE_FLAG_INTERLACE)
					continue;
				if (plain) {
					if (want_mode ? (sscanf(want_mode, "%dx%d", &w, &h) != 2 || mi->hdisplay != w ||
							 mi->vdisplay != h)
						      : !(mi->type & DRM_MODE_TYPE_PREFERRED))
						continue;
				} else if (mi->hdisplay != tunnel_modes[k].w || mi->vdisplay != tunnel_modes[k].h ||
					   (int)mi->clock != tunnel_modes[k].clock ||
					   mi->htotal != tunnel_modes[k].htotal) {
					continue;
				}
				o.mode = *mi;
				tried++;
				if (drmModeCreatePropertyBlob(o.fd, mi, sizeof(*mi), &mblob))
					continue;
				if (!buffer_make(o.fd, mi->hdisplay, mi->vdisplay, &tb)) {
					drmModeDestroyPropertyBlob(o.fd, mblob);
					continue;
				}
				req = mode_request(&o, mblob, tb.fb, plain, gblob);
				if (req && !drmModeAtomicCommit(o.fd, req,
								DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET,
								NULL)) {
					ok = true;
					say("mode: %dx%d, %.3f Hz, %d kHz%s\n", mi->hdisplay, mi->vdisplay,
					    mi->clock * 1000.0 / (mi->htotal * mi->vtotal), mi->clock,
					    plain ? "" : " (the first that the driver takes with the tunnel)");
				} else {
					drmModeDestroyPropertyBlob(o.fd, mblob);
					mblob = 0;
				}
				drmModeAtomicFree(req);
				buffer_free(o.fd, &tb);
			}
		}
		if (!ok && !tried) {
			finish("unavailable", "no-mode",
			       plain ? "the output has no such mode"
				     : "the display lists none of the modes that Dolby Vision is sent in",
			       "the CTA modes of 1080p and 2160p at 24 to 60 Hz are tried; the display offers none.");
			drmModeFreeConnector(c);
			return flush_report() ? 1 : 0;
		}
		if (!ok) {
			struct props cp;
			uint64_t refused = 0;
			char why[256] = "";

			if (props_get(o.fd, o.connector_id, DRM_MODE_OBJECT_CONNECTOR, &cp)) {
				if (prop_value(&cp, REFUSED_PROP, &refused))
					bitmask_names(&cp, REFUSED_PROP, refused, why, sizeof(why));
				props_free(&cp);
			}
			finish("unavailable", "refused",
			       plain ? "the driver refused the mode of the test"
				     : "the driver refused the Dolby Vision output in every mode tried",
			       "the display or the card does not take it; the kernel log says why.");
			say("refused: %s\n", why[0] ? why : "(no reason given)");
			drmModeFreeConnector(c);
			return flush_report() ? 1 : 0;
		}
		drmModeFreeConnector(c);

		for (k = 0; k < BUFFERS; k++)
			if (!buffer_make(o.fd, o.mode.hdisplay, o.mode.vdisplay, &r.buf[k])) {
				finish("unavailable", "memory", "the buffers of the test pictures could not be made",
				       "the graphics card or the system has too little memory free.");
				return flush_report() ? 1 : 0;
			}
		{
			struct oc_picture z = {.kind = OC_Z};
			drmModeAtomicReqPtr req;

			buffer_fill(&r.buf[0], o.mode.hdisplay, o.mode.vdisplay, &z);
			r.next = 1;
			req = mode_request(&o, mblob, r.buf[0].fb, plain, gblob);
			n = req ? drmModeAtomicCommit(o.fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) : -ENOMEM;
			drmModeAtomicFree(req);
			if (n) {
				finish("unavailable", "modeset", "the change of mode failed",
				       "the driver took the mode in a test but not when it was set.");
				say("error: %s\n", errname(n));
				return flush_report() ? 1 : 0;
			}
		}
	}

	/* the CRC: opened after the change of mode (the CRTC must be on), closed before the end */
	snprintf(path, sizeof(path), "%s/data", dir);
	r.crc_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (r.crc_fd < 0) {
		finish("unavailable", errno == EPERM ? "crc-locked" : "crc-open",
		       "the CRC of the output could not be opened",
		       errno == EPERM ? "the kernel's lockdown refused it." : "the driver refused it.");
		say("open %s: %s\n", path, strerror(errno));
		return flush_report() ? 1 : 0;
	}
	n = start_measure(&r);
	if (n < 0) {
		close(r.crc_fd);
		finish("not-judged", "incomplete", "the measure could not be made", r.fail[0] ? r.fail : errname(n));
		return flush_report() ? 1 : 0;
	}
	n = all_pictures(&r);
	close(r.crc_fd);
	{
		struct oc_result res;

		say("crc: %u values an entry\n", r.values);
		if (n)
			say("measure stopped: %s after %u pictures\n", over_time() ? "time limit" : errname(n),
			    r.pictures);
		oc_analyse(&r.m, &res);
		say_crc("Z", &r.m.z, r.values);
		say_crc("A1", &r.m.a[0], r.values);
		say_crc("B1", &r.m.b[0], r.values);
		say_crc("C1", &r.m.c[0], r.values);
		say_crc("D", &r.m.d, r.values);
		say_crc("Z at the end", &r.m.z_end, r.values);
		verdict_text(&res, &r);
	}
	for (i = 0; i < BUFFERS; i++)
		buffer_free(o.fd, &r.buf[i]);
	/* closing the device gives the display back: the console takes it, without the tunnel */
	close(o.fd);
	return flush_report() ? 1 : 0;
}
