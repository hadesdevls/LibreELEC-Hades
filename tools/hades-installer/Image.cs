// LibreELEC Hades installer for Windows: reading a Hades image and writing it, whole or partition by partition, to
// any seekable stream (a disk, a partition, or a file in the tests), and reading back what was written.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace Hades.Installer
{
    sealed class ImagePart
    {
        public int Index;        // in the partition table of the image, from 0
        public Guid Type;
        public long Start, Size; // bytes
        public long End { get { return Start + Size; } }
    }

    // What the installer needs to know of an image before it writes it.
    sealed class ImagePlan
    {
        public string Path;
        public long Size;
        public ImagePart System;   // FAT: the boot loader, KERNEL and SYSTEM
        public ImagePart Storage;  // ext4, made anew at the first boot over its whole partition
        public byte[] Head;        // the first Engine.Head bytes
        public byte[] Tail;        // the last Engine.TailSize bytes (the backup partition table)
        public bool Scanned;
        // Where the boot menus hide the internal disks (Engine.HideInternal), as offsets in the image
        public List<long> HideOffsets = new List<long>();
        // The image says that /storage can be any partition of its disk
        public bool StorageAnywhere;
        // ... and that, installed on an internal disk, it takes the other internal disks away by itself
        // when its entries ask for it (Engine.HideOthers)
        public bool HidesOtherDisks;
        // Where the image has the switch of its diagnostics, off: the name of the file DIAG-OFF.TXT in the
        // root directory of its system partition, as an offset in the image. -1: it has none.
        public long SwitchOffset = -1;
        // The image has its diagnostics on already (a file whose name starts with DIAG-ON): whatever is
        // written from it has them on, and they cannot be asked for or left out.
        public bool SwitchOn;
        // Where the boot menus name the storage partition ("disk=UUID=<uuid>": the offsets of the uuid
        // itself), the uuid they name, and the one that the file system of the image's storage has
        public List<long> StorageUuidOffsets = new List<long>();
        public string StorageUuid, StorageUuidInFs;
    }

    sealed class WriteResult
    {
        public long Bytes;
        public string Written; // hash of what was sent to the target
        public string ReadBack; // hash of what the target gives back
        public bool Verified { get { return Written != null && Written == ReadBack; } }
    }

    static class Crc32
    {
        static readonly uint[] Table = Make();

        static uint[] Make()
        {
            var t = new uint[256];
            for (uint i = 0; i < 256; ++i)
            {
                uint c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }

        public static uint Of(byte[] data, int offset, int count)
        {
            uint c = 0xFFFFFFFFu;
            for (int i = 0; i < count; ++i) c = Table[(c ^ data[offset + i]) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }
    }

    // Bytes that replace what the image has at an offset.
    sealed class Patch
    {
        public long Offset;
        public byte[] Bytes;
    }

    static class Engine
    {
        public const int Chunk = 4 * 1024 * 1024;
        // The start of a disk holds its partition table, and the start of a partition says which file
        // system it has. Both are written last: until then Windows sees nothing there that it could mount
        // while the rest is being written.
        public const int Head = 1024 * 1024;
        public const int TailSize = 64 * 1024;
        const int Sector = 512;

        // The kernel parameter with which the USB entries of the boot menu keep the NVMe and SATA
        // controllers from starting, so that the internal disks do not exist (scripts/mkimage).
        public const string HideInternal = "initcall_blacklist=nvme_init,ahci_pci_driver_init";
        public const string FeatureStorageAnywhere = "# hades-features: storage-anywhere";
        // On an internal disk that parameter would take away the disk of Hades itself. An image that says
        // this (the first line of its menus, which starts as the one above) knows another way: with this
        // parameter its init detaches the NVMe and SATA disks that Hades is not on, and has the kernel
        // refuse them from then on (busybox/scripts/init, hide_other_disks).
        public const string FeatureOtherDisks = "# hades-features: storage-anywhere other-disks-hidden";
        public const string HideOthers = "hades.otherdisks=hide";
        // The switch of the diagnostics is the name of a file at the top of the system partition, which an
        // image carries as DIAG-OFF.TXT (packages/sysutils/hades-diag). These are the names as the
        // directory of a FAT keeps them: eight characters and three, upper case, no dot.
        public const string SwitchOff = "DIAG-OFFTXT", SwitchOn = "DIAG-ON TXT";
        public static readonly Guid EfiSystem = new Guid("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
        public static readonly Guid LinuxData = new Guid("0fc63daf-8483-4772-8e79-3d69d8477de4");

        public delegate void Progress(string stage, long done, long total);
        // The stages that Progress names. They are not texts: Operations.Bar words them for the user.
        public const string Checking = "checking", Reading = "reading", Writing = "writing", Verifying = "verifying";

        public static bool IsGzip(string path)
        {
            return path.EndsWith(".gz", StringComparison.OrdinalIgnoreCase);
        }

        // The size of the image as it will be written. For a .gz, what its trailer says (modulo 4 GiB).
        public static long ImageSize(string path)
        {
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                if (!IsGzip(path)) return f.Length;
                if (f.Length < 18) throw new InvalidDataException(L.T("The image file is incomplete.", "El archivo de imagen está incompleto."));
                var tail = new byte[4];
                f.Seek(-4, SeekOrigin.End);
                f.Read(tail, 0, 4);
                return BitConverter.ToUInt32(tail, 0);
            }
        }

        static Stream Open(string path)
        {
            var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20);
            return IsGzip(path) ? (Stream)new GZipStream(file, CompressionMode.Decompress) : file;
        }

        public static string FileHash(string path, Progress progress, Func<bool> cancelled)
        {
            using (var sha = SHA256.Create())
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
            {
                var buffer = new byte[Chunk];
                long done = 0;
                int read;
                while ((read = f.Read(buffer, 0, buffer.Length)) > 0)
                {
                    if (cancelled != null && cancelled()) throw new OperationCanceledException();
                    sha.TransformBlock(buffer, 0, read, null, 0);
                    done += read;
                    if (progress != null) progress(Checking, done, f.Length);
                }
                sha.TransformFinalBlock(buffer, 0, 0);
                return Words.Hex(sha.Hash);
            }
        }

        // The hash that the file next to the image gives for it ("<hash>  <name>"), or null.
        public static string ExpectedHash(string imagePath)
        {
            string side = imagePath + ".sha256";
            if (!File.Exists(side)) return null;
            foreach (string line in File.ReadAllLines(side))
            {
                string t = line.Trim();
                if (t.Length < 64) continue;
                string hash = t.Substring(0, 64).ToLowerInvariant();
                bool hex = true;
                foreach (char c in hash) hex &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                if (hex) return hash;
            }
            return null;
        }

        static int Fill(Stream source, byte[] buffer, int count)
        {
            int total = 0;
            while (total < count)
            {
                int read = source.Read(buffer, total, count - total);
                if (read <= 0) break;
                total += read;
            }
            return total;
        }

        // ---- the partition table of the image

        sealed class Gpt
        {
            public int Count, EntrySize;
            public long MyLba, AlternateLba, EntriesLba;
            public byte[] Header;      // one sector
        }

        static Gpt ReadGpt(byte[] buffer, int headerAt)
        {
            if (Encoding.ASCII.GetString(buffer, headerAt, 8) != "EFI PART")
                throw new InvalidDataException(L.T("The image does not have the partition table of a Hades image.",
                                                   "La imagen no tiene la tabla de particiones de una imagen de Hades."));
            var g = new Gpt();
            g.Header = new byte[Sector];
            Buffer.BlockCopy(buffer, headerAt, g.Header, 0, Sector);
            int size = BitConverter.ToInt32(buffer, headerAt + 12);
            if (size < 92 || size > Sector)
                throw new InvalidDataException(L.T("The partition table of the image is not valid.",
                                                   "La tabla de particiones de la imagen no es válida."));
            var check = new byte[size];
            Buffer.BlockCopy(buffer, headerAt, check, 0, size);
            uint stored = BitConverter.ToUInt32(check, 16);
            check[16] = check[17] = check[18] = check[19] = 0;
            if (Crc32.Of(check, 0, size) != stored)
                throw new InvalidDataException(L.T("The partition table of the image is damaged.",
                                                   "La tabla de particiones de la imagen está dañada."));
            g.MyLba = BitConverter.ToInt64(buffer, headerAt + 24);
            g.AlternateLba = BitConverter.ToInt64(buffer, headerAt + 32);
            g.EntriesLba = BitConverter.ToInt64(buffer, headerAt + 72);
            g.Count = BitConverter.ToInt32(buffer, headerAt + 80);
            g.EntrySize = BitConverter.ToInt32(buffer, headerAt + 84);
            if (g.Count < 2 || g.Count > 256 || g.EntrySize != 128)
                throw new InvalidDataException(L.T("The partition table of the image is not that of a Hades image.",
                                                   "La tabla de particiones de la imagen no es la de una imagen de Hades."));
            return g;
        }

        static void SetHeaderCrcs(byte[] header, byte[] entries)
        {
            int size = BitConverter.ToInt32(header, 12);
            Buffer.BlockCopy(BitConverter.GetBytes(Crc32.Of(entries, 0, entries.Length)), 0, header, 88, 4);
            header[16] = header[17] = header[18] = header[19] = 0;
            Buffer.BlockCopy(BitConverter.GetBytes(Crc32.Of(header, 0, size)), 0, header, 16, 4);
        }

        static ImagePart Part(byte[] entries, int index)
        {
            int at = index * 128;
            var type = new byte[16];
            Buffer.BlockCopy(entries, at, type, 0, 16);
            long first = BitConverter.ToInt64(entries, at + 32), last = BitConverter.ToInt64(entries, at + 40);
            var p = new ImagePart();
            p.Index = index;
            p.Type = new Guid(type);
            p.Start = first * Sector;
            p.Size = (last - first + 1) * Sector;
            return p;
        }

        // Reads the image once. scan: all of it, to find what an install on an internal disk changes (it
        // takes as long as unpacking the image); else only its first megabyte.
        public static ImagePlan Plan(string path, bool scan, Progress progress, Func<bool> cancelled)
        {
            var plan = new ImagePlan();
            plan.Path = path;
            plan.Size = ImageSize(path);
            if (plan.Size <= 2 * Head)
                throw new InvalidDataException(L.T("The image is too small to be a Hades image.",
                                                   "La imagen es demasiado pequeña para ser una imagen de Hades."));
            if (plan.Size % Sector != 0)
                throw new InvalidDataException(L.T("The size of the image is not a multiple of 512 bytes.",
                                                   "El tamaño de la imagen no es múltiplo de 512 bytes."));
            using (Stream source = Open(path))
            {
                plan.Head = new byte[Head];
                if (Fill(source, plan.Head, Head) != Head)
                    throw new InvalidDataException(L.T("The image is incomplete.", "La imagen está incompleta."));
                Gpt gpt = ReadGpt(plan.Head, Sector);
                long entriesAt = gpt.EntriesLba * Sector;
                if (entriesAt + gpt.Count * 128 > Head)
                    throw new InvalidDataException(L.T("The partition table of the image does not fit where it is expected.",
                                                       "La tabla de particiones de la imagen no cabe donde se espera."));
                var entries = new byte[gpt.Count * 128];
                Buffer.BlockCopy(plan.Head, (int)entriesAt, entries, 0, entries.Length);
                plan.System = Part(entries, 0);
                plan.Storage = Part(entries, 1);
                if (plan.System.Start < Head || plan.System.Size < 64L * Head || plan.Storage.Start < plan.System.End ||
                    plan.Storage.Size < Head || plan.Storage.End > plan.Size || plan.Storage.Type != LinuxData)
                    throw new InvalidDataException(L.T("The partitions of the image are not those of a Hades image.",
                                                       "Las particiones de la imagen no son las de una imagen de Hades."));
                plan.SwitchOffset = FindSwitch(path, plan.System, out plan.SwitchOn);
                if (!scan) return plan;

                byte[] hide = Encoding.ASCII.GetBytes(HideInternal), feature = Encoding.ASCII.GetBytes(FeatureStorageAnywhere);
                byte[] others = Encoding.ASCII.GetBytes(FeatureOtherDisks);
                byte[] names = Encoding.ASCII.GetBytes(DiskUuid);
                int keep = Math.Max(Math.Max(hide.Length, others.Length), names.Length + UuidLength) - 1;
                var buffer = new byte[keep + Chunk];
                var super = new byte[2048]; // the start of the storage partition
                plan.Tail = new byte[TailSize];
                int carried = 0;
                long done = Head; // offset in the image of buffer[carried]
                int read;
                while ((read = Fill(source, buffer, carried, Chunk)) > 0)
                {
                    if (cancelled != null && cancelled()) throw new OperationCanceledException();
                    int have = carried + read;
                    long bufferStart = done - carried;
                    // only inside the system partition: that is where the boot menus are
                    if (bufferStart < plan.System.End && done + read > plan.System.Start)
                    {
                        foreach (int at in Find(buffer, have, hide))
                        {
                            long where = bufferStart + at;
                            if (where >= plan.System.Start && where + hide.Length <= plan.System.End &&
                                !plan.HideOffsets.Contains(where))
                                plan.HideOffsets.Add(where);
                        }
                        foreach (int at in Find(buffer, have, feature))
                            if (bufferStart + at >= plan.System.Start && bufferStart + at < plan.System.End)
                                plan.StorageAnywhere = true;
                        foreach (int at in Find(buffer, have, others))
                            if (bufferStart + at >= plan.System.Start && bufferStart + at < plan.System.End)
                                plan.HidesOtherDisks = true;
                        foreach (int at in Find(buffer, have, names))
                        {
                            int uuidAt = at + names.Length;
                            long where = bufferStart + uuidAt;
                            // one that the buffer cuts comes whole in the next buffer
                            if (uuidAt + UuidLength > have || bufferStart + at < plan.System.Start || where + UuidLength > plan.System.End ||
                                plan.StorageUuidOffsets.Contains(where))
                                continue;
                            string uuid = Encoding.ASCII.GetString(buffer, uuidAt, UuidLength);
                            if (!IsUuid(uuid)) continue;
                            if (plan.StorageUuid == null) plan.StorageUuid = uuid;
                            else if (plan.StorageUuid != uuid) plan.StorageUuid = "";
                            plan.StorageUuidOffsets.Add(where);
                        }
                    }
                    // the start of the storage partition, where its file system says which one it is
                    {
                        long from = Math.Max(done, plan.Storage.Start), to = Math.Min(done + read, plan.Storage.Start + super.Length);
                        if (from < to)
                            Buffer.BlockCopy(buffer, carried + (int)(from - done), super, (int)(from - plan.Storage.Start), (int)(to - from));
                    }
                    done += read;
                    // the last bytes of the image, whatever the chunks were: only what is new in this buffer
                    if (read >= TailSize) Buffer.BlockCopy(buffer, have - TailSize, plan.Tail, 0, TailSize);
                    else
                    {
                        Buffer.BlockCopy(plan.Tail, read, plan.Tail, 0, TailSize - read);
                        Buffer.BlockCopy(buffer, carried, plan.Tail, TailSize - read, read);
                    }
                    carried = Math.Min(keep, have);
                    Buffer.BlockCopy(buffer, have - carried, buffer, 0, carried);
                    if (progress != null) progress(Reading, done, plan.Size);
                }
                if (done != plan.Size) throw new InvalidDataException(Incomplete(done, plan.Size));
                plan.HideOffsets.Sort();
                plan.StorageUuidOffsets.Sort();
                plan.StorageUuidInFs = Ext4Uuid(super);
                plan.Scanned = true;
            }
            return plan;
        }

        static string Incomplete(long has, long declared)
        {
            return L.T("The image is incomplete: it has " + has + " of " + declared + " bytes.",
                       "La imagen está incompleta: tiene " + has + " bytes de " + declared + ".");
        }

        static int Fill(Stream source, byte[] buffer, int offset, int count)
        {
            int total = 0;
            while (total < count)
            {
                int read = source.Read(buffer, offset + total, count - total);
                if (read <= 0) break;
                total += read;
            }
            return total;
        }

        static List<int> Find(byte[] buffer, int length, byte[] what)
        {
            var found = new List<int>();
            for (int i = 0; i + what.Length <= length; ++i)
            {
                if (buffer[i] != what[0]) continue;
                int k = 1;
                while (k < what.Length && buffer[i + k] == what[k]) ++k;
                if (k == what.Length) found.Add(i);
            }
            return found;
        }

        // ---- an update: the system partition of a new image for a storage partition that is already there

        public const string DiskUuid = "disk=UUID=";
        public const int UuidLength = 36;

        public static bool IsUuid(string text)
        {
            if (text == null || text.Length != UuidLength) return false;
            for (int i = 0; i < text.Length; ++i)
            {
                char c = text[i];
                bool dash = i == 8 || i == 13 || i == 18 || i == 23;
                if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            }
            return true;
        }

        // The UUID of an ext4 file system, from the first 2048 bytes of its partition; null when those
        // are not the start of one.
        public static string Ext4Uuid(byte[] start)
        {
            if (start == null || start.Length < 2048 || start[1024 + 0x38] != 0x53 || start[1024 + 0x39] != 0xEF) return null;
            var sb = new StringBuilder(UuidLength);
            for (int i = 0; i < 16; ++i)
            {
                if (i == 4 || i == 6 || i == 8 || i == 10) sb.Append('-');
                sb.Append(start[1024 + 0x68 + i].ToString("x2"));
            }
            return sb.ToString();
        }

        // The boot menus of the image name the storage partition by the UUID of the image's own. A disk
        // that already has Hades has another one, made when it was installed: the menus are made to name
        // that one, so that what the user has there (settings, add-ons) stays.
        public static List<Patch> KeepStorage(ImagePlan plan, string existing)
        {
            if (!plan.Scanned) throw new InvalidOperationException("The image was not scanned.");
            if (!IsUuid(existing))
                throw new InvalidOperationException(L.T("The Hades storage on that disk is not recognized.",
                                                        "El almacenamiento de Hades de ese disco no se reconoce."));
            // two menus, two entries each that use the storage
            if (plan.StorageUuidOffsets.Count != 4 || !IsUuid(plan.StorageUuid) || plan.StorageUuid != plan.StorageUuidInFs)
                throw new InvalidDataException(L.T(
                    "The boot menus of this image are not the ones this installer knows (" + plan.StorageUuidOffsets.Count +
                    " entries that name the storage, 4 were expected). It cannot be used to update.",
                    "Los menús de arranque de esta imagen no son los que este instalador conoce (" + plan.StorageUuidOffsets.Count +
                    " entradas que nombran el almacenamiento, se esperaban 4). No se puede actualizar con ella."));
            var patches = new List<Patch>();
            foreach (long at in plan.StorageUuidOffsets) patches.Add(Make(at, Encoding.ASCII.GetBytes(existing)));
            return patches;
        }

        // ---- the switch of the diagnostics

        public enum Switch { None, Off, On }

        static void Skip(Stream source, long count)
        {
            var buffer = new byte[64 * 1024];
            while (count > 0)
            {
                int want = (int)Math.Min(buffer.Length, count);
                if (Fill(source, buffer, want) != want) throw new EndOfStreamException();
                count -= want;
            }
        }

        // How the name of a file starts when the diagnostics are on.
        public const string SwitchStart = "DIAG-ON";

        // The name that a directory entry has by itself: "DIAG-ON.TXT" for "DIAG-ON TXT".
        internal static string ShortName(byte[] root, int e)
        {
            string name = Encoding.ASCII.GetString(root, e, 8).TrimEnd(' '), ending = Encoding.ASCII.GetString(root, e + 8, 3).TrimEnd(' ');
            return ending.Length == 0 ? name : name + "." + ending;
        }

        // What the entries of a long name carry of the short name that they belong to.
        internal static byte ShortSum(byte[] root, int e)
        {
            int sum = 0;
            for (int i = 0; i < 11; ++i) sum = (((sum & 1) << 7) + (sum >> 1) + root[e + i]) & 0xFF;
            return (byte)sum;
        }

        // The (up to 13) letters that one entry of a long name holds.
        internal static string LongPart(byte[] root, int e)
        {
            var part = new StringBuilder();
            foreach (int at in new[] { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 })
            {
                char c = (char)BitConverter.ToUInt16(root, e + at);
                if (c == 0) break;         // the end of the name
                if (c != 0xFFFF) part.Append(c);
            }
            return part.ToString();
        }

        // The switch in a system partition of Hades. partition: a stream that is at the first byte of the
        // partition; it is only read forward and in whole sectors, so it can be an image that is unpacked
        // as it is read, or a disk. size: how long the partition is (or how much of it the stream has):
        // nothing is looked for beyond that. at: where the name of the file is, from the start of the
        // partition. None: there is no such file, or the partition is not a FAT with a root directory of
        // a fixed size (FAT12 and FAT16, which is what the images have).
        // Off is the file as an image has it, by its whole name and with no long name next to it: it is what
        // gets changed. On is any file whose name starts with DIAG-ON, as Hades itself takes it (somebody who
        // renames the file by hand may end up with another ending, or with two: DIAG-ON.TXT.TXT, when Windows
        // hides the ending). The name of a file is its long name when it has one, as Linux shows it: the
        // short name of DIAG-ON.TXT.TXT is DIAG-O~1.TXT.
        public static Switch ReadSwitch(Stream partition, long size, out long at)
        {
            at = -1;
            var boot = new byte[Sector];
            if (size < Sector || Fill(partition, boot, Sector) != Sector || boot[510] != 0x55 || boot[511] != 0xAA) return Switch.None;
            int bytesPerSector = BitConverter.ToUInt16(boot, 11), reserved = BitConverter.ToUInt16(boot, 14), fats = boot[16];
            int rootEntries = BitConverter.ToUInt16(boot, 17), fatSectors = BitConverter.ToUInt16(boot, 22);
            if (bytesPerSector != Sector || reserved < 1 || fats < 1 || fats > 2 || fatSectors < 1 ||
                rootEntries < 16 || rootEntries > 4096 || rootEntries % 16 != 0)
                return Switch.None;
            long rootAt = (long)(reserved + fats * fatSectors) * Sector;
            var root = new byte[rootEntries * 32];
            if (rootAt + root.Length > size) return Switch.None;
            try { Skip(partition, rootAt - Sector); }
            catch (EndOfStreamException) { return Switch.None; }
            if (Fill(partition, root, root.Length) != root.Length) return Switch.None;
            Switch found = Switch.None;
            // the long name that the entries before a file spell, last part first: what is gathered of it, the
            // number of the part that must come next (0: it is whole) and what it carries of its short name
            string longName = null;
            int next = 0;
            byte sum = 0;
            for (int e = 0; e < root.Length; e += 32)
            {
                if (root[e] == 0) break; // the end of the directory
                if (root[e] == 0xE5) { longName = null; continue; } // a deleted entry
                if ((root[e + 11] & 0x3F) == 0x0F)
                {
                    // a part of a long name
                    int order = root[e] & 0x3F;
                    if ((root[e] & 0x40) != 0) { longName = ""; next = order; sum = root[e + 13]; }
                    if (longName == null || order < 1 || order != next || root[e + 13] != sum) { longName = null; continue; }
                    longName = LongPart(root, e) + longName;
                    next = order - 1;
                    continue;
                }
                // (a long name that is not whole, or not of this entry, is nobody's: both systems leave it out)
                string spelled = longName != null && next == 0 && sum == ShortSum(root, e) ? longName : null;
                longName = null;
                if ((root[e + 11] & 0x18) != 0) continue; // the label of the volume or a folder
                bool on = (spelled ?? ShortName(root, e)).StartsWith(SwitchStart, StringComparison.OrdinalIgnoreCase);
                if (on || (found == Switch.None && spelled == null && Encoding.ASCII.GetString(root, e, 11) == SwitchOff))
                {
                    found = on ? Switch.On : Switch.Off;
                    at = rootAt + e;
                    if (on) break;
                }
            }
            return found;
        }

        // Where an image has the switch, off, as an offset in the image; -1 when it does not have it so
        // (also an image that cannot be read that far: what is wrong with it is said where it is read whole).
        // on: the image has its diagnostics on already.
        static long FindSwitch(string path, ImagePart system, out bool on)
        {
            on = false;
            try
            {
                using (Stream source = Open(path))
                {
                    Skip(source, system.Start);
                    long at;
                    Switch state = ReadSwitch(source, system.Size, out at);
                    on = state == Switch.On;
                    return state == Switch.Off ? system.Start + at : -1;
                }
            }
            catch (IOException) { return -1; }
            catch (InvalidDataException) { return -1; }
        }

        // The first "count" bytes of the system partition of the image, as the image has them (DiagFolder: the
        // start of its file system, up to its first cluster).
        public static byte[] SystemStart(ImagePlan plan, int count)
        {
            if (count < 0 || count > plan.System.Size) throw new InvalidOperationException("Past the system partition of the image.");
            using (Stream source = Open(plan.Path))
            {
                Skip(source, plan.System.Start);
                var bytes = new byte[count];
                if (Fill(source, bytes, count) != count) throw new InvalidDataException(L.T("The image is incomplete.", "La imagen está incompleta."));
                return bytes;
            }
        }

        // The diagnostics turned on in what is written: the file DIAG-OFF.TXT of the image is DIAG-ON.TXT
        // on the disk.
        public static Patch DiagnosticsOn(ImagePlan plan)
        {
            if (plan.SwitchOffset < 0)
                throw new InvalidDataException(plan.SwitchOn
                    ? L.T("This Hades image has its diagnostics on already.", "Esta imagen de Hades ya tiene el diagnóstico encendido.")
                    : L.T("This Hades image has no diagnostics to turn on. A more recent one is needed.",
                          "Esta imagen de Hades no tiene diagnóstico que encender. Hace falta una más reciente."));
            return Make(plan.SwitchOffset, Encoding.ASCII.GetBytes(SwitchOn));
        }

        // Every change that is made to an image as it is written, and nothing else decides them.
        // inside: for a disk inside the PC. table: the whole image is written, with its partition table.
        // storage: an update: the identifier of the storage that the disk already has (null: an install).
        // diagnostics: with the switch of the diagnostics turned on.
        public static List<Patch> Patches(ImagePlan plan, bool inside, bool table, string storage, bool diagnostics)
        {
            var patches = new List<Patch>();
            if (storage != null) patches.AddRange(KeepStorage(plan, storage));
            if (inside) patches.AddRange(ForInternalDisk(plan));
            if (inside && table) patches.AddRange(SystemAsEfi(plan));
            if (diagnostics) patches.Add(DiagnosticsOn(plan));
            return patches;
        }

        // ---- what an install on an internal disk changes in the image

        // The boot menus for a disk inside the PC. The parameter with which a USB drive hides the internal
        // disks would hide the disk of Hades itself. In its place goes the one with which the image takes
        // the other internal disks away by itself, when the image says that it can (FeatureOtherDisks); an
        // older image gets nothing there, and sees them all, read-only. Spaces up to the length of what was
        // there, so that no file changes its size.
        public static List<Patch> ForInternalDisk(ImagePlan plan)
        {
            if (!plan.Scanned) throw new InvalidOperationException("The image was not scanned.");
            // two menus (GRUB and syslinux), two entries each
            if (plan.HideOffsets.Count != 4)
                throw new InvalidDataException(L.T(
                    "The boot menus of this image are not the ones this installer knows (" + plan.HideOffsets.Count +
                    " entries that hide the internal disks, 4 were expected). It cannot be used to install on an internal disk.",
                    "Los menús de arranque de esta imagen no son los que este instalador conoce (" + plan.HideOffsets.Count +
                    " entradas que ocultan los discos internos, se esperaban 4). No se puede instalar en un disco interno con ella."));
            var patches = new List<Patch>();
            string put = (plan.HidesOtherDisks ? HideOthers : "").PadRight(HideInternal.Length);
            foreach (long at in plan.HideOffsets) patches.Add(Make(at, Encoding.ASCII.GetBytes(put)));
            return patches;
        }

        // The system partition marked as an EFI system partition, in both copies of the partition table:
        // the firmware of a PC looks for its boot loader there on a fixed disk.
        public static List<Patch> SystemAsEfi(ImagePlan plan)
        {
            if (!plan.Scanned) throw new InvalidOperationException("The image was not scanned.");
            Gpt primary = ReadGpt(plan.Head, Sector);
            long tailStart = plan.Size - TailSize;
            long backupAt = primary.AlternateLba * Sector - tailStart;
            if (backupAt < 0 || backupAt + Sector > TailSize)
                throw new InvalidDataException(L.T("The backup copy of the partition table is not at the end of the image.",
                                                   "La copia de la tabla de particiones no está al final de la imagen."));
            Gpt backup = ReadGpt(plan.Tail, (int)backupAt);
            var entries = new byte[primary.Count * 128];
            Buffer.BlockCopy(plan.Head, (int)(primary.EntriesLba * Sector), entries, 0, entries.Length);
            Buffer.BlockCopy(EfiSystem.ToByteArray(), 0, entries, 0, 16);
            SetHeaderCrcs(primary.Header, entries);
            SetHeaderCrcs(backup.Header, entries);
            long backupEntries = backup.EntriesLba * Sector;
            if (backupEntries < tailStart || backupEntries + entries.Length > plan.Size)
                throw new InvalidDataException(L.T("The backup copy of the partition table is not where it is expected.",
                                                   "La copia de la tabla de particiones no está donde se espera."));
            var patches = new List<Patch>();
            patches.Add(Make(primary.EntriesLba * Sector, entries));
            patches.Add(Make(Sector, primary.Header));
            patches.Add(Make(backupEntries, entries));
            patches.Add(Make(primary.AlternateLba * Sector, backup.Header));
            return patches;
        }

        static Patch Make(long offset, byte[] bytes)
        {
            var p = new Patch();
            p.Offset = offset;
            p.Bytes = bytes;
            return p;
        }

        static void Apply(List<Patch> patches, byte[] buffer, long bufferOffset, int count)
        {
            if (patches == null) return;
            foreach (Patch p in patches)
            {
                long from = Math.Max(p.Offset, bufferOffset), to = Math.Min(p.Offset + p.Bytes.Length, bufferOffset + count);
                if (from < to) Buffer.BlockCopy(p.Bytes, (int)(from - p.Offset), buffer, (int)(from - bufferOffset), (int)(to - from));
            }
        }

        // ---- writing

        // One place the image, or a part of it, goes to.
        public sealed class Target
        {
            public Stream Stream;
            public long Capacity;
            public long From, Length; // the part of the image that goes there, to the start of the stream
            public WriteResult Result = new WriteResult();
            internal byte[] First;
            internal SHA256 Sha = SHA256.Create();
        }

        // The whole image to a disk.
        public static WriteResult WriteWhole(ImagePlan plan, Stream target, long capacity, List<Patch> patches,
                                             Progress progress, Func<bool> cancelled)
        {
            var t = new Target();
            t.Stream = target;
            t.Capacity = capacity;
            t.From = 0;
            t.Length = plan.Size;
            Write(plan, new[] { t }, patches, progress, cancelled);
            return t.Result;
        }

        // Parts of the image, each to its target, in one pass over the image; then each is read back.
        public static void Write(ImagePlan plan, Target[] targets, List<Patch> patches, Progress progress, Func<bool> cancelled)
        {
            long total = 0;
            foreach (Target t in targets)
            {
                if (t.Length < Head || t.Length % Sector != 0 || t.From % Sector != 0 || t.From < 0 || t.From + t.Length > plan.Size)
                    throw new InvalidOperationException("The part of the image to write is not valid.");
                if (t.Length > t.Capacity)
                    throw new InvalidOperationException(L.T("The image does not fit on the destination.", "La imagen no cabe en el destino."));
                t.First = new byte[Head];
                total += t.Length;
            }
            long written = 0;
            using (Stream source = Open(plan.Path))
            {
                var buffer = new byte[Chunk];
                long offset = 0;
                int read;
                while ((read = Fill(source, buffer, Chunk)) > 0)
                {
                    if (cancelled != null && cancelled()) throw new OperationCanceledException();
                    if (read % Sector != 0 || offset + read > plan.Size)
                        throw new InvalidDataException(L.T("The image is not the size that it says it is.",
                                                           "La imagen no tiene el tamaño que declara."));
                    Apply(patches, buffer, offset, read);
                    foreach (Target t in targets)
                    {
                        long from = Math.Max(offset, t.From), to = Math.Min(offset + read, t.From + t.Length);
                        if (from >= to) continue;
                        int at = (int)(from - offset), count = (int)(to - from);
                        long where = from - t.From;
                        t.Sha.TransformBlock(buffer, at, count, null, 0);
                        // the first megabyte of each target is kept for the end
                        if (where < Head)
                        {
                            int keep = (int)Math.Min(count, Head - where);
                            Buffer.BlockCopy(buffer, at, t.First, (int)where, keep);
                            at += keep;
                            count -= keep;
                            where += keep;
                        }
                        if (count > 0)
                        {
                            if (count % Sector != 0 || where % Sector != 0) throw new InvalidOperationException("Unaligned write.");
                            t.Stream.Seek(where, SeekOrigin.Begin);
                            t.Stream.Write(buffer, at, count);
                        }
                        written += to - from;
                    }
                    offset += read;
                    if (progress != null) progress(Writing, written, total);
                }
                if (offset != plan.Size) throw new InvalidDataException(Incomplete(offset, plan.Size));
            }
            foreach (Target t in targets)
            {
                t.Sha.TransformFinalBlock(new byte[0], 0, 0);
                t.Stream.Seek(0, SeekOrigin.Begin);
                t.Stream.Write(t.First, 0, Head);
                t.Stream.Flush();
                t.Result.Bytes = t.Length;
                t.Result.Written = Words.Hex(t.Sha.Hash);
            }
            long checkedBytes = 0;
            foreach (Target t in targets)
            {
                long before = checkedBytes;
                t.Result.ReadBack = ReadHash(t.Stream, t.Length, delegate(string stage, long done, long all)
                {
                    if (progress != null) progress(Verifying, before + done, total);
                }, cancelled);
                checkedBytes += t.Length;
            }
        }

        public static string ReadHash(Stream target, long length, Progress progress, Func<bool> cancelled)
        {
            using (var sha = SHA256.Create())
            {
                var buffer = new byte[Chunk];
                long left = length;
                target.Seek(0, SeekOrigin.Begin);
                while (left > 0)
                {
                    if (cancelled != null && cancelled()) throw new OperationCanceledException();
                    int want = (int)Math.Min(buffer.Length, left);
                    int read = Fill(target, buffer, want);
                    if (read != want)
                        throw new IOException(L.T("Not everything that was written could be read back.", "No se pudo releer todo lo escrito."));
                    sha.TransformBlock(buffer, 0, read, null, 0);
                    left -= read;
                    if (progress != null) progress(Verifying, length - left, length);
                }
                sha.TransformFinalBlock(buffer, 0, 0);
                return Words.Hex(sha.Hash);
            }
        }
    }
}
