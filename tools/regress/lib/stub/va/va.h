/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * The types and calls of libva that tools/dvbridge names, for a host test that builds the bridge without libva
 * (the host FFmpeg of tools/regress has no VAAPI). As libva defines them (va/va.h): a display is a pointer, the ids
 * are unsigned integers, a status an int. Nothing here talks to a driver.
 */
#ifndef REGRESS_STUB_VA_H
#define REGRESS_STUB_VA_H
typedef void *VADisplay;
typedef unsigned int VAGenericID;
typedef VAGenericID VASurfaceID;
typedef VAGenericID VAConfigID;
typedef int VAStatus;
typedef struct _VASurfaceAttrib VASurfaceAttrib;
#ifndef vaQueryVendorString
static inline const char *vaQueryVendorString(VADisplay display)
{
    (void)display;
    return 0;
}
#endif
#ifndef vaSyncSurface
static inline VAStatus vaSyncSurface(VADisplay display, VASurfaceID surface)
{
    (void)display;
    (void)surface;
    return 0;
}
#endif
#endif
