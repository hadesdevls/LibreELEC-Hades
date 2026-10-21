// LibreELEC Hades installer for Windows: the folder hades-diag, where the diagnostics of Hades leave what they saved
// (packages/sysutils/hades-diag), kept across an update. An update replaces the system partition whole, and that
// folder is on it. So it is read from the partition before the partition is written over, and goes into the new one
// as the image is written: into clusters that are free in the image, entries of the root directory that are free
// there, and the two FATs, as patches like the other changes of an update. What the patches make is read back the
// way it is read on a disk before anything is written, and the folder is read back from the disk afterwards, file by
// file. Whatever goes wrong with the folder leaves the update as it would be without it, and is said: what was kept
// and what was lost. Only FAT16 is known here, which is what the system partitions of the images are (scripts/mkimage).
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;

namespace Hades.Installer
{
    // A FAT16 file system, read through a function that gives the bytes at an offset of its partition. Its two FATs
    // are read, its root directory, and the clusters that are asked for.
    sealed class Fat16
    {
        public const int Sector = 512;
        public int SectorsPerCluster, Reserved, Fats, RootEntries, FatSectors, ClusterBytes, Clusters;
        public long FatAt, RootAt, DataAt; // bytes from the start of the partition
        public ushort[] Table;             // the first FAT: an entry for each cluster, 2 to Clusters + 1
        public byte[] Root;
        public bool SameFats;              // the second FAT, where there is one, is the first
        readonly Func<long, int, byte[]> read;

        Fat16(Func<long, int, byte[]> read) { this.read = read; }

        public static int U16(byte[] b, int at) { return b[at] | (b[at + 1] << 8); }

        // null, with why, where it is not a FAT16 that this program knows. size: how long the partition is.
        public static Fat16 Open(Func<long, int, byte[]> read, long size, out string why)
        {
            why = null;
            byte[] boot = read(0, Sector);
            if (boot[510] != 0x55 || boot[511] != 0xAA) { why = "it has no boot sector"; return null; }
            var f = new Fat16(read);
            int bytesPerSector = U16(boot, 11);
            f.SectorsPerCluster = boot[13];
            f.Reserved = U16(boot, 14);
            f.Fats = boot[16];
            f.RootEntries = U16(boot, 17);
            long total = U16(boot, 19);
            if (total == 0) total = BitConverter.ToUInt32(boot, 32);
            f.FatSectors = U16(boot, 22);
            int spc = f.SectorsPerCluster;
            if (bytesPerSector != Sector || spc < 1 || spc > 128 || (spc & (spc - 1)) != 0 || f.Reserved < 1 || f.Fats < 1 ||
                f.Fats > 2 || f.RootEntries < 16 || f.RootEntries % 16 != 0 || f.FatSectors < 1 || total * Sector > size)
            {
                why = "its file system is not a FAT that this program knows";
                return null;
            }
            f.ClusterBytes = spc * Sector;
            f.FatAt = (long)f.Reserved * Sector;
            f.RootAt = f.FatAt + (long)f.Fats * f.FatSectors * Sector;
            f.DataAt = f.RootAt + f.RootEntries * 32L;
            long clusters = (total * Sector - f.DataAt) / f.ClusterBytes;
            // (the kinds of FAT are told apart by how many clusters they have, and by nothing else)
            if (clusters < 4085 || clusters >= 65525) { why = "its file system is not FAT16"; return null; }
            f.Clusters = (int)clusters;
            if ((long)f.FatSectors * Sector < 2L * (f.Clusters + 2)) { why = "its FAT is too small for its clusters"; return null; }
            byte[] fat = read(f.FatAt, f.FatSectors * Sector);
            f.Table = new ushort[f.Clusters + 2];
            for (int c = 0; c < f.Table.Length; ++c) f.Table[c] = (ushort)U16(fat, 2 * c);
            f.SameFats = true;
            if (f.Fats == 2)
            {
                byte[] second = read(f.FatAt + (long)f.FatSectors * Sector, f.FatSectors * Sector);
                for (int i = 0; i < fat.Length && f.SameFats; ++i) f.SameFats = fat[i] == second[i];
            }
            f.Root = read(f.RootAt, f.RootEntries * 32);
            return f;
        }

        public long ClusterAt(int cluster) { return DataAt + (long)(cluster - 2) * ClusterBytes; }
        public byte[] Cluster(int cluster) { return read(ClusterAt(cluster), ClusterBytes); }

        // The clusters of a chain, from its first, when it is a whole one of at most "most" clusters; null when it
        // is not (it leaves the partition, reaches a free, reserved or bad cluster, comes back to itself, or is
        // longer).
        public List<int> Chain(int first, int most)
        {
            var chain = new List<int>();
            var seen = new HashSet<int>();
            int c = first;
            while (true)
            {
                if (c < 2 || c > Clusters + 1 || chain.Count >= most || !seen.Add(c)) return null;
                chain.Add(c);
                int next = Table[c];
                if (next >= 0xFFF8) return chain;
                if (next < 2 || next == 0xFFF7) return null;
                c = next;
            }
        }
    }

    // A file or a folder of hades-diag, as its directory has it.
    sealed class DiagItem
    {
        public string Name;                 // its long name where it has one, as Windows and Linux show it
        public byte[] Names = new byte[0];  // the entries that spell its long name, as they are
        public byte[] Own;                  // its own entry: short name, attributes, times, first cluster, size
        public bool Folder;
        public byte[] Data;                 // what a file holds
        public List<DiagItem> Items = new List<DiagItem>();
        internal List<int> At;              // its clusters in the new partition
        public int First { get { return Fat16.U16(Own, 26); } }
        public long Size { get { return BitConverter.ToUInt32(Own, 28); } }
        public int Entries { get { return Names.Length / 32 + 1; } }
    }

    // What became of hades-diag in an update. Patches: what puts it into the new system partition (none where it
    // is not kept).
    sealed class DiagKeep
    {
        public enum Outcome { NoFolder, Kept, NotKept, Unknown }
        public enum Cause { None, Unreadable, Damaged, TooLarge, NoRoom, NameTaken, ImageUnknown, Failed }
        public Outcome State = Outcome.NoFolder;
        public Cause Why = Cause.None;
        public string Detail = "";                       // for the log, in English
        public bool Verified;                            // read back from the disk, file by file, as it was
        public bool ReadBack;                            // it has been read back (Confirm): Verified says how
        public int Files, Folders, Damaged;              // kept; and what could not be read on the old partition
        public long Bytes;
        public List<string> LeftOut = new List<string>(); // the oldest starts, for want of room
        public List<Patch> Patches = new List<Patch>();
        internal DiagItem Folder;

        internal DiagKeep Fail(Outcome state, Cause why, string detail)
        {
            State = state;
            Why = why;
            Detail = detail;
            Patches.Clear();
            Files = Folders = 0;
            Bytes = 0;
            Verified = false;
            return this;
        }

        public string Summary()
        {
            string s = State.ToString();
            if (State == Outcome.Kept)
                s += ": " + Files + " files in " + Folders + " folders, " + Bytes + " bytes, " + Patches.Count + " patches" +
                     (LeftOut.Count > 0 ? "; left out for want of room: " + string.Join(", ", LeftOut.ToArray()) : "") +
                     (Damaged > 0 ? "; " + Damaged + " that could not be read" : "") +
                     (ReadBack ? "; read back the same: " + Verified : "; to be read back once the partition is written");
            if (Why != Cause.None) s += " (" + Why + ")";
            if (Detail.Length > 0) s += ": " + Detail;
            return s;
        }

        string CauseText()
        {
            switch (Why)
            {
                case Cause.Unreadable:
                    return L.T("the old system partition cannot be read the way the installer reads it",
                               "la partición de sistema anterior no se puede leer como la lee el instalador");
                case Cause.Damaged:
                    return L.T("the folder is damaged on the old partition", "la carpeta está dañada en la partición anterior");
                case Cause.TooLarge:
                    return L.T("it holds more than the 256 MB that the installer keeps", "ocupa más de los 256 MB que conserva el instalador");
                case Cause.NoRoom:
                    return L.T("there is no room for it in the new partition", "no cabe en la partición nueva");
                case Cause.NameTaken:
                    return L.T("the new system already has something with that name", "el sistema nuevo ya tiene algo con ese nombre");
                case Cause.ImageUnknown:
                    return L.T("the system partition of the image is not one that the installer knows how to add it to",
                               "la partición de sistema de la imagen no es una a la que el instalador sepa añadirla");
                default:
                    return L.T("of an error (" + Detail + ")", "de un error (" + Detail + ")");
            }
        }

        // What the last panel of the window says of it; empty where there was no such folder.
        public string Text()
        {
            switch (State)
            {
                case Outcome.Kept:
                    if (!Verified)
                        return L.T("What its diagnostics had saved (the folder hades-diag) was written to the new system partition, but " +
                                   "reading it back did not give the same files: do not trust that copy. The update itself is complete " +
                                   "and was checked.",
                                   "Lo que había guardado su diagnóstico (la carpeta hades-diag) se escribió en la nueva partición de " +
                                   "sistema, pero al releerla no salieron los mismos archivos: no te fíes de esa copia. La actualización " +
                                   "en sí está completa y comprobada.");
                    string kept = L.T("What its diagnostics had saved (the folder hades-diag, " + Files + (Files == 1 ? " file" : " files") +
                                      ") is in the new system partition, checked file by file.",
                                      "Lo que había guardado su diagnóstico (la carpeta hades-diag, " + Files +
                                      (Files == 1 ? " archivo" : " archivos") + ") está en la nueva partición de sistema, comprobado " +
                                      "archivo por archivo.");
                    if (LeftOut.Count > 0)
                        kept += L.T(" There was no room for its oldest starts, which are lost: ", " No cupieron sus arranques más antiguos, que " +
                                    "se pierden: ") + string.Join(", ", LeftOut.ToArray()) + ".";
                    if (Damaged > 0)
                        kept += L.T(" " + Damaged + (Damaged == 1 ? " file or folder" : " files or folders") + " could not be read on " +
                                    "the old partition, and " + (Damaged == 1 ? "is" : "are") + " lost.",
                                    " " + Damaged + (Damaged == 1 ? " archivo o carpeta no se pudo" : " archivos o carpetas no se " +
                                    "pudieron") + " leer en la partición anterior, y " + (Damaged == 1 ? "se pierde." : "se pierden."));
                    return kept;
                case Outcome.NotKept:
                    return L.T("What its diagnostics had saved (the folder hades-diag) could not be kept, because " + CauseText() +
                               ": it is lost. The update itself is complete and was checked.",
                               "Lo que había guardado su diagnóstico (la carpeta hades-diag) no se pudo conservar porque " + CauseText() +
                               ": se pierde. La actualización en sí está completa y comprobada.");
                case Outcome.Unknown:
                    return L.T("Whether its diagnostics had saved anything (the folder hades-diag) could not be told, because " +
                               CauseText() + ": if they had, it is lost. The update itself is complete and was checked.",
                               "No se pudo saber si su diagnóstico había guardado algo (la carpeta hades-diag) porque " + CauseText() +
                               ": si lo había, se pierde. La actualización en sí está completa y comprobada.");
                default:
                    return "";
            }
        }
    }

    static class DiagFolder
    {
        public const string Name = "hades-diag";
        // What the diagnostics of Hades leave free on that partition when they write to it (collect.sh): an update
        // from Hades itself needs room there. A folder that would leave less loses its oldest starts first.
        public const long Reserve = 128L << 20;
        const long MostBytes = 256L << 20;
        const int MostItems = 50000, MostDepth = 8, MostDirClusters = 256;

        sealed class TooLargeException : Exception { }

        // ---- reading

        // A read function over a stream of a partition, in blocks of 64 KiB from the start of the partition: a
        // partition of a disk is read in whole sectors, and at places that are whole sectors from its start.
        public static Func<long, int, byte[]> Reader(Stream partition, long length)
        {
            const int Block = 64 * 1024;
            var cache = new Dictionary<long, byte[]>();
            return delegate(long offset, int count)
            {
                if (offset < 0 || count < 0 || offset + count > length) throw new InvalidDataException("a read past the end of the partition");
                var result = new byte[count];
                int done = 0;
                while (done < count)
                {
                    long at = offset + done, block = at / Block * Block;
                    byte[] b;
                    if (!cache.TryGetValue(block, out b))
                    {
                        int want = (int)Math.Min(Block, length - block);
                        b = new byte[Block];
                        partition.Seek(block, SeekOrigin.Begin);
                        int got = 0;
                        while (got < want)
                        {
                            int n = partition.Read(b, got, want - got);
                            if (n <= 0) throw new EndOfStreamException("the partition gave less than it is long");
                            got += n;
                        }
                        // (what is read is mostly in order: a few blocks are enough to keep)
                        if (cache.Count >= 64) cache.Clear();
                        cache[block] = b;
                    }
                    int from = (int)(at - block), piece = Math.Min(count - done, Block - from);
                    Buffer.BlockCopy(b, from, result, done, piece);
                    done += piece;
                }
                return result;
            };
        }

        public static Func<long, int, byte[]> Reader(byte[] data)
        {
            return delegate(long offset, int count)
            {
                if (offset < 0 || count < 0 || offset + count > data.Length) throw new InvalidDataException("a read past what there is");
                var result = new byte[count];
                Buffer.BlockCopy(data, (int)offset, result, 0, count);
                return result;
            };
        }

        // The items of a directory, in its order: "." and "..", the label of the volume and deleted entries are
        // left out. A long name is taken as both systems take it: only when it is whole and of the entry after it.
        public static List<DiagItem> Entries(byte[] dir)
        {
            var items = new List<DiagItem>();
            string longName = null;
            int next = 0, from = 0;
            byte sum = 0;
            for (int e = 0; e + 32 <= dir.Length; e += 32)
            {
                if (dir[e] == 0) break;
                if (dir[e] == 0xE5) { longName = null; continue; }
                if ((dir[e + 11] & 0x3F) == 0x0F)
                {
                    int order = dir[e] & 0x3F;
                    if ((dir[e] & 0x40) != 0) { longName = ""; next = order; sum = dir[e + 13]; from = e; }
                    if (longName == null || order < 1 || order != next || dir[e + 13] != sum) { longName = null; continue; }
                    longName = Engine.LongPart(dir, e) + longName;
                    next = order - 1;
                    continue;
                }
                bool whole = longName != null && next == 0 && sum == Engine.ShortSum(dir, e);
                string spelled = whole ? longName : null;
                longName = null;
                byte attributes = dir[e + 11];
                if ((attributes & 0x08) != 0 || dir[e] == 0x2E) continue;
                var item = new DiagItem();
                item.Own = new byte[32];
                Buffer.BlockCopy(dir, e, item.Own, 0, 32);
                if (whole)
                {
                    item.Names = new byte[e - from];
                    Buffer.BlockCopy(dir, from, item.Names, 0, e - from);
                }
                item.Name = spelled ?? Shown(dir, e);
                item.Folder = (attributes & 0x10) != 0;
                items.Add(item);
            }
            return items;
        }

        // A short name as Windows and Linux show it: its two parts in small letters where the entry says so (what
        // both write for a name such as start-1 or README.txt, which needs no long name).
        static string Shown(byte[] dir, int e)
        {
            string name = Engine.ShortName(dir, e);
            int dot = name.IndexOf('.');
            string stem = dot < 0 ? name : name.Substring(0, dot), ending = dot < 0 ? "" : name.Substring(dot);
            if ((dir[e + 12] & 0x08) != 0) stem = stem.ToLowerInvariant();
            if ((dir[e + 12] & 0x10) != 0) ending = ending.ToLowerInvariant();
            return stem + ending;
        }

        public static DiagItem Find(Fat16 fat)
        {
            foreach (DiagItem item in Entries(fat.Root))
                if (item.Folder && string.Equals(item.Name, Name, StringComparison.OrdinalIgnoreCase)) return item;
            return null;
        }

        // The folder hades-diag of a file system, read whole; null where it has none or the folder itself cannot be
        // read. damaged: what in it could not be read and is left out.
        public static DiagItem ReadWhole(Fat16 fat, out int damaged)
        {
            damaged = 0;
            DiagItem folder = Find(fat);
            if (folder == null) return null;
            var counts = new DiagKeep();
            long bytes = 0;
            int count = 0;
            if (!ReadFolder(fat, folder, 1, counts, ref bytes, ref count)) return null;
            damaged = counts.Damaged;
            return folder;
        }

        static byte[] Gather(Fat16 fat, List<int> chain, long length)
        {
            var data = new byte[length];
            for (int i = 0; i < chain.Count; ++i)
            {
                byte[] c = fat.Cluster(chain[i]);
                int count = (int)Math.Min(fat.ClusterBytes, length - (long)i * fat.ClusterBytes);
                Buffer.BlockCopy(c, 0, data, i * fat.ClusterBytes, count);
            }
            return data;
        }

        // The files and folders of a folder, read whole. False: the folder itself cannot be read. A file or a
        // folder in it that cannot be read is left out and counted (keep.Damaged).
        static bool ReadFolder(Fat16 fat, DiagItem folder, int depth, DiagKeep keep, ref long bytes, ref int count)
        {
            List<int> chain = fat.Chain(folder.First, MostDirClusters);
            if (chain == null) return false;
            folder.Items.Clear();
            foreach (DiagItem item in Entries(Gather(fat, chain, (long)chain.Count * fat.ClusterBytes)))
            {
                if (++count > MostItems) throw new TooLargeException();
                if (item.Folder)
                {
                    if (depth >= MostDepth || !ReadFolder(fat, item, depth + 1, keep, ref bytes, ref count)) { ++keep.Damaged; continue; }
                    folder.Items.Add(item);
                    continue;
                }
                long size = item.Size;
                if (size > MostBytes - bytes) throw new TooLargeException();
                if (size == 0) item.Data = new byte[0];
                else
                {
                    int needed = (int)((size + fat.ClusterBytes - 1) / fat.ClusterBytes);
                    List<int> clusters = fat.Chain(item.First, needed);
                    if (clusters == null || clusters.Count != needed) { ++keep.Damaged; continue; }
                    item.Data = Gather(fat, clusters, size);
                }
                bytes += size;
                folder.Items.Add(item);
            }
            return true;
        }

        static void Count(DiagItem folder, DiagKeep keep)
        {
            ++keep.Folders;
            foreach (DiagItem item in folder.Items)
            {
                if (item.Folder) Count(item, keep);
                else { ++keep.Files; keep.Bytes += item.Data.Length; }
            }
        }

        // ---- placing

        static int DirClusters(DiagItem folder, int cb)
        {
            int entries = 2; // "." and ".."
            foreach (DiagItem item in folder.Items) entries += item.Entries;
            return Math.Max(1, (entries * 32 + cb - 1) / cb);
        }

        internal static long Need(DiagItem folder, int cb)
        {
            long n = DirClusters(folder, cb);
            foreach (DiagItem item in folder.Items)
                n += item.Folder ? Need(item, cb) : (item.Data.Length + cb - 1) / cb;
            return n;
        }

        static void Assign(DiagItem folder, List<int> free, ref int next, int cb)
        {
            folder.At = free.GetRange(next, DirClusters(folder, cb));
            next += folder.At.Count;
            foreach (DiagItem item in folder.Items)
            {
                if (item.Folder) { Assign(item, free, ref next, cb); continue; }
                int n = (item.Data.Length + cb - 1) / cb;
                item.At = free.GetRange(next, n);
                next += n;
            }
        }

        static void Link(ushort[] table, List<int> chain)
        {
            for (int k = 0; k < chain.Count; ++k) table[chain[k]] = k + 1 < chain.Count ? (ushort)chain[k + 1] : (ushort)0xFFFF;
        }

        static void SetFirst(byte[] entry, int at, int cluster)
        {
            entry[at + 20] = entry[at + 21] = 0;
            entry[at + 26] = (byte)cluster;
            entry[at + 27] = (byte)(cluster >> 8);
        }

        // The entries of an item as they go into its new directory: the parts of its long name as they were, and its
        // own entry with its new first cluster.
        static byte[] EntriesOf(DiagItem item)
        {
            var e = new byte[item.Names.Length + 32];
            Buffer.BlockCopy(item.Names, 0, e, 0, item.Names.Length);
            int own = item.Names.Length;
            Buffer.BlockCopy(item.Own, 0, e, own, 32);
            SetFirst(e, own, item.Folder || item.Data.Length > 0 ? item.At[0] : 0);
            if (item.Folder) { e[own + 28] = e[own + 29] = e[own + 30] = e[own + 31] = 0; }
            return e;
        }

        // A folder's clusters: ".", "..", its items, and nothing after them.
        static byte[] DirBytes(DiagItem folder, int parentFirst, int cb)
        {
            var dir = new byte[folder.At.Count * cb];
            for (int i = 0; i < 2; ++i)
            {
                int at = 32 * i;
                Buffer.BlockCopy(folder.Own, 0, dir, at, 32);
                for (int k = 0; k < 11; ++k) dir[at + k] = (byte)' ';
                dir[at] = (byte)'.';
                if (i == 1) dir[at + 1] = (byte)'.';
                dir[at + 11] = 0x10;
                dir[at + 12] = 0;
                SetFirst(dir, at, i == 0 ? folder.At[0] : parentFirst);
                dir[at + 28] = dir[at + 29] = dir[at + 30] = dir[at + 31] = 0;
            }
            int put = 64;
            foreach (DiagItem item in folder.Items)
            {
                byte[] e = EntriesOf(item);
                Buffer.BlockCopy(e, 0, dir, put, e.Length);
                put += e.Length;
            }
            return dir;
        }

        static void Build(DiagItem folder, int parentFirst, ushort[] table, SortedDictionary<int, byte[]> clusters, int cb)
        {
            Link(table, folder.At);
            byte[] dir = DirBytes(folder, parentFirst, cb);
            for (int i = 0; i < folder.At.Count; ++i)
            {
                var c = new byte[cb];
                Buffer.BlockCopy(dir, i * cb, c, 0, cb);
                clusters[folder.At[i]] = c;
            }
            foreach (DiagItem item in folder.Items)
            {
                if (item.Folder) { Build(item, folder.At[0], table, clusters, cb); continue; }
                Link(table, item.At);
                for (int i = 0; i < item.At.Count; ++i)
                {
                    var c = new byte[cb];
                    Buffer.BlockCopy(item.Data, i * cb, c, 0, Math.Min(cb, item.Data.Length - i * cb));
                    clusters[item.At[i]] = c;
                }
            }
        }

        // Where in a directory "count" entries in a row are free (never used, or deleted); -1: nowhere.
        static int FreeRun(byte[] dir, int count)
        {
            int run = 0;
            for (int e = 0; e < dir.Length / 32; ++e)
            {
                byte b = dir[32 * e];
                run = b == 0 || b == 0xE5 ? run + 1 : 0;
                if (run == count) return e - count + 1;
            }
            return -1;
        }

        // The number N of a folder start-N, as start.sh reads it; -1 for any other name.
        static int StartNumber(DiagItem item)
        {
            if (!item.Folder || !item.Name.StartsWith("start-", StringComparison.OrdinalIgnoreCase) || item.Name.Length == 6 ||
                item.Name.Length > 15)
                return -1;
            int n = 0;
            for (int i = 6; i < item.Name.Length; ++i)
            {
                char c = item.Name[i];
                if (c < '0' || c > '9') return -1;
                n = n * 10 + (c - '0');
            }
            return n;
        }

        static Patch Make(long offset, byte[] bytes)
        {
            var p = new Patch();
            p.Offset = offset;
            p.Bytes = bytes;
            return p;
        }

        static void Apply(List<Patch> patches, byte[] buffer, long bufferOffset)
        {
            foreach (Patch p in patches)
            {
                long from = Math.Max(p.Offset, bufferOffset), to = Math.Min(p.Offset + p.Bytes.Length, bufferOffset + buffer.Length);
                if (from < to) Buffer.BlockCopy(p.Bytes, (int)(from - p.Offset), buffer, (int)(from - bufferOffset), (int)(to - from));
            }
        }

        // Two items are the same: names, attributes, times and what a file holds; where they are is not compared.
        public static bool Same(DiagItem a, DiagItem b, out string where)
        {
            where = null;
            if (a.Name != b.Name || a.Folder != b.Folder || !Equal(a.Names, b.Names)) { where = a.Name + " (its name)"; return false; }
            for (int i = 0; i < 32; ++i)
            {
                if (i == 20 || i == 21 || i == 26 || i == 27 || (a.Folder && i >= 28)) continue;
                if (a.Own[i] != b.Own[i]) { where = a.Name + " (its entry)"; return false; }
            }
            if (!a.Folder) { if (!Equal(a.Data, b.Data)) { where = a.Name + " (what it holds)"; return false; } return true; }
            if (a.Items.Count != b.Items.Count) { where = a.Name + " (how many items)"; return false; }
            for (int i = 0; i < a.Items.Count; ++i)
                if (!Same(a.Items[i], b.Items[i], out where)) { where = a.Name + "/" + where; return false; }
            return true;
        }

        static bool Equal(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; ++i) if (a[i] != b[i]) return false;
            return true;
        }

        // What an update keeps of hades-diag. old: the system partition that is replaced, as it is now. imageStart:
        // the system partition of the image up to its first cluster (only asked for when there is a folder to keep).
        // systemStart: where that partition is in the image, which is what the offsets of the patches are from.
        // others: the other changes of the update, as patches of the image. reserve: what has to stay free in the new
        // partition. Never throws: whatever goes wrong is in what it returns, with no patches.
        public static DiagKeep Keep(Func<long, int, byte[]> old, long oldLength, Func<byte[]> imageStart, long imageLength,
                                    long systemStart, List<Patch> others, long reserve)
        {
            var keep = new DiagKeep();
            bool found = false;
            DiagItem folder = null;
            try
            {
                string why;
                Fat16 was = Fat16.Open(old, oldLength, out why);
                if (was == null) return keep.Fail(DiagKeep.Outcome.Unknown, DiagKeep.Cause.Unreadable, "the old partition: " + why);
                folder = Find(was);
                if (folder == null) return keep;
                found = true;
                long bytes = 0;
                int count = 0;
                if (!ReadFolder(was, folder, 1, keep, ref bytes, ref count))
                    return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.Damaged, "the chain of the folder itself is broken");
            }
            catch (TooLargeException)
            {
                return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.TooLarge, "more than " + MostBytes + " bytes or " + MostItems + " items");
            }
            catch (Exception e)
            {
                return keep.Fail(found ? DiagKeep.Outcome.NotKept : DiagKeep.Outcome.Unknown, DiagKeep.Cause.Failed, e.Message);
            }
            return Place(keep, folder, imageStart, imageLength, systemStart, others, reserve);
        }

        // The patches that put a folder (as Entries and ReadFolder make it) into the new partition, into keep. The
        // rest is as for Keep. (For the tests as well: they put a folder into a partition of their own this way.)
        internal static DiagKeep Place(DiagKeep keep, DiagItem folder, Func<byte[]> imageStart, long imageLength, long systemStart,
                                       List<Patch> others, long reserve)
        {
            try
            {
                string why;
                keep.Folder = folder;
                // the new partition, as the image has it with the other changes of the update
                byte[] before = imageStart();
                Apply(others, before, systemStart);
                Fat16 image = Fat16.Open(Reader(before), imageLength, out why);
                if (image == null) return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.ImageUnknown, "the image: " + why);
                if (!image.SameFats) return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.ImageUnknown, "the two FATs of the image differ");
                if (before.Length < image.DataAt) throw new InvalidOperationException("the start of the image is shorter than its metadata");
                int cb = image.ClusterBytes;
                for (int e = 0; e < image.Root.Length; e += 32)
                {
                    byte b = image.Root[e];
                    if (b == 0) break;
                    if (b == 0xE5 || (image.Root[e + 11] & 0x3F) == 0x0F) continue;
                    bool sameShort = true;
                    for (int k = 0; k < 11; ++k) sameShort &= image.Root[e + k] == folder.Own[k];
                    if (sameShort) return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.NameTaken, "its short name is taken in the image");
                }
                foreach (DiagItem item in Entries(image.Root))
                    if (string.Equals(item.Name, Name, StringComparison.OrdinalIgnoreCase))
                        return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.NameTaken, "the image has an item of that name");

                // room: what stays free has to be at least the reserve; the oldest starts go first
                var free = new List<int>();
                for (int c = 2; c <= image.Clusters + 1; ++c) if (image.Table[c] == 0) free.Add(c);
                long keepFree = (reserve + cb - 1) / cb;
                var starts = new List<DiagItem>();
                foreach (DiagItem item in folder.Items) if (StartNumber(item) >= 0) starts.Add(item);
                starts.Sort(delegate(DiagItem x, DiagItem y) { return StartNumber(x).CompareTo(StartNumber(y)); });
                while (Need(folder, cb) + keepFree > free.Count && starts.Count > 0)
                {
                    folder.Items.Remove(starts[0]);
                    keep.LeftOut.Add(starts[0].Name);
                    starts.RemoveAt(0);
                }
                if (Need(folder, cb) + keepFree > free.Count)
                    return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.NoRoom,
                                     Need(folder, cb) + " clusters needed, " + free.Count + " free, " + keepFree + " to stay free");
                int slot = FreeRun(image.Root, folder.Entries);
                if (slot < 0)
                    return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.NoRoom, "no room in the root directory of the image");

                int next = 0;
                Assign(folder, free, ref next, cb);
                var table = (ushort[])image.Table.Clone();
                var clusters = new SortedDictionary<int, byte[]>();
                Build(folder, 0, table, clusters, cb);

                var mine = new List<Patch>();
                // the FATs: from the first entry that changes to the last, in both copies
                int low = -1, high = -1;
                for (int c = 0; c < table.Length; ++c)
                    if (table[c] != image.Table[c]) { if (low < 0) low = c; high = c; }
                var span = new byte[2 * (high - low + 1)];
                for (int c = low; c <= high; ++c) { span[2 * (c - low)] = (byte)table[c]; span[2 * (c - low) + 1] = (byte)(table[c] >> 8); }
                for (int copy = 0; copy < image.Fats; ++copy)
                    mine.Add(Make(systemStart + image.FatAt + (long)copy * image.FatSectors * Fat16.Sector + 2L * low, (byte[])span.Clone()));
                // the entries of the folder in the root directory
                mine.Add(Make(systemStart + image.RootAt + 32L * slot, EntriesOf(folder)));
                // the clusters, a run of them in a row as one patch
                int runStart = -1, runLast = -1;
                var run = new List<byte[]>();
                foreach (KeyValuePair<int, byte[]> c in clusters)
                {
                    if (runStart >= 0 && c.Key != runLast + 1)
                    {
                        mine.Add(Make(systemStart + image.ClusterAt(runStart), Join(run)));
                        runStart = -1;
                        run.Clear();
                    }
                    if (runStart < 0) runStart = c.Key;
                    runLast = c.Key;
                    run.Add(c.Value);
                }
                if (runStart >= 0) mine.Add(Make(systemStart + image.ClusterAt(runStart), Join(run)));

                // nothing that another change of the update changes
                foreach (Patch p in mine)
                    foreach (Patch o in others)
                        if (p.Offset < o.Offset + o.Bytes.Length && o.Offset < p.Offset + p.Bytes.Length)
                            return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.Failed, "it would change what another change of the update changes");

                // what the patches make, read back as it would be read on the disk
                string wrong = Check(before, image, mine, clusters, systemStart, imageLength, slot, folder);
                if (wrong != null) return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.Failed, wrong);
                keep.Patches = mine;
                keep.State = DiagKeep.Outcome.Kept;
                Count(folder, keep);
                return keep;
            }
            catch (Exception e)
            {
                return keep.Fail(DiagKeep.Outcome.NotKept, DiagKeep.Cause.Failed, e.Message);
            }
        }

        static byte[] Join(List<byte[]> pieces)
        {
            int length = 0;
            foreach (byte[] p in pieces) length += p.Length;
            var all = new byte[length];
            int at = 0;
            foreach (byte[] p in pieces) { Buffer.BlockCopy(p, 0, all, at, p.Length); at += p.Length; }
            return all;
        }

        // The new partition as the patches make it, read through the reader of this file: it is a FAT16 whose two
        // FATs are the same, whose boot sector, root directory and FAT are those of the image but for the entries
        // of the folder and the clusters that were free, and in which the folder is what was read. Null where it
        // is; else what is wrong. A cluster that is read and is not one of the folder's is wrong as well: nothing
        // of what the image has is looked at.
        static string Check(byte[] before, Fat16 image, List<Patch> mine, SortedDictionary<int, byte[]> clusters, long systemStart,
                            long imageLength, int slot, DiagItem folder)
        {
            byte[] after = (byte[])before.Clone();
            Apply(mine, after, systemStart);
            int cb = image.ClusterBytes;
            // the clusters as the patches put them on the disk (not as they were meant): whole clusters, each once
            var written = new Dictionary<int, byte[]>();
            foreach (Patch p in mine)
            {
                long at = p.Offset - systemStart;
                if (at + p.Bytes.Length <= image.DataAt) continue;
                if (at < image.DataAt || (at - image.DataAt) % cb != 0 || p.Bytes.Length % cb != 0) return "a patch is not of whole clusters";
                for (int i = 0; i < p.Bytes.Length / cb; ++i)
                {
                    int c = (int)((at - image.DataAt) / cb) + 2 + i;
                    var piece = new byte[cb];
                    Buffer.BlockCopy(p.Bytes, i * cb, piece, 0, cb);
                    if (written.ContainsKey(c)) return "two patches write the same cluster";
                    written[c] = piece;
                }
            }
            if (written.Count != clusters.Count) return "the patches do not write the clusters of the folder, and only them";
            foreach (int c in clusters.Keys) if (!written.ContainsKey(c)) return "the patches leave out a cluster of the folder";
            Func<long, int, byte[]> read = delegate(long offset, int count)
            {
                if (offset >= 0 && offset + count <= image.DataAt)
                {
                    var r = new byte[count];
                    Buffer.BlockCopy(after, (int)offset, r, 0, count);
                    return r;
                }
                byte[] c;
                if (offset >= image.DataAt && (offset - image.DataAt) % cb == 0 && count == cb &&
                    written.TryGetValue((int)((offset - image.DataAt) / cb) + 2, out c))
                    return (byte[])c.Clone();
                throw new InvalidDataException("a cluster that is not of the folder is read, at " + offset);
            };
            string why;
            Fat16 made = Fat16.Open(read, imageLength, out why);
            if (made == null) return "what the patches make is not a FAT16: " + why;
            if (!made.SameFats) return "the two FATs differ in what the patches make";
            for (int i = 0; i < Fat16.Sector; ++i) if (after[i] != before[i]) return "the boot sector changes";
            int entries = folder.Entries;
            for (int e = 0; e < image.RootEntries; ++e)
            {
                if (e >= slot && e < slot + entries) continue;
                for (int k = 0; k < 32; ++k)
                    if (made.Root[32 * e + k] != image.Root[32 * e + k]) return "an entry of the root directory that is not the folder's changes";
            }
            for (int c = 2; c <= image.Clusters + 1; ++c)
            {
                if (image.Table[c] != 0 && made.Table[c] != image.Table[c]) return "a cluster in use in the image changes in the FAT";
                if (made.Table[c] != image.Table[c] && !clusters.ContainsKey(c)) return "a free cluster that is not the folder's is taken";
            }
            foreach (int c in clusters.Keys) if (image.Table[c] != 0) return "the folder is put in a cluster that the image uses";
            DiagItem found = Find(made);
            if (found == null) return "the folder is not found in what the patches make";
            var counts = new DiagKeep();
            long bytes = 0;
            int count2 = 0;
            if (!ReadFolder(made, found, 1, counts, ref bytes, ref count2) || counts.Damaged > 0) return "the folder cannot be read whole in what the patches make";
            string where;
            if (!Same(folder, found, out where)) return "the folder read back is not the one that was read: " + where;
            return Dots(made, found, 0);
        }

        // Each folder starts with "." (itself) and ".." (the folder it is in; 0: the root directory), as fsck.fat
        // and the systems want them. Null where they are so.
        static string Dots(Fat16 fat, DiagItem folder, int parent)
        {
            byte[] first = fat.Cluster(folder.First);
            for (int i = 0; i < 2; ++i)
            {
                int at = 32 * i;
                string name = i == 0 ? ".          " : "..         ";
                for (int k = 0; k < 11; ++k) if (first[at + k] != (byte)name[k]) return folder.Name + ": its entry " + name.Trim() + " is missing";
                if ((first[at + 11] & 0x10) == 0 || Fat16.U16(first, at + 26) != (i == 0 ? folder.First : parent) ||
                    Fat16.U16(first, at + 20) != 0)
                    return folder.Name + ": its entry " + name.Trim() + " does not lead where it should";
            }
            foreach (DiagItem item in folder.Items)
                if (item.Folder)
                {
                    string wrong = Dots(fat, item, folder.First);
                    if (wrong != null) return wrong;
                }
            return null;
        }

        // What an update keeps of hades-diag, for a partition that is open (Operations.Update). Never throws.
        public static DiagKeep Prepare(ImagePlan plan, Stream partition, long length, List<Patch> others)
        {
            try
            {
                return Keep(Reader(partition, length), length, delegate
                {
                    byte[] boot = Engine.SystemStart(plan, Fat16.Sector);
                    long dataAt = ((long)Fat16.U16(boot, 14) + (long)boot[16] * Fat16.U16(boot, 22)) * Fat16.Sector + Fat16.U16(boot, 17) * 32L;
                    if (dataAt < Fat16.Sector || dataAt > (16L << 20) || dataAt > plan.System.Size) return boot;
                    return Engine.SystemStart(plan, (int)dataAt);
                }, plan.System.Size, plan.System.Start, others, Reserve);
            }
            catch (Exception e)
            {
                return new DiagKeep().Fail(DiagKeep.Outcome.Unknown, DiagKeep.Cause.Failed, e.Message);
            }
        }

        // After the partition has been written and read back: the folder, read from it, is what was kept. Never throws.
        public static void Confirm(DiagKeep keep, Func<long, int, byte[]> read, long length)
        {
            keep.Verified = false;
            if (keep.State != DiagKeep.Outcome.Kept) return;
            keep.ReadBack = true;
            try
            {
                string why;
                Fat16 fat = Fat16.Open(read, length, out why);
                DiagItem found = fat == null ? null : Find(fat);
                if (found == null) { keep.Detail = "not found when read back" + (why != null ? ": " + why : ""); return; }
                var counts = new DiagKeep();
                long bytes = 0;
                int count = 0;
                string where = "";
                keep.Verified = ReadFolder(fat, found, 1, counts, ref bytes, ref count) && counts.Damaged == 0 && Same(keep.Folder, found, out where);
                if (!keep.Verified) keep.Detail = "read back, it is not what was kept: " + where;
            }
            catch (Exception e)
            {
                keep.Detail = "read back: " + e.Message;
            }
        }
    }
}
