// LibreELEC Hades installer for Windows: the changes that Secure Boot makes to the system partition of an image (a FAT16), made on
// what the scan of the image collected and written as patches: the boot loader becomes shim, GRUB and the kernel go in signed with
// the key of the PC, and the certificate goes in beside them. Nothing is changed in place: the new contents go into free clusters,
// the old chains are freed, and both FATs are written alike.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Hades.Installer
{
    // One entry of a directory of a FAT.
    sealed class DirEnt
    {
        public int Offset;       // of its short entry in the bytes of the directory
        public int LongAt = -1;  // of the first of the entries of its long name (-1: it has none)
        public string Name;      // its long name where it has one, else its short name
        public byte Attr;
        public int First;
        public long Size;
        public bool Folder { get { return (Attr & 0x10) != 0; } }
    }

    static class SbDir
    {
        // The entries of a directory, in its order, up to the first that is the end of it. "." and "..", the label of the volume and
        // deleted entries are left out. A long name counts only when it is whole and belongs to the entry after it.
        public static List<DirEnt> List(byte[] dir)
        {
            var list = new List<DirEnt>();
            string longName = null;
            int next = 0, firstLong = -1;
            byte sum = 0;
            for (int e = 0; e + 32 <= dir.Length; e += 32)
            {
                if (dir[e] == 0) break;
                if (dir[e] == 0xE5) { longName = null; continue; }
                if ((dir[e + 11] & 0x3F) == 0x0F)
                {
                    int order = dir[e] & 0x3F;
                    if ((dir[e] & 0x40) != 0) { longName = ""; next = order; sum = dir[e + 13]; firstLong = e; }
                    if (longName == null || order < 1 || order != next || dir[e + 13] != sum) { longName = null; continue; }
                    longName = Engine.LongPart(dir, e) + longName;
                    next = order - 1;
                    continue;
                }
                bool whole = longName != null && next == 0 && sum == Engine.ShortSum(dir, e);
                string spelled = whole ? longName : null;
                int startLong = whole ? firstLong : -1;
                longName = null;
                if ((dir[e + 11] & 0x08) != 0) continue;                 // the label of the volume
                if (dir[e] == (byte)'.' && (spelled == null)) continue;  // "." and ".."
                var d = new DirEnt();
                d.Offset = e;
                d.LongAt = startLong;
                d.Name = spelled ?? Engine.ShortName(dir, e);
                d.Attr = dir[e + 11];
                d.First = BitConverter.ToUInt16(dir, e + 26);
                d.Size = BitConverter.ToUInt32(dir, e + 28);
                list.Add(d);
            }
            return list;
        }

        public static DirEnt Find(List<DirEnt> list, string name)
        {
            foreach (DirEnt d in list) if (string.Equals(d.Name, name, StringComparison.OrdinalIgnoreCase)) return d;
            return null;
        }

        // The entry that a new file takes: the first that is free or deleted. -1: none.
        public static int FreeSlot(byte[] dir, params int[] taken)
        {
            for (int e = 0; e + 32 <= dir.Length; e += 32)
                if ((dir[e] == 0 || dir[e] == 0xE5) && Array.IndexOf(taken, e) < 0) return e;
            return -1;
        }

        // A short entry for a new file: the name as 11 bytes ("GRUBX64 EFI"), the flags that say its parts are lower case (0x18: both),
        // the times of another entry, its first cluster and its size.
        public static byte[] NewEntry(string name11, byte lowerCase, byte[] timesOf, int first, long size)
        {
            var e = new byte[32];
            for (int i = 0; i < 11; ++i) e[i] = (byte)name11[i];
            e[11] = 0x20; // archive
            e[12] = lowerCase;
            Buffer.BlockCopy(timesOf, 13, e, 13, 13); // creation time (tenths, time, date), last access date, [high cluster], write time and date
            e[20] = e[21] = 0;
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)first), 0, e, 26, 2);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)size), 0, e, 28, 4);
            return e;
        }
    }

    // What the scan of an image learned of its system partition for Secure Boot.
    sealed class SbSystem
    {
        public bool Known;                      // everything that the change needs was found, and is as it has to be
        public string Why = "not read";
        public bool HasSecureDir;               // there is /secureboot with shimx64.efi and mmx64.efi
        public int ClusterBytes, FatCount, FatSectors;
        public long FatAt, RootAt, DataAt, PartitionSize;
        public byte[] Meta;                     // the start of the partition up to its first cluster: boot sector, FATs, root directory
        public ushort[] Table;
        public byte[] Root;
        public DirEnt Kernel;                   // in the root directory
        public byte[] BootDir;                  // /EFI/BOOT, its clusters one after the other
        public List<int> BootDirChain;
        public DirEnt Grub;                     // bootx64.efi in it
        public byte[] GrubBytes, KernelBytes, ShimBytes, MmBytes;
        public List<int> GrubChain, KernelChain;
        public byte[] Storage;                  // the storage partition, whole, when the image has /secureboot (for its key slot)
        public bool StorageReadAgain;           // the storage had to be read in a pass of its own
        public int Rereads, Passes;             // clusters that were read again because they came by before they were wanted, and in how many passes
        public long ClusterAt(int c) { return DataAt + (long)(c - 2) * ClusterBytes; }
    }

    // Reads, as the scan of the image goes by (forward only, the image may be compressed), what the change needs: the FAT, the root
    // directory, the directories /EFI, /EFI/BOOT and /secureboot, and the contents of bootx64.efi, KERNEL and the two files of
    // /secureboot. What it learns late (a cluster that came by before the entry that names it) is read in a pass of its own.
    sealed class SbCollector
    {
        readonly ImagePlan plan;
        readonly bool probe;
        public readonly SbSystem S = new SbSystem();
        bool failed;
        int stage;                              // 0: the boot sector, 1: the rest of the metadata, 2: the clusters
        byte[] meta = new byte[4 << 20];
        long metaTop, dataAt;
        Fat16 fat;
        long curOff, curEnd;
        byte[] curBuf;
        int curAt;
        readonly Dictionary<int, byte[]> stash = new Dictionary<int, byte[]>();
        readonly Dictionary<int, int> have = new Dictionary<int, int>();
        readonly HashSet<int> missed = new HashSet<int>();
        bool rootDone, efiDone, bootDone, secDone;
        List<int> efiChain, bootChain, secChain, shimChain, mmChain;
        DirEnt shimEnt, mmEnt, bootEnt;
        long storageHave;

        public SbCollector(ImagePlan plan, bool probe)
        {
            this.plan = plan;
            this.probe = probe;
        }

        public bool Failed { get { return failed; } }
        // for the quick look: the directory of /secureboot has been read (or there is none)
        public bool Settled { get { return failed || secDone || (rootDone && !S.HasSecureDir && secChain == null); } }

        void Fail(string why)
        {
            failed = true;
            S.Known = false;
            S.Why = why;
        }

        // A piece of the image, from the offset "imageOffset" of it. Never throws.
        public void Feed(long imageOffset, byte[] buffer, int at, int count)
        {
            if (failed) return;
            try
            {
                long a = imageOffset, b = imageOffset + count;
                long from = Math.Max(a, plan.System.Start), to = Math.Min(b, plan.System.End);
                if (from < to) FeedSystem(from - plan.System.Start, buffer, at + (int)(from - a), (int)(to - from));
                if (!probe && S.HasSecureDir && plan.Storage.Size <= (64L << 20))
                {
                    from = Math.Max(a, plan.Storage.Start);
                    to = Math.Min(b, plan.Storage.End);
                    if (from < to)
                    {
                        if (S.Storage == null) S.Storage = new byte[plan.Storage.Size];
                        // (the storage comes after the system partition, in one piece: what is missing is read again in Finish)
                        if (from - plan.Storage.Start == storageHave)
                        {
                            Buffer.BlockCopy(buffer, at + (int)(from - a), S.Storage, (int)(from - plan.Storage.Start), (int)(to - from));
                            storageHave += to - from;
                        }
                    }
                }
            }
            catch (Exception e) { Fail(e.Message); }
        }

        void FeedSystem(long partOffset, byte[] buffer, int at, int count)
        {
            curOff = partOffset;
            curEnd = partOffset + count;
            curBuf = buffer;
            curAt = at;
            if (partOffset < meta.Length)
            {
                int n = (int)Math.Min(count, meta.Length - partOffset);
                Buffer.BlockCopy(buffer, at, meta, (int)partOffset, n);
                if (partOffset <= metaTop) metaTop = Math.Max(metaTop, partOffset + n);
            }
            if (stage == 2)
            {
                // the clusters that are wanted and overlap this piece
                long first = Math.Max(partOffset, dataAt), last = curEnd;
                if (first < last)
                {
                    int c0 = (int)((first - dataAt) / S.ClusterBytes) + 2, c1 = (int)((last - 1 - dataAt) / S.ClusterBytes) + 2;
                    for (int c = c0; c <= c1; ++c)
                        if (have.ContainsKey(c) && have[c] < S.ClusterBytes) CopyCluster(c);
                }
            }
            Advance();
        }

        // what of the cluster is in the piece that is being fed
        void CopyCluster(int c)
        {
            long cs = S.ClusterAt(c), ce = cs + S.ClusterBytes;
            long from = Math.Max(cs, curOff), to = Math.Min(ce, curEnd);
            if (from >= to) return;
            byte[] data;
            if (!stash.TryGetValue(c, out data)) { data = new byte[S.ClusterBytes]; stash[c] = data; }
            Buffer.BlockCopy(curBuf, curAt + (int)(from - curOff), data, (int)(from - cs), (int)(to - from));
            have[c] += (int)(to - from);
        }

        // A cluster is wanted: if what the stream gave before this piece had part of it, it has to be read again.
        void Want(int c)
        {
            if (have.ContainsKey(c) && have[c] >= S.ClusterBytes) return;
            long cs = S.ClusterAt(c);
            if (cs < curOff) { missed.Add(c); return; }
            if (!have.ContainsKey(c)) have[c] = 0;
            if (cs < curEnd) CopyCluster(c);
        }

        bool Complete(List<int> chain)
        {
            foreach (int c in chain) if (!have.ContainsKey(c) || have[c] < S.ClusterBytes) return false;
            return true;
        }

        byte[] Join(List<int> chain, long size)
        {
            var bytes = new byte[size];
            long at = 0;
            foreach (int c in chain)
            {
                int n = (int)Math.Min(S.ClusterBytes, size - at);
                if (n <= 0) break;
                Buffer.BlockCopy(stash[c], 0, bytes, (int)at, n);
                at += n;
            }
            return bytes;
        }

        byte[] JoinWhole(List<int> chain)
        {
            var bytes = new byte[(long)chain.Count * S.ClusterBytes];
            for (int i = 0; i < chain.Count; ++i) Buffer.BlockCopy(stash[chain[i]], 0, bytes, i * S.ClusterBytes, S.ClusterBytes);
            return bytes;
        }

        // The chain of a file or folder; null where it is not whole, or not as long as its size says (a folder: at most 64 clusters)
        List<int> ChainOf(DirEnt d, bool file)
        {
            if (d.First < 2) return null;
            int need = file ? (int)((d.Size + S.ClusterBytes - 1) / S.ClusterBytes) : 64;
            if (file && (d.Size == 0 || need > fat.Clusters)) return null;
            List<int> chain = fat.Chain(d.First, need);
            if (chain == null || (file && chain.Count != need)) return null;
            return chain;
        }

        bool TryMeta()
        {
            if (stage == 0)
            {
                if (metaTop < 512) return false;
                int bytesPerSector = BitConverter.ToUInt16(meta, 11), spc = meta[13], reserved = BitConverter.ToUInt16(meta, 14), fats = meta[16];
                int rootEntries = BitConverter.ToUInt16(meta, 17), fatSectors = BitConverter.ToUInt16(meta, 22);
                if (meta[510] != 0x55 || meta[511] != 0xAA || bytesPerSector != 512 || spc < 1 || (spc & (spc - 1)) != 0 || fats < 1 || fats > 2 ||
                    fatSectors < 1 || reserved < 1 || rootEntries < 16 || rootEntries % 16 != 0)
                { Fail("the system partition is not a FAT16 that this program knows"); return false; }
                dataAt = ((long)reserved + (long)fats * fatSectors) * 512 + rootEntries * 32L;
                if (dataAt > meta.Length) { Fail("the metadata of its file system is larger than expected"); return false; }
                stage = 1;
            }
            if (metaTop < dataAt) return false;
            string why;
            Fat16 f = Fat16.Open(DiagFolder.Reader(meta), plan.System.Size, out why);
            if (f == null) { Fail("the system partition is not a FAT16 that this program knows (" + why + ")"); return false; }
            if (!f.SameFats) { Fail("the two FATs of the system partition differ"); return false; }
            fat = f;
            S.ClusterBytes = f.ClusterBytes;
            S.FatCount = f.Fats;
            S.FatSectors = f.FatSectors;
            S.FatAt = f.FatAt;
            S.RootAt = f.RootAt;
            S.DataAt = f.DataAt;
            S.PartitionSize = plan.System.Size;
            S.Table = f.Table;
            S.Root = f.Root;
            S.Meta = Der.Slice(meta, 0, (int)dataAt);
            stage = 2;
            return true;
        }

        void Advance()
        {
            if (failed) return;
            if (stage < 2 && !TryMeta()) return;
            bool progress;
            do
            {
                progress = false;
                if (!rootDone) { DoRoot(); progress = true; }
                else if (secChain != null && !secDone && Complete(secChain)) { DoSecure(); progress = true; }
                else if (!probe && efiChain != null && !efiDone && Complete(efiChain)) { DoEfi(); progress = true; }
                else if (!probe && bootChain != null && !bootDone && Complete(bootChain)) { DoBoot(); progress = true; }
            } while (progress && !failed);
        }

        void DoRoot()
        {
            rootDone = true;
            List<DirEnt> root = SbDir.List(S.Root);
            DirEnt sec = SbDir.Find(root, "secureboot");
            // (an image with no /secureboot is not read further: nothing of it is collected)
            if (sec == null || !sec.Folder) { S.Why = "the image has no /secureboot folder"; return; }
            secChain = ChainOf(sec, false);
            if (secChain == null) { Fail("the chain of /secureboot is broken"); return; }
            foreach (int c in secChain) Want(c);
            if (probe) return;
            DirEnt efi = SbDir.Find(root, "EFI");
            DirEnt kernel = SbDir.Find(root, "KERNEL");
            if (efi == null || !efi.Folder) { Fail("the image has no /EFI folder"); return; }
            if (kernel == null || kernel.Folder) { Fail("the image has no /KERNEL file"); return; }
            if (SbDir.Find(root, "HADES-SB.CER") != null) { Fail("the image has a HADES-SB.CER already"); return; }
            efiChain = ChainOf(efi, false);
            S.KernelChain = ChainOf(kernel, true);
            if (efiChain == null || S.KernelChain == null) { Fail("the chain of /EFI or of /KERNEL is broken"); return; }
            S.Kernel = kernel;
            foreach (int c in efiChain) Want(c);
            foreach (int c in S.KernelChain) Want(c);
        }

        void DoSecure()
        {
            secDone = true;
            List<DirEnt> list = SbDir.List(JoinWhole(secChain));
            shimEnt = SbDir.Find(list, "shimx64.efi");
            mmEnt = SbDir.Find(list, "mmx64.efi");
            if (shimEnt == null || shimEnt.Folder || shimEnt.Size < 4096 || mmEnt == null || mmEnt.Folder || mmEnt.Size < 4096)
            { S.Why = "/secureboot does not have shimx64.efi and mmx64.efi"; if (!probe) Fail(S.Why); return; }
            S.HasSecureDir = true;
            if (probe) return;
            shimChain = ChainOf(shimEnt, true);
            mmChain = ChainOf(mmEnt, true);
            if (shimChain == null || mmChain == null) { Fail("the chain of shimx64.efi or of mmx64.efi is broken"); return; }
            foreach (int c in shimChain) Want(c);
            foreach (int c in mmChain) Want(c);
        }

        void DoEfi()
        {
            efiDone = true;
            DirEnt boot = SbDir.Find(SbDir.List(JoinWhole(efiChain)), "BOOT");
            if (boot == null || !boot.Folder) { Fail("the image has no /EFI/BOOT folder"); return; }
            bootChain = ChainOf(boot, false);
            if (bootChain == null) { Fail("the chain of /EFI/BOOT is broken"); return; }
            S.BootDirChain = bootChain;
            foreach (int c in bootChain) Want(c);
        }

        void DoBoot()
        {
            bootDone = true;
            S.BootDir = JoinWhole(bootChain);
            List<DirEnt> list = SbDir.List(S.BootDir);
            bootEnt = SbDir.Find(list, "bootx64.efi");
            if (bootEnt == null || bootEnt.Folder) { Fail("the image has no /EFI/BOOT/bootx64.efi"); return; }
            foreach (string n in new[] { "grubx64.efi", "mmx64.efi", "fbx64.efi" })
                if (SbDir.Find(list, n) != null) { Fail("the image has /EFI/BOOT/" + n + " already"); return; }
            S.Grub = bootEnt;
            S.GrubChain = ChainOf(bootEnt, true);
            if (S.GrubChain == null) { Fail("the chain of bootx64.efi is broken"); return; }
            foreach (int c in S.GrubChain) Want(c);
        }

        // At the end of the scan: what came by too soon is read again (the clusters in order, a pass at a time, for as long as each
        // pass finds something new, at most eight), the storage if it was not all collected, and the files are put together.
        public void Finish()
        {
            if (failed || probe) return;
            try
            {
                if (stage < 2) { Fail("the system partition was not read to its metadata"); return; }
                for (int round = 0; round < 8 && missed.Count > 0 && !failed; ++round) ReadAgain();
                if (failed) return;
                if (S.HasSecureDir && plan.Storage.Size <= (64L << 20) && storageHave < plan.Storage.Size) ReadStorage();
                if (!rootDone || !secDone || !efiDone || !bootDone) { if (S.Why == "not read" || S.Why.Length == 0) S.Why = "its folders could not be read"; S.Known = false; return; }
                foreach (List<int> chain in new[] { S.KernelChain, S.GrubChain, shimChain, mmChain })
                    if (chain == null || !Complete(chain)) { Fail("a file of the image could not be read whole"); return; }
                S.GrubBytes = Join(S.GrubChain, S.Grub.Size);
                S.KernelBytes = Join(S.KernelChain, S.Kernel.Size);
                S.ShimBytes = Join(shimChain, shimEnt.Size);
                S.MmBytes = Join(mmChain, mmEnt.Size);
                stash.Clear();
                if (S.Storage == null || storageHave < plan.Storage.Size) { Fail("the storage partition of the image could not be read"); return; }
                S.Known = true;
                S.Why = null;
            }
            catch (Exception e) { Fail(e.Message); }
        }

        void ReadAgain()
        {
            List<int> list = missed.OrderBy(delegate(int c) { return c; }).ToList();
            missed.Clear();
            S.Rereads += list.Count;
            ++S.Passes;
            foreach (int c in list) have[c] = 0;
            stash.Remove(-1);
            foreach (int c in list) stash.Remove(c);
            using (Stream source = Engine.OpenImage(plan.Path))
            {
                long pos = 0;
                foreach (int c in list)
                {
                    long at = plan.System.Start + S.ClusterAt(c);
                    Engine.SkipForward(source, at - pos);
                    var data = new byte[S.ClusterBytes];
                    if (Engine.ReadFull(source, data, data.Length) != data.Length) throw new EndOfStreamException("the image ended while a cluster was read again");
                    pos = at + data.Length;
                    FeedSystem(S.ClusterAt(c), data, 0, data.Length);
                    if (failed) return;
                }
            }
        }

        void ReadStorage()
        {
            if (S.Storage == null) S.Storage = new byte[plan.Storage.Size];
            S.StorageReadAgain = true;
            storageHave = 0;
            using (Stream source = Engine.OpenImage(plan.Path))
            {
                Engine.SkipForward(source, plan.Storage.Start);
                if (Engine.ReadFull(source, S.Storage, S.Storage.Length) != S.Storage.Length) throw new EndOfStreamException("the image ended in its storage partition");
                storageHave = S.Storage.Length;
            }
        }
    }

    // What the change is, as patches.
    sealed class SbBuild
    {
        public List<Patch> System = new List<Patch>();  // the system partition: FATs, directories and the new contents
        public List<Patch> Storage = new List<Patch>(); // the key slot in the storage partition (none: it was there already)
        public byte[] Cert;
        public bool NewKey;
        public string Summary = "";
        public byte[] Grub, Kernel;                     // the signed files (for the tests)
    }

    static class SecureBoot
    {
        const long Mebi = 1048576;

        static Patch Make(long offset, byte[] bytes, bool layered)
        {
            var p = new Patch();
            p.Offset = offset;
            p.Bytes = bytes;
            p.Layered = layered;
            return p;
        }

        static void Free(ushort[] table, List<int> chain)
        {
            foreach (int c in chain) table[c] = 0;
        }

        // n clusters in a row, the first free run that is long enough; else the lowest free ones (a file in pieces). null: no room.
        static List<int> Take(ushort[] table, int clusters, int n)
        {
            var list = new List<int>();
            int run = 0, start = 0;
            for (int c = 2; c <= clusters + 1; ++c)
            {
                if (table[c] == 0) { if (run == 0) start = c; if (++run == n) { for (int i = 0; i < n; ++i) list.Add(start + i); break; } }
                else run = 0;
            }
            if (list.Count == 0)
            {
                for (int c = 2; c <= clusters + 1 && list.Count < n; ++c) if (table[c] == 0) list.Add(c);
                if (list.Count < n) return null;
            }
            for (int i = 0; i < list.Count; ++i) table[list[i]] = (ushort)(i + 1 < list.Count ? list[i + 1] : 0xFFFF);
            return list;
        }

        // The patches for a file that goes into those clusters: each run of clusters in a row is one patch.
        static void Put(List<Patch> into, SbSystem s, long systemStart, List<int> chain, byte[] data)
        {
            int cb = s.ClusterBytes;
            var padded = new byte[(long)chain.Count * cb];
            Buffer.BlockCopy(data, 0, padded, 0, data.Length);
            int i = 0;
            while (i < chain.Count)
            {
                int j = i;
                while (j + 1 < chain.Count && chain[j + 1] == chain[j] + 1) ++j;
                byte[] piece = i == 0 && j == chain.Count - 1 ? padded : Der.Slice(padded, i * cb, (j + 1) * cb);
                into.Add(Make(systemStart + s.ClusterAt(chain[i]), piece, false));
                i = j + 1;
            }
        }

        static byte[] PutAt(byte[] bytes, int at, byte[] what)
        {
            Buffer.BlockCopy(what, 0, bytes, at, what.Length);
            return bytes;
        }

        // The patches that make an image start with Secure Boot: key and cert are those of the PC (or of the Hades that is updated).
        // writeSlot: the key goes into the key slot of the image's storage (an install: the update of a Hades that has none yet
        // writes its slot on the disk instead, and one that has its key does not touch it).
        public static SbBuild Build(ImagePlan plan, SbKey key, byte[] cert, bool writeSlot, string signingTime)
        {
            SbSystem s = plan.Sb;
            if (s == null || !s.Known)
                throw new InvalidDataException(L.T("This image cannot start with Secure Boot", "Esta imagen no puede arrancar con Secure Boot") +
                                               (s != null && s.Why != null ? " (" + s.Why + ")." : "."));
            string wrong = SbCert.Check(cert, key);
            if (wrong != null) throw new InvalidDataException("the certificate is not the key's: " + wrong);
            var build = new SbBuild();
            build.Cert = cert;
            build.NewKey = writeSlot;
            long start = plan.System.Start;
            int cb = s.ClusterBytes;
            int clusters = s.Table.Length - 2;

            byte[] grub = Authenticode.Sign(s.GrubBytes, key, cert, signingTime);
            byte[] kernel = Authenticode.Sign(s.KernelBytes, key, cert, signingTime);
            build.Grub = grub;
            build.Kernel = kernel;

            // the FAT
            ushort[] table = (ushort[])s.Table.Clone();
            Free(table, s.GrubChain);
            Free(table, s.KernelChain);
            Func<int, int> need = delegate(int bytes) { return (bytes + cb - 1) / cb; };
            List<int> toGrubx = Take(table, clusters, need(grub.Length));
            List<int> toMm = Take(table, clusters, need(s.MmBytes.Length));
            List<int> toBoot = Take(table, clusters, need(s.ShimBytes.Length));
            List<int> toKernel = Take(table, clusters, need(kernel.Length));
            List<int> toCert = Take(table, clusters, need(cert.Length));
            if (toGrubx == null || toMm == null || toBoot == null || toKernel == null || toCert == null)
                throw new InvalidDataException(L.T("There is not enough room in the system partition of the image for Secure Boot.",
                                                   "No hay sitio suficiente en la partición de sistema de la imagen para Secure Boot."));

            // the directories
            byte[] root = (byte[])s.Root.Clone(), bootDir = (byte[])s.BootDir.Clone();
            PutAt(root, s.Kernel.Offset + 26, BitConverter.GetBytes((ushort)toKernel[0]));
            PutAt(root, s.Kernel.Offset + 28, BitConverter.GetBytes((uint)kernel.Length));
            PutAt(bootDir, s.Grub.Offset + 26, BitConverter.GetBytes((ushort)toBoot[0]));
            PutAt(bootDir, s.Grub.Offset + 28, BitConverter.GetBytes((uint)s.ShimBytes.Length));
            byte[] times = Der.Slice(s.BootDir, s.Grub.Offset, s.Grub.Offset + 32);
            int slotGrub = SbDir.FreeSlot(bootDir), slotMm = SbDir.FreeSlot(bootDir, slotGrub), slotCer = SbDir.FreeSlot(root);
            if (slotGrub < 0 || slotMm < 0 || slotCer < 0)
                throw new InvalidDataException(L.T("There is no room for three more entries in the folders of the image.",
                                                   "No hay sitio para tres entradas más en las carpetas de la imagen."));
            PutAt(bootDir, slotGrub, SbDir.NewEntry("GRUBX64 EFI", 0x18, times, toGrubx[0], grub.Length));
            PutAt(bootDir, slotMm, SbDir.NewEntry("MMX64   EFI", 0x18, times, toMm[0], s.MmBytes.Length));
            PutAt(root, slotCer, SbDir.NewEntry("HADES-SBCER", 0x00, times, toCert[0], cert.Length));
            // the end of a directory is its first entry that is 0: where a new entry went in front of it, what comes after must be
            // 0 too (it was: the new entry took the first free place, and after it nothing was in use)

            // what is written
            int low = -1, high = -1;
            for (int c = 0; c < table.Length; ++c)
                if (table[c] != s.Table[c]) { if (low < 0) low = c; high = c; }
            var span = new byte[2 * (high - low + 1)];
            for (int c = low; c <= high; ++c) { span[2 * (c - low)] = (byte)table[c]; span[2 * (c - low) + 1] = (byte)(table[c] >> 8); }
            for (int copy = 0; copy < s.FatCount; ++copy)
                build.System.Add(Make(start + s.FatAt + (long)copy * s.FatSectors * 512 + 2L * low, (byte[])span.Clone(), true));
            foreach (int at in new[] { s.Kernel.Offset, slotCer })
                build.System.Add(Make(start + s.RootAt + at, Der.Slice(root, at, at + 32), true));
            foreach (int at in new[] { s.Grub.Offset, slotGrub, slotMm })
            {
                int cluster = s.BootDirChain[at / cb];
                build.System.Add(Make(start + s.ClusterAt(cluster) + at % cb, Der.Slice(bootDir, at, at + 32), false));
            }
            Put(build.System, s, start, toBoot, s.ShimBytes);
            Put(build.System, s, start, toGrubx, grub);
            Put(build.System, s, start, toMm, s.MmBytes);
            Put(build.System, s, start, toKernel, kernel);
            Put(build.System, s, start, toCert, cert);

            if (writeSlot)
            {
                SbSlot slot = SbSlot.Read(DiagFolder.Reader(s.Storage), s.Storage.Length);
                if (!slot.Valid || !slot.Empty)
                    throw new InvalidDataException(L.T("The key slot of this image cannot be used", "El hueco de la clave de esta imagen no se puede usar") +
                                                   (slot.Why != null ? " (" + slot.Why + ")." : "."));
                build.Storage.AddRange(slot.Patches(SbSlot.Fill(key.Pem()), plan.Storage.Start));
            }

            string bad = Check(plan, build, s, table, new[] { toGrubx, toMm, toBoot, toKernel, toCert }, new[] { grub, s.MmBytes, s.ShimBytes, kernel, cert });
            if (bad != null) throw new InvalidDataException("the changes for Secure Boot do not check: " + bad);
            build.Summary = "bootx64.efi = shim (" + s.ShimBytes.Length + " bytes, clusters from " + toBoot[0] + "), grubx64.efi = GRUB signed (" + grub.Length +
                            ", from " + toGrubx[0] + "), mmx64.efi (" + s.MmBytes.Length + ", from " + toMm[0] + "), KERNEL signed (" + kernel.Length +
                            ", from " + toKernel[0] + "), HADES-SB.CER (" + cert.Length + "); " + build.System.Count + " patches in the system partition, " +
                            build.Storage.Count + " in the storage; key " + key.Short();
            return build;
        }

        // The partition as the patches make it, read back: the two FATs are alike, no chain is crossed or broken, every file that
        // was put in reads as what it has to be, the entries of the directories are the ones that were meant, and nothing that was
        // in use changed but the two files whose chains were replaced.
        static string Check(ImagePlan plan, SbBuild build, SbSystem s, ushort[] table, List<int>[] chains, byte[][] contents)
        {
            long start = plan.System.Start;
            int cb = s.ClusterBytes;
            // the start of the partition as the patches make it
            byte[] after = (byte[])s.Meta.Clone();
            var data = new Dictionary<int, byte[]>();
            foreach (Patch p in build.System)
            {
                long at = p.Offset - start;
                if (at + p.Bytes.Length <= s.DataAt)
                {
                    Buffer.BlockCopy(p.Bytes, 0, after, (int)at, p.Bytes.Length);
                    continue;
                }
                if (at < s.DataAt) return "a patch crosses the end of the metadata";
                // data: the clusters it covers, read as whole clusters, except entries of /EFI/BOOT
                long first = (at - s.DataAt) / cb;
                for (long k = first; k * cb < at - s.DataAt + p.Bytes.Length; ++k)
                {
                    int c = (int)k + 2;
                    byte[] cluster;
                    if (!data.TryGetValue(c, out cluster)) { cluster = new byte[cb]; data[c] = cluster; }
                    long from = Math.Max(at, s.ClusterAt(c)), to = Math.Min(at + p.Bytes.Length, s.ClusterAt(c) + cb);
                    Buffer.BlockCopy(p.Bytes, (int)(from - at), cluster, (int)(from - s.ClusterAt(c)), (int)(to - from));
                }
            }
            string why;
            Fat16 fat = Fat16.Open(DiagFolder.Reader(after), plan.System.Size, out why);
            if (fat == null) return "what the patches make is not a FAT16: " + why;
            if (!fat.SameFats) return "the two FATs differ in what the patches make";
            for (int i = 0; i < 512; ++i) if (after[i] != s.Meta[i]) return "the boot sector changes";
            for (int c = 2; c < table.Length; ++c) if (fat.Table[c] != table[c]) return "the FAT is not the one that was meant at cluster " + c;
            // the clusters: those that were in use and are not the two chains that went, unchanged; those that came in were free
            var gone = new HashSet<int>(s.GrubChain.Concat(s.KernelChain));
            var came = new HashSet<int>();
            foreach (List<int> chain in chains) foreach (int c in chain) if (!came.Add(c)) return "a cluster is given twice";
            for (int c = 2; c < table.Length; ++c)
            {
                bool wasUsed = s.Table[c] != 0;
                if (came.Contains(c) && wasUsed && !gone.Contains(c)) return "a cluster that was in use is taken";
                if (!came.Contains(c) && !gone.Contains(c) && fat.Table[c] != s.Table[c]) return "a cluster changes that was not meant to";
                if (gone.Contains(c) && !came.Contains(c) && fat.Table[c] != 0) return "a cluster that was freed is not free";
            }
            // nothing is the next of two clusters (a crossed chain)
            var pointedTo = new HashSet<int>();
            for (int c = 2; c < table.Length; ++c)
            {
                int next = fat.Table[c];
                if (next >= 2 && next < 0xFFF0 && !pointedTo.Add(next)) return "cluster " + next + " is the next of two clusters";
            }
            // the directories
            Func<int, byte[]> cluster2 = delegate(int c)
            {
                byte[] d;
                return data.TryGetValue(c, out d) ? d : null;
            };
            List<DirEnt> root = SbDir.List(fat.Root);
            DirEnt kernel = SbDir.Find(root, "KERNEL"), cer = SbDir.Find(root, "HADES-SB.CER");
            if (kernel == null || cer == null) return "KERNEL or HADES-SB.CER is not in the root directory";
            if (SbDir.List(fat.Root).Count != SbDir.List(s.Root).Count + 1) return "the root directory does not have one entry more";
            // /EFI/BOOT as it is after the patches: the clusters of the folder, from the old ones with the new entries
            var bootBytes = new byte[s.BootDir.Length];
            for (int i = 0; i < s.BootDirChain.Count; ++i)
            {
                byte[] was = Der.Slice(s.BootDir, i * cb, (i + 1) * cb);
                foreach (Patch p in build.System)
                {
                    long at = p.Offset - start - s.ClusterAt(s.BootDirChain[i]);
                    if (at >= 0 && at + p.Bytes.Length <= cb && p.Bytes.Length == 32) Buffer.BlockCopy(p.Bytes, 0, was, (int)at, 32);
                }
                Buffer.BlockCopy(was, 0, bootBytes, i * cb, cb);
            }
            List<DirEnt> boot = SbDir.List(bootBytes);
            DirEnt bx = SbDir.Find(boot, "bootx64.efi"), gx = SbDir.Find(boot, "grubx64.efi"), mx = SbDir.Find(boot, "mmx64.efi");
            if (bx == null || gx == null || mx == null) return "bootx64.efi, grubx64.efi or mmx64.efi is not in /EFI/BOOT";
            if (boot.Count != SbDir.List(s.BootDir).Count + 2) return "/EFI/BOOT does not have two entries more";
            // every file, read through its chain as the patches leave it
            DirEnt[] ents = { gx, mx, bx, kernel, cer };
            for (int i = 0; i < ents.Length; ++i)
            {
                if (ents[i].Size != contents[i].Length) return ents[i].Name + " has a size that is not its content's";
                List<int> chain = fat.Chain(ents[i].First, (contents[i].Length + cb - 1) / cb);
                if (chain == null || chain.Count != chains[i].Count) return ents[i].Name + ": its chain is broken or not as long as it has to be";
                for (int k = 0; k < chain.Count; ++k) if (chain[k] != chains[i][k]) return ents[i].Name + ": its chain is not the one that was allocated";
                var read = new byte[chain.Count * cb];
                for (int k = 0; k < chain.Count; ++k)
                {
                    byte[] c = cluster2(chain[k]);
                    if (c == null) return ents[i].Name + ": a cluster of it is not written";
                    Buffer.BlockCopy(c, 0, read, k * cb, cb);
                }
                for (int k = 0; k < contents[i].Length; ++k) if (read[k] != contents[i][k]) return ents[i].Name + " does not read back as its content, at byte " + k;
                for (int k = contents[i].Length; k < read.Length; ++k) if (read[k] != 0) return ents[i].Name + " has something after its end in its last cluster";
            }
            // the other entries of the directories did not change (the names, the first clusters and the sizes of the rest)
            Func<List<DirEnt>, List<DirEnt>, string[], string> same = delegate(List<DirEnt> a, List<DirEnt> b, string[] except)
            {
                foreach (DirEnt x in a)
                {
                    if (Array.IndexOf(except, x.Name.ToLowerInvariant()) >= 0) continue;
                    DirEnt y = SbDir.Find(b, x.Name);
                    if (y == null || y.First != x.First || y.Size != x.Size || y.Attr != x.Attr) return x.Name;
                }
                return null;
            };
            string moved = same(SbDir.List(s.Root), root, new[] { "kernel" }) ?? same(SbDir.List(s.BootDir), boot, new[] { "bootx64.efi" });
            if (moved != null) return "the entry " + moved + " changes and was not meant to";
            // the signatures of what was put in
            if (Authenticode.Verify(build.Grub, build.Cert) != null || Authenticode.Verify(build.Kernel, build.Cert) != null) return "a signature does not verify";
            return null;
        }
    }
}
