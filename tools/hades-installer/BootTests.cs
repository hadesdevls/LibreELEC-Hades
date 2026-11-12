// LibreELEC Hades installer for Windows: the tests of BootMenus (the choices of the start that the boot menus keep,
// carried across an update), part of --selftest. What they write is for whoever works on the program: in English.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Hades.Installer
{
    static partial class SelfTest
    {
        // The two menus as scripts/mkimage of 1.0 writes them (the lines that matter here), with a wait and the options:
        // gmp, dce: the value of each option in every entry that starts the system; crlf: Windows line ends
        static string Syslinux(string timeout, string say, string gmp = "0", string dce = "0", bool crlf = false, string extra = "")
        {
            string t = "# hades-features: storage-anywhere other-disks-hidden boot-choices\n" +
                       "SAY Wait for run mode to start automatically in " + say + "\nSAY\nDEFAULT run\n" + timeout + "PROMPT 1\n" +
                       "INCLUDE /hades/menu.cfg\n\nLABEL run\n  MENU LABEL Run (recommended)\n  KERNEL /KERNEL\n" +
                       "  APPEND boot=UUID=0511-0515 disk=UUID=e4a24bbb-334b-4c1c-b566-bfb2a9cbb0f3 portable quiet initcall_blacklist=nvme_init,ahci_pci_driver_init lockdown=integrity i915.hdmi_hdr_gmp=" + gmp + " amdgpu.dvbridge_dce=" + dce + "\n\n" +
                       "LABEL internal\n  KERNEL /KERNEL\n  APPEND boot=UUID=0511-0515 disk=UUID=e4a24bbb-334b-4c1c-b566-bfb2a9cbb0f3 portable quiet i915.hdmi_hdr_gmp=" + gmp + " amdgpu.dvbridge_dce=" + dce + "\n\n" +
                       "LABEL live\n  KERNEL /KERNEL\n  APPEND boot=UUID=0511-0515 live quiet vga=current i915.hdmi_hdr_gmp=" + gmp + " amdgpu.dvbridge_dce=" + dce + extra + "\n\n" +
                       "LABEL installer\n  KERNEL /KERNEL\n  APPEND boot=UUID=0511-0515 installer quiet systemd.debug_shell vga=current\n";
            return crlf ? t.Replace("\n", "\r\n") : t;
        }

        static string Grub(string timeout, string gmp = "0", string dce = "0")
        {
            Func<string, string, string> entry = delegate(string name, string args)
            {
                return "menuentry \"" + name + "\" --class hades-x {\n\tterminal_output console\n\tlinux /KERNEL " + args + "\n}\n";
            };
            string options = " i915.hdmi_hdr_gmp=" + gmp + " amdgpu.dvbridge_dce=" + dce;
            return "# hades-features: storage-anywhere other-disks-hidden boot-choices\n" + timeout + "set default=\"Run\"\n" +
                   entry("Run", "boot=UUID=0511-0515 grub_portable quiet lockdown=integrity" + options) +
                   entry("Run (internal disks read-only)", "boot=UUID=0511-0515 grub_portable quiet" + options) +
                   entry("Live", "boot=UUID=0511-0515 grub_live quiet" + options) +
                   entry("Installer", "boot=UUID=0511-0515 installer quiet");
        }

        static string Choices(BootChoices c)
        {
            return (c.Read ? "read" : "unread") + "/" + c.Wait + "/" + string.Join(",", c.On.ToArray());
        }

        // A menu as before 1.0 wrote it: no boot-choices in its first line
        static string Unmarked(string menu)
        {
            return menu.Replace(" boot-choices\r\n", "\r\n").Replace(" boot-choices\n", "\n");
        }

        // Menus of before 1.0 and of 1.0: which say that their wait is a choice, and which an update gives the image's wait
        static void BootMarked()
        {
            string s10 = Syslinux("TIMEOUT 100\n", BootMenus.Words(10)), g10 = Grub("set timeout=\"10\"\n");
            BootChoices rc8 = BootMenus.FromText(Unmarked(s10), Unmarked(g10)), chosen10 = BootMenus.FromText(s10, g10);
            BootChoices oneMarked = BootMenus.FromText(s10, Unmarked(g10)), otherMarked = BootMenus.FromText(Unmarked(s10), g10);
            BootChoices rc8Three = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 30\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"3\"\n")));
            BootChoices rc5 = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 50\n", "5 seconds...")), Unmarked(Grub("set timeout=\"5\"\n")));
            BootChoices crlf = BootMenus.FromText(Syslinux("TIMEOUT 100\n", BootMenus.Words(10), "0", "0", true), null);
            BootChoices crlfBefore = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10), "0", "0", true)), null);
            Check("the menus of 1.0 RC8 (no boot-choices) wait 10 s and are not marked; menus of 1.0 at 10 s are; one menu that says it is enough; " +
                  "Windows line ends do not hide it",
                  rc8.Read && rc8.Wait == 10 && !rc8.Marked && chosen10.Marked && chosen10.Wait == 10 && oneMarked.Marked &&
                      otherMarked.Marked && crlf.Marked && !crlfBefore.Marked && crlfBefore.Wait == 10);
            string later = Unmarked(s10).Replace("PROMPT 1\n", "PROMPT 1\n# boot-choices\n");
            string glued = s10.Replace("other-disks-hidden boot-choices\n", "other-disks-hiddenboot-choices\n");
            string notHades = s10.Replace("# hades-features:", "# features:");
            Check("boot-choices counts only as a word of the first line of a menu of Hades",
                  !BootMenus.FromText(later, null).Marked && !BootMenus.FromText(glued, null).Marked && !BootMenus.FromText(notHades, null).Marked);
            Check("an update gives the image's wait only to menus without boot-choices that wait 10 s (not to 3 s or 5 s of before, nor " +
                  "to 10 s chosen in 1.0, nor to menus that could not be read)",
                  BootMenus.Resets(rc8) && BootMenus.Resets(crlfBefore) && !BootMenus.Resets(chosen10) && !BootMenus.Resets(oneMarked) &&
                      !BootMenus.Resets(rc8Three) && rc8Three.Wait == 3 && !BootMenus.Resets(rc5) && rc5.Wait == 5 &&
                      !BootMenus.Resets(BootMenus.FromText(null, null)) && !BootMenus.Resets(null));
        }

        // The text of the menus: what a pair of them says, as the system itself reads it (internal-disks)
        static void BootText()
        {
            string say5 = BootMenus.Words(5), say3 = BootMenus.Words(3);
            Check("the line of the text menu, 13 characters for every wait from 1 to 30, and \"1 second\"",
                  BootMenus.Words(5) == "5 seconds... " && BootMenus.Words(10) == "10 seconds..." && BootMenus.Words(1) == "1 second...  " &&
                      BootMenus.Words(30) == "30 seconds..." && BootMenus.Words(9).Length == 13 && BootMenus.Words(1).Length == 13);
            Action<string, string, string, string> one = delegate(string what, string s, string g, string want)
            {
                string got = Choices(BootMenus.FromText(s, g));
                Check("the menus " + what + ": " + want + (got == want ? "" : " (got " + got + ")"), got == want);
            };
            one("as 1.0 writes them", Syslinux("TIMEOUT 050\n", say5), Grub("set timeout=\"05\"\n"), "read/5/");
            one("at 3 s, both options on", Syslinux("TIMEOUT 030\n", say3, "1", "1"), Grub("set timeout=\"03\"\n", "1", "1"),
                "read/3/i915.hdmi_hdr_gmp,amdgpu.dvbridge_dce");
            one("at 10 s as 1.0 RC8 wrote them", Syslinux("TIMEOUT 100\n", BootMenus.Words(10)), Grub("set timeout=\"10\"\n"), "read/10/");
            one("at 3 s as the setting of 1.0 RC8 wrote them (and the line still at 10)", Syslinux("TIMEOUT 30\n", BootMenus.Words(10)),
                Grub("set timeout=\"3\"\n"), "read/3/");
            one("that wait differently", Syslinux("TIMEOUT 030\n", say3), Grub("set timeout=\"05\"\n"), "read/0/");
            one("at 7 s, by hand", Syslinux("TIMEOUT 070\n", say5), Grub("set timeout=\"07\"\n"), "read/7/");
            one("at 30 s", Syslinux("TIMEOUT 300\n", say5), Grub("set timeout=\"30\"\n"), "read/30/");
            one("at 31 s (more than the setting takes)", Syslinux("TIMEOUT 310\n", say5), Grub("set timeout=\"31\"\n"), "read/0/");
            one("at 0 (for ever in syslinux)", Syslinux("TIMEOUT 0\n", say5), Grub("set timeout=\"0\"\n"), "read/0/");
            one("at 2.5 s", Syslinux("TIMEOUT 25\n", say5), null, "read/0/");
            one("with two lines of the wait", Syslinux("TIMEOUT 050\nTIMEOUT 050\n", say5), Grub("set timeout=\"05\"\n"), "read/0/");
            one("with the block of one start after an update", Syslinux("TIMEOUT 030\n" + BootMenus.OnceBegin + "\nMENU SHIFTKEY\nTIMEOUT 1\n" + BootMenus.OnceEnd + "\n", say3),
                Grub("set timeout=\"03\"\n" + BootMenus.OnceBegin + "\nset timeout_style=\"hidden\"\nset timeout=\"1\"\n" + BootMenus.OnceEnd + "\n"), "read/3/");
            one("with that block cut short", Syslinux("TIMEOUT 030\n" + BootMenus.OnceBegin + "\nTIMEOUT 1\n", say3), Grub("set timeout=\"03\"\n"), "unread/0/");
            one("with Windows line ends", Syslinux("TIMEOUT 030\n", say3, "1", "0", true), Grub("set timeout=\"03\"\n", "1", "0").Replace("\n", "\r\n"),
                "read/3/i915.hdmi_hdr_gmp");
            one("of BIOS only", Syslinux("TIMEOUT 010\n", BootMenus.Words(1), "0", "1"), null, "read/1/amdgpu.dvbridge_dce");
            one("of UEFI only", null, Grub("set timeout=\"10\"\n", "1", "0"), "read/10/i915.hdmi_hdr_gmp");
            one("an option on in one menu only", Syslinux("TIMEOUT 050\n", say5, "1"), Grub("set timeout=\"05\"\n", "0"), "read/5/");
            one("an option with the word twice", Syslinux("TIMEOUT 050\n", say5, "1", "0", false, " i915.hdmi_hdr_gmp=1"),
                Grub("set timeout=\"05\"\n", "1"), "read/5/");
            one("an option with another value", Syslinux("TIMEOUT 050\n", say5, "2"), Grub("set timeout=\"05\"\n", "2"), "read/5/");
            one("none at all", null, null, "unread/0/");
            // (the installer entry does not count: on there alone is nothing)
            string onlyInstaller = Syslinux("TIMEOUT 050\n", say5).Replace("installer quiet systemd.debug_shell vga=current", "installer quiet i915.hdmi_hdr_gmp=1");
            one("an option on in the installer entry only", onlyInstaller, null, "read/5/");
        }

        static void Put(byte[] img, long at, string text)
        {
            byte[] b = Encoding.ASCII.GetBytes(text);
            Buffer.BlockCopy(b, 0, img, (int)at, b.Length);
        }

        // A made-up image with the lines of the choices in its system partition, one of them across a chunk of the scan
        // (they start at 1 MiB and are 4 MiB long), as an image of 1.0 has them at 5 s with the options off. feature: it
        // says boot-choices; words: how many places carry the two option words (6 in an image of 1.0); timeouts: how many
        // TIMEOUT lines.
        static byte[] BootImage(bool feature, int words, int timeouts, out long[] wait, out long[] gmp)
        {
            long[] unused;
            byte[] img = MakeImage(4, true, out unused, 4, true, 1);
            long sy = P1 * S + 40000;
            string syslinux = "\n" + (feature ? BootMenus.Feature : "# hades-features: storage-anywhere other-disks-hidden\n") +
                              "SAY Wait for run mode to start automatically in " + BootMenus.Words(5) + "\nDEFAULT run\n";
            for (int i = 0; i < timeouts; ++i) syslinux += "TIMEOUT 050\n";
            syslinux += "PROMPT 1\n";
            Put(img, sy, syslinux);
            // (GRUB's line across the chunk that ends at 17 MiB)
            long gr = 17L * 1048576 - 8;
            Put(img, gr, "\nset timeout=\"05\"\n");
            long[] places = { P1 * S + 50000, P1 * S + 51000, P1 * S + 52000, 21L * 1048576 - 15, 30L * 1048576 + 500, 40L * 1048576, 50L * 1048576 };
            gmp = new long[words];
            for (int i = 0; i < words; ++i)
            {
                Put(img, places[i], " quiet i915.hdmi_hdr_gmp=0 amdgpu.dvbridge_dce=0\n");
                gmp[i] = places[i] + " quiet i915.hdmi_hdr_gmp=".Length;
            }
            wait = new long[] { sy + syslinux.IndexOf("TIMEOUT ") + 8, gr + 14,
                                sy + syslinux.IndexOf("automatically in ") + "automatically in ".Length };
            return img;
        }

        // What the scan finds, and what an update writes with the choices of the old menus
        static void BootPatches(string directory)
        {
            string raw = Path.Combine(directory, "boot.img"), packed = raw + ".gz";
            long[] wait, gmp;
            byte[] img = BootImage(true, 6, 1, out wait, out gmp);
            File.WriteAllBytes(raw, img);
            using (var f = File.Create(packed))
            using (var gz = new System.IO.Compression.GZipStream(f, System.IO.Compression.CompressionMode.Compress))
                gz.Write(img, 0, img.Length);
            foreach (string path in new[] { raw, packed })
            {
                string kind = Engine.IsGzip(path) ? "compressed" : "uncompressed";
                ImagePlan plan = Engine.Plan(path, true, null, null);
                bool places = plan.WaitOffsets[0].Count == 1 && plan.WaitOffsets[0][0] == wait[0] && plan.WaitOffsets[1].Count == 1 &&
                              plan.WaitOffsets[1][0] == wait[1] && plan.WaitOffsets[2].Count == 1 && plan.WaitOffsets[2][0] == wait[2];
                bool words = plan.OptionOffsets[0].Count == 6 && plan.OptionOffsets[1].Count == 6;
                for (int i = 0; words && i < 6; ++i) words &= plan.OptionOffsets[0][i] == gmp[i];
                Check(kind + " image of 1.0: it says boot-choices, waits 5 s, and the three lines of the wait and the six words of each " +
                      "option are found where they are, the one across a chunk of the scan once",
                      plan.HasBootChoices && plan.BootWait == 5 && places && words);

                var old = BootMenus.FromText(Syslinux("TIMEOUT 030\n", BootMenus.Words(3), "1", "0"), Grub("set timeout=\"03\"\n", "1", "0"));
                List<Patch> patches = BootMenus.Keep(plan, old);
                var target = new ProbeStream(Disk(P1Size * S));
                var part = new Engine.Target();
                part.Stream = target; part.Capacity = P1Size * S; part.From = plan.System.Start; part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part }, patches, null, null);
                var sys = new byte[P1Size * S];
                target.Position = 0;
                target.Read(sys, 0, sys.Length);
                long differ = 0;
                for (int i = 0; i < sys.Length; ++i) differ += sys[i] != img[P1 * S + i] ? 1 : 0;
                Func<long, int, string> at = delegate(long where, int n) { return Encoding.ASCII.GetString(sys, (int)(where - P1 * S), n); };
                bool on = true, offStay = true;
                foreach (long w in plan.OptionOffsets[0]) on &= at(w, 1) == "1";
                foreach (long w in plan.OptionOffsets[1]) offStay &= at(w, 1) == "0";
                Check(kind + " image of 1.0, an update from menus at 3 s with the Intel option on: the three lines say 3, the six words " +
                      "of that option say 1, those of the other 0, and nothing else differs (" + differ + " bytes, " + patches.Count + " patches)",
                      part.Result.Verified && patches.Count == 9 && at(wait[0], 3) == "030" && at(wait[1], 2) == "03" &&
                          at(wait[2], 13) == BootMenus.Words(3) && on && offStay && differ == 9);

                var ten = BootMenus.FromText(Syslinux("TIMEOUT 100\n", BootMenus.Words(10)), Grub("set timeout=\"10\"\n"));
                List<Patch> tenPatches = BootMenus.Keep(plan, ten);
                var same = BootMenus.FromText(Syslinux("TIMEOUT 050\n", BootMenus.Words(5)), Grub("set timeout=\"05\"\n"));
                var unread = BootMenus.FromText(null, null);
                var mixed = BootMenus.FromText(Syslinux("TIMEOUT 030\n", BootMenus.Words(3)), Grub("set timeout=\"05\"\n"));
                bool tenRight = tenPatches.Count == 3;
                foreach (Patch p in tenPatches)
                {
                    string t = Encoding.ASCII.GetString(p.Bytes);
                    tenRight &= (p.Offset == wait[0] && t == "100") || (p.Offset == wait[1] && t == "10") || (p.Offset == wait[2] && t == "10 seconds...");
                }
                Check(kind + " image of 1.0: an update from menus of 1.0 that wait 10 s (a choice) writes 10 s in the three lines; one from " +
                      "menus that wait as the image does, from none, or from menus that wait differently, changes nothing",
                      tenRight && BootMenus.Keep(plan, same).Count == 0 && BootMenus.Keep(plan, unread).Count == 0 &&
                          BootMenus.Keep(plan, mixed).Count == 0 && BootMenus.Keep(plan, null).Count == 0);
                // menus of before 1.0: 10 s, which they had by themselves, is not carried (the image's 5 s stay); 3 s is; the
                // options are carried either way
                var rc8 = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"10\"\n")));
                var rc8On = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10), "1", "0")), Unmarked(Grub("set timeout=\"10\"\n", "1", "0")));
                var rc8Three = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 30\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"3\"\n")));
                List<Patch> rc8OnPatches = BootMenus.Keep(plan, rc8On), rc8ThreePatches = BootMenus.Keep(plan, rc8Three);
                bool onlyOptions = rc8OnPatches.Count == 6;
                foreach (Patch p in rc8OnPatches) onlyOptions &= plan.OptionOffsets[0].Contains(p.Offset) && Encoding.ASCII.GetString(p.Bytes) == "1";
                bool three = rc8ThreePatches.Count == 3;
                foreach (Patch p in rc8ThreePatches)
                {
                    string t = Encoding.ASCII.GetString(p.Bytes);
                    three &= (p.Offset == wait[0] && t == "030") || (p.Offset == wait[1] && t == "03") || (p.Offset == wait[2] && t == BootMenus.Words(3));
                }
                Check(kind + " image of 1.0: an update from menus of 1.0 RC8 that wait 10 s (no boot-choices) leaves the image's 5 s (no patch; " +
                      "with an option on, only its six words); from such menus at 3 s, 3 s is carried",
                      BootMenus.Keep(plan, rc8).Count == 0 && onlyOptions && three);
                // (1 s: the line says "1 second...  ", with its own padding, in all of its 13 bytes)
                var oneSecond = BootMenus.FromText(Syslinux("TIMEOUT 010\n", BootMenus.Words(1)), Grub("set timeout=\"01\"\n"));
                var oneTarget = new ProbeStream(Disk(P1Size * S));
                var onePart = new Engine.Target();
                onePart.Stream = oneTarget; onePart.Capacity = P1Size * S; onePart.From = plan.System.Start; onePart.Length = plan.System.Size;
                Engine.Write(plan, new[] { onePart }, BootMenus.Keep(plan, oneSecond), null, null);
                var one = new byte[P1Size * S];
                oneTarget.Position = 0;
                oneTarget.Read(one, 0, one.Length);
                Func<long, int, string> oneAt = delegate(long where, int n) { return Encoding.ASCII.GetString(one, (int)(where - P1 * S), n); };
                Check(kind + " image of 1.0: an update from menus at 1 s writes \"010\", \"01\" and \"1 second...  \", the line of the text " +
                      "menu whole", onePart.Result.Verified && oneAt(wait[0], 3) == "010" && oneAt(wait[1], 2) == "01" &&
                          oneAt(wait[2], 14) == "1 second...  \n");

                // every way of writing: the choices are carried by an update only, and with the other changes
                const string there = "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0";
                List<Patch> update = Engine.Patches(plan, true, false, there, true, old);
                bool apart = true;
                for (int i = 0; i < update.Count; ++i)
                    for (int j = i + 1; j < update.Count; ++j)
                        apart &= update[i].Offset + update[i].Bytes.Length <= update[j].Offset || update[j].Offset + update[j].Bytes.Length <= update[i].Offset;
                Check(kind + " image of 1.0: an update for an internal disk with the diagnostics on and the old choices makes the changes " +
                      "of each (4 names of the storage, 4 entries, the switch, 9 of the choices) and none of them overlaps another",
                      update.Count == 4 + 4 + 1 + 9 && apart && Engine.Patches(plan, true, false, there, true).Count == 9 &&
                          Engine.Patches(plan, false, true, null, false, old).Count == 9);
            }

            // images whose lines are not the ones this installer knows, and older images
            Func<bool, int, int, ImagePlan> plan2 = delegate(bool feature, int words, int timeouts)
            {
                long[] w, g;
                File.WriteAllBytes(raw, BootImage(feature, words, timeouts, out w, out g));
                return Engine.Plan(raw, true, null, null);
            };
            var choices = BootMenus.FromText(Syslinux("TIMEOUT 030\n", BootMenus.Words(3), "1", "1"), Grub("set timeout=\"03\"\n", "1", "1"));
            ImagePlan fiveWords = plan2(true, 5, 1), twoTimeouts = plan2(true, 6, 2), older = plan2(false, 6, 1), seven = plan2(true, 7, 1);
            Check("an image that says boot-choices with 5 or 7 words of each option, or with two TIMEOUT lines, is not one this installer " +
                  "updates; one that does not say it writes its own menus (no patch), and its wait is still told",
                  Throws(delegate { BootMenus.Keep(fiveWords, choices); }) && Throws(delegate { BootMenus.Keep(seven, choices); }) &&
                      Throws(delegate { BootMenus.Keep(twoTimeouts, choices); }) && !older.HasBootChoices &&
                      BootMenus.Keep(older, choices).Count == 0 && older.BootWait == 5 && twoTimeouts.BootWait == 0);
            // the words of the panel
            bool spanish = L.Spanish;
            L.Spanish = false;
            var k = new BootKept();
            k.Old = choices; k.ImageKnows = true; k.ImageWait = 5; k.Patches = 15;
            string carried = k.Text();
            k.ImageKnows = false;
            string notCarried = k.Text();
            k.Old = BootMenus.FromText(Syslinux("TIMEOUT 050\n", BootMenus.Words(5)), Grub("set timeout=\"05\"\n"));
            string nothing = k.Text();
            // menus of 1.0 RC8 (10 s by themselves): the new wait, and where it is changed
            k.ImageKnows = true;
            k.Old = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"10\"\n")));
            string reset = k.Text(), resetLog = k.Summary();
            k.Old = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10), "1", "0")), Unmarked(Grub("set timeout=\"10\"\n", "1", "0")));
            string resetOn = k.Text();
            k.Old = BootMenus.FromText(Syslinux("TIMEOUT 100\n", BootMenus.Words(10)), Grub("set timeout=\"10\"\n"));
            string chosen10 = k.Text();
            k.ImageKnows = false; k.ImageWait = 10;
            k.Old = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"10\"\n")));
            string olderImage = k.Text();
            k.ImageWait = 5;
            L.Spanish = true;
            k.Old = choices; k.ImageKnows = true;
            string carriedEs = k.Text();
            k.Old = BootMenus.FromText(Unmarked(Syslinux("TIMEOUT 100\n", BootMenus.Words(10))), Unmarked(Grub("set timeout=\"10\"\n")));
            string resetEs = k.Text();
            L.Spanish = spanish;
            Check("the last panel of an update: what was carried, what an older image does instead, and nothing where the old menus " +
                  "were as the image (\"" + carried + "\")",
                  carried == "The boot menu waits 3 seconds and starts with HDR over HDMI on the Intel graphics and with Dolby Vision on the Radeon, as before the update." &&
                      notCarried.StartsWith("This image writes its own boot menus") && nothing == "" &&
                      carriedEs.StartsWith("El menú de arranque espera 3 segundos y arranca con HDR"));
            Check("the last panel, menus of 1.0 RC8 at 10 s: the new wait and where it is changed, with the options that were carried after it; " +
                  "nothing for 10 s chosen in 1.0, nor for an image that waits 10 s by itself (\"" + reset + "\")",
                  reset == "The boot menu now waits 5 seconds, the new default (it waited 10). It can be changed in Settings > LibreELEC > System > Boot menu." &&
                      resetOn == reset + " The boot menu starts with HDR over HDMI on the Intel graphics, as before the update." &&
                      resetEs == "El menú de arranque espera ahora 5 segundos, el nuevo valor por defecto (esperaba 10). Se puede cambiar en Ajustes > LibreELEC > Sistema > Menú de arranque." &&
                      chosen10.StartsWith("The boot menu waits 10 seconds, as before") && olderImage == "" &&
                      resetLog.Contains("the old wait, 10 s by itself before 1.0, is not carried: the image's 5 s stay"));
            File.Delete(raw);
            File.Delete(packed);
        }

        // A real image: its system partition, written as an install writes it (sys), read through its FAT as an update
        // reads the old one; then the same partition with the choices that a person made (same-length changes, as the
        // system makes them), and an update from it.
        static void BootReal(string directory, ImagePlan plan, string sys)
        {
            if (!plan.HasBootChoices)
            {
                Say("NOT RUN: this image does not say boot-choices (the choices of its boot menus are not carried by an update).");
                ++notRun;
                return;
            }
            Say("      the image's own wait: " + plan.BootWait + " s; option words: " + plan.OptionOffsets[0].Count + " and " + plan.OptionOffsets[1].Count);
            BootChoices own;
            using (var f = new FileStream(sys, FileMode.Open, FileAccess.Read)) own = BootMenus.Read(DiagFolder.Reader(f, f.Length), f.Length);
            Say("      read from its FAT: " + own.Summary());
            Check("real image: its menus, read through the FAT of the system partition as an update reads the old one, wait " + plan.BootWait +
                  " s with no option on", own.Read && own.Wait == plan.BootWait && own.Wait > 0 && own.On.Count == 0);

            // the old partition with a person's choices: 3 s, HDR over HDMI on the Intel graphics
            string old = Path.Combine(directory, "real-sistema-elegido.bin");
            File.Copy(sys, old, true);
            int changed = 0;
            using (var o = new FileStream(old, FileMode.Open, FileAccess.ReadWrite))
            {
                Action<long, string> put = delegate(long where, string text)
                {
                    o.Seek(where - plan.System.Start, SeekOrigin.Begin);
                    byte[] b = Encoding.ASCII.GetBytes(text);
                    o.Write(b, 0, b.Length);
                    ++changed;
                };
                put(plan.WaitOffsets[0][0], "030");
                put(plan.WaitOffsets[1][0], "03");
                put(plan.WaitOffsets[2][0], BootMenus.Words(3));
                foreach (long w in plan.OptionOffsets[0]) put(w, "1");
            }
            BootChoices chosen;
            using (var f = new FileStream(old, FileMode.Open, FileAccess.Read)) chosen = BootMenus.Read(DiagFolder.Reader(f, f.Length), f.Length);
            Check("... with a person's choices put in (" + changed + " places): read as 3 s with HDR over HDMI on the Intel graphics",
                  chosen.Read && chosen.Wait == 3 && chosen.On.Count == 1 && chosen.On[0] == BootMenus.Options[0]);

            // an update from it, and one from the partition of the image itself
            const string there = "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0";
            string with = Path.Combine(directory, "real-sistema-actualizado-elegido.bin"), without = Path.Combine(directory, "real-sistema-actualizado-sin.bin");
            bool verified = true;
            foreach (string file in new[] { with, without })
                using (var f = new FileStream(file, FileMode.Create, FileAccess.ReadWrite))
                {
                    var part = new Engine.Target();
                    part.Stream = f; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
                    Engine.Write(plan, new[] { part }, Engine.Patches(plan, true, false, there, false, file == with ? chosen : own), null, null);
                    verified &= part.Result.Verified;
                }
            BootChoices after;
            using (var f = new FileStream(with, FileMode.Open, FileAccess.Read)) after = BootMenus.Read(DiagFolder.Reader(f, f.Length), f.Length);
            long differ = 0;
            using (var a = new FileStream(with, FileMode.Open, FileAccess.Read))
            using (var b = new FileStream(without, FileMode.Open, FileAccess.Read))
            {
                var x = new byte[1 << 20];
                var y = new byte[1 << 20];
                for (long done = 0; done < plan.System.Size; done += x.Length)
                {
                    int want = (int)Math.Min(x.Length, plan.System.Size - done);
                    a.Read(x, 0, want);
                    b.Read(y, 0, want);
                    for (int i = 0; i < want; ++i) differ += x[i] != y[i] ? 1 : 0;
                }
            }
            Say("      bytes that the choices change in the update: " + differ);
            Check("real image, update: the new menus, read back through the FAT, wait 3 s with HDR over HDMI on the Intel graphics, " +
                  "and they differ from an update without the choices in 9 bytes (3 of the wait, 6 of the option)",
                  verified && after.Read && after.Wait == 3 && after.On.Count == 1 && after.On[0] == BootMenus.Options[0] && differ == 9);

            // the old partition as before 1.0 had it: 10 s, and no boot-choices in the first line of either menu (the word
            // blanked out, the length kept); an update from it leaves the image's own wait
            string before = Path.Combine(directory, "real-sistema-antes-de-1.0.bin");
            File.Copy(sys, before, true);
            int blanked = 0;
            using (var o = new FileStream(before, FileMode.Open, FileAccess.ReadWrite))
            {
                Action<long, string> put = delegate(long where, string text)
                {
                    o.Seek(where - plan.System.Start, SeekOrigin.Begin);
                    byte[] b = Encoding.ASCII.GetBytes(text);
                    o.Write(b, 0, b.Length);
                };
                put(plan.WaitOffsets[0][0], "100");
                put(plan.WaitOffsets[1][0], "10");
                put(plan.WaitOffsets[2][0], BootMenus.Words(10));
                // (the first line of each menu is within 4 KB before its line of the wait)
                foreach (long at in new[] { plan.WaitOffsets[2][0], plan.WaitOffsets[1][0] })
                {
                    long from = Math.Max(plan.System.Start, at - 4096);
                    var window = new byte[at - from];
                    o.Seek(from - plan.System.Start, SeekOrigin.Begin);
                    o.Read(window, 0, window.Length);
                    int i = Encoding.ASCII.GetString(window).LastIndexOf(" boot-choices\n", StringComparison.Ordinal);
                    if (i < 0) continue;
                    put(from + i, new string(' ', " boot-choices".Length));
                    ++blanked;
                }
            }
            BootChoices rc8;
            using (var f = new FileStream(before, FileMode.Open, FileAccess.Read)) rc8 = BootMenus.Read(DiagFolder.Reader(f, f.Length), f.Length);
            Check("... as before 1.0 (10 s, the word blanked out in " + blanked + " menus): read as 10 s without boot-choices",
                  blanked == 2 && rc8.Read && rc8.Wait == 10 && !rc8.Marked && BootMenus.Resets(rc8));
            string updated = Path.Combine(directory, "real-sistema-actualizado-antes.bin");
            bool verified2;
            using (var f = new FileStream(updated, FileMode.Create, FileAccess.ReadWrite))
            {
                var part = new Engine.Target();
                part.Stream = f; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part }, Engine.Patches(plan, true, false, there, false, rc8), null, null);
                verified2 = part.Result.Verified;
            }
            BootChoices after2;
            using (var f = new FileStream(updated, FileMode.Open, FileAccess.Read)) after2 = BootMenus.Read(DiagFolder.Reader(f, f.Length), f.Length);
            long differ2 = 0;
            using (var a = new FileStream(updated, FileMode.Open, FileAccess.Read))
            using (var b = new FileStream(without, FileMode.Open, FileAccess.Read))
            {
                var x = new byte[1 << 20];
                var y = new byte[1 << 20];
                for (long done = 0; done < plan.System.Size; done += x.Length)
                {
                    int want = (int)Math.Min(x.Length, plan.System.Size - done);
                    a.Read(x, 0, want);
                    b.Read(y, 0, want);
                    for (int i = 0; i < want; ++i) differ2 += x[i] != y[i] ? 1 : 0;
                }
            }
            var kept = new BootKept();
            kept.Old = rc8; kept.ImageKnows = plan.HasBootChoices; kept.ImageWait = plan.BootWait;
            kept.Patches = BootMenus.Keep(plan, rc8).Count;
            Say("      " + kept.Summary());
            bool spanish = L.Spanish;
            L.Spanish = false;
            string panel = kept.Text();
            L.Spanish = spanish;
            Check("real image, update from menus of before 1.0 at 10 s: the new menus wait the image's " + plan.BootWait + " s and say " +
                  "boot-choices, the same bytes as an update without choices, and the panel says so (\"" + panel + "\")",
                  verified2 && after2.Read && after2.Wait == plan.BootWait && after2.Marked && differ2 == 0 && kept.Patches == 0 &&
                      panel.StartsWith("The boot menu now waits " + plan.BootWait + " seconds, the new default (it waited 10)."));
            File.Delete(old);
            File.Delete(with);
            File.Delete(without);
            File.Delete(before);
            File.Delete(updated);
        }
    }
}
