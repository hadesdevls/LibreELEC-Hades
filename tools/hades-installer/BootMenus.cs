// LibreELEC Hades installer for Windows: the choices of the start that the boot menus keep, carried across an update.
// The boot menus of the system partition (syslinux.cfg for BIOS, EFI/BOOT/grub.cfg for UEFI) are the one place where
// Hades keeps how long they wait (LibreELEC settings, System, Boot menu) and the kernel options that Kodi's display
// settings turn on (i915.hdmi_hdr_gmp, amdgpu.dvbridge_dce); the system changes them in place (internal-disks). An
// update replaces the system partition whole, menus included: so the old menus are read before the partition is written
// over, and what they say goes into the new one. An image whose menus say "boot-choices" in their first line writes
// each choice in a form whose length does not depend on its value (scripts/mkimage): the wait with a fixed number of
// digits and the line of the BIOS text menu that says it padded with blanks, each option as NAME=0 or NAME=1; so the
// choices go in as patches of the same length, as the other changes of an update do.
// The word also says that the wait of those menus is a choice: since 1.0 an update keeps it whatever it is, 10 s as well.
// Menus without it are of before 1.0, which waited 10 s by themselves, and nearly no one chose that: an update gives such
// menus the image's own wait (5 s since 1.0) and keeps any other, as an update in place does with the menus themselves
// (usr/share/bootloader/update.sh, wait_update).
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Hades.Installer
{
    // What a pair of boot menus says of the start.
    sealed class BootChoices
    {
        public int Wait;                                  // seconds, 1 to 30; 0: it cannot be told
        public List<string> On = new List<string>();      // the kernel options on in every entry that starts the system
        public bool Read;                                 // the menus were read (one of them at least)
        public bool Marked;                               // the first line of one of them says boot-choices (1.0 or later)
        public string Detail = "";                        // for the log, in English

        public string Summary()
        {
            return (Read ? "wait " + (Wait > 0 ? Wait + " s" : "not told") + (Marked ? " (boot-choices)" : " (no boot-choices)") +
                           ", options on: " + (On.Count > 0 ? string.Join(" ", On.ToArray()) : "none") : "not read") +
                   (Detail.Length > 0 ? " (" + Detail + ")" : "");
        }
    }

    static class BootMenus
    {
        // The word of the first line of the menus that says the choices are written so (scripts/mkimage)
        public const string Feature = "# hades-features: storage-anywhere other-disks-hidden boot-choices\n";
        public const string FeatureWord = "boot-choices";
        // The wait that the menus of before 1.0 had by themselves (1.0 RC6 to 1.0 RC8)
        public const int WaitBefore = 10;
        // The kernel options, as on the kernel line (the order of scripts/mkimage)
        public static readonly string[] Options = { "i915.hdmi_hdr_gmp", "amdgpu.dvbridge_dce" };
        // The lines of the wait, up to their value; the value has a fixed length: three digits, two, and the end of the
        // line that says it, 13 characters ("10 seconds...", "5 seconds... ", "1 second...  ")
        public const string TimeoutLine = "\nTIMEOUT ", GrubLine = "\nset timeout=\"", SayLine = "\nSAY Wait for run mode to start automatically in ";
        public const int TimeoutDigits = 3, GrubDigits = 2, SayWords = 13;
        // The entries that start the system for use, whose kernel lines carry the options
        static readonly string[] SyslinuxEntries = { "run", "internal", "live" };
        static readonly string[] GrubEntries = { "Run", "Run (internal disks read-only)", "Live" };
        public const string OnceBegin = "# hades-once-begin", OnceEnd = "# hades-once-end";

        // The end of the line of the BIOS text menu for a wait of so many seconds
        public static string Words(int seconds)
        {
            return (seconds == 1 ? "1 second..." : seconds + " seconds...").PadRight(SayWords);
        }

        // ---- reading

        // The choices of the menus of a system partition, read through its FAT16. Never throws: what cannot be read is
        // in Detail, with Read false.
        public static BootChoices Read(Func<long, int, byte[]> read, long length)
        {
            try
            {
                string why;
                Fat16 fat = Fat16.Open(read, length, out why);
                if (fat == null)
                {
                    var none = new BootChoices();
                    none.Detail = "the old partition: " + why;
                    return none;
                }
                string syslinux = FileText(fat, fat.Root, "syslinux.cfg");
                string grub = null;
                DiagItem efi = Item(fat.Root, "EFI", true);
                byte[] efiDir = efi == null ? null : Folder(fat, efi);
                DiagItem boot = efiDir == null ? null : Item(efiDir, "BOOT", true);
                byte[] bootDir = boot == null ? null : Folder(fat, boot);
                if (bootDir != null) grub = FileText(fat, bootDir, "grub.cfg");
                return FromText(syslinux, grub);
            }
            catch (Exception e)
            {
                var failed = new BootChoices();
                failed.Detail = "the old menus could not be read: " + e.Message;
                return failed;
            }
        }

        static DiagItem Item(byte[] dir, string name, bool folder)
        {
            foreach (DiagItem item in DiagFolder.Entries(dir))
                if (item.Folder == folder && string.Equals(item.Name, name, StringComparison.OrdinalIgnoreCase)) return item;
            return null;
        }

        // The clusters of a folder, as one directory; null where its chain is broken
        static byte[] Folder(Fat16 fat, DiagItem folder)
        {
            List<int> chain = fat.Chain(folder.First, 256);
            if (chain == null) return null;
            var data = new byte[chain.Count * fat.ClusterBytes];
            for (int i = 0; i < chain.Count; ++i) Buffer.BlockCopy(fat.Cluster(chain[i]), 0, data, i * fat.ClusterBytes, fat.ClusterBytes);
            return data;
        }

        // The text of a file of a directory (a menu is a few KB: at most 256 KB are read); null where it is not there
        static string FileText(Fat16 fat, byte[] dir, string name)
        {
            DiagItem item = Item(dir, name, false);
            if (item == null) return null;
            long size = item.Size;
            if (size > 256 * 1024) throw new InvalidDataException(name + " is too large to be a boot menu");
            if (size == 0) return "";
            int needed = (int)((size + fat.ClusterBytes - 1) / fat.ClusterBytes);
            List<int> chain = fat.Chain(item.First, needed);
            if (chain == null || chain.Count != needed) throw new InvalidDataException(name + ": its chain of clusters is broken");
            var data = new byte[size];
            for (int i = 0; i < chain.Count; ++i)
            {
                byte[] c = fat.Cluster(chain[i]);
                int count = (int)Math.Min(fat.ClusterBytes, size - (long)i * fat.ClusterBytes);
                Buffer.BlockCopy(c, 0, data, i * fat.ClusterBytes, count);
            }
            return Encoding.ASCII.GetString(data);
        }

        // The lines of a menu, without their line ends, and without a block that an update put in for one start
        // (usr/share/bootloader/update.sh; the system takes it out at its next start). Null where a block does not end.
        static List<string> Lines(string text)
        {
            var lines = new List<string>();
            bool skip = false;
            foreach (string raw in text.Split('\n'))
            {
                string l = raw.EndsWith("\r") ? raw.Substring(0, raw.Length - 1) : raw;
                if (!skip && l == OnceBegin) { skip = true; continue; }
                if (skip && l == OnceEnd) { skip = false; continue; }
                if (!skip) lines.Add(l);
            }
            return skip ? null : lines;
        }

        // The number of a line that is the prefix and a number (leading zeros allowed), and only that; -1 otherwise
        static int Number(string line, string prefix, string suffix)
        {
            if (!line.StartsWith(prefix, StringComparison.Ordinal) || !line.EndsWith(suffix, StringComparison.Ordinal)) return -1;
            string digits = line.Substring(prefix.Length, line.Length - prefix.Length - suffix.Length);
            if (digits.Length == 0 || digits.Length > 6) return -1;
            foreach (char c in digits) if (c < '0' || c > '9') return -1;
            return int.Parse(digits);
        }

        // The wait of a menu in seconds: from its one line that says it; 0 where there is not exactly one, or it is not a
        // whole number of seconds from 1 to 30 (what internal-disks writes)
        static int WaitOf(List<string> lines, bool grub)
        {
            int found = 0, value = -1;
            foreach (string l in lines)
            {
                int n = grub ? Number(l, "set timeout=\"", "\"") : Number(l, "TIMEOUT ", "");
                if (n < 0) continue;
                ++found;
                value = n;
            }
            if (found != 1) return 0;
            if (!grub)
            {
                if (value % 10 != 0) return 0;
                value /= 10;
            }
            return value >= 1 && value <= 30 ? value : 0;
        }

        // Whether the first line of a menu is the one of a menu of Hades and has the word boot-choices
        static bool SaysChoices(List<string> lines)
        {
            if (lines.Count == 0 || !lines[0].StartsWith("# hades-features:", StringComparison.Ordinal)) return false;
            return Array.IndexOf(lines[0].Split(' '), FeatureWord) > 0;
        }

        // Whether an update gives the menus the image's own wait in place of theirs: menus of before 1.0 (no boot-choices)
        // that wait what those menus had by themselves
        public static bool Resets(BootChoices choices)
        {
            return choices != null && choices.Read && !choices.Marked && choices.Wait == WaitBefore;
        }

        // The kernel lines of the entries that start the system for use
        static List<string> KernelLines(List<string> lines, bool grub)
        {
            var found = new List<string>();
            string entry = null;
            foreach (string l in lines)
            {
                if (!grub && l.StartsWith("LABEL ", StringComparison.Ordinal)) { entry = l.Substring(6); continue; }
                if (grub && l.StartsWith("menuentry \"", StringComparison.Ordinal))
                {
                    int end = l.IndexOf('"', 11);
                    entry = end > 11 ? l.Substring(11, end - 11) : null;
                    continue;
                }
                if (grub && l == "}") { entry = null; continue; }
                if (entry == null || Array.IndexOf(grub ? GrubEntries : SyslinuxEntries, entry) < 0) continue;
                string t = l.TrimStart(' ', '\t');
                if (t.StartsWith(grub ? "linux " : "APPEND ", StringComparison.Ordinal)) found.Add(t);
            }
            return found;
        }

        // Whether every one of those lines has the option on: its word once, NAME=1
        static bool OptionOn(List<string> kernelLines, string option)
        {
            if (kernelLines.Count == 0) return false;
            foreach (string l in kernelLines)
            {
                int words = 0, on = 0;
                foreach (string w in l.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    if (!w.StartsWith(option + "=", StringComparison.Ordinal)) continue;
                    ++words;
                    if (w == option + "=1") ++on;
                }
                if (words != 1 || on != 1) return false;
            }
            return true;
        }

        // The choices of two menus (null: that menu is not there). Where both say a wait, they must say the same; an
        // option is on only where every entry of both says so.
        internal static BootChoices FromText(string syslinux, string grub)
        {
            var c = new BootChoices();
            List<string> s = syslinux == null ? null : Lines(syslinux), g = grub == null ? null : Lines(grub);
            if ((syslinux != null && s == null) || (grub != null && g == null))
            {
                c.Detail = "a menu has the lines of an update that do not end";
                return c;
            }
            if (s == null && g == null)
            {
                c.Detail = "no boot menu";
                return c;
            }
            c.Read = true;
            c.Marked = (s != null && SaysChoices(s)) || (g != null && SaysChoices(g));
            int ws = s == null ? -1 : WaitOf(s, false), wg = g == null ? -1 : WaitOf(g, true);
            if (ws == 0 || wg == 0) c.Detail = "a menu says its wait in a way that this installer does not take";
            else if (ws > 0 && wg > 0 && ws != wg) c.Detail = "the two menus wait differently (" + ws + " s and " + wg + " s)";
            else c.Wait = ws > 0 ? ws : wg;
            var lines = new List<string>();
            if (s != null) lines.AddRange(KernelLines(s, false));
            if (g != null) lines.AddRange(KernelLines(g, true));
            foreach (string option in Options)
                if (OptionOn(lines, option)) c.On.Add(option);
            return c;
        }

        // ---- writing

        // The patches that put the choices into the image (an update; null: none to carry). Only for an image that says
        // boot-choices: one that does not writes its own menus, as before. Bytes that the image has already are not
        // patched. An image that says it and does not have the lines exactly is not one this installer knows. The wait
        // of menus of before 1.0 that wait 10 s is not carried (Resets): the image's own stays.
        public static List<Patch> Keep(ImagePlan plan, BootChoices choices)
        {
            var patches = new List<Patch>();
            if (choices == null || !choices.Read || !plan.HasBootChoices) return patches;
            if (!plan.Scanned) throw new InvalidOperationException("The image was not scanned.");
            bool known = plan.WaitOffsets[0].Count == 1 && plan.WaitOffsets[1].Count == 1 && plan.WaitOffsets[2].Count == 1;
            foreach (List<long> o in plan.OptionOffsets) known &= o.Count == 6;
            if (!known)
                throw new InvalidDataException(L.T(
                    "The boot menus of this image are not the ones this installer knows (the lines of their wait and options). " +
                    "It cannot be used to update.",
                    "Los menús de arranque de esta imagen no son los que este instalador conoce (las líneas de su espera y de sus " +
                    "opciones). No se puede actualizar con ella."));
            if (choices.Wait > 0 && choices.Wait != plan.BootWait && !Resets(choices))
            {
                patches.Add(Make(plan.WaitOffsets[0][0], (choices.Wait * 10).ToString("D3")));
                patches.Add(Make(plan.WaitOffsets[1][0], choices.Wait.ToString("D2")));
                patches.Add(Make(plan.WaitOffsets[2][0], Words(choices.Wait)));
            }
            for (int i = 0; i < Options.Length; ++i)
                if (choices.On.Contains(Options[i]))
                    foreach (long at in plan.OptionOffsets[i]) patches.Add(Make(at, "1"));
            return patches;
        }

        static Patch Make(long offset, string text)
        {
            var p = new Patch();
            p.Offset = offset;
            p.Bytes = Encoding.ASCII.GetBytes(text);
            return p;
        }

        // ---- what the image has (Engine.Plan): the value of each fixed-width line where it is whole in the buffer

        // The places of the values: of the three wait lines, and of the 0 of each option word (each 0 or 1)
        internal static void Scan(ImagePlan plan, byte[] buffer, int have, long bufferStart)
        {
            string[] heads = { TimeoutLine, GrubLine, SayLine };
            int[] lengths = { TimeoutDigits, GrubDigits, SayWords };
            for (int k = 0; k < 3; ++k)
            {
                byte[] head = Encoding.ASCII.GetBytes(heads[k]);
                foreach (int at in Engine.Find(buffer, have, head))
                {
                    int value = at + head.Length;
                    // (one that the buffer cuts comes whole in the next buffer)
                    if (value + lengths[k] + 1 > have) continue;
                    long where = bufferStart + value;
                    if (where < plan.System.Start || where + lengths[k] >= plan.System.End || plan.WaitOffsets[k].Contains(where)) continue;
                    string text = Encoding.ASCII.GetString(buffer, value, lengths[k]);
                    if (buffer[value + lengths[k]] != (byte)(k == 1 ? '"' : '\n')) continue;
                    int seconds = ValueOf(k, text);
                    if (seconds <= 0) continue;
                    plan.WaitOffsets[k].Add(where);
                    plan.WaitValues[k].Add(seconds);
                }
            }
            for (int i = 0; i < Options.Length; ++i)
            {
                byte[] word = Encoding.ASCII.GetBytes(" " + Options[i] + "=");
                foreach (int at in Engine.Find(buffer, have, word))
                {
                    int value = at + word.Length;
                    if (value + 2 > have) continue;
                    long where = bufferStart + value;
                    if (where < plan.System.Start || where >= plan.System.End || plan.OptionOffsets[i].Contains(where)) continue;
                    byte v = buffer[value], after = buffer[value + 1];
                    if ((v != (byte)'0' && v != (byte)'1') || (after != (byte)' ' && after != (byte)'\n')) continue;
                    plan.OptionOffsets[i].Add(where);
                }
            }
        }

        // The seconds that a value of the fixed form says, 0 where it is not one
        static int ValueOf(int kind, string text)
        {
            if (kind == 2)
            {
                for (int s = 1; s <= 30; ++s) if (Words(s) == text) return s;
                return 0;
            }
            foreach (char c in text) if (c < '0' || c > '9') return 0;
            int n = int.Parse(text);
            if (kind == 0) return n % 10 == 0 && n >= 10 && n <= 300 ? n / 10 : 0;
            return n >= 1 && n <= 30 ? n : 0;
        }
    }

    // What an update did with the choices of the old boot menus (Operations.Update), for the log and for the last panel
    // of the window
    sealed class BootKept
    {
        public BootChoices Old;   // what the old menus said (null: not read)
        public bool ImageKnows;   // the image says boot-choices: what the old menus said was carried
        public int ImageWait;     // the wait that the image writes by itself; 0: not told
        public int Patches;       // how many bytes-runs were changed to carry it

        // The image's own wait in place of the old 10 s of menus of before 1.0
        bool Reset()
        {
            return ImageKnows && BootMenus.Resets(Old) && ImageWait > 0 && ImageWait != Old.Wait;
        }

        public string Summary()
        {
            return "the old boot menus: " + (Old == null ? "not read" : Old.Summary()) + "; the image " +
                   (ImageKnows ? "says boot-choices, waits " + ImageWait + " s by itself; " + Patches + " patches carry the choices" +
                                 (Reset() ? "; the old wait, " + Old.Wait + " s by itself before 1.0, is not carried: the image's " +
                                            ImageWait + " s stay" : "")
                               : "does not say boot-choices (" + (ImageWait > 0 ? "it waits " + ImageWait + " s" : "its wait not told") +
                                 "): it writes its own menus");
        }

        // A sentence for the panel: what was carried, or that it could not be, and the new wait of menus of before 1.0 that
        // waited 10 s; empty where there is nothing to say (the old menus waited as the image does and had no option on, or
        // could not be told)
        public string Text()
        {
            if (Old == null || !Old.Read) return "";
            bool reset = Reset();
            bool waitDiffers = !reset && Old.Wait > 0 && Old.Wait != ImageWait;
            if (!reset && !waitDiffers && Old.On.Count == 0) return "";
            if (!ImageKnows)
                return L.T("This image writes its own boot menus: their wait and the options of Kodi's display that are kept in them are the " +
                           "image's own, whatever the old ones said. Choose them again in the settings if you had changed them.",
                           "Esta imagen escribe sus propios menús de arranque: su espera y las opciones de la pantalla de Kodi que se guardan en " +
                           "ellos son las de la imagen, dijeran lo que dijeran los anteriores. Vuelve a elegirlas en los ajustes si las habías cambiado.");
            var parts = new List<string>();
            if (waitDiffers)
                parts.Add(L.T("waits " + Old.Wait + (Old.Wait == 1 ? " second" : " seconds"),
                              "espera " + Old.Wait + (Old.Wait == 1 ? " segundo" : " segundos")));
            var options = new List<string>();
            foreach (string o in Old.On)
                options.Add(o == BootMenus.Options[0] ? L.T("HDR over HDMI on the Intel graphics", "HDR por HDMI en la gráfica Intel")
                                                      : L.T("Dolby Vision on the Radeon", "Dolby Vision en la Radeon"));
            if (options.Count > 0)
                parts.Add(L.T("starts with " + string.Join(" and with ", options.ToArray()),
                              "arranca con " + string.Join(" y con ", options.ToArray())));
            string kept = parts.Count == 0 ? "" :
                L.T("The boot menu " + string.Join(" and ", parts.ToArray()) + ", as before the update.",
                    "El menú de arranque " + string.Join(" y ", parts.ToArray()) + ", como antes de la actualización.");
            if (!reset) return kept;
            string now = L.T("The boot menu now waits " + ImageWait + (ImageWait == 1 ? " second" : " seconds") + ", the new default (it waited " +
                             Old.Wait + "). It can be changed in Settings > LibreELEC > System > Boot menu.",
                             "El menú de arranque espera ahora " + ImageWait + (ImageWait == 1 ? " segundo" : " segundos") +
                             ", el nuevo valor por defecto (esperaba " + Old.Wait + "). Se puede cambiar en Ajustes > LibreELEC > Sistema > " +
                             "Menú de arranque.");
            return kept.Length == 0 ? now : now + " " + kept;
        }
    }
}
