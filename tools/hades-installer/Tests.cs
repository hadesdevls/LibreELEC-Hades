// LibreELEC Hades installer for Windows: its tests. --selftest writes images to files and to memory, never
// to a disk; --capture draws the window with made-up disks, in both languages; --list says how the disks of
// this PC are classified, reading nothing from them. What they write is for whoever works on the program:
// it is in English.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

namespace Hades.Installer
{
    // A stream that remembers where it was written, and can give one byte back wrong.
    sealed class ProbeStream : Stream
    {
        readonly Stream inner;
        public readonly List<long> Writes = new List<long>();
        public long FlipAt = -1;

        public ProbeStream(Stream inner) { this.inner = inner; }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return true; } }
        public override bool CanWrite { get { return true; } }
        public override long Length { get { return inner.Length; } }
        public override long Position { get { return inner.Position; } set { inner.Position = value; } }
        public override void Flush() { inner.Flush(); }
        public override long Seek(long offset, SeekOrigin origin) { return inner.Seek(offset, origin); }
        public override void SetLength(long value) { inner.SetLength(value); }
        public override int Read(byte[] buffer, int offset, int count)
        {
            long at = inner.Position;
            int read = inner.Read(buffer, offset, count);
            if (FlipAt >= at && FlipAt < at + read) buffer[offset + (int)(FlipAt - at)] ^= 0x40;
            return read;
        }
        public override void Write(byte[] buffer, int offset, int count)
        {
            Writes.Add(inner.Position);
            inner.Write(buffer, offset, count);
        }
    }

    static class SelfTest
    {
        static StreamWriter output;
        static int failures;

        public static void Say(string text)
        {
            output.WriteLine(text);
            output.Flush();
        }

        public static void Check(string what, bool ok)
        {
            Say((ok ? "ok:   " : "FAIL: ") + what);
            if (!ok) ++failures;
        }

        const int S = 512;
        const long Sectors = 180224; // 88 MiB
        const long P1 = 8192, P1Size = 131072, P2 = 139264, P2Size = 32768;

        static void PutGuid(byte[] buffer, long at, Guid g) { Buffer.BlockCopy(g.ToByteArray(), 0, buffer, (int)at, 16); }
        static void Put64(byte[] buffer, long at, long v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, buffer, (int)at, 8); }
        static void Put32(byte[] buffer, long at, uint v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, buffer, (int)at, 4); }

        // An image with the partition table of a Hades image, both copies, and boot menus made of noise
        // with the strings that the installer looks for.
        public const string ImageUuid = "acee72c1-eba0-4086-bd80-15a3a0d180d8";

        // others: the image says that it hides the other internal disks by itself. fat: its system partition
        // starts as a FAT16 does, with a root directory that holds the switch of the diagnostics: 1 off, as an
        // image has it; 2 on; 3 no such file, only what must not be taken for it; 4 to 11: see PutFat.
        public const int RootAt = (1 + 2 * 8) * S; // where that root directory is, from the start of the partition
        public const int SwitchEntry = 6;          // ... and which of its entries the switch is

        static void Entry(byte[] img, long root, int index, string name, byte attributes)
        {
            Buffer.BlockCopy(Encoding.ASCII.GetBytes(name), 0, img, (int)(root + index * 32), 11);
            img[root + index * 32 + 11] = attributes;
        }

        // A file with a long name, as Windows and Linux write it: the entries that spell the name, last part
        // first, and then the entry with its short name. Returns the index of that last entry. astray: the
        // parts say that they are of another short name.
        static int LongEntry(byte[] img, long root, int index, string longName, string shortName, bool astray = false)
        {
            byte check = 0;
            foreach (byte b in Encoding.ASCII.GetBytes(shortName)) check = (byte)((((check & 1) << 7) + (check >> 1) + b) & 0xFF);
            int parts = (longName.Length + 12) / 13;
            for (int part = parts; part >= 1; --part, ++index)
            {
                long e = root + index * 32;
                Array.Clear(img, (int)e, 32);
                img[e] = (byte)(part | (part == parts ? 0x40 : 0));
                img[e + 11] = 0x0F;
                img[e + 13] = astray ? (byte)(check ^ 0x55) : check;
                int[] places = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int i = 0; i < 13; ++i)
                {
                    int letter = (part - 1) * 13 + i;
                    ushort c = letter < longName.Length ? longName[letter] : letter == longName.Length ? (ushort)0 : (ushort)0xFFFF;
                    img[e + places[i]] = (byte)c; img[e + places[i] + 1] = (byte)(c >> 8);
                }
            }
            Entry(img, root, index, shortName, 0x20);
            return index;
        }

        // Which entry of the root directory the switch of an image made with "fat" is, and what it says.
        public static int SwitchIndex(int fat) { return fat == 5 ? 7 : fat == 7 || fat == 8 ? 8 : SwitchEntry; }
        public static Engine.Switch SwitchState(int fat)
        {
            return fat == 1 || fat == 4 ? Engine.Switch.Off
                 : fat == 2 || (fat >= 5 && fat <= 9) ? Engine.Switch.On : Engine.Switch.None;
        }

        static void PutFat(byte[] img, int fat)
        {
            long p = P1 * S;
            Array.Clear(img, (int)p, S);
            img[p + 12] = 2;   // 512 bytes in a sector
            img[p + 13] = 4;   // sectors in a cluster
            img[p + 14] = 1;   // reserved sectors
            img[p + 16] = 2;   // copies of the FAT
            img[p + 18] = 2;   // 512 entries in the root directory
            img[p + 22] = 8;   // sectors in each FAT
            img[p + 510] = 0x55; img[p + 511] = 0xAA;
            long root = p + RootAt;
            Array.Clear(img, (int)root, 512 * 32);
            // (4: the label of the volume has the name of the switch, on: a label is no file)
            Entry(img, root, 0, fat == 4 ? Engine.SwitchOn : "LIBREELEC  ", 0x08);
            Entry(img, root, 1, "KERNEL     ", 0x20);
            // a file that had the name of the switch and was deleted, a folder with that name, and a long name
            // whose letters are those of the name
            Entry(img, root, 2, Engine.SwitchOn, 0x20);
            img[root + 2 * 32] = 0xE5;
            Entry(img, root, 3, Engine.SwitchOn, 0x10);
            Entry(img, root, 4, Engine.SwitchOn, 0x0F);
            Entry(img, root, 5, "NOTICE~1MD ", 0x20);
            int last = SwitchEntry;
            if (fat == 2) Entry(img, root, SwitchEntry, Engine.SwitchOn, 0x20);
            // (6: turned on by hand, and the file was given another ending)
            else if (fat == 6) Entry(img, root, SwitchEntry, "DIAG-ON TX ", 0x20);
            else if (fat == 3) Entry(img, root, SwitchEntry, "SYSTEM     ", 0x20);
            // (7: turned on by hand in Windows, which hid the ending: the file has two, and a long name whose
            // short one does not start as the switch does; 8: the same in small letters)
            else if (fat == 7) last = LongEntry(img, root, SwitchEntry, "DIAG-ON.TXT.TXT", "DIAG-O~1TXT");
            else if (fat == 8) last = LongEntry(img, root, SwitchEntry, "diag-on.txt.txt", "DIAG-O~1TXT");
            // (9: another letter after the name, and no long name)
            else if (fat == 9) Entry(img, root, SwitchEntry, "DIAG-ONXTXT", 0x20);
            // (10: a long name that says the switch is off, with a short name that could be of either;
            // 11: a long name that says "on" and is not of the file after it: it is nobody's;
            // 12: the switch, off, with a long name next to it: its short name alone cannot be changed)
            else if (fat == 10) last = LongEntry(img, root, SwitchEntry, "DIAG-OFF.TXT.TXT", "DIAG-O~1TXT");
            else if (fat == 11) last = LongEntry(img, root, SwitchEntry, "DIAG-ON.TXT.TXT", "SYSTEM     ", true);
            else if (fat == 12) last = LongEntry(img, root, SwitchEntry, "diag-off.txt", Engine.SwitchOff);
            else Entry(img, root, SwitchEntry, Engine.SwitchOff, 0x20);
            // (5: both files are there: the diagnostics are on)
            if (fat == 5) Entry(img, root, last = 7, Engine.SwitchOn, 0x20);
            // (after the end of the directory nothing counts)
            Entry(img, root, last + 2, Engine.SwitchOn, 0x20);
        }

        public static byte[] MakeImage(int hides, bool feature, out long[] hideAt, int names = 4, bool others = false, int fat = 0)
        {
            var img = new byte[Sectors * S];
            uint x = 12345;
            for (int i = 0; i < img.Length; ++i) { x = x * 1664525 + 1013904223; img[i] = (byte)(x >> 24); }
            Array.Clear(img, 0, 34 * S);
            Array.Clear(img, (int)((Sectors - 33) * S), 33 * S);
            img[510] = 0x55; img[511] = 0xAA; img[450] = 0xEE;
            var entries = new byte[128 * 128];
            PutGuid(entries, 0, new Guid("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"));
            PutGuid(entries, 16, new Guid("824afa7e-2531-48d1-bf90-a06814c618d9"));
            Put64(entries, 32, P1); Put64(entries, 40, P1 + P1Size - 1);
            PutGuid(entries, 128, Engine.LinuxData);
            PutGuid(entries, 144, new Guid("b5763f06-f704-413a-9498-c4d993f05bbe"));
            Put64(entries, 160, P2); Put64(entries, 168, P2 + P2Size - 1);
            for (int copy = 0; copy < 2; ++copy)
            {
                long me = copy == 0 ? 1 : Sectors - 1, other = copy == 0 ? Sectors - 1 : 1;
                long entriesLba = copy == 0 ? 2 : Sectors - 33;
                var h = new byte[S];
                Buffer.BlockCopy(Encoding.ASCII.GetBytes("EFI PART"), 0, h, 0, 8);
                Put32(h, 8, 0x00010000); Put32(h, 12, 92);
                Put64(h, 24, me); Put64(h, 32, other); Put64(h, 40, 34); Put64(h, 48, Sectors - 34);
                PutGuid(h, 56, new Guid("2bca3777-06db-4914-a864-14639097bb67"));
                Put64(h, 72, entriesLba); Put32(h, 80, 128); Put32(h, 84, 128);
                Put32(h, 88, Crc32.Of(entries, 0, entries.Length));
                Put32(h, 16, Crc32.Of(h, 0, 92));
                Buffer.BlockCopy(h, 0, img, (int)(me * S), S);
                Buffer.BlockCopy(entries, 0, img, (int)(entriesLba * S), entries.Length);
            }
            // one string across a chunk of the scan (they start at 1 MiB), one across a chunk of the write.
            // With "others", the one at the chunk of the scan ends right before it instead: it is whole in
            // what the scan carries over to its next chunk, and must not be counted twice.
            long[] places = { P1 * S + 1000, 9L * 1048576 - (others ? Engine.HideInternal.Length + 1 : 20), 8L * 1048576 - 10,
                              (P1 + P1Size) * S - 100 };
            hideAt = new long[hides];
            byte[] hide = Encoding.ASCII.GetBytes(Engine.HideInternal);
            for (int i = 0; i < hides; ++i)
            {
                hideAt[i] = places[i];
                Buffer.BlockCopy(hide, 0, img, (int)places[i], hide.Length);
            }
            Array.Sort(hideAt);
            if (feature)
            {
                byte[] tag = Encoding.ASCII.GetBytes(others ? Engine.FeatureOtherDisks : Engine.FeatureStorageAnywhere);
                // (the longer one with all but its last byte in one chunk of the scan: the scan has to carry
                // that much over to the next)
                Buffer.BlockCopy(tag, 0, img, others ? 5 * 1048576 - (tag.Length - 1) : (int)(P1 * S + 5000), tag.Length);
            }
            if (fat != 0) PutFat(img, fat);
            // the entries that use the storage name it; one of them across a chunk of the scan
            long[] nameAt = { P1 * S + 2000, 13L * 1048576 - 20, 30L * 1048576 + 5, (P1 + P1Size) * S - 400 };
            byte[] name = Encoding.ASCII.GetBytes(Engine.DiskUuid + ImageUuid);
            for (int i = 0; i < names; ++i) Buffer.BlockCopy(name, 0, img, (int)nameAt[i], name.Length);
            // ... and the storage partition starts as an ext4 file system of that UUID does
            long super = P2 * S + 1024;
            img[super + 0x38] = 0x53; img[super + 0x39] = 0xEF;
            Buffer.BlockCopy(Guid.ParseExact(ImageUuid, "D").ToByteArray(), 0, img, (int)(super + 0x68), 16);
            // (a Guid keeps its first three fields the other way round: put them as the text reads)
            byte[] u = new byte[16];
            string hex = ImageUuid.Replace("-", "");
            for (int i = 0; i < 16; ++i) u[i] = Convert.ToByte(hex.Substring(2 * i, 2), 16);
            Buffer.BlockCopy(u, 0, img, (int)(super + 0x68), 16);
            return img;
        }

        static string Sha(byte[] data, int offset, int count)
        {
            using (var sha = SHA256.Create()) return Words.Hex(sha.ComputeHash(data, offset, count));
        }

        static int Count(byte[] data, string what)
        {
            byte[] w = Encoding.ASCII.GetBytes(what);
            int n = 0;
            for (int i = 0; i + w.Length <= data.Length; ++i)
            {
                if (data[i] != w[0]) continue;
                int k = 1;
                while (k < w.Length && data[i + k] == w[k]) ++k;
                if (k == w.Length) ++n;
            }
            return n;
        }

        static MemoryStream Disk(long size)
        {
            var m = new MemoryStream();
            var fill = new byte[1 << 20];
            for (int i = 0; i < fill.Length; ++i) fill[i] = 0xEE;
            for (long done = 0; done < size; done += fill.Length) m.Write(fill, 0, fill.Length);
            m.Position = 0;
            return m;
        }

        static bool Throws(Action action)
        {
            try { action(); return false; }
            catch (Exception) { return true; }
        }

        public static int Run(string directory, string realImage)
        {
            Directory.CreateDirectory(directory);
            L.Spanish = false;
            output = new StreamWriter(Path.Combine(directory, "selftest.txt"), false, new UTF8Encoding(false));
            try
            {
                long[] hideAt;
                byte[] img = MakeImage(4, true, out hideAt);
                string raw = Path.Combine(directory, "synthetic.img"), packed = raw + ".gz";
                File.WriteAllBytes(raw, img);
                using (var f = File.Create(packed))
                using (var gz = new GZipStream(f, CompressionMode.Compress))
                    gz.Write(img, 0, img.Length);
                string whole = Sha(img, 0, img.Length);

                foreach (string path in new[] { raw, packed })
                {
                    string kind = Engine.IsGzip(path) ? "compressed" : "uncompressed";
                    ImagePlan plan = Engine.Plan(path, true, null, null);
                    bool places = plan.HideOffsets.Count == 4;
                    for (int i = 0; places && i < 4; ++i) places &= plan.HideOffsets[i] == hideAt[i];
                    Check(kind + " image: its size, its two partitions and what its menus say",
                          plan.Size == img.Length && plan.System.Start == P1 * S && plan.System.Size == P1Size * S &&
                              plan.Storage.Start == P2 * S && plan.Storage.Size == P2Size * S && places && plan.StorageAnywhere);
                    Check(kind + " image: the four entries that use the storage name it by the identifier that the storage of " +
                          "the image has",
                          plan.StorageUuidOffsets.Count == 4 && plan.StorageUuid == ImageUuid && plan.StorageUuidInFs == ImageUuid);

                    // an update: the system partition alone, its menus naming the storage that the disk has
                    {
                        const string there = "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0";
                        List<Patch> keepStorage = Engine.KeepStorage(plan, there);
                        keepStorage.AddRange(Engine.ForInternalDisk(plan));
                        var target = new ProbeStream(Disk(P1Size * S));
                        var part = new Engine.Target();
                        part.Stream = target; part.Capacity = P1Size * S; part.From = plan.System.Start; part.Length = plan.System.Size;
                        Engine.Write(plan, new[] { part }, keepStorage, null, null);
                        var updated = new byte[P1Size * S];
                        target.Position = 0;
                        target.Read(updated, 0, updated.Length);
                        long different = 0;
                        for (int i = 0; i < updated.Length; ++i) different += updated[i] != img[P1 * S + i] ? 1 : 0;
                        Check(kind + " image, update: the system partition names the storage that the disk already has in its " +
                              "four entries, and nothing changes but that and the hiding of the internal disks",
                              part.Result.Verified && Count(updated, there) == 4 && Count(updated, ImageUuid) == 0 &&
                                  Count(updated, Engine.HideInternal) == 0 && different > 4 * 20 &&
                                  different <= 4 * Engine.UuidLength + 4 * Engine.HideInternal.Length);
                        Check("... and an identifier that is not one is refused",
                              Throws(delegate { Engine.KeepStorage(plan, "not-a-uuid"); }) &&
                                  Throws(delegate { Engine.KeepStorage(plan, null); }) &&
                                  Throws(delegate { Engine.KeepStorage(plan, there.ToUpperInvariant()); }));
                    }

                    // as on a USB drive: the image as it is
                    var disk = new ProbeStream(Disk(100L << 20));
                    WriteResult r = Engine.WriteWhole(plan, disk, 100L << 20, null, null, null);
                    disk.Position = 0;
                    var got = new byte[100 << 20];
                    disk.Read(got, 0, got.Length);
                    bool tail = true;
                    for (int i = img.Length; i < got.Length; ++i) tail &= got[i] == 0xEE;
                    Check(kind + " image, whole disk: what is written is the image, what is read back matches, and nothing beyond it",
                          r.Verified && r.Written == whole && Sha(got, 0, img.Length) == whole && tail && r.Bytes == img.Length);
                    Check("... the first megabyte, with the partition table, is written last",
                          disk.Writes.Count > 2 && disk.Writes[0] >= Engine.Head && disk.Writes[disk.Writes.Count - 1] == 0);

                    // as on an internal disk: the menus changed and the system partition an EFI one
                    List<Patch> patches = Engine.ForInternalDisk(plan);
                    patches.AddRange(Engine.SystemAsEfi(plan));
                    var inner = Disk(100L << 20);
                    r = Engine.WriteWhole(plan, inner, 100L << 20, patches, null, null);
                    inner.Position = 0;
                    inner.Read(got, 0, got.Length);
                    string changed = Path.Combine(directory, "internal.img");
                    using (var f = File.Create(changed)) f.Write(got, 0, img.Length);
                    ImagePlan again = Engine.Plan(changed, true, null, null);
                    long differing = 0;
                    for (int i = 0; i < img.Length; ++i) differing += got[i] != img[i] ? 1 : 0;
                    Check(kind + " image, internal disk: the menus no longer hide the internal disks and the system partition " +
                          "is an EFI partition in both copies of the table, which are still valid",
                          r.Verified && again.HideOffsets.Count == 0 && again.System.Type == Engine.EfiSystem &&
                              again.Storage.Type == Engine.LinuxData && again.StorageAnywhere && !Throws(delegate { Engine.SystemAsEfi(again); }) &&
                              // (an image that does not say that it hides the other disks by itself is not asked to)
                              !plan.HidesOtherDisks && Count(got, Engine.HideOthers) == 0);
                    Say("      bytes that differ from the image: " + differing);
                    Check("... and nothing else changes: at most the 4 texts, the two partition types and the checksums of the two headers",
                          differing > 4 * 40 && differing <= 4 * Engine.HideInternal.Length + 2 * 16 + 2 * 8);

                    // next to the data of a disk: each partition to its own place
                    var system = new ProbeStream(Disk(P1Size * S));
                    var storage = new ProbeStream(Disk((P2Size + 4096) * S));
                    var parts = new Engine.Target[2];
                    parts[0] = new Engine.Target();
                    parts[0].Stream = system; parts[0].Capacity = P1Size * S; parts[0].From = plan.System.Start; parts[0].Length = plan.System.Size;
                    parts[1] = new Engine.Target();
                    parts[1].Stream = storage; parts[1].Capacity = (P2Size + 4096) * S; parts[1].From = plan.Storage.Start; parts[1].Length = plan.Storage.Size;
                    Engine.Write(plan, parts, Engine.ForInternalDisk(plan), null, null);
                    var sys = new byte[P1Size * S];
                    system.Position = 0;
                    system.Read(sys, 0, sys.Length);
                    var sto = new byte[(P2Size + 4096) * S];
                    storage.Position = 0;
                    storage.Read(sto, 0, sto.Length);
                    long sysDiff = 0;
                    for (int i = 0; i < sys.Length; ++i) sysDiff += sys[i] != img[P1 * S + i] ? 1 : 0;
                    bool stoSame = Sha(sto, 0, (int)(P2Size * S)) == Sha(img, (int)(P2 * S), (int)(P2Size * S));
                    bool beyond = true;
                    for (long i = P2Size * S; i < sto.Length; ++i) beyond &= sto[i] == 0xEE;
                    // (an image that does not hide the other disks by itself gets spaces where the parameter was)
                    bool spaces = true;
                    foreach (long where in hideAt)
                        for (int i = 0; i < Engine.HideInternal.Length; ++i) spaces &= sys[where - P1 * S + i] == (byte)' ';
                    Check(kind + " image, next to the data: each partition goes to its place, the system one with its menus changed " +
                          "and the storage one as it is, both read back the same, and nothing beyond them",
                          parts[0].Result.Verified && parts[1].Result.Verified && Count(sys, Engine.HideInternal) == 0 && spaces &&
                              sysDiff > 4 * 40 && sysDiff <= 4 * Engine.HideInternal.Length && stoSame && beyond &&
                              system.Writes[system.Writes.Count - 1] == 0 && storage.Writes[storage.Writes.Count - 1] == 0);

                    // what must not go through
                    var small = Disk(80L << 20);
                    bool refused = Throws(delegate { Engine.WriteWhole(plan, small, 80L << 20, null, null, null); });
                    var kept = small.ToArray();
                    bool untouched = true;
                    foreach (byte b in kept) untouched &= b == 0xEE;
                    Check(kind + " image: a destination smaller than the image is refused and nothing is written", refused && untouched);
                    var faulty = new ProbeStream(Disk(100L << 20));
                    faulty.FlipAt = 40L << 20;
                    r = Engine.WriteWhole(plan, faulty, 100L << 20, null, null, null);
                    Check(kind + " image: a single bit that the disk gives back changed is found when reading back", !r.Verified);
                }

                long[] unused;
                Newer(directory);
                File.WriteAllBytes(raw, MakeImage(3, false, out unused));
                ImagePlan odd = Engine.Plan(raw, true, null, null);
                Check("an image whose menus are not the known ones (3 entries that hide disks) is not installed on an internal disk, " +
                      "and one without the mark is not offered next to the data of a disk",
                      Throws(delegate { Engine.ForInternalDisk(odd); }) && !odd.StorageAnywhere);
                File.WriteAllBytes(raw, MakeImage(4, true, out unused, 3));
                ImagePlan fewer = Engine.Plan(raw, true, null, null);
                Check("an image whose menus name the storage another number of times cannot be used to update",
                      Throws(delegate { Engine.KeepStorage(fewer, "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0"); }));
                byte[] broken = MakeImage(4, true, out unused);
                broken[S + 60] ^= 1;
                File.WriteAllBytes(raw, broken);
                Check("a damaged partition table is refused", Throws(delegate { Engine.Plan(raw, false, null, null); }));
                File.WriteAllBytes(raw, new byte[3 << 20]);
                Check("a file that is not an image is refused", Throws(delegate { Engine.Plan(raw, false, null, null); }));

                File.WriteAllBytes(raw, img);
                File.WriteAllText(raw + ".sha256", whole + "  synthetic.img\n");
                bool match = Engine.ExpectedHash(raw) == whole && Engine.FileHash(raw, null, null) == whole;
                img[img.Length / 2] ^= 1;
                File.WriteAllBytes(raw, img);
                Check("the hash in the .sha256 file is compared with that of the image, and a changed image does not match",
                      match && Engine.FileHash(raw, null, null) != Engine.ExpectedHash(raw) && Engine.ExpectedHash(packed) == null);

                // A partition that a program does not let go of. The program that serves the files of Windows
                // to WSL is told from the others that Windows starts in the same way; and the question that is
                // then asked needs nothing typed, does not take the partition with the Enter key, names the
                // letter of the partition when it has one, and speaks of WSL only where WSL is running.
                Check("the file server of WSL is known by its command line, and no other program is taken for it",
                      Wsl.IsFileServer(@"C:\WINDOWS\system32\DllHost.exe /Processid:{DFB65C4C-B34F-435D-AFE9-A86218684AA8}") &&
                          Wsl.IsFileServer("\"c:\\windows\\system32\\dllhost.exe\" /processid:{dfb65c4c-b34f-435d-afe9-a86218684aa8}") &&
                          !Wsl.IsFileServer(@"C:\WINDOWS\system32\DllHost.exe /Processid:{973D20D7-562D-44B9-B70B-5A0F49CCDF3F}") &&
                          !Wsl.IsFileServer(@"C:\Program Files\WSL\wslhost.exe {DFB65C4C-B34F-435D-AFE9-A86218684AA8}") &&
                          !Wsl.IsFileServer("") && !Wsl.IsFileServer(null));
                {
                    DiskInfo drive = Make(5, "Portable SSD", 7, 117, '\0');
                    bool asked;
                    using (ConfirmForm withAll = MainForm.AskTake(drive, 'H', true))
                    using (ConfirmForm bare = MainForm.AskTake(drive, '\0', false))
                    using (ConfirmForm typed = MainForm.AskUpdate(drive, "image.img.gz"))
                        asked = withAll.ActionReady && withAll.EnterCancels && !withAll.EnterActs && withAll.Said.Contains("(H:)") &&
                                withAll.Said.Contains("WSL") &&
                                bare.ActionReady && bare.EnterCancels && !bare.EnterActs && !bare.Said.Contains(":)") &&
                                !bare.Said.Contains("WSL") &&
                                !typed.ActionReady && typed.EnterActs && !typed.EnterCancels;
                    Check("a partition that a program has open: the question whether to take it needs nothing typed, the Enter key " +
                          "does not take it, and it names the partition and WSL only when there is a letter and WSL is running", asked);
                }

                Classification(realImage);
                if (realImage != null) Real(directory, realImage);
                foreach (string f in new[] { raw, packed, raw + ".sha256", Path.Combine(directory, "internal.img") })
                    File.Delete(f);
            }
            catch (Exception e)
            {
                Check("the tests end without an exception: " + e, false);
            }
            // (as the test of the virtual disk says it: an image that lacks what a part is about is not a pass of that part)
            Say(failures > 0 ? "FAILED: " + failures
                             : notRun > 0 ? "PASSED, BUT " + notRun + " PART(S) NOT RUN: see above" : "ALL PASSED");
            output.Close();
            return failures;
        }

        static int notRun;

        // An image that says that it hides the other internal disks by itself and that has the switch of the
        // diagnostics: what the installer finds in it, and what it writes for an internal disk and with the
        // diagnostics turned on.
        static void Newer(string directory)
        {
            long[] at, unused;
            byte[] img = MakeImage(4, true, out at, 4, true, 1);
            string raw = Path.Combine(directory, "newer.img"), packed = raw + ".gz";
            File.WriteAllBytes(raw, img);
            using (var f = File.Create(packed))
            using (var gz = new GZipStream(f, CompressionMode.Compress))
                gz.Write(img, 0, img.Length);
            const long inPartition = RootAt + SwitchEntry * 32;
            long switchAt = P1 * S + inPartition;
            foreach (string path in new[] { raw, packed })
            {
                string kind = Engine.IsGzip(path) ? "compressed" : "uncompressed";
                ImagePlan quick = Engine.Plan(path, false, null, null), plan = Engine.Plan(path, true, null, null);
                Check(kind + " newer image: it says that it hides the other internal disks by itself, and the switch of its " +
                      "diagnostics is found, off, with and without reading the image through",
                      plan.HidesOtherDisks && plan.StorageAnywhere && plan.HideOffsets.Count == 4 && plan.SwitchOffset == switchAt &&
                          !quick.Scanned && quick.SwitchOffset == switchAt);

                // the system partition for an internal disk
                var system = new ProbeStream(Disk(P1Size * S));
                var part = new Engine.Target();
                part.Stream = system; part.Capacity = P1Size * S; part.From = plan.System.Start; part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part }, Engine.ForInternalDisk(plan), null, null);
                var sys = new byte[P1Size * S];
                system.Position = 0;
                system.Read(sys, 0, sys.Length);
                long different = 0;
                for (int i = 0; i < sys.Length; ++i) different += sys[i] != img[P1 * S + i] ? 1 : 0;
                bool inPlace = true;
                string put = Engine.HideOthers.PadRight(Engine.HideInternal.Length);
                foreach (long where in at)
                    inPlace &= Encoding.ASCII.GetString(sys, (int)(where - P1 * S), put.Length) == put;
                long there;
                Check(kind + " newer image, internal disk: each of its four entries asks it to hide the other disks, in the place " +
                      "of what hid them all and no longer than that; nothing else changes, and the switch stays off",
                      part.Result.Verified && inPlace && Count(sys, Engine.HideOthers) == 4 && Count(sys, Engine.HideInternal) == 0 &&
                          different > 4 * 20 && different <= 4 * Engine.HideInternal.Length &&
                          Engine.ReadSwitch(new MemoryStream(sys), sys.Length, out there) == Engine.Switch.Off && there == inPartition);

                // the whole image, as on a USB drive, with the diagnostics turned on
                var disk = new ProbeStream(Disk(100L << 20));
                WriteResult r = Engine.WriteWhole(quick, disk, 100L << 20, Engine.Patches(quick, false, true, null, true), null, null);
                var got = new byte[img.Length];
                disk.Position = 0;
                disk.Read(got, 0, got.Length);
                different = 0;
                for (int i = 0; i < got.Length; ++i) different += got[i] != img[i] ? 1 : 0;
                Engine.Switch state = Engine.ReadSwitch(new MemoryStream(got, (int)(P1 * S), (int)(P1Size * S)), P1Size * S, out there);
                Check(kind + " newer image, diagnostics: what is written has the switch on and is two bytes away from the image " +
                      "(the FF of OFF), and it reads back the same",
                      r.Verified && different == 2 && state == Engine.Switch.On && there == inPartition &&
                          Encoding.ASCII.GetString(got, (int)switchAt, 11) == Engine.SwitchOn);

                // which changes each way of writing makes: an install on a USB drive none, unless the diagnostics
                // are asked for; inside the PC the four entries, and with the whole image its partition table
                // too (twice the entries and the header); an update the four names of the storage
                const string there2 = "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0";
                Func<List<Patch>, bool> turnsOn = delegate(List<Patch> list)
                {
                    int hits = 0;
                    foreach (Patch p in list) if (p.Offset == switchAt && Encoding.ASCII.GetString(p.Bytes) == Engine.SwitchOn) ++hits;
                    return hits == 1;
                };
                bool counts = Engine.Patches(plan, false, true, null, false).Count == 0 &&
                              Engine.Patches(plan, false, true, null, true).Count == 1 &&
                              Engine.Patches(plan, true, true, null, false).Count == 8 &&
                              Engine.Patches(plan, true, true, null, true).Count == 9 &&
                              Engine.Patches(plan, true, false, null, false).Count == 4 &&
                              Engine.Patches(plan, true, false, null, true).Count == 5 &&
                              Engine.Patches(plan, false, false, there2, false).Count == 4 &&
                              Engine.Patches(plan, false, false, there2, true).Count == 5 &&
                              Engine.Patches(plan, true, false, there2, false).Count == 8 &&
                              Engine.Patches(plan, true, false, there2, true).Count == 9;
                bool asked = true;
                foreach (bool inside in new[] { false, true })
                    foreach (bool table in new[] { false, true })
                        foreach (string storage in new[] { null, there2 })
                            asked &= turnsOn(Engine.Patches(plan, inside, table, storage, true)) &&
                                     !turnsOn(Engine.Patches(plan, inside, table, storage, false));
                Check(kind + " newer image: every way of writing makes the changes it should and no other, and turns the " +
                      "diagnostics on exactly when they are asked for", counts && asked);
            }
            // what is not a switch that can be turned on, and what tells that the diagnostics are on
            bool none = true, told = true;
            string wrong = "";
            foreach (int fat in new[] { 0, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12 })
            {
                byte[] other = MakeImage(4, true, out unused, 4, true, fat);
                File.WriteAllBytes(raw, other);
                ImagePlan plan = Engine.Plan(raw, false, null, null);
                long there;
                Engine.Switch state = Engine.ReadSwitch(new MemoryStream(other, (int)(P1 * S), (int)(P1Size * S)), P1Size * S, out there);
                bool on = fat != 0 && SwitchState(fat) == Engine.Switch.On;
                bool ok = plan.SwitchOffset == -1 && Throws(delegate { Engine.DiagnosticsOn(plan); }) &&
                          state == (on ? Engine.Switch.On : Engine.Switch.None) &&
                          there == (on ? RootAt + SwitchIndex(fat) * 32 : -1);
                none &= ok;
                // ... and the image says that it has them on, which is what is written from it whatever is asked
                told &= plan.SwitchOn == on;
                if (!ok || plan.SwitchOn != on) wrong += " " + fat + ":" + state + "@" + there + (plan.SwitchOn ? "+" : "-");
            }
            Check("an image without the switch, off, has no diagnostics to turn on: one whose system partition is not a FAT, " +
                  "one that has only a deleted file, a folder and a long name like it, one whose long name says OFF or is of " +
                  "no file, one whose switch has a long name next to it, and the ones that have them on already (the file as " +
                  "the installer leaves it, next to the one that is off, with another ending, with two endings and a long " +
                  "name, in small letters, or with one more letter)" + wrong,
                  none);
            Check("an image says that it has its diagnostics on exactly when it has a file whose name starts with DIAG-ON" + wrong,
                  told);
            File.WriteAllBytes(raw, MakeImage(4, true, out unused, 4, true, 4));
            Check("a volume whose label is the name of the switch, on: the label is no file, and the switch is found, off",
                  Engine.Plan(raw, false, null, null).SwitchOffset == switchAt);
            // a partition that ends before its root directory does, and what is not a FAT of the kind an image has
            long cut;
            var whole = new byte[RootAt + 512 * 32];
            Buffer.BlockCopy(img, (int)(P1 * S), whole, 0, whole.Length);
            Check("the switch is read from a partition that ends with its root directory, and not from one that is cut short " +
                  "or said to be shorter than that",
                  Engine.ReadSwitch(new MemoryStream(whole), whole.Length, out cut) == Engine.Switch.Off && cut == inPartition &&
                      Engine.ReadSwitch(new MemoryStream(whole, 0, whole.Length - S), whole.Length, out cut) == Engine.Switch.None &&
                      Engine.ReadSwitch(new MemoryStream(whole), whole.Length - 1, out cut) == Engine.Switch.None &&
                      Engine.ReadSwitch(new MemoryStream(whole, 0, S), whole.Length, out cut) == Engine.Switch.None &&
                      Engine.ReadSwitch(new MemoryStream(new byte[100]), 100, out cut) == Engine.Switch.None);
            // (offset in the boot sector, value): no root directory of its own (FAT32), no sector size (exFAT), nothing
            // reserved (NTFS), three copies of the FAT, a FAT of no sectors, sectors of 4096 bytes, no signature
            bool refused = true;
            foreach (int[] change in new[] { new[] { 18, 0 }, new[] { 12, 0 }, new[] { 14, 0 }, new[] { 16, 3 }, new[] { 22, 0 },
                                             new[] { 12, 16 }, new[] { 510, 0 } })
            {
                var odd = (byte[])whole.Clone();
                odd[change[0]] = (byte)change[1];
                refused &= Engine.ReadSwitch(new MemoryStream(odd), odd.Length, out cut) == Engine.Switch.None && cut == -1;
            }
            Check("nothing is looked for in a partition that is not a FAT with a root directory of its own and sectors of 512 bytes",
                  refused);
            File.Delete(raw);
            File.Delete(packed);
        }

        static DiskInfo Make(int number, string name, int bus, long gb, char letter)
        {
            var d = new DiskInfo();
            d.Number = number;
            d.Name = name;
            d.Serial = "SERIAL" + number;
            d.UniqueId = "ID" + number;
            d.BusType = bus;
            d.Size = gb << 30;
            d.PartitionStyle = 2;
            if (letter != '\0')
            {
                var p = new PartitionInfo();
                p.Number = 2; p.Offset = 16L << 20; p.Size = d.Size - (32L << 20); p.Letter = letter; p.GptType = Disks.BasicType;
                d.Partitions.Add(p);
                var v = new VolumeInfo();
                v.Letter = letter; v.Label = L.T("Data", "Datos"); v.FileSystem = "NTFS"; v.Ready = true; v.Size = p.Size; v.Used = p.Size / 3;
                d.Volumes.Add(v);
            }
            return d;
        }

        static PartitionInfo Part(int number, long size, string type, char letter)
        {
            var p = new PartitionInfo();
            p.Number = number; p.Size = size; p.GptType = type; p.Letter = letter;
            return p;
        }

        // A made-up internal disk with the given partitions, one after another, and after them the two of
        // Hades. systemLetter: the letter that Windows gives the system partition of Hades when the disk was
        // written as a USB drive is ('\0': none, as the installer leaves it on an internal disk).
        static DiskInfo HadesDisk(int number, char systemLetter, out PartitionInfo system, out PartitionInfo storage,
                                  params PartitionInfo[] others)
        {
            DiskInfo d = Make(number, "Disk with Hades " + number, 17, 1863, '\0');
            long at = 1L << 20;
            foreach (PartitionInfo p in others)
            {
                p.Offset = at;
                at = p.End;
                d.Partitions.Add(p);
            }
            system = Part(others.Length + 1, 1L << 30, systemLetter != '\0' ? Disks.BasicType : Disks.EfiType, systemLetter);
            system.Offset = at;
            storage = Part(others.Length + 2, 64L << 30, Disks.LinuxType, '\0');
            storage.Offset = system.End;
            d.Partitions.Add(system);
            d.Partitions.Add(storage);
            foreach (PartitionInfo p in d.Partitions)
            {
                if (p.Letter == '\0') continue;
                var v = new VolumeInfo();
                v.Letter = p.Letter; v.FileSystem = p == system ? "FAT32" : "NTFS"; v.Ready = true; v.Size = p.Size; v.Used = p.Size / 2;
                d.Volumes.Add(v);
            }
            return d;
        }

        static Keep Keeps(char windows, params char[] others)
        {
            var keep = new Keep();
            keep.Windows = windows;
            keep.Letters.Add(windows);
            keep.Letters.AddRange(others);
            return keep;
        }

        static Extent Stretch(int disk, long offset, long length)
        {
            var e = new Extent();
            e.Disk = disk; e.Offset = offset; e.Length = length;
            return e;
        }

        // realImage: the image that the tests were given, if any (here only where it is counts: on a PC with
        // more than one disk, an image on another disk than Windows makes the last check tell disks apart).
        static void Classification(string realImage)
        {
            Keep keep = Keeps('C', 'D');
            var guarded = new List<string>(new[] { "SERIAL7" });
            long image = 1L << 30;
            DiskInfo windows = Make(0, "Windows disk", 17, 1863, 'C');
            windows.IsBoot = windows.IsSystem = true;
            DiskInfo data = Make(1, "Data disk", 17, 1863, 'E');
            DiskInfo program = Make(2, "Disk with the program", 17, 1863, 'D');
            DiskInfo stick = Make(3, "Stick", 7, 29, 'F');
            DiskInfo tiny = Make(4, "Small stick", 7, 0, 'G');
            tiny.Size = 512L << 20;
            DiskInfo locked = Make(5, "Card", 12, 64, 'H');
            locked.IsReadOnly = true;
            DiskInfo virtualDisk = Make(6, "Virtual disk", 15, 20, 'I');
            DiskInfo guardedDisk = Make(7, "Protected disk", 17, 1863, 'J');
            DiskInfo usbWithProgram = Make(8, "USB with the program", 7, 500, 'D');
            // the program on a partition of 100 MB, with nothing else on the disk that has a letter
            DiskInfo programSmall = Make(9, "Disk with the program on a small partition", 17, 1863, 'D');
            programSmall.Partitions[0].Size = programSmall.Volumes[0].Size = 100L << 20;
            // ... and on the second of two volumes
            DiskInfo programSecond = Make(10, "Disk with two volumes", 17, 1863, 'K');
            {
                var p = new PartitionInfo();
                p.Number = 3; p.Offset = programSecond.Partitions[0].End; p.Size = 64L << 30; p.Letter = 'D'; p.GptType = Disks.BasicType;
                programSecond.Partitions[0].Size -= p.Size;
                p.Offset = programSecond.Partitions[0].End;
                programSecond.Partitions.Add(p);
                var v = new VolumeInfo();
                v.Letter = 'D'; v.FileSystem = "NTFS"; v.Ready = true; v.Size = p.Size; v.Used = p.Size / 2;
                programSecond.Volumes.Add(v);
            }
            DiskInfo guardedProgram = Make(11, "Protected disk with the program", 17, 1863, 'D');
            guarded.Add("SERIAL11");
            DiskInfo lockedInside = Make(12, "Write-protected internal disk", 17, 1863, 'L');
            lockedInside.IsReadOnly = true;
            Disks.AllowVirtual = false;
            Check("USB: the stick is offered and nothing else (not the Windows disk, nor the internal ones, nor the small one, nor " +
                  "the write-protected one, nor the one that holds this program)",
                  Disks.Classify(stick, Place.Usb, image, keep, guarded) == null &&
                      Disks.Classify(windows, Place.Usb, image, keep, guarded) != null &&
                      Disks.Classify(data, Place.Usb, image, keep, guarded) != null &&
                      Disks.Classify(tiny, Place.Usb, image, keep, guarded) != null &&
                      Disks.Classify(locked, Place.Usb, image, keep, guarded) != null &&
                      Disks.Classify(virtualDisk, Place.Usb, image, keep, guarded) != null &&
                      Disks.Classify(usbWithProgram, Place.Usb, image, keep, guarded) != null);
            Check("internal disk: the data disk and the one that holds this program are offered, and nothing else (not the " +
                  "Windows disk, nor the one on the list of protected disks, nor a USB stick, nor a virtual disk)",
                  Disks.Classify(data, Place.Internal, image, keep, guarded) == null &&
                      Disks.Classify(program, Place.Internal, image, keep, guarded) == null &&
                      Disks.Classify(windows, Place.Internal, image, keep, guarded) != null &&
                      Disks.Classify(guardedDisk, Place.Internal, image, keep, guarded) != null &&
                      Disks.Classify(stick, Place.Internal, image, keep, guarded) != null &&
                      Disks.Classify(virtualDisk, Place.Internal, image, keep, guarded) != null);
            // What erases a whole disk and what does not. The disk that holds this program (or the image, or the
            // files of the user: any letter that is kept) can give room, be updated and have Hades taken off it,
            // and can never be erased.
            Check("the disk that holds this program: room can be made on it, and erasing it is refused; the data disk can be both",
                  Disks.Kept(program, keep) == "D:" && Disks.Kept(data, keep) == null &&
                      Disks.Refusal(program, Place.Internal, image, keep, guarded, false) == null &&
                      Disks.Refusal(program, Place.Internal, image, keep, guarded, true) != null &&
                      Disks.Refusal(data, Place.Internal, image, keep, guarded, false) == null &&
                      Disks.Refusal(data, Place.Internal, image, keep, guarded, true) == null);
            Check("... whatever the partition that it is on: a small one, or one of several",
                  Disks.Refusal(programSmall, Place.Internal, image, keep, guarded, true) != null &&
                      Disks.Refusal(programSmall, Place.Internal, image, keep, guarded, false) == null &&
                      Disks.Refusal(programSecond, Place.Internal, image, keep, guarded, true) != null &&
                      Disks.Kept(programSecond, keep) == "D:");
            Check("... and no way of using a disk gets past what keeps it from being offered: the Windows disk, a protected " +
                  "one that also holds this program, a write-protected one",
                  Disks.Refusal(windows, Place.Internal, image, keep, guarded, false) != null &&
                      Disks.Refusal(windows, Place.Internal, image, keep, guarded, true) != null &&
                      Disks.Refusal(guardedProgram, Place.Internal, image, keep, guarded, false) != null &&
                      Disks.Refusal(guardedProgram, Place.Internal, image, keep, guarded, true) != null &&
                      Disks.Refusal(lockedInside, Place.Internal, image, keep, guarded, false) != null);
            Check("a USB drive is only ever used whole: the one that holds this program is not offered, to make room or otherwise",
                  Disks.Refusal(usbWithProgram, Place.Usb, image, keep, guarded, false) != null &&
                      Disks.Refusal(usbWithProgram, Place.Usb, image, keep, guarded, true) != null &&
                      Disks.Refusal(stick, Place.Usb, image, keep, guarded, true) == null);
            Disks.AllowVirtual = true;
            Check("only in the tests does a virtual disk count as an internal one", Disks.Classify(virtualDisk, Place.Internal, image, keep, guarded) == null);
            Disks.AllowVirtual = false;

            // Windows says of its disk that it starts from it. Should it not say so, the folder of Windows does:
            // by its letter, and by where it is.
            DiskInfo unflagged = Make(13, "Windows disk that Windows does not flag", 17, 1863, 'C');
            DiskInfo elsewhere = Make(14, "... and whose volume is not known by that letter", 17, 1863, 'M');
            Keep windowsThere = Keeps('C', 'D');
            windowsThere.WindowsDisks.Add(14);
            string onIt = L.T("Windows is on it", "en él está Windows");
            Check("the disk that Windows is on is not used in any way, even if Windows does not say that it starts from it: it is " +
                  "known by the letter of the folder of Windows, and by where that folder is",
                  Disks.Refusal(unflagged, Place.Internal, image, keep, guarded, false) == onIt &&
                      Disks.Refusal(unflagged, Place.Internal, image, keep, guarded, true) == onIt &&
                      Disks.Refusal(elsewhere, Place.Internal, image, windowsThere, guarded, false) == onIt &&
                      Disks.Refusal(elsewhere, Place.Internal, image, windowsThere, guarded, true) == onIt &&
                      // (the same disk when the folder of Windows is not on it: a data disk)
                      Disks.Refusal(elsewhere, Place.Internal, image, keep, guarded, true) == null);

            // What must be kept, known by where its volume is and not by a letter of its path: this program or
            // the image in a folder that is a mount point or a junction, on a SUBST letter, on a volume
            // without a letter.
            DiskInfo reached = Make(15, "Disk reached through a folder", 17, 1863, 'N');
            DiskInfo unlettered = Make(16, "Disk whose volume has no letter", 17, 1863, '\0');
            unlettered.Partitions.Add(Part(2, unlettered.Size - (32L << 20), Disks.BasicType, '\0'));
            unlettered.Partitions[0].Offset = 16L << 20;
            DiskInfo stickReached = Make(17, "Stick reached through a folder", 7, 500, 'O');
            Keep byPlace = Keeps('C', 'S'); // (S: a letter that no volume has)
            byPlace.Extents.Add(Stretch(15, reached.Partitions[0].Offset, reached.Partitions[0].Size));
            byPlace.Extents.Add(Stretch(16, unlettered.Partitions[0].Offset, unlettered.Partitions[0].Size));
            byPlace.Extents.Add(Stretch(17, stickReached.Partitions[0].Offset, stickReached.Partitions[0].Size));
            Check("what must be kept is found by where it really is, when the letter of its path does not say so (a folder that " +
                  "leads to another disk, a volume without a letter): that disk is not erased either, and room can be made on it",
                  Disks.Kept(reached, byPlace) == "N:" && Disks.Kept(unlettered, byPlace) == "" && Disks.Kept(data, byPlace) == null &&
                      Disks.Refusal(reached, Place.Internal, image, byPlace, guarded, true) != null &&
                      Disks.Refusal(reached, Place.Internal, image, byPlace, guarded, false) == null &&
                      Disks.Refusal(unlettered, Place.Internal, image, byPlace, guarded, true) != null &&
                      Disks.Refusal(unlettered, Place.Internal, image, byPlace, guarded, false) == null &&
                      Disks.Refusal(data, Place.Internal, image, byPlace, guarded, true) == null);
            Check("... and a USB drive that holds it is not offered",
                  Disks.Classify(stickReached, Place.Usb, image, byPlace, guarded) != null &&
                      Disks.Classify(stickReached, Place.Usb, image, keep, guarded) == null);

            // Taking Hades off a disk: when the disk is erased, and when only the two partitions of Hades go.
            PartitionInfo s, t;
            Keep nothing = Keeps('C');
            bool erased = Disks.OnlyHades(HadesDisk(20, '\0', out s, out t), s, t, nothing);
            erased = Disks.OnlyHades(HadesDisk(21, '\0', out s, out t, Part(1, 16L << 20, Disks.ReservedType, '\0')), s, t, nothing) && erased;
            // (written as a USB drive is: its system partition has a letter, and nothing that is kept is on it)
            erased = Disks.OnlyHades(HadesDisk(22, 'H', out s, out t), s, t, nothing) && erased;
            Check("taking Hades off: a disk with nothing but Hades (and, at most, the reserved partition of Windows, which holds no " +
                  "data) is erased and left empty for Windows", erased);
            bool spared = !Disks.OnlyHades(HadesDisk(23, '\0', out s, out t, Part(1, 100L << 20, Disks.BasicType, 'P')), s, t, nothing);
            spared = !Disks.OnlyHades(HadesDisk(24, '\0', out s, out t, Part(1, 100L << 20, Disks.EfiType, '\0')), s, t, nothing) && spared;
            spared = !Disks.OnlyHades(HadesDisk(25, '\0', out s, out t, Part(1, 16L << 20, Disks.ReservedType, '\0'),
                                                Part(2, 500L << 30, Disks.BasicType, 'Q')), s, t, nothing) && spared;
            Check("... and with any other partition on it, however small and with or without a letter, the disk is not erased: only " +
                  "the two partitions of Hades go", spared);
            DiskInfo held = HadesDisk(26, '\0', out s, out t);
            Keep beyond = Keeps('C');
            beyond.Extents.Add(Stretch(26, t.End + (1L << 30), 1L << 30)); // (a volume that Windows does not list as a partition)
            Check("... nor is a disk erased that holds what must be kept, wherever on it that is",
                  !Disks.OnlyHades(held, s, t, beyond) && Disks.OnlyHades(held, s, t, nothing));
            // A disk written as a USB drive is and then put inside the PC: Windows gives the system partition
            // of Hades a letter, and this program or the image can be on it.
            DiskInfo lettered = HadesDisk(27, 'H', out s, out t);
            Keep onHades = Keeps('C', 'H');
            Keep onHadesByPlace = Keeps('C', 'S');
            onHadesByPlace.Extents.Add(Stretch(27, s.Offset, s.Size));
            Check("the partition of Hades that this program or the image is on is found, by its letter and by its place: that " +
                  "Hades is neither updated nor taken off; with them elsewhere, there is none",
                  Disks.KeptOnHades(lettered, s, t, onHades) == s && Disks.KeptOnHades(lettered, s, t, onHadesByPlace) == s &&
                      Disks.KeptOnHades(lettered, s, t, nothing) == null && Disks.KeptOnHades(lettered, s, t, keep) == null &&
                      !Disks.OnlyHades(lettered, s, t, onHades) && !Disks.OnlyHades(lettered, s, t, onHadesByPlace));

            // The whole of what "Remove" decides (Operations.Uninstall does what this says, and nothing else).
            PartitionInfo heldOn, grows;
            DiskInfo shared = HadesDisk(28, '\0', out s, out t, Part(1, 16L << 20, Disks.ReservedType, '\0'),
                                        Part(2, 500L << 30, Disks.BasicType, 'Q'));
            bool how = Operations.HowToRemove(shared, s, t, nothing, out heldOn, out grows) == Operations.Removal.Partitions &&
                       heldOn == null && grows != null && grows.Letter == 'Q';
            // (with this program on that volume: the same)
            how = Operations.HowToRemove(shared, s, t, Keeps('C', 'Q'), out heldOn, out grows) == Operations.Removal.Partitions &&
                  heldOn == null && grows != null && grows.Letter == 'Q' && how;
            // (a partition without a letter before Hades: nothing gets the room back, and nothing else is touched)
            DiskInfo afterOther = HadesDisk(29, '\0', out s, out t, Part(1, 100L << 20, Disks.EfiType, '\0'));
            how = Operations.HowToRemove(afterOther, s, t, nothing, out heldOn, out grows) == Operations.Removal.Partitions &&
                  grows == null && how;
            DiskInfo onlyIt = HadesDisk(30, '\0', out s, out t);
            how = Operations.HowToRemove(onlyIt, s, t, nothing, out heldOn, out grows) == Operations.Removal.WholeDisk &&
                  heldOn == null && grows == null && how;
            DiskInfo asUsb = HadesDisk(31, 'H', out s, out t);
            how = Operations.HowToRemove(asUsb, s, t, Keeps('C', 'H'), out heldOn, out grows) == Operations.Removal.Refused &&
                  heldOn == s && how;
            how = Operations.HowToRemove(asUsb, s, t, nothing, out heldOn, out grows) == Operations.Removal.WholeDisk && how;
            Check("what removing Hades does: next to a volume, its two partitions go and that volume gets the room back, also with " +
                  "this program on it; after a partition without a letter, they go and nothing else is touched; a disk with " +
                  "nothing else is erased; with this program or the image on a partition of Hades, nothing is done", how);

            // This PC: nothing is read from its disks but what Windows says of them. Where the folder of
            // Windows is, asked through its path, has to be on the disk that Windows says it starts from.
            Keep here = Disks.Keeping(null);
            List<DiskInfo> real = Disks.List();
            bool known = here.WindowsDisks.Count > 0, agrees = true;
            foreach (DiskInfo d in real)
                if (here.WindowsDisks.Contains(d.Number) && !d.IsBoot) agrees = false;
            Say("      this PC: Windows (" + here.Windows + ":) is on disk " + string.Join(", ", here.WindowsDisks) + "; this program, on disk " +
                string.Join(", ", Native.Extents(Application.ExecutablePath).ConvertAll(delegate(Extent e) { return e.Disk; })));
            Check("this PC: the disk that the folder of Windows is on, asked through its path, is one that Windows says it starts " +
                  "from, and the disk that this program is on is known too",
                  known && agrees && Native.Extents(Application.ExecutablePath).Count > 0);
            // ... and the place that Windows gives for the volume of a path is that of the partition which has
            // the letter of that path: the same disk, the same start and the same length
            int compared = 0;
            bool samePlace = true;
            foreach (string path in new[] { Environment.SystemDirectory, Application.ExecutablePath, realImage })
            {
                if (path == null || path.Length < 2 || path[1] != ':') continue;
                foreach (DiskInfo d in real)
                    foreach (PartitionInfo p in d.Partitions)
                    {
                        if (p.Letter != char.ToUpperInvariant(path[0])) continue;
                        List<Extent> at = Native.Extents(path);
                        bool same = at.Count == 1 && at[0].Disk == d.Number && at[0].Offset == p.Offset && at[0].Length == p.Size;
                        Say("      " + path.Substring(0, 2) + " partition " + d.Number + "/" + p.Number + " at " + p.Offset + ", " + p.Size +
                            " bytes; Windows says of that path: " + (at.Count == 0 ? "nothing" : "disk " + at[0].Disk + " at " +
                            at[0].Offset + ", " + at[0].Length + " bytes" + (at.Count > 1 ? ", and " + (at.Count - 1) + " more" : "")));
                        ++compared;
                        if (!same) samePlace = false;
                    }
            }
            Check("... and the place that Windows gives for a path is that of the partition with its letter: disk, start and length",
                  compared > 0 && samePlace);
            // an empty list: the PC is said to have no other disk only when it has none
            bool spanish = L.Spanish;
            L.Spanish = false;
            var onlyWindows = new List<DiskInfo>(new[] { windows, stick });
            var leftOut = new List<DiskInfo>(new[] { windows, program, guardedDisk, stick });
            Check("no disk offered: a PC with nothing but the Windows disk is told that it has no other, and one whose two other " +
                  "internal disks are left out is told that it has two and where to read why",
                  Disks.NothingToOffer(onlyWindows, Place.Internal).Contains("has no internal disk") &&
                      Disks.NothingToOffer(leftOut, Place.Internal).Contains("has 2 more internal disks") &&
                      Disks.NothingToOffer(leftOut, Place.Internal).Contains("says why") &&
                      Disks.NothingToOffer(new List<DiskInfo>(new[] { windows, guardedDisk }), Place.Internal).Contains("one more internal disk"));
            Check("... and the same for USB: nothing plugged in asks for a drive, a drive that cannot be used is said to be there",
                  Disks.NothingToOffer(new List<DiskInfo>(new[] { windows, program }), Place.Usb).Contains("Plug in") &&
                      Disks.NothingToOffer(new List<DiskInfo>(new[] { windows, tiny }), Place.Usb).Contains("plugged in cannot be used") &&
                      Disks.NothingToOffer(new List<DiskInfo>(new[] { tiny, locked }), Place.Usb).Contains("None of the 2 USB drives"));
            L.Spanish = spanish;
        }

        // A real image: what the installer finds in it, and what it would write for an internal disk, left
        // in files so that a virtual machine can start from them.
        static void Real(string directory, string image)
        {
            ImagePlan plan = Engine.Plan(image, true, null, null);
            Say("real image " + Path.GetFileName(image) + ": " + plan.Size + " bytes; system " + plan.System.Start + "+" + plan.System.Size +
                " (" + plan.System.Type + "), storage " + plan.Storage.Start + "+" + plan.Storage.Size + "; " + plan.HideOffsets.Count +
                " entries that hide the internal disks; mark of storage on any partition: " + plan.StorageAnywhere +
                "; hides the other internal disks by itself: " + plan.HidesOtherDisks + "; switch of the diagnostics at: " + plan.SwitchOffset);
            Check("real image: 4 entries that hide the internal disks, in the system partition", plan.HideOffsets.Count == 4);
            // (an older image has neither of the two: not failed, and not passed either)
            if (!plan.HidesOtherDisks)
            {
                Say("NOT RUN: this image does not say that it hides the other internal disks by itself.");
                ++notRun;
            }
            if (plan.SwitchOffset < 0)
            {
                Say("NOT RUN: this image has no switch for the diagnostics.");
                ++notRun;
            }
            bool diagnose = plan.SwitchOffset >= 0;
            List<Patch> patches = Engine.ForInternalDisk(plan);
            patches.AddRange(Engine.SystemAsEfi(plan));
            // the whole disk with the diagnostics on, the two partitions next to the data without them: a virtual machine
            // starts both
            if (diagnose) patches.Add(Engine.DiagnosticsOn(plan));
            // (the names of these files, and of the one of SimulateUpdate, are the ones that the scripts that start them in a
            // virtual machine know)
            string whole = Path.Combine(directory, "real-disco-entero.img");
            using (var f = new FileStream(whole, FileMode.Create, FileAccess.ReadWrite))
            {
                f.SetLength(plan.Size);
                WriteResult r = Engine.WriteWhole(plan, f, plan.Size, patches, null, null);
                Check("real image, whole internal disk: written to a file and read back the same", r.Verified);
            }
            ImagePlan again = Engine.Plan(whole, true, null, null);
            Check("... its partition table is valid, the system partition is an EFI one and the menus do not hide the internal disks",
                  again.System.Type == Engine.EfiSystem && again.HideOffsets.Count == 0 && !Throws(delegate { Engine.SystemAsEfi(again); }));
            if (plan.HidesOtherDisks || diagnose)
            {
                // what was written, read again: the start of the system partition, where the menus of syslinux and the
                // root directory are, and the menu of GRUB wherever it is
                var head = new byte[4 << 20];
                long others = 0;
                Engine.Switch state;
                long there;
                using (var f = new FileStream(whole, FileMode.Open, FileAccess.Read))
                {
                    f.Seek(plan.System.Start, SeekOrigin.Begin);
                    f.Read(head, 0, head.Length);
                    state = Engine.ReadSwitch(new MemoryStream(head), head.Length, out there);
                    f.Seek(plan.System.Start, SeekOrigin.Begin);
                    // (in pieces that overlap by less than the text: one that a piece cuts is whole in the next,
                    // and none is counted twice, because only those that start in the piece itself count)
                    const int piece = 8 << 20;
                    byte[] text = Encoding.ASCII.GetBytes(Engine.HideOthers + " ");
                    var chunk = new byte[piece + text.Length - 1];
                    for (long done = 0; done < plan.System.Size; done += piece)
                    {
                        f.Seek(plan.System.Start + done, SeekOrigin.Begin);
                        int read = f.Read(chunk, 0, (int)Math.Min(chunk.Length, plan.System.Size - done));
                        for (int i = 0; i < piece && i + text.Length <= read; ++i)
                        {
                            int k = 0;
                            while (k < text.Length && chunk[i + k] == text[k]) ++k;
                            if (k == text.Length) ++others;
                        }
                    }
                }
                Say("      entries that ask to hide the other disks: " + others + "; switch: " + state);
                if (plan.HidesOtherDisks)
                    Check("... its four entries ask the image to hide the other internal disks", others == 4);
                if (diagnose)
                    Check("... and the switch of its diagnostics is on, where the image has it off",
                          state == Engine.Switch.On && plan.System.Start + there == plan.SwitchOffset && again.SwitchOffset == -1);
            }
            string sys = Path.Combine(directory, "real-sistema.bin"), sto = Path.Combine(directory, "real-almacenamiento.bin");
            using (var a = new FileStream(sys, FileMode.Create, FileAccess.ReadWrite))
            using (var b = new FileStream(sto, FileMode.Create, FileAccess.ReadWrite))
            {
                var parts = new Engine.Target[2];
                parts[0] = new Engine.Target();
                parts[0].Stream = a; parts[0].Capacity = plan.System.Size; parts[0].From = plan.System.Start; parts[0].Length = plan.System.Size;
                parts[1] = new Engine.Target();
                parts[1].Stream = b; parts[1].Capacity = plan.Storage.Size; parts[1].From = plan.Storage.Start; parts[1].Length = plan.Storage.Size;
                Engine.Write(plan, parts, Engine.ForInternalDisk(plan), null, null);
                Check("real image, next to the data: the two partitions written to files and read back the same",
                      parts[0].Result.Verified && parts[1].Result.Verified);
            }
        }

        // An image like the ones that the self-test makes, with what a current image has (it says that it hides the other
        // disks by itself, and has the switch of the diagnostics), in a file with its .sha256: for the screenshots.
        public static int MakeTestImage(string file)
        {
            long[] unused;
            byte[] img = MakeImage(4, true, out unused, 4, true, 1);
            File.WriteAllBytes(file, img);
            File.WriteAllText(file + ".sha256", Sha(img, 0, img.Length) + "  " + Path.GetFileName(file) + "\n");
            return 0;
        }

        // The system partition as an update would write it for a storage of the given UUID, to a file: for
        // a virtual machine to start a disk that was updated.
        public static int SimulateUpdate(string directory, string image, string existing, bool inside, bool diagnostics)
        {
            Directory.CreateDirectory(directory);
            ImagePlan plan = Engine.Plan(image, true, null, null);
            List<Patch> patches = Engine.Patches(plan, inside, false, existing, diagnostics);
            using (var f = new FileStream(Path.Combine(directory, "sistema-actualizado.bin"), FileMode.Create, FileAccess.ReadWrite))
            {
                var part = new Engine.Target();
                part.Stream = f; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part }, patches, null, null);
                File.WriteAllText(Path.Combine(directory, "sistema-actualizado.txt"),
                                  "image " + Path.GetFileName(image) + " names " + plan.StorageUuid + "; written for " + existing +
                                  (inside ? "; for a disk inside the PC" : "") + (diagnostics ? "; diagnostics on" : "") +
                                  "; verified " + part.Result.Verified + "; " + part.Result.Written + "\r\n");
                return part.Result.Verified ? 0 : 1;
            }
        }

        // Outside Application.Run, Windows Forms takes its synchronization context away each time
        // DoEvents returns, and a BackgroundWorker started then reports from another thread, while the
        // test goes on. The tests keep one of their own; the program itself runs inside Application.Run.
        static void KeepUiContext()
        {
            WindowsFormsSynchronizationContext.AutoInstall = false;
            if (!(System.Threading.SynchronizationContext.Current is WindowsFormsSynchronizationContext))
                System.Threading.SynchronizationContext.SetSynchronizationContext(new WindowsFormsSynchronizationContext());
        }

        static void Pump(Func<bool> until, int seconds)
        {
            KeepUiContext();
            DateTime end = DateTime.Now.AddSeconds(seconds);
            do { Application.DoEvents(); System.Threading.Thread.Sleep(30); } while (!until() && DateTime.Now < end);
            Application.DoEvents();
        }

        static void Save(Control what, string path)
        {
            Application.DoEvents();
            using (var bmp = new Bitmap(what.Width, what.Height))
            {
                what.DrawToBitmap(bmp, new Rectangle(0, 0, what.Width, what.Height));
                bmp.Save(path, ImageFormat.Png);
            }
        }

        // The window with made-up disks, drawn to files: once in each language, in the folders "en" and "es".
        public static int Capture(string directory, string image)
        {
            captureFailures = 0;
            foreach (bool spanish in new[] { false, true })
            {
                L.Spanish = spanish;
                CaptureIn(Path.Combine(directory, spanish ? "es" : "en"), image);
            }
            return captureFailures;
        }

        static int captureFailures;

        static void CaptureIn(string directory, string image)
        {
            Directory.CreateDirectory(directory);
            var data = new MainForm.TestData();
            data.Spare = 700L << 30;
            DiskInfo stick = Make(3, "USB Flash Drive 32GB", 7, 29, 'X');
            stick.Volumes[0].Label = "USB";
            stick.Volumes[0].FileSystem = "FAT32";
            DiskInfo external = Make(4, "Samsung T7", 7, 931, 'Y');
            external.Volumes[0].Label = L.T("Backups", "Copias");
            DiskInfo second = Make(1, "WD Blue SN580 1TB", 17, 931, 'Z');
            // an internal disk that this program is on (made up, as the disks are: where the program really is
            // does not count here)
            const char ownLetter = 'K';
            DiskInfo own = Make(2, "Kingston KC3000 2TB", 17, 1863, ownLetter);
            own.Volumes[0].Label = L.T("Games", "Juegos");
            data.Keep.Windows = 'C';
            data.Keep.Letters.Add('C');
            data.Keep.Letters.Add(ownLetter);
            data.Disks.Add(stick);
            data.Disks.Add(external);
            data.Disks.Add(second);
            data.Disks.Add(own);
            DiskInfo windows = Make(0, "Samsung SSD 980 1TB", 17, 931, 'C');
            windows.IsBoot = windows.IsSystem = true;
            data.Disks.Add(windows);
            MainForm.Test = data;
            using (var form = new MainForm(image))
            {
                form.Show();
                Pump(delegate { return !form.Scanning; }, 180);
                form.SelectFirst();
                Save(form, Path.Combine(directory, "1-usb.png"));
                form.ShowInternal();
                Pump(delegate { return !form.Scanning; }, 180);
                form.SelectFirst();
                Pump(delegate { return true; }, 1);
                Save(form, Path.Combine(directory, "2-internal-beside.png"));
                form.ChooseWhole();
                Save(form, Path.Combine(directory, "3-internal-whole.png"));
                using (ConfirmForm confirm = MainForm.AskErase(stick))
                {
                    confirm.Show();
                    Save(confirm, Path.Combine(directory, "4-confirm.png"));
                }
                // the disk that this program is on: room can be made on it, and it cannot be used whole
                bool offered = form.SelectDisk(own);
                form.ChooseBeside();
                Pump(delegate { return true; }, 1);
                bool roomReady = form.GoReady;
                string roomStatus = form.StatusText;
                Save(form, Path.Combine(directory, "9-own-disk-beside.png"));
                form.ChooseWhole();
                Pump(delegate { return true; }, 1);
                bool wholeReady = form.GoReady;
                string wholeStatus = form.StatusText;
                Save(form, Path.Combine(directory, "10-own-disk-whole.png"));
                form.SelectDisk(second);
                Pump(delegate { return true; }, 1);
                bool otherWholeReady = form.GoReady;
                // (without an image nothing can be installed anywhere and the window says only that the image is
                // missing: all that can be looked at then is that the disk is listed and its button is off)
                bool ok = offered && !wholeReady &&
                          (image == null || (wholeStatus.Contains("(" + ownLetter + ":)") && roomReady && otherWholeReady));
                if (!ok) ++captureFailures;
                File.WriteAllText(Path.Combine(directory, "9-state.txt"),
                                  (ok ? "ok:   " : "FAIL: ") + (image == null ? "(no image: only what does not need one) " : "") +
                                  "the disk that this program is on (" + ownLetter + ":) is offered: " + offered +
                                  "; to make room, the button is on: " + roomReady + " (" + roomStatus + "); whole, the button is on: " +
                                  wholeReady + " (" + wholeStatus + "); another disk whole, the button is on: " + otherWholeReady + "\r\n");
                form.ShowDone(second, Place.Internal, false);
                Save(form, Path.Combine(directory, "5-done.png"));
            }
            // a unit that already has Hades: updating it comes first
            DiskInfo has = Make(5, "Portable SSD", 7, 117, '\0');
            var hadesSystem = new PartitionInfo();
            hadesSystem.Number = 1; hadesSystem.Offset = 4L << 20; hadesSystem.Size = 1L << 30; hadesSystem.GptType = Disks.BasicType;
            var hadesStorage = new PartitionInfo();
            hadesStorage.Number = 2; hadesStorage.Offset = hadesSystem.End; hadesStorage.Size = has.Size - hadesStorage.Offset - (1L << 20);
            hadesStorage.GptType = Disks.LinuxType;
            has.Partitions.Add(hadesSystem);
            has.Partitions.Add(hadesStorage);
            has.HasHades = true;
            data.Disks.Insert(0, has);
            data.Diagnostics.Add(has); // ... and its Hades has the diagnostics turned on
            // The box of the diagnostics, with an image chosen after the disk was: it is off without an image,
            // and then says what the disk has, when the image can turn them on.
            bool offWithout, followsImage = true;
            using (var form = new MainForm(null))
            {
                form.Show();
                Pump(delegate { return true; }, 1);
                form.SelectFirst();
                Pump(delegate { return true; }, 1);
                offWithout = !form.DiagnosticsTicked && !form.DiagnosticsCanBeSet;
                if (image != null)
                {
                    form.ChooseImageForTest(image);
                    Pump(delegate { return !form.Scanning; }, 180);
                    followsImage = form.DiagnosticsTicked == form.SwitchKnown && form.DiagnosticsCanBeSet == form.SwitchKnown;
                }
            }
            // An image that has its diagnostics on already: the box is on, a click does not change it, and it
            // says that the image always saves them.
            bool always;
            {
                long[] unused;
                string on = Path.Combine(directory, "always-on.img");
                byte[] made = MakeImage(4, true, out unused, 4, true, 2);
                File.WriteAllBytes(on, made);
                File.WriteAllText(on + ".sha256", Sha(made, 0, made.Length) + "  " + Path.GetFileName(on) + "\n");
                using (var form = new MainForm(on))
                {
                    form.Show();
                    Pump(delegate { return !form.Scanning; }, 180);
                    form.SelectDisk(stick);
                    Pump(delegate { return true; }, 1);
                    bool before = form.DiagnosticsTicked;
                    form.ClickDiagnostics();
                    always = form.SwitchAlways && !form.SwitchKnown && before && form.DiagnosticsTicked && !form.DiagnosticsCanBeSet &&
                             form.DiagnosticsText.Contains(L.T("always", "siempre"));
                    Save(form, Path.Combine(directory, "12-image-always-on.png"));
                }
                File.Delete(on);
                File.Delete(on + ".sha256");
            }
            using (var form = new MainForm(image))
            {
                form.Show();
                Pump(delegate { return !form.Scanning; }, 180);
                form.SelectFirst();
                Pump(delegate { return form.UpdateReady; }, 180);
                // ... it starts as the disk has them; once the user has set it, it stays as set whatever disk is
                // chosen; and for a disk without Hades it starts off
                bool known = form.SwitchKnown;
                bool startsAsDisk = form.DiagnosticsTicked == known && form.DiagnosticsCanBeSet == known;
                bool staysAsSet = true, offForNew = true;
                if (known)
                {
                    form.SelectDisk(stick);
                    Pump(delegate { return true; }, 1);
                    offForNew = !form.DiagnosticsTicked;
                    form.SelectDisk(has);
                    Pump(delegate { return true; }, 1);
                    startsAsDisk &= form.DiagnosticsTicked;
                    form.ClickDiagnostics(); // the user turns them off
                    staysAsSet &= !form.DiagnosticsTicked;
                    form.SelectDisk(stick);
                    Pump(delegate { return true; }, 1);
                    staysAsSet &= !form.DiagnosticsTicked;
                    form.SelectDisk(has);
                    Pump(delegate { return true; }, 1);
                    staysAsSet &= !form.DiagnosticsTicked;
                    form.ClickDiagnostics(); // ... and on again
                    form.SelectDisk(stick);
                    Pump(delegate { return true; }, 1);
                    staysAsSet &= form.DiagnosticsTicked;
                    form.SelectDisk(has);
                    Pump(delegate { return form.UpdateReady; }, 30);
                }
                bool boxOk = offWithout && followsImage && startsAsDisk && staysAsSet && offForNew && always;
                if (!boxOk) ++captureFailures;
                File.WriteAllText(Path.Combine(directory, "11-diagnostics.txt"),
                                  (boxOk ? "ok:   " : "FAIL: ") + "the box of the diagnostics. The image has the switch: " + known +
                                  "; without an image it is off: " + offWithout + "; with the image chosen after the disk it says what " +
                                  "the disk has: " + followsImage + "; it starts as the disk has them: " + startsAsDisk +
                                  "; off for a disk without Hades: " + offForNew + "; it stays as the user set it: " + staysAsSet +
                                  "; with an image that has them on already it is on, cannot be changed and says so: " + always + "\r\n");
                File.WriteAllText(Path.Combine(directory, "6-state.txt"), "update ready: " + form.UpdateReady + "; scanning: " +
                                  form.Scanning + "; status: " + form.StatusText + "\r\n");
                Save(form, Path.Combine(directory, "6-usb-with-hades.png"));
                using (ConfirmForm confirm = MainForm.AskUpdate(has, Path.GetFileName(image ?? L.T("image", "imagen"))))
                {
                    confirm.Show();
                    Save(confirm, Path.Combine(directory, "7-confirm-update.png"));
                }
                // its system partition is open in a program that does not let go of it: the question that is
                // asked then, where WSL is running and where it is not
                using (ConfirmForm take = MainForm.AskTake(has, 'H', true))
                {
                    take.Show();
                    Save(take, Path.Combine(directory, "13-confirm-take-wsl.png"));
                }
                using (ConfirmForm take = MainForm.AskTake(has, 'H', false))
                {
                    take.Show();
                    Save(take, Path.Combine(directory, "14-confirm-take.png"));
                }
                form.ShowDone(has, Place.Usb, true);
                Save(form, Path.Combine(directory, "8-updated.png"));
            }
        }

        // How the disks of this PC are classified. Nothing is read from them but what Windows says of them.
        // image: the path of an image, as if it had been chosen in the window (it is not opened).
        public static int ListDisks(string file, string image)
        {
            L.Spanish = false;
            using (var w = new StreamWriter(file, false, new UTF8Encoding(false)))
            {
                Keep keep = Disks.Keeping(image);
                List<string> guarded = Disks.Protected();
                w.WriteLine("UEFI: " + Native.Uefi + "; Secure Boot: " + Pc.SecureBoot + "; administrator: " + Pc.Elevated +
                            "; letters that are not touched: " + new string(keep.Letters.ToArray()) + "; protected disks: " + guarded.Count);
                w.WriteLine("Windows (" + keep.Windows + ":) is on disk " + string.Join(", ", keep.WindowsDisks) +
                            "; what is kept is, by where its volumes are, on:");
                var told = new List<string>();
                foreach (Extent e in keep.Extents)
                {
                    string line = "   disk " + e.Disk + " at " + e.Offset + ", " + e.Length + " bytes";
                    if (!told.Contains(line)) { told.Add(line); w.WriteLine(line); }
                }
                foreach (string path in new[] { Application.ExecutablePath, image })
                {
                    if (path == null) continue;
                    var on = new List<string>();
                    foreach (Extent e in Native.Extents(path)) on.Add("disk " + e.Disk + " at " + e.Offset);
                    w.WriteLine("   " + path + ": " + (on.Count > 0 ? string.Join(", ", on) : "NOT KNOWN (only its letter counts)"));
                }
                foreach (DiskInfo d in Disks.List())
                {
                    w.WriteLine("disk " + d.Number + ": " + d.Title + "; " + d.Detail + "; partitions " + d.Partitions.Count +
                                "; style " + d.PartitionStyle);
                    w.WriteLine("   USB: " + (d.Size <= 0 ? "no medium" : Disks.Classify(d, Place.Usb, 1L << 30, keep, guarded) ?? "OFFERED"));
                    string inside = d.Size <= 0 ? "no medium" : Disks.Classify(d, Place.Internal, 1L << 30, keep, guarded);
                    string whole = Disks.Refusal(d, Place.Internal, 1L << 30, keep, guarded, true);
                    w.WriteLine("   internal: " + (inside ?? (whole == null ? "OFFERED" : "OFFERED TO MAKE ROOM ONLY (not whole: " + whole + ")")));
                }
            }
            return 0;
        }
    }
}
