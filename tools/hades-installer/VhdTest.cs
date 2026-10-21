// LibreELEC Hades installer for Windows: the test of what it does to an internal disk, on a virtual disk (a
// .vhd file that Windows attaches as a disk). It needs administrator rights, so it is the user who starts it
// (--vhd-test, from probar-instalador.cmd). Every step that changes a disk first makes sure that the disk is
// that file.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace Hades.Installer
{
    static class VhdTest
    {
        const long VhdMegabytes = 24000;
        static string vhd;
        static char letter;

        static string DiskPart(string script)
        {
            string file = Path.Combine(Path.GetTempPath(), "hades-diskpart-" + Guid.NewGuid().ToString("N") + ".txt");
            File.WriteAllText(file, script, Encoding.ASCII);
            try
            {
                var start = new ProcessStartInfo("diskpart.exe", "/s \"" + file + "\"");
                start.UseShellExecute = false;
                start.RedirectStandardOutput = true;
                start.CreateNoWindow = true;
                using (Process p = Process.Start(start))
                {
                    string text = p.StandardOutput.ReadToEnd();
                    p.WaitForExit();
                    if (p.ExitCode != 0) throw new InvalidOperationException("diskpart: " + p.ExitCode + "\r\n" + text);
                    return text;
                }
            }
            finally { File.Delete(file); }
        }

        // The disk that the .vhd file is, and no other.
        static DiskInfo Find()
        {
            DiskInfo found = null;
            int count = 0;
            foreach (DiskInfo d in Disks.List())
            {
                if (!d.Virtual || d.IsBoot || d.IsSystem) continue;
                if (d.Location.IndexOf(Path.GetFileName(vhd), StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (Math.Abs(d.Size - VhdMegabytes * 1048576) > 64L * 1048576) continue;
                found = d;
                ++count;
            }
            if (count != 1) throw new InvalidOperationException("The virtual disk of the test was not found (" + count + ").");
            return found;
        }

        static string FilesHash(string directory)
        {
            using (var sha = SHA256.Create())
            {
                string[] files = Directory.GetFiles(directory);
                Array.Sort(files, StringComparer.Ordinal);
                foreach (string f in files)
                {
                    byte[] name = Encoding.UTF8.GetBytes(Path.GetFileName(f));
                    sha.TransformBlock(name, 0, name.Length, null, 0);
                    byte[] data = File.ReadAllBytes(f);
                    sha.TransformBlock(data, 0, data.Length, null, 0);
                }
                sha.TransformFinalBlock(new byte[0], 0, 0);
                return Words.Hex(sha.Hash);
            }
        }

        static PartitionInfo Lettered(DiskInfo d)
        {
            foreach (PartitionInfo p in d.Partitions) if (p.Letter == letter) return p;
            return null;
        }

        // The partitions of a disk, as a text: to see that none of them changed.
        static string Table(DiskInfo d)
        {
            var sb = new StringBuilder();
            foreach (PartitionInfo p in d.Partitions)
                sb.Append(p.Number).Append('@').Append(p.Offset).Append('+').Append(p.Size).Append(' ').Append(p.GptType).Append(';');
            return sb.ToString();
        }

        static void Quiet(int permille, string text) { }

        // A program that does not let go of a partition. All the time it does three things: it opens one of
        // its files and reads from it, it writes a small file of its own there, and it goes on writing to a
        // file that it has had open since before (own). Phase says where the update is: 0, the partition has
        // not been taken yet, and all three must work (or this program proves nothing); 1, the partition is
        // being written, and none of them may work; 2, the update is ending or has ended, and it does
        // nothing (what it wrote then would be on the new partition). What it gets done is counted for
        // each phase, and only what was tried from start to end inside one phase counts.
        sealed class Pest
        {
            public volatile int Phase;
            public volatile bool Stop;
            public volatile int ReadsBefore, WritesBefore, OwnBefore, Tries, Reads, Writes, Own;
            readonly string file, scrap;
            readonly FileStream own;
            readonly byte[] more = new byte[4096];
            readonly System.Threading.Thread thread;

            public Pest(string file, string scrap, FileStream own)
            {
                this.file = file;
                this.scrap = scrap;
                this.own = own;
                thread = new System.Threading.Thread(Loop);
                thread.IsBackground = true;
                thread.Start();
            }

            void Loop()
            {
                while (!Stop)
                {
                    int phase = Phase;
                    if (phase == 2)
                    {
                        System.Threading.Thread.Sleep(5);
                        continue;
                    }
                    bool read = false, wrote = false, kept = false;
                    try
                    {
                        using (var f = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                        {
                            f.ReadByte();
                            read = true;
                        }
                    }
                    catch (Exception) { }
                    if (Phase == phase)
                    {
                        try { File.WriteAllText(scrap, "x"); wrote = true; }
                        catch (Exception) { }
                    }
                    if (Phase == phase)
                    {
                        try { own.Write(more, 0, more.Length); own.Flush(); kept = true; }
                        catch (Exception) { }
                    }
                    if (Phase == phase)
                    {
                        if (phase == 0)
                        {
                            if (read) ++ReadsBefore;
                            if (wrote) ++WritesBefore;
                            if (kept) ++OwnBefore;
                        }
                        else
                        {
                            ++Tries;
                            if (read) ++Reads;
                            if (wrote) ++Writes;
                            if (kept) ++Own;
                        }
                    }
                    System.Threading.Thread.Sleep(3);
                }
            }

            public void End()
            {
                Phase = 2;
                Stop = true;
                thread.Join();
            }
        }

        static void ReadFull(Stream s, byte[] buffer, int count)
        {
            int got = 0;
            while (got < count)
            {
                int n = s.Read(buffer, got, count - got);
                if (n <= 0) throw new IOException("could not read " + count + " bytes: " + got);
                got += n;
            }
        }

        // How many blocks of 4 KiB of a partition are not what a file says that they should be, and where
        // the first of them are. The partition is read from the disk itself, read-only: nothing is mounted
        // or locked for it.
        static int RawDifference(int disk, PartitionInfo p, string expected, long length, List<long> where)
        {
            int differ = 0;
            var a = new byte[1 << 20];
            var b = new byte[1 << 20];
            using (FileStream d = Native.OpenDisk(disk, false))
            using (var f = new FileStream(expected, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
            {
                d.Seek(p.Offset, SeekOrigin.Begin);
                for (long done = 0; done < length; )
                {
                    int want = (int)Math.Min(length - done, a.Length);
                    ReadFull(d, a, want);
                    ReadFull(f, b, want);
                    for (int at = 0; at < want; at += 4096)
                    {
                        int end = Math.Min(at + 4096, want);
                        for (int i = at; i < end; ++i)
                        {
                            if (a[i] == b[i]) continue;
                            ++differ;
                            if (where.Count < 8) where.Add(done + at);
                            break;
                        }
                    }
                    done += want;
                }
            }
            return differ;
        }

        // The clusters of each file and folder of a FAT16, by its path: what a difference in a partition is in.
        static Dictionary<int, string> ClusterOwners(Fat16 fat)
        {
            var owners = new Dictionary<int, string>();
            var folders = new List<KeyValuePair<string, List<DiagItem>>>();
            folders.Add(new KeyValuePair<string, List<DiagItem>>("", DiagFolder.Entries(fat.Root)));
            for (int i = 0; i < folders.Count && i < 10000; ++i)
                foreach (DiagItem item in folders[i].Value)
                {
                    string path = folders[i].Key + "/" + item.Name;
                    List<int> chain = item.First < 2 ? null : fat.Chain(item.First, fat.Clusters);
                    if (chain == null) continue;
                    foreach (int c in chain) owners[c] = path;
                    if (!item.Folder) continue;
                    var dir = new byte[(long)chain.Count * fat.ClusterBytes];
                    for (int k = 0; k < chain.Count; ++k) Buffer.BlockCopy(fat.Cluster(chain[k]), 0, dir, k * fat.ClusterBytes, fat.ClusterBytes);
                    folders.Add(new KeyValuePair<string, List<DiagItem>>(path, DiagFolder.Entries(dir)));
                }
            return owners;
        }

        // The part of a FAT16 partition that an offset is in.
        static string Region(Fat16 fat, Dictionary<int, string> owners, long at)
        {
            if (at < Fat16.Sector) return "the boot sector";
            if (at < fat.FatAt) return "the reserved sectors";
            if (at < fat.RootAt)
            {
                long fatBytes = (long)fat.FatSectors * Fat16.Sector, into = (at - fat.FatAt) % fatBytes;
                return "FAT " + (1 + (at - fat.FatAt) / fatBytes) + ", the entry of cluster " + into / 2;
            }
            if (at < fat.DataAt) return "the root directory, entry " + (at - fat.RootAt) / 32;
            int cluster = (int)((at - fat.DataAt) / fat.ClusterBytes) + 2;
            string who;
            return "cluster " + cluster + (owners.TryGetValue(cluster, out who) ? " of " + who : ", free in the image");
        }

        // 32 bytes from "from" (rounded down to 16), in hexadecimal and as letters: a signature shows (NTFS writes FILE,
        // INDX, RCRD and RSTR at the start of its records).
        static string Bytes(byte[] b, int from)
        {
            from &= ~15;
            var hex = new StringBuilder();
            var text = new StringBuilder();
            for (int i = from; i < Math.Min(b.Length, from + 32); ++i)
            {
                hex.Append(b[i].ToString("x2"));
                text.Append(b[i] >= 32 && b[i] < 127 ? (char)b[i] : '.');
            }
            return "[" + from + "] " + hex + " \"" + text + "\"";
        }

        // A part of another stream, from "start" and "length" long, read-only: a partition of a disk read through the disk.
        sealed class PartOf : Stream
        {
            readonly Stream inner;
            readonly long start, length;
            long position;

            public PartOf(Stream inner, long start, long length) { this.inner = inner; this.start = start; this.length = length; }
            public override bool CanRead { get { return true; } }
            public override bool CanSeek { get { return true; } }
            public override bool CanWrite { get { return false; } }
            public override long Length { get { return length; } }
            public override long Position { get { return position; } set { position = value; } }
            public override void Flush() { }
            public override void SetLength(long value) { throw new NotSupportedException(); }
            public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
            public override long Seek(long offset, SeekOrigin origin)
            {
                position = origin == SeekOrigin.Begin ? offset : origin == SeekOrigin.Current ? position + offset : length + offset;
                return position;
            }
            public override int Read(byte[] buffer, int offset, int count)
            {
                count = (int)Math.Max(0, Math.Min(count, length - position));
                if (count == 0) return 0;
                inner.Seek(start + position, SeekOrigin.Begin);
                int read = inner.Read(buffer, offset, count);
                position += read;
                return read;
            }
        }

        public const string SystemVolumeInformation = "System Volume Information";

        // The clusters of a folder and of everything in it (files by their chains, folders the same way). Null, or what is
        // wrong with them.
        static string Owned(Fat16 fat, DiagItem folder, List<int> clusters, int depth)
        {
            if (depth > 8) return "its folders go too deep";
            List<int> chain = folder.First < 2 ? null : fat.Chain(folder.First, 256);
            if (chain == null) return "the chain of one of its folders is broken";
            clusters.AddRange(chain);
            var dir = new byte[(long)chain.Count * fat.ClusterBytes];
            for (int k = 0; k < chain.Count; ++k) Buffer.BlockCopy(fat.Cluster(chain[k]), 0, dir, k * fat.ClusterBytes, fat.ClusterBytes);
            foreach (DiagItem item in DiagFolder.Entries(dir))
            {
                if (item.Folder)
                {
                    string wrong = Owned(fat, item, clusters, depth + 1);
                    if (wrong != null) return wrong;
                    continue;
                }
                if (item.First < 2) continue; // a file with nothing in it
                List<int> c = fat.Chain(item.First, fat.Clusters);
                if (c == null) return "the chain of one of its files is broken";
                clusters.AddRange(c);
            }
            return null;
        }

        // How a FAT16 partition read from a disk (actual) differs from what was written there (expected) when Windows has
        // mounted it meanwhile. Its FAT driver puts a folder System Volume Information (with files of its own) in the
        // root directory of a FAT that it mounts, and may set the bits that say that the volume is in use (bits 0 and 1
        // of byte 37 of the boot sector, bits 14 and 15 of the entry of cluster 1 in each FAT). Those are allowed, and
        // only those: the entries of that folder in the root directory, where the image has free entries; its clusters
        // and those of what is in it, which the image has free, and their entries in each FAT. A block of 4 KiB that
        // differs is Windows' when every byte that differs in it is one of those, and "other" when any is not. why:
        // what is not as Windows makes it (that folder in entries or clusters that the image uses, a broken chain), or
        // null.
        internal static void WindowsOnly(Stream expected, Stream actual, long length, out int windows, out int other, out string why)
        {
            windows = other = 0;
            why = null;
            var allowed = new List<long[]>(); // ranges of bytes, from and to
            long dirtyByte = -1;
            var inUse = new List<long>();     // the high byte of the entry of cluster 1, in each FAT
            string unused;
            Fat16 was = Fat16.Open(DiagFolder.Reader(expected, length), length, out unused);
            Fat16 now = Fat16.Open(DiagFolder.Reader(actual, length), length, out unused);
            if (was == null || now == null || was.FatAt != now.FatAt || was.RootAt != now.RootAt || was.DataAt != now.DataAt ||
                was.ClusterBytes != now.ClusterBytes || was.Fats != now.Fats)
                why = "the partition on the disk is not the FAT16 that was written";
            else
            {
                long fatBytes = (long)was.FatSectors * Fat16.Sector;
                dirtyByte = 37;
                for (int copy = 0; copy < was.Fats; ++copy) inUse.Add(was.FatAt + copy * fatBytes + 3);
                DiagItem folder = null;
                foreach (DiagItem item in DiagFolder.Entries(now.Root))
                    if (item.Folder && string.Equals(item.Name, SystemVolumeInformation, StringComparison.OrdinalIgnoreCase)) folder = item;
                if (folder != null)
                {
                    var entries = new byte[folder.Names.Length + 32];
                    Buffer.BlockCopy(folder.Names, 0, entries, 0, folder.Names.Length);
                    Buffer.BlockCopy(folder.Own, 0, entries, folder.Names.Length, 32);
                    int at = -1;
                    for (int e = 0; e + entries.Length <= now.Root.Length && at < 0; e += 32)
                    {
                        bool same = true;
                        for (int k = 0; k < entries.Length && same; ++k) same = now.Root[e + k] == entries[k];
                        if (same) at = e;
                    }
                    bool wasFree = at >= 0;
                    for (int e = at; wasFree && e < at + entries.Length; e += 32) wasFree = was.Root[e] == 0 || was.Root[e] == 0xE5;
                    var clusters = new List<int>();
                    string wrong = wasFree ? Owned(now, folder, clusters, 0) : null;
                    if (!wasFree) why = "a folder " + SystemVolumeInformation + " is in the root directory where the image has an entry";
                    else if (wrong != null) why = SystemVolumeInformation + ": " + wrong;
                    else
                    {
                        allowed.Add(new long[] { was.RootAt + at, was.RootAt + at + entries.Length });
                        foreach (int c in clusters)
                        {
                            if (was.Table[c] != 0) { why = SystemVolumeInformation + " is in cluster " + c + ", which the image uses"; break; }
                            for (int copy = 0; copy < was.Fats; ++copy)
                                allowed.Add(new long[] { was.FatAt + copy * fatBytes + 2L * c, was.FatAt + copy * fatBytes + 2L * c + 2 });
                            allowed.Add(new long[] { now.ClusterAt(c), now.ClusterAt(c) + now.ClusterBytes });
                        }
                        if (why != null) allowed.Clear();
                    }
                }
            }
            var a = new byte[1 << 20];
            var b = new byte[1 << 20];
            expected.Seek(0, SeekOrigin.Begin);
            actual.Seek(0, SeekOrigin.Begin);
            for (long done = 0; done < length; done += a.Length)
            {
                int want = (int)Math.Min(a.Length, length - done);
                ReadFull(expected, a, want);
                ReadFull(actual, b, want);
                for (int block = 0; block < want; block += 4096)
                {
                    int end = Math.Min(block + 4096, want);
                    bool differs = false, outside = false;
                    for (int i = block; i < end && !outside; ++i)
                    {
                        if (a[i] == b[i]) continue;
                        differs = true;
                        long where = done + i;
                        int changed = a[i] ^ b[i];
                        bool ok = (where == dirtyByte && (changed & ~0x03) == 0) || (inUse.Contains(where) && (changed & ~0xC0) == 0);
                        foreach (long[] r in allowed) if (where >= r[0] && where < r[1]) ok = true;
                        if (!ok) outside = true;
                    }
                    if (differs)
                    {
                        if (outside) ++other;
                        else ++windows;
                    }
                }
            }
        }

        const string SameSystem = "... and the system partition is the same as in the other install, but for what Windows puts in a FAT " +
                                  "partition that it mounts (a folder " + SystemVolumeInformation + ", and the bits of a volume in use)";

        // The system partition that the whole-disk install left is not, byte for byte, what was written (and read back the
        // same by the install itself). Windows may have mounted it meanwhile (it is a FAT partition, and the test reads it
        // through its volume) and put in it what it puts in every FAT that it mounts (WindowsOnly): that, and nothing
        // else, may differ. The blocks of each kind are said; anything else fails, and is shown (Differences).
        static void WholeSystem(string directory, ImagePlan plan, int disk, PartitionInfo p, Action<string, bool> check, Action<string> say)
        {
            string expected = Path.Combine(directory, "esperado-entero.bin");
            try
            {
                using (var e = new FileStream(expected, FileMode.Create, FileAccess.ReadWrite))
                {
                    var part = new Engine.Target();
                    part.Stream = e; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
                    Engine.Write(plan, new[] { part }, Engine.ForInternalDisk(plan), null, null);
                }
                int windows, other;
                string why;
                using (var e = new FileStream(expected, FileMode.Open, FileAccess.Read))
                using (FileStream d = Native.OpenDisk(disk, false))
                using (var a = new PartOf(d, p.Offset, plan.System.Size))
                    WindowsOnly(e, a, plan.System.Size, out windows, out other, out why);
                say("   blocks of 4 KiB of the system partition, read from the disk itself, that are not what was written: " + (windows + other) +
                    "; " + windows + " of them hold what Windows puts in a FAT partition that it mounts, " + other + " anything else" +
                    (why != null ? "; " + why : ""));
                bool ok = other == 0 && why == null;
                check(SameSystem, ok);
                if (!ok) Differences(expected, plan, disk, p, say);
            }
            catch (Exception e)
            {
                check(SameSystem + ": " + e, false);
            }
            finally
            {
                try { File.Delete(expected); }
                catch (Exception) { }
            }
        }

        // The system partition that the whole-disk install left is not what was written (and read back the same by the
        // install itself), and not only by what Windows puts in it. Where it is not, read from the disk itself and not
        // through the partition and its file system, at once and six seconds later; in which part of the file system; and
        // what is there instead. expected: what was written, in a file.
        static void Differences(string expected, ImagePlan plan, int disk, PartitionInfo p, Action<string> say)
        {
            try
            {
                var where = new List<long>();
                int now = RawDifference(disk, p, expected, plan.System.Size, where);
                System.Threading.Thread.Sleep(6000);
                var whereLater = new List<long>();
                int later = RawDifference(disk, p, expected, plan.System.Size, whereLater);
                say("   blocks of 4 KiB of the system partition, read from the disk itself, that are not what was written: " + now +
                    " at once, " + later + " six seconds later");
                if (now == 0 && later == 0)
                {
                    say("   (the disk itself holds what was written: what differed came from reading it through the partition)");
                    return;
                }
                var blocks = new List<long>(where);
                foreach (long x in whereLater) if (!blocks.Contains(x)) blocks.Add(x);
                using (var a = new FileStream(expected, FileMode.Open, FileAccess.Read))
                using (FileStream d = Native.OpenDisk(disk, false))
                {
                    string why;
                    Fat16 fat = Fat16.Open(DiagFolder.Reader(a, a.Length), a.Length, out why);
                    Dictionary<int, string> owners = fat == null ? new Dictionary<int, string>() : ClusterOwners(fat);
                    foreach (long at in blocks)
                    {
                        var was = new byte[4096];
                        var there = new byte[4096];
                        a.Seek(at, SeekOrigin.Begin);
                        ReadFull(a, was, was.Length);
                        d.Seek(p.Offset + at, SeekOrigin.Begin);
                        ReadFull(d, there, there.Length);
                        int first = 0, count = 0, zeros = 0;
                        while (first < 4096 && was[first] == there[first]) ++first;
                        for (int i = 0; i < 4096; ++i)
                        {
                            if (was[i] != there[i]) ++count;
                            if (there[i] == 0) ++zeros;
                        }
                        if (first == 4096) first = 0;
                        say("   at " + at + " (" + (fat == null ? why : Region(fat, owners, at + first)) + "): " + count +
                            " bytes differ, the first at +" + first + (zeros == 4096 ? "; on the disk the block is all zeros" : "") +
                            "; the block starts on the disk with " + Bytes(there, 0));
                        say("      written " + Bytes(was, first));
                        say("      on disk " + Bytes(there, first));
                    }
                }
            }
            catch (Exception e)
            {
                say("   the differences could not be looked at: " + e.Message);
            }
        }

        // The folder hades-diag, where the diagnostics of Hades leave what they saved on the system partition, is
        // written there by the FAT driver of Windows itself (long names, names in small letters, files of no
        // cluster, of one, of a part of one and of many), and is still there after an update, as that driver reads
        // it: every file, with the same hash. It is then taken away again, so that what follows finds the partition
        // as it was. The system partition has the letter "second".
        static void KeptByWindows(ImagePlan plan, char second, Action<string, bool> check, Action<string> say)
        {
            string diag = second + ":\\hades-diag";
            string[] files = { @"README.txt", @"LEEME.txt", @"start-1\1-minute\kodi.log.gz", @"start-1\1-minute\empty.txt",
                               @"start-1\.filtered", @"start-1\shutdown\dmesg.txt", @"start-2\during-dv-1\edid\card1-HDMI-A-1.bin",
                               @"start-2\4-minutes\results.txt", @"start-10\after-dv-1\a name longer than one entry of a long name.txt" };
            int[] sizes = { 4900, 5100, 700000, 0, 0, 16384, 256, 16385, 777 };
            var made = new Dictionary<string, string>();
            var random = new Random(7);
            using (var sha = SHA256.Create())
                for (int i = 0; i < files.Length; ++i)
                {
                    string path = Path.Combine(diag, files[i]);
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    var bytes = new byte[sizes[i]];
                    random.NextBytes(bytes);
                    File.WriteAllBytes(path, bytes);
                    made[files[i]] = Words.Hex(sha.ComputeHash(bytes));
                }
            DiagKeep kept = null;
            string failed = "";
            try { kept = Operations.Update(Find(), plan, Place.Internal, false, Quiet); }
            catch (Exception e) { failed = e.ToString(); }
            say("   hades-diag: " + (kept != null ? kept.Summary() : failed));
            // (Windows takes a moment to mount the partition again)
            bool same = false;
            string differ = "";
            for (int wait = 0; wait < 30 && !same; ++wait)
            {
                try
                {
                    same = true;
                    using (var sha = SHA256.Create())
                        foreach (KeyValuePair<string, string> f in made)
                        {
                            string path = Path.Combine(diag, f.Key);
                            if (!File.Exists(path) || Words.Hex(sha.ComputeHash(File.ReadAllBytes(path))) != f.Value)
                            {
                                same = false;
                                differ = f.Key;
                                break;
                            }
                        }
                }
                catch (Exception e) { same = false; differ = e.Message; }
                if (!same) System.Threading.Thread.Sleep(500);
            }
            if (!same) say("   not as it was written: " + differ);
            check("the folder hades-diag, written by the FAT driver of Windows on the system partition (" + second + ":), is kept " +
                  "by an update, which says so and reads it back the same, and Windows reads every file of it back with its hash",
                  kept != null && kept.State == DiagKeep.Outcome.Kept && kept.Verified && kept.Files == files.Length && same);
            try { Directory.Delete(diag, true); }
            catch (Exception e) { say("   it could not be taken away again: " + e.Message); }
        }

        // A system partition that a program keeps open, as WSL keeps the drives that were there when it
        // started (its file server holds a folder of each). The update leaves such a partition alone and
        // says so. Told to take it, it replaces it whole, and this is looked at:
        //  - what the program had open stops working;
        //  - for as long as the partition is being written, the program can neither open a file of it, nor
        //    write a new one, nor write to the file that it had open for writing, however much it insists
        //    (before that it could do all three: Pest);
        //  - nothing of the old partition comes to be written over the new one afterwards. The update reads
        //    back what it wrote before it lets go of the partition, so it could not see that: here the old
        //    partition is given 32 MiB of a file of that program and is being written to when it is taken,
        //    and the new one is read from the disk itself some seconds after the update, twice;
        //  - Windows then shows the new partition, with its files whole and without those of the program.
        // The system partition has the letter "second"; nothing on the disk has changed since "tableBefore"
        // and "kernelBefore" were taken.
        static void Taken(string directory, ImagePlan plan, DiskInfo withLetter, PartitionInfo w1, PartitionInfo w2, char second,
                          string tableBefore, string kernelBefore, Action<string, bool> check, Action<string> say)
        {
            bool withSwitch = plan.SwitchOffset >= 0;
            string root = second + ":\\";
            string kernel = root + "KERNEL", off = root + "DIAG-OFF.TXT", on = root + "DIAG-ON.TXT";
            string scrap = root + "pest.txt", junkPath = root + "junk.bin";
            string systemBefore = Engine.FileHash(root + "SYSTEM", null, null);
            long freeBefore = new DriveInfo(second.ToString()).AvailableFreeSpace;
            // what the partition has to hold once it has been taken and updated: written to a file by the same code
            string expected = Path.Combine(directory, "esperado-tomada.bin");
            using (var a = new FileStream(expected, FileMode.Create, FileAccess.ReadWrite))
            {
                var part = new Engine.Target();
                part.Stream = a; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part },
                             Engine.Patches(plan, true, false, Disks.StorageUuid(withLetter.Number, w2), withSwitch), null, null);
            }
            FileStream junk = null;
            Pest pest = null;
            try
            {
                using (Microsoft.Win32.SafeHandles.SafeFileHandle folder = Native.OpenFolder(root))
                using (var open = new FileStream(kernel, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    string inUse = "";
                    try { Operations.Update(Find(), plan, Place.Internal, withSwitch, Quiet); }
                    catch (PartitionInUseException e) { inUse = e.Message; }
                    say("   " + inUse);
                    check("a system partition that a program has open (" + second + ":): the update leaves it alone and says so; " +
                          "its files and the partitions of the disk are as they were",
                          inUse.Length > 0 && Table(Find()) == tableBefore && (!withSwitch || (File.Exists(off) && !File.Exists(on))) &&
                              Engine.FileHash(kernel, null, null) == kernelBefore);

                    // the program changes the old partition through the file system, and does not stop
                    junk = new FileStream(junkPath, FileMode.Create, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete);
                    var noise = new byte[1 << 20];
                    new Random(11).NextBytes(noise);
                    for (int i = 0; i < 32; ++i) junk.Write(noise, 0, noise.Length);
                    junk.Flush();
                    pest = new Pest(kernel, scrap, junk);
                    for (int wait = 0; wait < 100 && (pest.ReadsBefore == 0 || pest.WritesBefore == 0 || pest.OwnBefore == 0); ++wait)
                        System.Threading.Thread.Sleep(100);
                    check("before the partition is taken, that program opens a file of it, writes a file of its own there and goes on " +
                          "writing to another that it keeps open",
                          pest.ReadsBefore > 0 && pest.WritesBefore > 0 && pest.OwnBefore > 0 && File.Exists(scrap));

                    // (from the first report of what is written until nine tenths of what is read back, the
                    // partition is held by the update: Operations.Bar. The last tenth is left out so that
                    // nothing that the program started inside that time can end after it.)
                    Operations.Report watch = delegate(int permille, string text)
                    {
                        if (permille >= 900) pest.Phase = 2;
                        else if (permille >= 50 && pest.Phase == 0) pest.Phase = 1;
                    };
                    string failed = "";
                    try { Operations.Update(Find(), plan, Place.Internal, withSwitch, watch, true); }
                    catch (Exception e) { failed = e.ToString(); }
                    pest.End();
                    bool dead = false;
                    try { open.Seek(1 << 20, SeekOrigin.Begin); open.ReadByte(); }
                    catch (IOException) { dead = true; }
                    say("   while it was written, that program tried " + pest.Tries + " times: files opened " + pest.Reads +
                        ", files written " + pest.Writes + ", writes to the file it had open " + pest.Own +
                        (failed.Length > 0 ? "; " + failed : ""));
                    check("told to take it, the update replaces it (read back, it is what was written), and the file that the program " +
                          "had open for reading no longer works", failed.Length == 0 && dead);
                    check("... while it was being written, that program could not open a file of it, nor write a new one, nor write " +
                          "to the one that it had open", pest.Tries > 0 && pest.Reads == 0 && pest.Writes == 0 && pest.Own == 0);

                    var where = new List<long>();
                    System.Threading.Thread.Sleep(3000);
                    int soon = RawDifference(withLetter.Number, w1, expected, plan.System.Size, where);
                    System.Threading.Thread.Sleep(6000);
                    int later = RawDifference(withLetter.Number, w1, expected, plan.System.Size, new List<long>());
                    var places = new StringBuilder();
                    foreach (long at in where) places.Append(' ').Append(at);
                    say("   blocks of 4 KiB of the partition that are not what was written: " + soon + " three seconds after the update, " +
                        later + " some seconds later" + (where.Count > 0 ? "; the first ones at" + places : ""));
                    check("... nothing of the old partition is written over the new one afterwards: read from the disk, it is what was " +
                          "written, but for what Windows itself writes on a partition that it mounts (64 blocks of 4 KiB at most; " +
                          "the file of that program was more than 8000)", soon <= 64 && later <= 64);

                    // Windows mounts what was written, not what it remembered of the partition
                    bool shown = false;
                    for (int wait = 0; wait < 30 && !shown; ++wait)
                    {
                        try { shown = File.Exists(kernel) && (!withSwitch || File.Exists(on)); }
                        catch (Exception) { }
                        if (!shown) System.Threading.Thread.Sleep(500);
                    }
                    long freeAfter = shown ? new DriveInfo(second.ToString()).AvailableFreeSpace : -1;
                    say("   free on it: " + freeBefore + " before that program wrote to it, " + freeAfter + " after the update");
                    check("... and Windows then shows the partition as it was written: " +
                          (withSwitch ? "the switch of the diagnostics is on, " : "") +
                          "its kernel and its system are those of the image, the files of that program are not on it, and the room " +
                          "that they took is free",
                          shown && (!withSwitch || (!File.Exists(off) && Disks.DiagnosticsOn(withLetter.Number, w1))) &&
                              !File.Exists(scrap) && !File.Exists(junkPath) && Math.Abs(freeAfter - freeBefore) <= (4L << 20) &&
                              Table(Find()) == tableBefore && Engine.FileHash(kernel, null, null) == kernelBefore &&
                              Engine.FileHash(root + "SYSTEM", null, null) == systemBefore);
                }
            }
            finally
            {
                // (whatever happened, that program stops before its file is closed)
                if (pest != null) pest.End();
                // (the handle of that file is dead: closing it cannot write what was left to write, and says so)
                if (junk != null)
                    try { junk.Dispose(); }
                    catch (IOException) { }
                try { File.Delete(expected); }
                catch (Exception) { }
            }
        }

        public static int Run(string directory, string image)
        {
            Directory.CreateDirectory(directory);
            L.Spanish = false; // what the operations say goes into the report, which is in English
            string report = Path.Combine(directory, "prueba-vhd.txt");
            int failures = 0, notRun = 0, found = 0;
            using (var w = new StreamWriter(report, false, new UTF8Encoding(false)))
            {
                Action<string, bool> check = delegate(string what, bool ok)
                {
                    w.WriteLine((ok ? "ok:   " : "FAIL: ") + what);
                    w.Flush();
                    if (!ok) ++failures;
                };
                Action<string> say = delegate(string text) { w.WriteLine(text); w.Flush(); };
                if (!Pc.Elevated) { say("FAIL: this test needs administrator rights"); return 1; }
                Log.Open(directory);
                Disks.AllowVirtual = true;
                vhd = Path.Combine(directory, "hades-prueba.vhd");
                bool attached = false;
                try
                {
                    foreach (char c in "ZYXWVUTSR")
                        if (!Directory.Exists(c + ":\\")) { letter = c; break; }
                    if (letter == '\0') throw new InvalidOperationException("No free drive letter.");
                    if (File.Exists(vhd)) File.Delete(vhd);
                    say("virtual disk: " + vhd + " as " + letter + ":");
                    DiskPart("create vdisk file=\"" + vhd + "\" maximum=" + VhdMegabytes + " type=expandable\r\n" +
                             "select vdisk file=\"" + vhd + "\"\r\nattach vdisk\r\nconvert gpt\r\ncreate partition primary\r\n" +
                             "format fs=ntfs quick label=PRUEBA\r\nassign letter=" + letter + "\r\n");
                    attached = true;
                    System.Threading.Thread.Sleep(2000);
                    DiskInfo disk = Find();
                    say("it is disk " + disk.Number + ": " + disk.Title + "; " + disk.Detail + "; location " + disk.Location);

                    string files = letter + ":\\datos";
                    Directory.CreateDirectory(files);
                    var random = new Random(7);
                    for (int i = 0; i < 40; ++i)
                    {
                        var data = new byte[4 << 20];
                        random.NextBytes(data);
                        File.WriteAllBytes(Path.Combine(files, "archivo-" + i.ToString("00") + ".bin"), data);
                    }
                    string before = FilesHash(files);
                    PartitionInfo volume = Lettered(disk);
                    long volumeSize = volume.Size;
                    int partitionsBefore = disk.Partitions.Count;

                    ImagePlan plan = Engine.Plan(image, true, null, null);
                    say("image: " + Path.GetFileName(image) + ", " + plan.Size + " bytes, storage anywhere: " + plan.StorageAnywhere);
                    List<Patch> patches = Engine.ForInternalDisk(plan);
                    // what the two partitions have to hold, written to files by the same code
                    string expectSystem, expectStorage;
                    string sysFile = Path.Combine(directory, "esperado-sistema.bin"), stoFile = Path.Combine(directory, "esperado-almacenamiento.bin");
                    using (var a = new FileStream(sysFile, FileMode.Create, FileAccess.ReadWrite))
                    using (var b = new FileStream(stoFile, FileMode.Create, FileAccess.ReadWrite))
                    {
                        var parts = new Engine.Target[2];
                        parts[0] = new Engine.Target();
                        parts[0].Stream = a; parts[0].Capacity = plan.System.Size; parts[0].From = plan.System.Start; parts[0].Length = plan.System.Size;
                        parts[1] = new Engine.Target();
                        parts[1].Stream = b; parts[1].Capacity = plan.Storage.Size; parts[1].From = plan.Storage.Start; parts[1].Length = plan.Storage.Size;
                        Engine.Write(plan, parts, patches, null, null);
                        expectSystem = parts[0].Result.Written;
                        expectStorage = parts[1].Result.Written;
                    }
                    File.Delete(sysFile);
                    File.Delete(stoFile);

                    if (plan.StorageAnywhere)
                    {
                        // ---- next to the data
                        long storage = Operations.StorageLeast;
                        Find();
                        Operations.InstallBeside(disk, volume, storage, plan, false, Quiet);
                        DiskInfo now = Find();
                        PartitionInfo system, store;
                        bool has = Disks.FindHades(now, out system, out store);
                        check("next to the data: Hades is in two new partitions, after the volume", has &&
                              now.Partitions.Count == partitionsBefore + 2 && system.Offset >= Lettered(now).End);
                        if (has)
                        {
                            say("   system #" + system.Number + " at " + system.Offset + " size " + system.Size + " type " + system.GptType +
                                "; storage #" + store.Number + " at " + store.Offset + " size " + store.Size + " type " + store.GptType);
                            check("... the system one is an EFI partition of the size of the one in the image, and the storage one is a " +
                                  "Linux partition and has the space that was asked for",
                                  system.GptType == Disks.EfiType && system.Size == plan.System.Size && store.GptType == Disks.LinuxType &&
                                      store.Size >= storage - (4L << 20) && store.Size <= storage + (4L << 20));
                            string gotSystem, gotStorage;
                            using (FileStream f = Native.OpenPartition(now.Number, system.Number, system.Offset, system.Size))
                                gotSystem = Engine.ReadHash(f, plan.System.Size, null, null);
                            using (FileStream f = Native.OpenPartition(now.Number, store.Number, store.Offset, store.Size))
                                gotStorage = Engine.ReadHash(f, plan.Storage.Size, null, null);
                            check("... and they hold, byte for byte, the partitions of the image with the menus changed",
                                  gotSystem == expectSystem && gotStorage == expectStorage);
                        }
                        if (has)
                        {
                            // an update over it: the same image, so the same system partition; nothing else moves
                            string storageBefore;
                            using (FileStream f = Native.OpenPartition(now.Number, store.Number, store.Offset, store.Size))
                                storageBefore = Engine.ReadHash(f, plan.Storage.Size, null, null);
                            check("... the installer reads the identifier of the storage that is on the disk",
                                  Disks.StorageUuid(now.Number, store) == plan.StorageUuid);
                            Find();
                            Operations.Update(now, plan, Place.Internal, false, Quiet);
                            string systemAfter, storageAfter;
                            using (FileStream f = Native.OpenPartition(now.Number, system.Number, system.Offset, system.Size))
                                systemAfter = Engine.ReadHash(f, plan.System.Size, null, null);
                            using (FileStream f = Native.OpenPartition(now.Number, store.Number, store.Offset, store.Size))
                                storageAfter = Engine.ReadHash(f, plan.Storage.Size, null, null);
                            check("update: the system partition is left as the one of the image, and the storage partition and the " +
                                  "files of the volume do not change", systemAfter == expectSystem && storageAfter == storageBefore && FilesHash(files) == before);
                            // the switch of the diagnostics: an update turns it on, and the next one, without it, off again
                            if (plan.SwitchOffset >= 0)
                            {
                                bool offBefore = !Disks.DiagnosticsOn(now.Number, system);
                                Find();
                                Operations.Update(now, plan, Place.Internal, true, Quiet);
                                bool on = Disks.DiagnosticsOn(now.Number, system);
                                Find();
                                Operations.Update(now, plan, Place.Internal, false, Quiet);
                                string systemBack;
                                using (FileStream f = Native.OpenPartition(now.Number, system.Number, system.Offset, system.Size))
                                    systemBack = Engine.ReadHash(f, plan.System.Size, null, null);
                                check("update with the diagnostics: the disk has them on after it, and after an update without them it " +
                                      "has them off and its system partition is the one of the image again",
                                      offBefore && on && !Disks.DiagnosticsOn(now.Number, system) && systemBack == expectSystem);
                            }
                            else
                            {
                                say("NOT RUN: this image has no switch for the diagnostics.");
                                ++notRun;
                            }
                        }
                        check("... the volume is smaller by just what it gave up, and its files are the same",
                              Lettered(now) != null && Lettered(now).Size <= volumeSize - storage - plan.System.Size &&
                                  Lettered(now).Size >= volumeSize - storage - plan.System.Size - (8L << 20) && FilesHash(files) == before);

                        // ---- and off again
                        Find();
                        string told = Operations.Uninstall(now, plan.Path, Quiet);
                        say("   " + told);
                        DiskInfo back = Find();
                        check("removing Hades: its partitions are gone, the volume has its size back and its files are the same",
                              back.Partitions.Count == partitionsBefore && Math.Abs(Lettered(back).Size - volumeSize) <= (2L << 20) &&
                                  FilesHash(files) == before);
                        disk = back;

                        // ---- the image on the disk itself: a PC with one data disk, and the folder of the installer
                        // put on it. Room can be made on that disk (its volume is shrunk while the image is read from
                        // it), its Hades can be updated and taken off, and the disk is never erased.
                        string folderHere = letter + ":\\instalador";
                        Directory.CreateDirectory(folderHere);
                        string imageHere = Path.Combine(folderHere, Path.GetFileName(image));
                        File.Copy(image, imageHere, true);
                        if (File.Exists(image + ".sha256")) File.Copy(image + ".sha256", imageHere + ".sha256", true);
                        string imageHash = Engine.FileHash(imageHere, null, null);
                        ImagePlan planHere = Engine.Plan(imageHere, true, null, null);
                        DiskInfo diskHere = Find();
                        PartitionInfo volumeHere = Lettered(diskHere);
                        long sizeHere = volumeHere.Size;
                        check("the image on the disk itself: the disk is one that holds what must be kept",
                              Disks.Kept(diskHere, Disks.Keeping(imageHere)) == letter + ":" &&
                                  Disks.Kept(diskHere, Disks.Keeping(image)) == null);
                        // (what a path through a folder of another disk, or a SUBST letter, depends on)
                        bool byPlace = false;
                        foreach (Extent x in Native.Extents(imageHere))
                            if (x.Disk == diskHere.Number && x.Offset == volumeHere.Offset) byPlace = true;
                        check("... and Windows says where that image really is: on the volume of this disk", byPlace);
                        // (refused for being that disk, and not because Windows would not erase it)
                        string kept = "(" + letter + ":)";
                        string refusal = "";
                        Find();
                        try { Operations.InstallWhole(diskHere, planHere, Place.Internal, false, Quiet); }
                        catch (InvalidOperationException e) { refusal = e.Message; }
                        say("   " + refusal);
                        DiskInfo untouched = Find();
                        check("... using it whole is refused, for what it holds, and nothing of it changes: its partitions, its " +
                              "files, the image",
                              refusal.Contains(kept) && untouched.Partitions.Count == partitionsBefore && Lettered(untouched) != null &&
                                  Lettered(untouched).Size == sizeHere && FilesHash(files) == before &&
                                  Engine.FileHash(imageHere, null, null) == imageHash);

                        Find();
                        Operations.InstallBeside(diskHere, volumeHere, storage, planHere, false, Quiet);
                        DiskInfo nowHere = Find();
                        PartitionInfo systemHere, storeHere;
                        bool hasHere = Disks.FindHades(nowHere, out systemHere, out storeHere);
                        check("... room is made on it: Hades is in two new partitions after the volume, which is smaller by just what " +
                              "it gave up; its files and the image are the same",
                              hasHere && nowHere.Partitions.Count == partitionsBefore + 2 && systemHere.Offset >= Lettered(nowHere).End &&
                                  Lettered(nowHere).Size <= sizeHere - storage - planHere.System.Size &&
                                  Lettered(nowHere).Size >= sizeHere - storage - planHere.System.Size - (8L << 20) &&
                                  FilesHash(files) == before && Engine.FileHash(imageHere, null, null) == imageHash);
                        if (hasHere)
                        {
                            string systemGot, storageGot;
                            using (FileStream f = Native.OpenPartition(nowHere.Number, systemHere.Number, systemHere.Offset, systemHere.Size))
                                systemGot = Engine.ReadHash(f, planHere.System.Size, null, null);
                            using (FileStream f = Native.OpenPartition(nowHere.Number, storeHere.Number, storeHere.Offset, storeHere.Size))
                                storageGot = Engine.ReadHash(f, planHere.Storage.Size, null, null);
                            check("... and the two partitions hold what the image has, as when the image was elsewhere",
                                  systemGot == expectSystem && storageGot == expectStorage);
                            Find();
                            Operations.Update(nowHere, planHere, Place.Internal, false, Quiet);
                            using (FileStream f = Native.OpenPartition(nowHere.Number, systemHere.Number, systemHere.Offset, systemHere.Size))
                                systemGot = Engine.ReadHash(f, planHere.System.Size, null, null);
                            string storageThen;
                            using (FileStream f = Native.OpenPartition(nowHere.Number, storeHere.Number, storeHere.Offset, storeHere.Size))
                                storageThen = Engine.ReadHash(f, planHere.Storage.Size, null, null);
                            check("... its Hades can be updated with the image that is on it: the system partition as in the image, " +
                                  "the storage, the files and the image unchanged",
                                  systemGot == expectSystem && storageThen == storageGot && FilesHash(files) == before &&
                                      Engine.FileHash(imageHere, null, null) == imageHash);
                            string refusalAgain = "";
                            Find();
                            try { Operations.InstallWhole(nowHere, planHere, Place.Internal, false, Quiet); }
                            catch (InvalidOperationException e) { refusalAgain = e.Message; }
                            DiskInfo still = Find();
                            PartitionInfo systemStill, storeStill;
                            check("... using it whole is refused with Hades on it too, for what it holds, and Hades and the files stay",
                                  refusalAgain.Contains(kept) && Disks.FindHades(still, out systemStill, out storeStill) &&
                                      still.Partitions.Count == partitionsBefore + 2 && FilesHash(files) == before);
                            Find();
                            say("   " + Operations.Uninstall(nowHere, planHere.Path, Quiet));
                            DiskInfo backHere = Find();
                            check("... and Hades can be taken off it: its partitions are gone, the volume has its size back, and " +
                                  "its files and the image are the same",
                                  backHere.Partitions.Count == partitionsBefore && Math.Abs(Lettered(backHere).Size - sizeHere) <= (2L << 20) &&
                                      FilesHash(files) == before && Engine.FileHash(imageHere, null, null) == imageHash);
                        }
                        Directory.Delete(folderHere, true);
                        disk = Find();
                    }
                    else
                    {
                        ++notRun;
                        say("NOT RUN: this image does not say that /storage can be any partition: nothing next to the data");
                    }

                    // ---- the whole disk. A program has a folder of its volume open meanwhile, as WSL has of
                    // every drive that was there when it started. Whether Windows erases such a disk all the same
                    // is not decided by this program: it is found out here, and said.
                    DiskInfo beforeWhole = Find();
                    string tableWhole = Table(beforeWhole);
                    Exception heldError = null;
                    using (Microsoft.Win32.SafeHandles.SafeFileHandle heldFolder = Native.OpenFolder(letter + ":\\"))
                    {
                        try { Operations.InstallWhole(beforeWhole, plan, Place.Internal, false, Quiet); }
                        catch (Exception e) { heldError = e; }
                    }
                    if (heldError == null)
                        say("   (a program had a folder of the volume of that disk open: the disk was erased and written all the same)");
                    else
                    {
                        // Only a disk that was not erased, and that is as it was, is something found out. An
                        // install that fails in any other way has failed, whatever comes after it.
                        DiskInfo still = Find();
                        if (Table(still) == tableWhole && Lettered(still) != null && FilesHash(files) == before)
                        {
                            ++found;
                            say("FOUND: a disk is not erased while a program has a folder of its volume open, and nothing of it " +
                                "changes: " + heldError.Message);
                        }
                        else
                            check("the whole disk, with a folder of its volume open in a program: installed, or left as it was: " +
                                  heldError, false);
                        Operations.InstallWhole(Find(), plan, Place.Internal, false, Quiet);
                    }
                    DiskInfo whole = Find();
                    PartitionInfo s1, s2;
                    bool hades = Disks.FindHades(whole, out s1, out s2);
                    check("whole disk: the partition table that was written is the one that Windows reads, with the system partition " +
                          "as an EFI partition and the storage partition after it",
                          hades && whole.Partitions.Count == 2 && s1.GptType == Disks.EfiType && s1.Offset == plan.System.Start &&
                              s1.Size == plan.System.Size && s2.GptType == Disks.LinuxType && s2.Offset == plan.Storage.Start);
                    if (hades)
                    {
                        string got;
                        using (FileStream f = Native.OpenPartition(whole.Number, s1.Number, s1.Offset, s1.Size))
                            got = Engine.ReadHash(f, plan.System.Size, null, null);
                        // (what the install wrote was read back the same by the install itself: what is not the same now
                        // came after it. Windows may have mounted the partition meanwhile, and that alone is allowed)
                        if (got == expectSystem) check(SameSystem, true);
                        else WholeSystem(directory, plan, whole.Number, s1, check, say);
                    }

                    Find();
                    string said = Operations.Uninstall(whole, plan.Path, Quiet);
                    say("   " + said);
                    DiskInfo empty = Find();
                    bool usable = false;
                    foreach (PartitionInfo p in empty.Partitions)
                        if (p.Letter != '\0' && Disks.Volume(p.Letter).FileSystem == "NTFS" && p.Size > empty.Size - (64L << 20)) usable = true;
                    check("removing Hades from a whole disk: an empty NTFS volume that takes up the disk is left", usable);

                    // ---- this program or the image on a partition of Hades itself. It happens with a disk that was
                    // written as a USB drive is and then put inside the PC: Windows gives its system partition a
                    // letter. That Hades is neither updated nor taken off while they are there. Here Hades is put
                    // on the whole disk once more, its system partition is given a letter, and the path of the
                    // image is one on it: the path is all that is looked at. (Last, and with an install of its
                    // own: nothing of what was checked above depends on it.)
                    {
                        Operations.InstallWhole(Find(), plan, Place.Internal, false, Quiet);
                        DiskInfo usbLike = Find();
                        PartitionInfo u1, u2;
                        bool installed = Disks.FindHades(usbLike, out u1, out u2);
                        check("Hades is on the whole disk again", installed);
                        char second = '\0';
                        foreach (char c in "ZYXWVUTSR")
                            if (c != letter && !Directory.Exists(c + ":\\")) { second = c; break; }
                        bool given = false;
                        if (installed)
                        {
                            try
                            {
                                if (second == '\0') throw new InvalidOperationException("no free drive letter");
                                Find();
                                Disks.AddLetter(usbLike.Number, u1.Number, second);
                                // (Windows takes a moment to mount it)
                                for (int wait = 0; wait < 30 && !File.Exists(second + ":\\KERNEL"); ++wait) System.Threading.Thread.Sleep(500);
                                if (!File.Exists(second + ":\\KERNEL")) throw new InvalidOperationException("Windows does not show its files");
                                given = true;
                            }
                            catch (Exception e)
                            {
                                ++notRun;
                                say("NOT RUN: the system partition of Hades could not be given a drive letter (" + e.Message +
                                    "): nothing about this program or the image on a partition of Hades");
                            }
                        }
                        if (given)
                        {
                            DiskInfo withLetter = Find();
                            PartitionInfo w1, w2;
                            if (Disks.FindHades(withLetter, out w1, out w2) && w1.Letter == second)
                            {
                                string pathThere = second + ":\\" + Path.GetFileName(image);
                                ImagePlan planThere = Engine.Plan(image, true, null, null);
                                planThere.Path = pathThere;
                                string tableBefore = Table(withLetter);
                                string kernelBefore = Engine.FileHash(second + ":\\KERNEL", null, null);
                                string whyUpdate = "", whyRemove = "";
                                try { Operations.Update(withLetter, planThere, Place.Internal, false, Quiet); }
                                catch (InvalidOperationException e) { whyUpdate = e.Message; }
                                Find();
                                try { Operations.Uninstall(withLetter, pathThere, Quiet); }
                                catch (InvalidOperationException e) { whyRemove = e.Message; }
                                say("   " + whyRemove);
                                DiskInfo stays = Find();
                                string partition = "(" + second + ":)";
                                check("this program or the image on a partition of Hades (" + second + ":): that Hades is not updated " +
                                      "and is not taken off, and nothing of the disk changes",
                                      whyUpdate.Contains(partition) && whyRemove.Contains(partition) && Table(stays) == tableBefore &&
                                          Engine.FileHash(second + ":\\KERNEL", null, null) == kernelBefore);

                                KeptByWindows(plan, second, check, say);
                                Taken(directory, plan, withLetter, w1, w2, second, tableBefore, kernelBefore, check, say);
                            }
                            else
                                check("the system partition of Hades, with the letter " + second + ":, is still found as such", false);
                        }
                    }
                }
                catch (Exception e)
                {
                    check("the test ends without an exception: " + e, false);
                }
                finally
                {
                    try
                    {
                        if (attached) DiskPart("select vdisk file=\"" + vhd + "\"\r\ndetach vdisk\r\n");
                        if (File.Exists(vhd)) File.Delete(vhd);
                        say("virtual disk detached and deleted");
                    }
                    catch (Exception e) { say("FAIL: the virtual disk could not be removed: " + e.Message); ++failures; }
                }
                // (a part that could not be run is not a part that passed: the last line says so)
                // (and something that was found out is said with the result, whatever the result is)
                say((failures > 0 ? "FAILED: " + failures
                                  : notRun > 0 ? "PASSED, BUT " + notRun + " PART(S) NOT RUN: see above" : "ALL PASSED") +
                    (found > 0 ? " (" + found + " FOUND: see above)" : ""));
            }
            return failures;
        }
    }
}
