// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test of libbluray-9107: HGraphicsDevice and HBackgroundDevice give the configuration that matches a
// template best (the first on a tie), and a PREFERRED or PREFERRED_NOT preference counts. The devices are made
// without their constructors (which ask libbluray for the player's profile) and given the configurations of
// HScreenConfigTemplate.defaultConfig, as the constructors make them.
package org.havi.ui;

import java.awt.Color;
import java.awt.Dimension;
import java.lang.reflect.Field;

public class HaviBestTest {
    // the standard output as it is before org.videolan.Logger takes it over (it sends what is printed to the
    // native side of libbluray, which a host test does not have)
    static final java.io.PrintStream OUT =
        new java.io.PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), true);
    static int checks = 0, failures = 0;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            OUT.println("FAIL: " + what);
        }
    }

    static Object bare(Class c) throws Exception {
        Field f = Class.forName("sun.misc.Unsafe").getDeclaredField("theUnsafe");
        f.setAccessible(true);
        Object unsafe = f.get(null);
        return unsafe.getClass().getMethod("allocateInstance", new Class[] { Class.class })
            .invoke(unsafe, new Object[] { c });
    }

    static void set(Object o, Class c, String name, Object value) throws Exception {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        f.set(o, value);
    }

    static HGraphicsDevice graphics() throws Exception {
        HGraphicsDevice d = (HGraphicsDevice) bare(HGraphicsDevice.class);
        int n = HScreenConfigTemplate.defaultConfig.length;
        HGraphicsConfiguration[] a = new HGraphicsConfiguration[n];
        for (int i = 0; i < n; i++) {
            HGraphicsConfigTemplate t = new HGraphicsConfigTemplate();
            HScreenConfigTemplate.initDefaultConfigTemplate(t, i);
            a[i] = new HGraphicsConfiguration(t);
        }
        set(d, HGraphicsDevice.class, "hgcArray", a);
        return d;
    }

    static HBackgroundDevice background() throws Exception {
        HBackgroundDevice d = (HBackgroundDevice) bare(HBackgroundDevice.class);
        int n = HScreenConfigTemplate.defaultConfig.length;
        HBackgroundConfiguration[] a = new HBackgroundConfiguration[n];
        for (int i = 0; i < n; i++) {
            HBackgroundConfigTemplate t = new HBackgroundConfigTemplate();
            HScreenConfigTemplate.initDefaultConfigTemplate(t, i);
            a[i] = new HStillImageBackgroundConfiguration(t, new Color(0, 0, 0, 0));
        }
        set(d, HBackgroundDevice.class, "hbcArray", a);
        return d;
    }

    // the video device of a profile 5 (3D) player: its configurations carry 3D templates, as its constructor makes
    // them (libbluray-9106: getS3DConfiguration() reports the 3D mode of the template that was chosen)
    static HVideoDevice video3d() throws Exception {
        HVideoDevice d = (HVideoDevice) bare(HVideoDevice.class);
        int n = HScreenConfigTemplate.defaultConfig.length;
        HVideoConfiguration[] a = new HVideoConfiguration[n];
        for (int i = 0; i < n; i++) {
            HVideoConfigTemplate t = new org.blurayx.s3d.ui.HVideoConfigTemplateS3D();
            HScreenConfigTemplate.initDefaultConfigTemplate(t, i);
            a[i] = new HVideoConfiguration(t);
        }
        set(d, HVideoDevice.class, "hvcArray", a);
        set(d, HVideoDevice.class, "s3dArray", new java.util.ArrayList[n]);
        return d;
    }

    static HVideoConfigTemplate video3dTemplate(int w, int h, String mode) {
        HVideoConfigTemplate t = new org.blurayx.s3d.ui.HVideoConfigTemplateS3D();
        t.setPreference(HScreenConfigTemplate.PIXEL_RESOLUTION, new Dimension(w, h), HScreenConfigTemplate.PREFERRED);
        t.setPreference(org.blurayx.s3d.ui.HVideoConfigTemplateS3D.S3D, mode, HScreenConfigTemplate.REQUIRED);
        return t;
    }

    static String mode(HVideoConfiguration c) {
        return c == null ? "none" : String.valueOf(c.getConfigTemplate().getPreferenceObject(
            org.blurayx.s3d.ui.HVideoConfigTemplateS3D.S3D));
    }

    static String size(HScreenConfiguration c) {
        if (c == null)
            return "none";
        Dimension d = c.getPixelResolution();
        return d.width + "x" + d.height;
    }

    static HGraphicsConfigTemplate graphicsTemplate(int w, int h, int priority) {
        HGraphicsConfigTemplate t = new HGraphicsConfigTemplate();
        if (w > 0)
            t.setPreference(HScreenConfigTemplate.PIXEL_RESOLUTION, new Dimension(w, h), priority);
        return t;
    }

    public static void main(String[] args) throws Exception {
        HGraphicsDevice g = graphics();
        String s;

        s = size(g.getBestConfiguration(graphicsTemplate(1280, 720, HScreenConfigTemplate.REQUIRED)));
        check(s.equals("1280x720"), "REQUIRED 1280x720 gives 1280x720 (" + s + ")");

        s = size(g.getBestConfiguration(graphicsTemplate(1920, 1080, HScreenConfigTemplate.PREFERRED)));
        check(s.equals("1920x1080"), "PREFERRED 1920x1080 gives 1920x1080 (" + s + ")");

        s = size(g.getBestConfiguration(graphicsTemplate(1280, 720, HScreenConfigTemplate.PREFERRED)));
        check(s.equals("1280x720"), "PREFERRED 1280x720 gives 1280x720 (" + s + ")");

        s = size(g.getBestConfiguration(graphicsTemplate(0, 0, 0)));
        check(s.equals("1920x1080"), "a template that cares for nothing gives the default, the first (" + s + ")");

        s = size(g.getBestConfiguration(graphicsTemplate(1920, 1080, HScreenConfigTemplate.PREFERRED_NOT)));
        check(s.equals("1280x720"), "PREFERRED_NOT 1920x1080 gives the first other one, 1280x720 (" + s + ")");

        s = size(g.getBestConfiguration(graphicsTemplate(4000, 4000, HScreenConfigTemplate.REQUIRED)));
        check(s.equals("none"), "REQUIRED 4000x4000: no configuration (" + s + ")");

        s = size(g.getBestConfiguration(new HGraphicsConfigTemplate[] {
            graphicsTemplate(4000, 4000, HScreenConfigTemplate.REQUIRED),
            graphicsTemplate(720, 576, HScreenConfigTemplate.PREFERRED) }));
        check(s.equals("720x576"), "two templates, one that nothing meets: the best of the other, 720x576 (" + s + ")");

        HVideoDevice v = video3d();
        HVideoConfiguration vc = v.getBestConfiguration(video3dTemplate(1280, 720, "A"));
        check(size(vc).equals("1280x720") && mode(vc).equals("A"),
              "video (3D), PREFERRED 1280x720 with 3D mode A: 1280x720 in mode A (" + size(vc) + ", " + mode(vc) + ")");
        vc = v.getBestConfiguration(new HVideoConfigTemplate[] {
            video3dTemplate(1280, 720, "A"), video3dTemplate(1920, 1080, "B") });
        check(size(vc).equals("1920x1080") && mode(vc).equals("B"),
              "video (3D), two templates: the configuration and the 3D mode of the template that matches best, the "
              + "first on a tie (" + size(vc) + ", " + mode(vc) + ")");

        HBackgroundDevice b = background();
        HBackgroundConfigTemplate bt = new HBackgroundConfigTemplate();
        bt.setPreference(HScreenConfigTemplate.PIXEL_RESOLUTION, new Dimension(1920, 1080), HScreenConfigTemplate.PREFERRED);
        s = size(b.getBestConfiguration(bt));
        check(s.equals("1920x1080"), "background, PREFERRED 1920x1080 gives 1920x1080 (" + s + ")");
        s = size(b.getBestConfiguration(new HBackgroundConfigTemplate[] { new HBackgroundConfigTemplate() }));
        check(s.equals("1920x1080"), "background, a template that cares for nothing gives the default (" + s + ")");

        OUT.println("HAVI-BEST " + checks + " checks, " + failures + " failed");
        if (failures == 0)
            OUT.println("ALL PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
