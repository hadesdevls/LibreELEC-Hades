// LibreELEC Hades installer for Windows: a read-only look at an ext4 file system, enough to find one file by its path and to
// say which stretches of the partition hold it; and the key slot of the Hades storage (/.hades-secureboot/key), the file
// that keeps the private key of Secure Boot, which the installer writes only into a slot that is empty.
// Nothing here makes or changes a file system: the slot is a file of exactly 4096 bytes that the image has already, and a
// write puts new bytes where the old ones are, in the blocks that the file already has.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Hades.Installer
{
    // Where a file is in an ext4: its blocks, as stretches of the partition.
    sealed class Ext4File
    {
        public long Size;
        public int BlockBytes;
        public uint Inode;
        public List<long[]> Runs = new List<long[]>(); // {offset in the partition, length}: in the order of the file, up to its size
    }

    sealed class Ext4
    {
        readonly Func<long, int, byte[]> read;
        readonly long size;
        public int BlockBytes, InodeBytes, InodesPerGroup, DescBytes;
        long firstDataBlock;
        public uint State, Incompat;
        public bool Clean { get { return (State & 1) != 0 && (State & 2) == 0 && (Incompat & 0x4) == 0; } }

        Ext4(Func<long, int, byte[]> read, long size) { this.read = read; this.size = size; }

        static uint U32(byte[] b, int at) { return BitConverter.ToUInt32(b, at); }
        static int U16(byte[] b, int at) { return BitConverter.ToUInt16(b, at); }

        // null, with why, where it is not an ext4 this reader knows.
        public static Ext4 Open(Func<long, int, byte[]> read, long size, out string why)
        {
            why = null;
            if (size < 8192) { why = "too small"; return null; }
            byte[] sb = read(1024, 1024);
            if (U16(sb, 0x38) != 0xEF53) { why = "no ext superblock"; return null; }
            var e = new Ext4(read, size);
            uint logBlock = U32(sb, 0x18);
            if (logBlock > 6) { why = "a block size that is not known"; return null; }
            e.BlockBytes = 1024 << (int)logBlock;
            e.firstDataBlock = U32(sb, 0x14);
            e.InodesPerGroup = (int)U32(sb, 0x28);
            e.State = (uint)U16(sb, 0x3A);
            uint rev = U32(sb, 0x4C);
            e.InodeBytes = rev == 0 ? 128 : U16(sb, 0x58);
            e.Incompat = rev == 0 ? 0 : U32(sb, 0x60);
            e.DescBytes = (e.Incompat & 0x80) != 0 ? U16(sb, 0xFE) : 32;
            if (e.InodesPerGroup <= 0 || e.InodeBytes < 128 || e.InodeBytes > e.BlockBytes || e.DescBytes < 32 || e.DescBytes > 1024 ||
                (e.Incompat & 0x80) != 0 && e.DescBytes < 64)
            { why = "a superblock with values that are not sane"; return null; }
            return e;
        }

        byte[] Block(long number)
        {
            long at = number * BlockBytes;
            if (number < 0 || at + BlockBytes > size) throw new InvalidDataException("a block past the end of the partition");
            return read(at, BlockBytes);
        }

        sealed class Inode
        {
            public int Mode;
            public long Size;
            public uint Flags;
            public byte[] IBlock; // 60 bytes
        }

        // where the inode is, in the partition (the tests change a field of it)
        internal long InodeOffset(uint number)
        {
            long group = (number - 1) / InodesPerGroup, index = (number - 1) % InodesPerGroup;
            long table = (firstDataBlock + 1) * BlockBytes + group * DescBytes;
            byte[] gd = read(table, DescBytes);
            long inodeTable = U32(gd, 0x08);
            if (DescBytes >= 64) inodeTable |= (long)U32(gd, 0x28) << 32;
            return inodeTable * BlockBytes + index * InodeBytes;
        }

        Inode ReadInode(uint number)
        {
            if (number == 0) throw new InvalidDataException("inode 0");
            long group = (number - 1) / InodesPerGroup, index = (number - 1) % InodesPerGroup;
            long table = (firstDataBlock + 1) * BlockBytes + group * DescBytes;
            byte[] gd = read(table, DescBytes);
            long inodeTable = U32(gd, 0x08);
            if (DescBytes >= 64) inodeTable |= (long)U32(gd, 0x28) << 32;
            byte[] raw = read((inodeTable * BlockBytes) + index * InodeBytes, InodeBytes);
            var i = new Inode();
            i.Mode = U16(raw, 0);
            i.Size = U32(raw, 4) | ((long)U32(raw, 0x6C) << 32);
            i.Flags = U32(raw, 0x20);
            i.IBlock = Der.Slice(raw, 0x28, 0x28 + 60);
            return i;
        }

        // The blocks of a file, in order: {logical block, first physical block, count}. An extent that is not initialized, or a
        // file with no extents (inline data, the old block map), is refused.
        List<long[]> Extents(Inode node)
        {
            if ((node.Flags & 0x10000000) != 0) throw new InvalidDataException("the data is in the inode");
            if ((node.Flags & 0x80000) == 0) throw new InvalidDataException("the file has no extents");
            var list = new List<long[]>();
            Walk(node.IBlock, 0, list);
            return list;
        }

        void Walk(byte[] node, int depthSeen, List<long[]> list)
        {
            if (depthSeen > 5) throw new InvalidDataException("an extent tree that is too deep");
            if (U16(node, 0) != 0xF30A) throw new InvalidDataException("an extent node with no magic");
            int entries = U16(node, 2), depth = U16(node, 6);
            if (12 + entries * 12 > node.Length) throw new InvalidDataException("an extent node with more entries than it can hold");
            for (int i = 0; i < entries; ++i)
            {
                int e = 12 + 12 * i;
                if (depth == 0)
                {
                    long first = U32(node, e), len = U16(node, e + 4);
                    if (len > 32768) throw new InvalidDataException("an extent that is not initialized");
                    long start = ((long)U16(node, e + 6) << 32) | U32(node, e + 8);
                    list.Add(new[] { first, start, len });
                }
                else
                {
                    long leaf = ((long)U16(node, e + 8) << 32) | U32(node, e + 4);
                    Walk(Block(leaf), depthSeen + 1, list);
                }
            }
        }

        // The bytes of a directory, block after block.
        byte[] DirectoryBytes(Inode dir)
        {
            if ((dir.Mode & 0xF000) != 0x4000) throw new InvalidDataException("not a directory");
            List<long[]> ex = Extents(dir);
            long blocks = (dir.Size + BlockBytes - 1) / BlockBytes;
            if (blocks > 4096) throw new InvalidDataException("a directory that is too big");
            var bytes = new byte[blocks * BlockBytes];
            foreach (long[] x in ex)
                for (long b = 0; b < x[2]; ++b)
                {
                    if (x[0] + b >= blocks) continue;
                    Buffer.BlockCopy(Block(x[1] + b), 0, bytes, (int)((x[0] + b) * BlockBytes), BlockBytes);
                }
            return bytes;
        }

        // The inode that a directory gives to a name. The entries are read in a line: the blocks of an index look like one empty entry
        // (its inode is 0), and the tail of the checksum of a block is an entry with inode 0 too. 0: there is none.
        uint Lookup(Inode dir, string name)
        {
            byte[] bytes = DirectoryBytes(dir);
            byte[] want = Encoding.UTF8.GetBytes(name);
            for (int block = 0; block < bytes.Length; block += BlockBytes)
            {
                int at = block;
                while (at + 8 <= block + BlockBytes)
                {
                    uint inode = U32(bytes, at);
                    int rec = U16(bytes, at + 4), nameLength = bytes[at + 6];
                    if (rec == 0 && BlockBytes == 65536) rec = 65536;
                    if (rec < 8 || (rec & 3) != 0 || at + rec > block + BlockBytes || 8 + nameLength > rec)
                        throw new InvalidDataException("a directory entry that is not valid");
                    if (inode != 0 && nameLength == want.Length)
                    {
                        bool same = true;
                        for (int k = 0; same && k < nameLength; ++k) same = bytes[at + 8 + k] == want[k];
                        if (same) return inode;
                    }
                    at += rec;
                }
            }
            return 0;
        }

        // A regular file by its path ("a/b"), of exactly "expectedSize" bytes, mapped whole to blocks; null, with why, otherwise.
        public Ext4File Find(string path, long expectedSize, out string why)
        {
            why = null;
            try
            {
                Inode node = ReadInode(2);
                uint number = 2;
                string[] parts = path.Split('/');
                for (int i = 0; i < parts.Length; ++i)
                {
                    uint next = Lookup(node, parts[i]);
                    if (next == 0) { why = parts[i] + " is not there"; return null; }
                    node = ReadInode(next);
                    number = next;
                    bool last = i == parts.Length - 1;
                    if (!last && (node.Mode & 0xF000) != 0x4000) { why = parts[i] + " is not a directory"; return null; }
                    if (last && (node.Mode & 0xF000) != 0x8000) { why = parts[i] + " is not a regular file"; return null; }
                }
                if (node.Size != expectedSize) { why = "it is " + node.Size + " bytes long, " + expectedSize + " were expected"; return null; }
                List<long[]> ex = Extents(node);
                long blocks = (expectedSize + BlockBytes - 1) / BlockBytes;
                var f = new Ext4File();
                f.Inode = number;
                f.Size = expectedSize;
                f.BlockBytes = BlockBytes;
                for (long b = 0; b < blocks; ++b)
                {
                    long physical = -1;
                    foreach (long[] x in ex)
                        if (b >= x[0] && b < x[0] + x[2]) physical = x[1] + (b - x[0]);
                    if (physical < 0) { why = "block " + b + " of the file is not mapped"; return null; }
                    long at = physical * BlockBytes;
                    if (at + BlockBytes > size) { why = "the file is past the end of the partition"; return null; }
                    long length = Math.Min(BlockBytes, expectedSize - b * BlockBytes);
                    if (f.Runs.Count > 0 && f.Runs[f.Runs.Count - 1][0] + f.Runs[f.Runs.Count - 1][1] == at && b * BlockBytes == SumLength(f))
                        f.Runs[f.Runs.Count - 1][1] += length;
                    else f.Runs.Add(new[] { at, length });
                }
                return f;
            }
            catch (InvalidDataException e) { why = e.Message; return null; }
            catch (IOException e) { why = e.Message; return null; }
        }

        static long SumLength(Ext4File f)
        {
            long n = 0;
            foreach (long[] r in f.Runs) n += r[1];
            return n;
        }

        public byte[] ReadFile(Ext4File f)
        {
            var bytes = new byte[f.Size];
            long at = 0;
            foreach (long[] r in f.Runs)
            {
                byte[] piece = read(r[0], (int)r[1]);
                Buffer.BlockCopy(piece, 0, bytes, (int)at, piece.Length);
                at += r[1];
            }
            return bytes;
        }
    }

    // The slot for the private key of Secure Boot, in the storage of Hades: /.hades-secureboot/key, 4096 bytes. Its first line is
    // "hades-secureboot-slot 1"; empty, the rest is LF; with a key, the key as PKCS#8 PEM and LF to fill it up.
    sealed class SbSlot
    {
        public const string Path = ".hades-secureboot/key";
        public const int Bytes = 4096;
        public const string FirstLine = "hades-secureboot-slot 1\n";

        public Ext4File File;      // where it is (null: there is no slot that this reader accepts)
        public string Why;         // why not
        public byte[] Content;
        public bool Empty;         // the slot has no key
        public string KeyPem;      // the key, as text, when it has one
        public bool Clean;         // the file system is clean: nothing has to be replayed (a write needs it)
        public bool Valid { get { return File != null && Content != null && (Empty || KeyPem != null); } }

        // The slot of an ext4 that is read through "read". Never throws.
        public static SbSlot Read(Func<long, int, byte[]> read, long size)
        {
            var s = new SbSlot();
            try
            {
                string why;
                Ext4 fs = Ext4.Open(read, size, out why);
                if (fs == null) { s.Why = "not an ext4 that can be read (" + why + ")"; return s; }
                s.Clean = fs.Clean;
                Ext4File f = fs.Find(Path, Bytes, out why);
                if (f == null) { s.Why = "no key slot (" + why + ")"; return s; }
                byte[] content = fs.ReadFile(f);
                string text = Encoding.ASCII.GetString(content);
                if (!text.StartsWith(FirstLine, StringComparison.Ordinal)) { s.Why = "the slot does not start with its first line"; return s; }
                string rest = text.Substring(FirstLine.Length);
                string body = rest.TrimEnd('\n');
                for (int i = 0; i < content.Length; ++i) if (content[i] >= 0x80) { s.Why = "the slot has bytes that are not text"; return s; }
                if (body.Length == 0) s.Empty = true;
                else
                {
                    const string tail = "-----END PRIVATE KEY-----";
                    if (!body.StartsWith("-----BEGIN PRIVATE KEY-----\n", StringComparison.Ordinal) || !body.EndsWith(tail, StringComparison.Ordinal))
                    { s.Why = "the slot holds something that is not a PKCS#8 key and is not empty"; return s; }
                    s.KeyPem = body + "\n";
                }
                s.File = f;
                s.Content = content;
                return s;
            }
            catch (Exception e)
            {
                s.Why = "the slot could not be read (" + e.Message + ")";
                return s;
            }
        }

        // The 4096 bytes of a slot with this key.
        public static byte[] Fill(string pem)
        {
            string text = FirstLine + pem;
            if (text.Length > Bytes) throw new InvalidOperationException("the key does not fit in its slot");
            var bytes = new byte[Bytes];
            for (int i = 0; i < Bytes; ++i) bytes[i] = (byte)'\n';
            Encoding.ASCII.GetBytes(text, 0, text.Length, bytes, 0);
            return bytes;
        }

        // The patches that put these 4096 bytes into the slot of an image: where the stretches of the slot are, from the start of the
        // image (partitionStart is where the storage partition starts in it).
        public List<Patch> Patches(byte[] content, long partitionStart)
        {
            var list = new List<Patch>();
            int at = 0;
            foreach (long[] r in File.Runs)
            {
                var p = new Patch();
                p.Offset = partitionStart + r[0];
                p.Bytes = Der.Slice(content, at, at + (int)r[1]);
                list.Add(p);
                at += (int)r[1];
            }
            return list;
        }

        // A key into the slot of a partition of a disk, which is read and written through "partition" (a stream of the partition from its
        // start). Only into a slot that is empty, of a file system that is clean, and by writing whole 4096-byte stretches
        // (read, changed where the slot is, written, read again), so that it works whatever the sector of the disk is. Throws,
        // having written nothing, when the slot is not as it has to be; throws after having put the old bytes back, when what is read
        // back is not what was written.
        public static void WriteTo(Stream partition, long size, string pem)
        {
            Func<long, int, byte[]> reader = DiagFolder.Reader(partition, size);
            SbSlot slot = Read(reader, size);
            if (!slot.Valid) throw new InvalidOperationException("the key slot of that storage cannot be used: " + slot.Why);
            if (!slot.Empty) throw new InvalidOperationException("the key slot of that storage is not empty");
            if (!slot.Clean) throw new InvalidOperationException("the file system of that storage was not shut down cleanly: it is not written");
            byte[] content = Fill(pem);
            var originals = new List<long[]>();
            var spans = new List<byte[]>();
            // each stretch, inside the 4096-byte pieces that hold it
            int at = 0;
            var done = new List<long>();
            foreach (long[] r in slot.File.Runs)
            {
                long first = r[0] / 4096 * 4096, last = (r[0] + r[1] + 4095) / 4096 * 4096;
                if (last > size) throw new InvalidOperationException("a stretch of the slot is at the end of the partition");
                byte[] piece = reader(first, (int)(last - first));
                spans.Add((byte[])piece.Clone());
                originals.Add(new[] { first, last - first });
                Buffer.BlockCopy(content, at, piece, (int)(r[0] - first), (int)r[1]);
                at += (int)r[1];
                partition.Seek(first, SeekOrigin.Begin);
                partition.Write(piece, 0, piece.Length);
                done.Add(first);
            }
            partition.Flush();
            // read it again, from the disk, not from what the reader remembers
            SbSlot after = Read(DiagFolder.Reader(partition, size), size);
            if (!after.Valid || after.Empty || after.KeyPem == null || after.KeyPem != pem)
            {
                for (int i = 0; i < originals.Count; ++i)
                {
                    partition.Seek(originals[i][0], SeekOrigin.Begin);
                    partition.Write(spans[i], 0, spans[i].Length);
                }
                partition.Flush();
                throw new IOException("the key slot does not read back as what was written; the old bytes were put back");
            }
        }
    }
}
