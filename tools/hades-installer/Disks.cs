// LibreELEC Hades installer for Windows: the disks of the PC through the storage management of Windows (what
// Get-Disk, Resize-Partition and New-Partition use), and raw access to a disk or to one partition.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace Hades.Installer
{
    enum Place { Usb, Internal }

    static class Disks
    {
        const string Scope = @"\\.\root\Microsoft\Windows\Storage";
        public const string EfiType = "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}";
        public const string LinuxType = "{0fc63daf-8483-4772-8e79-3d69d8477de4}";
        public const string BasicType = "{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}";
        // The reserved partition that Windows makes on a GPT disk: it holds no data
        public const string ReservedType = "{e3c9e316-0b5c-4db8-817d-f92df00215ae}";
        // The volume label of the system partition of a Hades image (DISTRO_BOOTLABEL; its serial number
        // changes with every build)
        const string HadesLabel = "LIBREELEC";

        // Tests only: a virtual disk (a .vhd attached to Windows) counts as an internal one.
        public static bool AllowVirtual;

        public static List<DiskInfo> List()
        {
            var parts = new Dictionary<int, List<PartitionInfo>>();
            using (var search = new ManagementObjectSearcher(Scope, "SELECT * FROM MSFT_Partition"))
            {
                foreach (ManagementObject p in search.Get())
                {
                    var part = new PartitionInfo();
                    int disk = Convert.ToInt32(p["DiskNumber"]);
                    part.Number = Convert.ToInt32(p["PartitionNumber"]);
                    part.Offset = Convert.ToInt64(p["Offset"] ?? 0);
                    part.Size = Convert.ToInt64(p["Size"] ?? 0);
                    part.GptType = Clean(p["GptType"]).ToLowerInvariant();
                    char letter = p["DriveLetter"] == null ? '\0' : Convert.ToChar(p["DriveLetter"]);
                    part.Letter = letter >= 'A' && letter <= 'Z' ? letter : '\0';
                    if (!parts.ContainsKey(disk)) parts[disk] = new List<PartitionInfo>();
                    parts[disk].Add(part);
                }
            }
            var result = new List<DiskInfo>();
            using (var search = new ManagementObjectSearcher(Scope, "SELECT * FROM MSFT_Disk"))
            {
                foreach (ManagementObject d in search.Get())
                {
                    var disk = new DiskInfo();
                    disk.Number = Convert.ToInt32(d["Number"]);
                    disk.Name = Clean(d["FriendlyName"]);
                    if (disk.Name.Length == 0) disk.Name = Clean(d["Model"]); // (with neither, DiskInfo.ShownName)
                    disk.Serial = Clean(d["SerialNumber"]);
                    disk.UniqueId = Clean(d["UniqueId"]);
                    disk.Location = Clean(d["Location"]);
                    disk.Size = Convert.ToInt64(d["Size"] ?? 0);
                    disk.BusType = Convert.ToInt32(d["BusType"] ?? 0);
                    disk.IsBoot = Convert.ToBoolean(d["IsBoot"] ?? false);
                    disk.IsSystem = Convert.ToBoolean(d["IsSystem"] ?? false);
                    disk.IsOffline = Convert.ToBoolean(d["IsOffline"] ?? false);
                    disk.IsReadOnly = Convert.ToBoolean(d["IsReadOnly"] ?? false);
                    disk.PartitionStyle = Convert.ToInt32(d["PartitionStyle"] ?? 0);
                    List<PartitionInfo> own;
                    if (parts.TryGetValue(disk.Number, out own))
                    {
                        own.Sort(delegate(PartitionInfo a, PartitionInfo b) { return a.Offset.CompareTo(b.Offset); });
                        disk.Partitions = own;
                        foreach (PartitionInfo p in own)
                            if (p.Letter != '\0') disk.Volumes.Add(Volume(p.Letter));
                    }
                    result.Add(disk);
                }
            }
            result.Sort(delegate(DiskInfo a, DiskInfo b) { return a.Number.CompareTo(b.Number); });
            return result;
        }

        static string Clean(object value)
        {
            return value == null ? "" : Convert.ToString(value).Trim();
        }

        public static VolumeInfo Volume(char letter)
        {
            var v = new VolumeInfo();
            v.Letter = letter;
            try
            {
                var info = new DriveInfo(letter.ToString());
                if (info.IsReady)
                {
                    v.Ready = true;
                    v.Label = info.VolumeLabel ?? "";
                    v.FileSystem = info.DriveFormat ?? "";
                    v.Size = info.TotalSize;
                    v.Used = info.TotalSize - info.TotalFreeSpace;
                }
            }
            catch (Exception) { }
            return v;
        }

        // What must never be erased: Windows, the files of the user, this program, the image, temporary
        // files. Each by the drive letter of its path and by where on a disk its volume really is
        // (Native.Extents), which is what counts when the path does not say it. A disk that has one of them
        // is never erased whole: a USB drive, which can only be used whole, is not offered; an internal
        // disk is offered to make room on it, which erases nothing, to update the Hades it has and to take
        // it off again. The disk of Windows itself is not used in any way.
        public static Keep Keeping(string imagePath)
        {
            var keep = new Keep();
            // (the first of them is Windows itself)
            string[] paths = { Environment.SystemDirectory, Application.ExecutablePath, imagePath, Path.GetTempPath(),
                               Environment.GetFolderPath(Environment.SpecialFolder.UserProfile) };
            for (int i = 0; i < paths.Length; ++i)
            {
                string p = paths[i];
                if (string.IsNullOrEmpty(p)) continue;
                if (p.Length >= 2 && p[1] == ':')
                {
                    char c = char.ToUpperInvariant(p[0]);
                    if (!keep.Letters.Contains(c)) keep.Letters.Add(c);
                    if (i == 0) keep.Windows = c;
                }
                foreach (Extent e in Native.Extents(p))
                {
                    keep.Extents.Add(e);
                    if (i == 0 && !keep.WindowsDisks.Contains(e.Disk)) keep.WindowsDisks.Add(e.Disk);
                }
            }
            return keep;
        }

        // Serial numbers that the owner of this PC never wants offered: one per line in
        // %ProgramData%\Hades\discos-protegidos.txt or in a file of that name next to the program. The same
        // files under their English name, protected-disks.txt, are read too: what any of them lists is kept.
        public static List<string> Protected()
        {
            var list = new List<string>();
            string common = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Hades");
            string here = Path.GetDirectoryName(Application.ExecutablePath);
            string[] files = {
                Path.Combine(common, "discos-protegidos.txt"), Path.Combine(common, "protected-disks.txt"),
                Path.Combine(here, "discos-protegidos.txt"), Path.Combine(here, "protected-disks.txt") };
            foreach (string f in files)
            {
                try
                {
                    if (!File.Exists(f)) continue;
                    foreach (string line in File.ReadAllLines(f))
                    {
                        string t = line.Trim();
                        if (t.Length > 0 && !t.StartsWith("#")) list.Add(t.ToUpperInvariant());
                    }
                }
                catch (Exception) { }
            }
            return list;
        }

        // Whether something that must be kept (see Keeping) is on a partition of a disk.
        public static bool KeptOn(DiskInfo d, PartitionInfo p, Keep keep)
        {
            if (p.Letter != '\0' && keep.Letters.Contains(p.Letter)) return true;
            foreach (Extent e in keep.Extents)
                if (e.Disk == d.Number && e.Offset < p.End && p.Offset < e.Offset + e.Length) return true;
            return false;
        }

        // Where on the disk something is that must not be erased: the letter of its volume ("D:"), "" when
        // that volume has no letter, or null when nothing of the kind is on the disk.
        public static string Kept(DiskInfo d, Keep keep)
        {
            string found = null;
            foreach (PartitionInfo p in d.Partitions)
            {
                if (!KeptOn(d, p, keep)) continue;
                if (p.Letter != '\0') return p.Letter + ":";
                found = "";
            }
            foreach (VolumeInfo v in d.Volumes)
                if (keep.Letters.Contains(v.Letter)) return v.Letter + ":";
            // (a volume that Windows does not list as a partition of the disk)
            foreach (Extent e in keep.Extents)
                if (e.Disk == d.Number) found = "";
            return found;
        }

        // (goes after "because")
        public static string KeptReason(string where)
        {
            return L.T("it holds Windows, your user files, this program or the Hades image",
                       "en él están Windows, tus archivos de usuario, este programa o la imagen de Hades") +
                   (where.Length > 0 ? " (" + where + ")"
                                     : L.T(", on a volume without a drive letter", ", en un volumen sin letra de unidad"));
        }

        // Whether Windows itself is on the disk: by the letter of its folder, and by where that folder is.
        static bool HoldsWindows(DiskInfo d, Keep keep)
        {
            if (keep.WindowsDisks.Contains(d.Number)) return true;
            if (keep.Windows == '\0') return false;
            foreach (PartitionInfo p in d.Partitions) if (p.Letter == keep.Windows) return true;
            foreach (VolumeInfo v in d.Volumes) if (v.Letter == keep.Windows) return true;
            return false;
        }

        // Why a disk is not offered as a destination, or null. The reason goes after "because". An internal
        // disk that is offered may still be one that cannot be used whole: see Refusal().
        public static string Classify(DiskInfo d, Place place, long imageSize, Keep keep, ICollection<string> guarded)
        {
            if (d.IsBoot || d.IsSystem) return L.T("it is the disk that Windows starts from", "es el disco desde el que arranca Windows");
            // (... should Windows not say so of the disk that it is on)
            if (HoldsWindows(d, keep)) return L.T("Windows is on it", "en él está Windows");
            // a USB drive can only be used whole
            string kept = Kept(d, keep);
            if (place == Place.Usb && kept != null) return KeptReason(kept);
            if (guarded != null && d.Serial.Length > 0 && guarded.Contains(d.Serial.ToUpperInvariant()))
                return L.T("it is on the list of protected disks of this PC", "está en la lista de discos protegidos de este PC");
            if (place == Place.Usb)
            {
                if (!d.Removable) return L.T("it is not connected by USB (", "no está conectado por USB (") + d.BusName + ")";
            }
            else if (!(d.Fixed || (AllowVirtual && d.Virtual)))
                return d.Removable ? L.T("it is connected by USB: choose “On a USB drive”", "está conectado por USB: elige «Unidad USB»")
                                   : L.T("it is not an internal disk of a kind that this program knows (",
                                         "no es un disco interno que este programa conozca (") + d.BusName + ")";
            if (d.IsReadOnly) return L.T("it is write-protected", "está protegido contra escritura");
            if (d.IsOffline) return L.T("it is offline in Windows", "está sin conexión en Windows");
            if (imageSize > 0 && d.Size < imageSize)
                return L.T("it is smaller than the image (", "es más pequeño que la imagen (") + Words.Size(imageSize) + ")";
            return null;
        }

        // Why a disk may not be changed in the way that is meant, or null. What erases the whole disk is
        // also refused for a disk that holds something that must be kept; what erases nothing of what the
        // disk had (room next to its data, an update of its Hades, taking Hades off it) is not.
        public static string Refusal(DiskInfo d, Place place, long imageSize, Keep keep, ICollection<string> guarded, bool erases)
        {
            string why = Classify(d, place, imageSize, keep, guarded);
            if (why == null && erases)
            {
                string kept = Kept(d, keep);
                if (kept != null) why = KeptReason(kept);
            }
            return why;
        }

        // What to say when no disk is offered. A PC whose other internal disks are all left out (protected, or
        // holding Windows' files, this program or the image) has disks: saying that it has none would be false.
        public static string NothingToOffer(IEnumerable<DiskInfo> excluded, Place place)
        {
            int there = 0;
            foreach (DiskInfo d in excluded)
                if (place == Place.Usb ? d.Removable : (d.Fixed && !d.IsBoot && !d.IsSystem)) ++there;
            if (place == Place.Usb)
            {
                if (there == 0)
                    return L.T("Plug in a USB stick or a USB disk. The list updates by itself.",
                               "Conecta una memoria o un disco USB. La lista se actualiza sola.");
                return there == 1
                    ? L.T("The USB drive that is plugged in cannot be used here: “Don't see your drive?” says why.",
                          "La unidad USB conectada no se puede usar aquí: «¿No aparece tu unidad?» dice por qué.")
                    : L.T("None of the " + there + " USB drives plugged in can be used here: “Don't see your drive?” says why.",
                          "Ninguna de las " + there + " unidades USB conectadas se puede usar aquí: «¿No aparece tu unidad?» dice por qué.");
            }
            if (there == 0)
                return L.T("This PC has no internal disk other than the Windows one.",
                           "Este PC no tiene ningún disco interno que no sea el de Windows.");
            return there == 1
                ? L.T("This PC has one more internal disk, but it is not offered: “Don't see your disk?” says why.",
                      "Este PC tiene otro disco interno, pero no se ofrece: «¿No aparece tu disco?» dice por qué.")
                : L.T("This PC has " + there + " more internal disks, but none of them is offered: “Don't see your disk?” says why.",
                      "Este PC tiene otros " + there + " discos internos, pero ninguno se ofrece: «¿No aparece tu disco?» dice por qué.");
        }

        // ---- changes (administrator)

        static ManagementObject FindDisk(int number)
        {
            using (var search = new ManagementObjectSearcher(Scope, "SELECT * FROM MSFT_Disk WHERE Number = " + number))
                foreach (ManagementObject d in search.Get()) return d;
            throw new InvalidOperationException(L.T("The disk is no longer connected.", "El disco ya no está conectado."));
        }

        static ManagementObject FindPartition(int disk, int number)
        {
            using (var search = new ManagementObjectSearcher(Scope, "SELECT * FROM MSFT_Partition WHERE DiskNumber = " + disk +
                                                                    " AND PartitionNumber = " + number))
                foreach (ManagementObject p in search.Get()) return p;
            throw new InvalidOperationException(L.T("The partition no longer exists.", "La partición ya no existe."));
        }

        // what: what Windows was asked to do, to go after "Windows could not"
        static void Check(ManagementBaseObject result, string what)
        {
            uint code = Convert.ToUInt32(result["ReturnValue"]);
            if (code == 0) return;
            string detail = "";
            try
            {
                var status = result["ExtendedStatus"] as ManagementBaseObject;
                if (status != null && status["Message"] != null) detail = " " + status["Message"];
            }
            catch (Exception) { }
            throw new InvalidOperationException(L.T("Windows could not ", "Windows no pudo ") + what +
                                                L.T(" (code ", " (código ") + code + ")." + detail);
        }

        // Removes every partition (what Clear-Disk -RemoveData -RemoveOEM does).
        public static void Clear(int number)
        {
            using (ManagementObject d = FindDisk(number))
            {
                if (Convert.ToInt32(d["PartitionStyle"] ?? 0) == 0) return; // nothing on it
                ManagementBaseObject p = d.GetMethodParameters("Clear");
                p["RemoveData"] = true;
                p["RemoveOEM"] = true;
                p["ZeroOutEntireDisk"] = false;
                Check(d.InvokeMethod("Clear", p, null), L.T("delete the partitions of the disk", "borrar las particiones del disco"));
            }
        }

        public static void Refresh(int number)
        {
            try { using (ManagementObject d = FindDisk(number)) d.InvokeMethod("Refresh", null, null); }
            catch (Exception e) { Log.Line("refresh: " + e.Message); }
        }

        public static void InitializeGpt(int number)
        {
            using (ManagementObject d = FindDisk(number))
            {
                ManagementBaseObject p = d.GetMethodParameters("Initialize");
                p["PartitionStyle"] = (ushort)2;
                Check(d.InvokeMethod("Initialize", p, null), L.T("prepare the disk", "preparar el disco"));
            }
        }

        // The least and the most that a partition can be (Windows asks its file system).
        public static void SupportedSize(int disk, int partition, out long least, out long most)
        {
            using (ManagementObject p = FindPartition(disk, partition))
            {
                ManagementBaseObject r = p.InvokeMethod("GetSupportedSize", null, null);
                Check(r, L.T("work out how much the partition can be shrunk", "calcular cuánto se puede encoger la partición"));
                least = Convert.ToInt64(r["SizeMin"]);
                most = Convert.ToInt64(r["SizeMax"]);
            }
        }

        public static void Resize(int disk, int partition, long size)
        {
            using (ManagementObject p = FindPartition(disk, partition))
            {
                ManagementBaseObject a = p.GetMethodParameters("Resize");
                a["Size"] = (ulong)size;
                Check(p.InvokeMethod("Resize", a, null), L.T("resize the partition", "cambiar el tamaño de la partición"));
            }
        }

        // A new partition of the given type at the given place. Returns it as Windows made it.
        public static PartitionInfo Create(int disk, long offset, long size, string gptType)
        {
            using (ManagementObject d = FindDisk(disk))
            {
                ManagementBaseObject a = d.GetMethodParameters("CreatePartition");
                a["Size"] = (ulong)size;
                a["Offset"] = (ulong)offset;
                a["AssignDriveLetter"] = false;
                a["GptType"] = gptType;
                Check(d.InvokeMethod("CreatePartition", a, null), L.T("create a partition", "crear una partición"));
            }
            foreach (DiskInfo d in List())
            {
                if (d.Number != disk) continue;
                foreach (PartitionInfo p in d.Partitions)
                    if (p.Offset == offset) return p;
            }
            throw new InvalidOperationException(L.T("The new partition is not where it was asked for.",
                                                    "La partición creada no aparece donde se pidió."));
        }

        // A partition is deleted by its number, and only when the one of that number starts where the one
        // that is meant does: a number can come to be that of another partition.
        public static void Delete(int disk, int partition, long offset)
        {
            using (ManagementObject p = FindPartition(disk, partition))
            {
                long at = Convert.ToInt64(p["Offset"] ?? -1);
                if (at != offset)
                    throw new InvalidOperationException(L.T(
                        "The partition to delete is not where it was (it starts at " + at + ", not at " + offset + "). It was not deleted.",
                        "La partición que había que borrar no está donde estaba (empieza en " + at + ", no en " + offset +
                        "). No se ha borrado."));
                Check(p.InvokeMethod("DeleteObject", null, null), L.T("delete the partition", "borrar la partición"));
            }
        }

        // One NTFS volume over the whole disk, with a drive letter: a disk that Windows can use again.
        public static char MakeDataVolume(int disk, string label)
        {
            using (ManagementObject d = FindDisk(disk))
            {
                ManagementBaseObject a = d.GetMethodParameters("CreatePartition");
                a["UseMaximumSize"] = true;
                a["AssignDriveLetter"] = true;
                a["GptType"] = BasicType;
                Check(d.InvokeMethod("CreatePartition", a, null), L.T("create the data partition", "crear la partición de datos"));
            }
            System.Threading.Thread.Sleep(1500);
            foreach (DiskInfo d in List())
            {
                if (d.Number != disk) continue;
                foreach (PartitionInfo p in d.Partitions)
                {
                    if (p.Letter == '\0' || p.GptType != BasicType) continue;
                    using (ManagementObject part = FindPartition(disk, p.Number))
                    {
                        foreach (ManagementObject volume in part.GetRelated("MSFT_Volume"))
                        {
                            ManagementBaseObject f = volume.GetMethodParameters("Format");
                            f["FileSystem"] = "NTFS";
                            f["FileSystemLabel"] = label;
                            f["Full"] = false;
                            f["Force"] = true;
                            Check(volume.InvokeMethod("Format", f, null), L.T("format the data partition", "formatear la partición de datos"));
                            return p.Letter;
                        }
                    }
                }
            }
            throw new InvalidOperationException(L.T("The data partition that was just created was not found.",
                                                    "No se encontró la partición de datos recién creada."));
        }

        // 0: not encrypted, 1: encrypted or being encrypted, -1: not known
        public static int Encrypted(char letter)
        {
            try
            {
                using (var search = new ManagementObjectSearcher(@"\\.\root\CIMV2\Security\MicrosoftVolumeEncryption",
                                                                 "SELECT * FROM Win32_EncryptableVolume WHERE DriveLetter = '" + letter + ":'"))
                    foreach (ManagementObject v in search.Get())
                        return Convert.ToInt32(v["ConversionStatus"] ?? 0) == 0 ? 0 : 1;
            }
            catch (Exception) { }
            return -1;
        }

        // ---- Hades on a disk

        // Whether a partition starts with the FAT boot sector of the system partition of a Hades image.
        public static bool IsHadesSystem(int disk, PartitionInfo p)
        {
            if (p.Size < 512L * 1024 * 1024) return false;
            try
            {
                var sector = new byte[512];
                using (FileStream f = Native.OpenDisk(disk, false))
                {
                    f.Seek(p.Offset, SeekOrigin.Begin);
                    if (f.Read(sector, 0, 512) != 512) return false;
                }
                if (sector[510] != 0x55 || sector[511] != 0xAA) return false;
                // the volume label is at 71 in a FAT32 boot sector and at 43 in a FAT16 one
                string label32 = System.Text.Encoding.ASCII.GetString(sector, 71, 11).TrimEnd();
                string label16 = System.Text.Encoding.ASCII.GetString(sector, 43, 11).TrimEnd();
                return label32 == HadesLabel || label16 == HadesLabel;
            }
            catch (Exception) { return false; }
        }

        // The UUID of the file system on the storage partition of Hades (ext4), or null.
        public static string StorageUuid(int disk, PartitionInfo storage)
        {
            try
            {
                var start = new byte[2048];
                using (FileStream f = Native.OpenDisk(disk, false))
                {
                    f.Seek(storage.Offset, SeekOrigin.Begin);
                    if (f.Read(start, 0, start.Length) != start.Length) return null;
                }
                return Engine.Ext4Uuid(start);
            }
            catch (Exception) { return null; }
        }

        // Whether the Hades of a disk has its diagnostics turned on: the switch on its system partition
        // (Engine.ReadSwitch). False also when it cannot be read.
        public static bool DiagnosticsOn(int disk, PartitionInfo system)
        {
            try
            {
                // the first megabyte of the partition, in one read: the root directory of the FAT that an
                // image has ends well before that
                var start = new byte[1024 * 1024];
                using (FileStream f = Native.OpenDisk(disk, false))
                {
                    f.Seek(system.Offset, SeekOrigin.Begin);
                    if (f.Read(start, 0, start.Length) != start.Length) return false;
                }
                long at;
                return Engine.ReadSwitch(new MemoryStream(start), Math.Min(system.Size, start.Length), out at) == Engine.Switch.On;
            }
            catch (Exception) { return false; }
        }

        // The system partition of Hades on a disk and the storage partition right after it, or false.
        public static bool FindHades(DiskInfo d, out PartitionInfo system, out PartitionInfo storage)
        {
            system = storage = null;
            for (int i = 0; i + 1 < d.Partitions.Count; ++i)
            {
                PartitionInfo a = d.Partitions[i], b = d.Partitions[i + 1];
                if (b.GptType != LinuxType || b.Offset < a.End || b.Offset - a.End > 16L * 1024 * 1024) continue;
                if (!IsHadesSystem(d.Number, a)) continue;
                system = a;
                storage = b;
                return true;
            }
            return false;
        }

        // The partition of Hades, of its two, that something that must be kept is on, or null. It happens
        // with a disk that was written as a USB drive is and then put inside the PC: Windows gives its
        // system partition a letter, and the image or this program may have been copied to it. Such a
        // partition is neither written over nor deleted.
        public static PartitionInfo KeptOnHades(DiskInfo d, PartitionInfo system, PartitionInfo storage, Keep keep)
        {
            if (KeptOn(d, system, keep)) return system;
            if (KeptOn(d, storage, keep)) return storage;
            return null;
        }

        // Whether a disk has nothing but Hades: its two partitions and, at most, the reserved partition
        // that Windows puts on a disk, which holds no data. Only such a disk is erased when Hades is taken
        // off it: with any other partition on it, whatever its size and with or without a letter, only the
        // two of Hades go.
        public static bool OnlyHades(DiskInfo d, PartitionInfo system, PartitionInfo storage, Keep keep)
        {
            if (Kept(d, keep) != null) return false;
            foreach (PartitionInfo p in d.Partitions)
                if (p != system && p != storage && p.GptType != ReservedType) return false;
            return true;
        }

        // Tests only: a drive letter for a partition that has none.
        public static void AddLetter(int disk, int partition, char letter)
        {
            using (ManagementObject p = FindPartition(disk, partition))
            {
                ManagementBaseObject a = p.GetMethodParameters("AddAccessPath");
                a["AccessPath"] = letter + ":";
                Check(p.InvokeMethod("AddAccessPath", a, null), "give the partition a drive letter");
            }
        }
    }

    static class Native
    {
        const uint GenericRead = 0x80000000, GenericWrite = 0x40000000;
        const uint ShareRead = 1, ShareWrite = 2, ShareDelete = 4, OpenExisting = 3, WriteThrough = 0x80000000;
        const uint BackupSemantics = 0x02000000; // (what opening a folder asks for)
        const uint FsctlLockVolume = 0x00090018, FsctlDismountVolume = 0x00090020, IoctlDiskGetPartitionInfoEx = 0x00070048;
        const uint FsctlAllowExtendedDasdIo = 0x00090083;
        const uint IoctlVolumeGetVolumeDiskExtents = 0x00560000;
        const int ErrorMoreData = 234, ErrorAccessDenied = 5;

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint disposition,
                                               uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool DeviceIoControl(SafeFileHandle device, uint code, IntPtr input, uint inputSize, IntPtr output,
                                           uint outputSize, out uint returned, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern uint GetFinalPathNameByHandle(SafeFileHandle file, StringBuilder path, uint size, uint flags);

        [DllImport("kernel32.dll")]
        static extern bool SetThreadErrorMode(uint mode, out uint old);

        [DllImport("kernel32.dll")]
        static extern bool GetFirmwareType(out uint type);

        // Where the file or the folder of a path really is: the disk, and the place on it, of the volume
        // that holds it (more than one stretch when the volume spans disks). Windows is asked for the final
        // name of what the path opens, which names its volume whatever the path looks like: a folder that is
        // a mount point or a junction, a SUBST letter, a volume without a letter. Nothing is read from the
        // file, and no rights are needed. Empty when it cannot be known (a network path, a path that does
        // not exist): the drive letter of the path is then all there is.
        public static List<Extent> Extents(string path)
        {
            var found = new List<Extent>();
            uint mode = 0;
            bool quiet = false;
            try
            {
                // (a path on a drive with no medium in it, such as a card reader whose card was taken out: Windows
                // is told not to ask for the medium in a window)
                quiet = SetThreadErrorMode(1, out mode);
                string volume;
                using (SafeFileHandle file = CreateFile(path, 0, ShareRead | ShareWrite | ShareDelete, IntPtr.Zero, OpenExisting,
                                                        BackupSemantics, IntPtr.Zero))
                {
                    if (file.IsInvalid) return found;
                    var name = new StringBuilder(1024);
                    uint length = GetFinalPathNameByHandle(file, name, (uint)name.Capacity, 1 /* the volume by its GUID */);
                    if (length == 0 || length >= name.Capacity) return found;
                    volume = name.ToString();
                }
                // \\?\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\folder\file: the volume is up to the brace
                int brace = volume.IndexOf('}');
                if (!volume.StartsWith(@"\\?\Volume{", StringComparison.OrdinalIgnoreCase) || brace < 0) return found;
                using (SafeFileHandle device = CreateFile(volume.Substring(0, brace + 1), 0, ShareRead | ShareWrite, IntPtr.Zero,
                                                          OpenExisting, 0, IntPtr.Zero))
                {
                    if (device.IsInvalid) return found;
                    // VOLUME_DISK_EXTENTS: a count, and from byte 8 on, 24 bytes for each stretch: the number of
                    // the disk, where the stretch starts on it and how long it is
                    int room = 64;
                    for (int attempt = 0; attempt < 2; ++attempt)
                    {
                        IntPtr buffer = Marshal.AllocHGlobal(8 + 24 * room);
                        try
                        {
                            Marshal.WriteInt32(buffer, 0, 0);
                            uint returned;
                            if (DeviceIoControl(device, IoctlVolumeGetVolumeDiskExtents, IntPtr.Zero, 0, buffer, (uint)(8 + 24 * room),
                                                out returned, IntPtr.Zero))
                            {
                                int count = Math.Min(Marshal.ReadInt32(buffer, 0), room);
                                for (int i = 0; i < count; ++i)
                                {
                                    var e = new Extent();
                                    e.Disk = Marshal.ReadInt32(buffer, 8 + 24 * i);
                                    e.Offset = Marshal.ReadInt64(buffer, 8 + 24 * i + 8);
                                    e.Length = Marshal.ReadInt64(buffer, 8 + 24 * i + 16);
                                    found.Add(e);
                                }
                                return found;
                            }
                            // more stretches than there was room for: Windows says how many, and it is asked
                            // again with room for all of them (what it wrote meanwhile is not used)
                            if (Marshal.GetLastWin32Error() != ErrorMoreData) return found;
                            int needed = Marshal.ReadInt32(buffer, 0);
                            if (needed <= room || needed > 4096) return found;
                            room = needed;
                        }
                        finally { Marshal.FreeHGlobal(buffer); }
                    }
                }
            }
            catch (Exception) { found.Clear(); }
            finally
            {
                uint ignored;
                try { if (quiet) SetThreadErrorMode(mode, out ignored); }
                catch (Exception) { }
            }
            return found;
        }

        static FileStream Open(string name, bool write)
        {
            SafeFileHandle handle = CreateFile(name, write ? GenericRead | GenericWrite : GenericRead, ShareRead | ShareWrite,
                                               IntPtr.Zero, OpenExisting, write ? WriteThrough : 0, IntPtr.Zero);
            if (handle.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return new FileStream(handle, write ? FileAccess.ReadWrite : FileAccess.Read, 4096, false);
        }

        public static FileStream OpenDisk(int number, bool write)
        {
            return Open(@"\\.\PhysicalDrive" + number, write);
        }

        // A folder, held open as a program that shows or serves its files holds it. (For the tests.)
        public static SafeFileHandle OpenFolder(string path)
        {
            SafeFileHandle folder = CreateFile(path, GenericRead, ShareRead | ShareWrite | ShareDelete, IntPtr.Zero, OpenExisting,
                                               BackupSemantics, IntPtr.Zero);
            if (folder.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            return folder;
        }

        // One partition, locked and with no file system mounted on it, so that it can be written whole.
        // It is opened by its number, so Windows is asked where the partition behind the handle starts
        // and how long it is: when that is not the place that was meant, nothing is written. A handle of
        // a partition cannot write outside that partition.
        // mustLock: the partition holds a file system that Windows has mounted. It is written only once no
        // other program has it: locked, which Windows grants when nothing has a file or a folder of it
        // open, or, with take, taken from the programs that have.
        // take: when the lock is refused because the partition is in use, Windows is told to dismount the
        // file system all the same (what "format /X" and "chkdsk /X" do). Every handle that other programs
        // have on it stops working. The FAT driver of Windows then puts off the end of the dismount until
        // the handle that asked for it is closed: until then it refuses every attempt to open a file of
        // the partition, and the partition is not mounted again, so what is written through this handle
        // is not mixed with anything of the old file system. That is how the driver is written (fastfat,
        // FatDismountVolume: "we defer the physical dismount until this handle is closed"), not something
        // that the documentation of Windows promises; the test on a virtual disk looks at it (VhdTest).
        // The partition is one that is replaced whole, so what those programs lose is what the update
        // removes anyway.
        public static FileStream OpenPartition(int disk, int partition, long offset, long length, bool mustLock = false,
                                               bool take = false)
        {
            FileStream f = Open(@"\\.\Harddisk" + disk + "Partition" + partition, true);
            uint returned;
            IntPtr info = Marshal.AllocHGlobal(256);
            try
            {
                bool asked = DeviceIoControl(f.SafeFileHandle, IoctlDiskGetPartitionInfoEx, IntPtr.Zero, 0, info, 256, out returned, IntPtr.Zero);
                long start = asked ? Marshal.ReadInt64(info, 8) : -1, size = asked ? Marshal.ReadInt64(info, 16) : -1;
                if (!asked || start != offset || size != length)
                {
                    f.Dispose();
                    throw new InvalidOperationException(L.T(
                        "Partition " + partition + " is not where it was expected (it starts at " + start + " and is " + size +
                        " long; " + offset + " and " + length + " were expected). Nothing was written to it.",
                        "La partición " + partition + " no está donde se esperaba (empieza en " + start + " y mide " + size +
                        "; se esperaba " + offset + " y " + length + "). No se ha escrito nada en ella."));
                }
            }
            finally { Marshal.FreeHGlobal(info); }
            // The whole partition can be read and written through this handle, whatever size the file system
            // that is on it now says that it has (without this, the FAT driver cuts short, without an error,
            // what goes past the end of that file system). A partition with no file system does not know the
            // request, and does not need it.
            bool whole = DeviceIoControl(f.SafeFileHandle, FsctlAllowExtendedDasdIo, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, IntPtr.Zero);
            // (a unit that was just plugged in is often being looked at by the antivirus: that passes. When
            // the partition is going to be taken, the wait was done already, by the attempt before this one)
            bool locked = false;
            int lockError = 0;
            for (int attempt = 0; attempt < (!mustLock ? 1 : take ? 4 : 20) && !locked; ++attempt)
            {
                if (attempt > 0) System.Threading.Thread.Sleep(500);
                locked = DeviceIoControl(f.SafeFileHandle, FsctlLockVolume, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, IntPtr.Zero);
                lockError = locked ? 0 : Marshal.GetLastWin32Error();
            }
            // The lock was refused, and not because a program has the partition open (that is "access
            // denied"): the drive is not ready, was changed, or gives errors. Nothing is taken from anybody
            // on that account, and nothing is written.
            if (mustLock && !locked && lockError != ErrorAccessDenied)
            {
                Log.Line("partition " + disk + "/" + partition + ": not locked (" + lockError + "), left alone");
                f.Dispose();
                throw new InvalidOperationException(L.T(
                    "Windows did not give the installer the Hades system partition of that drive (error " + lockError +
                    "). Nothing was written. Unplug the drive and plug it in again, or restart the PC, and try again.",
                    "Windows no ha dado al instalador la partición de sistema de Hades de esa unidad (error " + lockError +
                    "). No se ha escrito nada. Desconecta la unidad y vuelve a conectarla, o reinicia el PC, y vuelve a intentarlo."));
            }
            // a partition that Windows is using (a window or a program has it open) is not written over,
            // and its file system is left mounted, unless it was asked to take it
            if (mustLock && !locked && !take)
            {
                Log.Line("partition " + disk + "/" + partition + ": in use (" + lockError + "), left alone");
                f.Dispose();
                throw new PartitionInUseException(L.T(
                    "Windows is using the Hades system partition of that drive. Close the File Explorer windows and the programs " +
                    "that have it open (if you have just plugged it in, wait half a minute) and try again. Nothing was written.",
                    "Windows tiene en uso la partición de sistema de Hades de esa unidad. Cierra las ventanas del Explorador y los " +
                    "programas que la tengan abierta (si acabas de conectarla, espera medio minuto) y vuelve a intentarlo. No se ha " +
                    "escrito nada."));
            }
            bool dismounted = DeviceIoControl(f.SafeFileHandle, FsctlDismountVolume, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, IntPtr.Zero);
            int dismountError = Marshal.GetLastWin32Error();
            Log.Line("partition " + disk + "/" + partition + ": locked " + locked + (locked ? "" : " (" + lockError + ")") +
                     ", dismounted " + dismounted + (dismounted ? "" : " (" + dismountError + ")") +
                     (mustLock && !locked ? dismounted ? ", taken from what had it open" : ", not taken" : "") +
                     (whole ? "" : ", as long as its file system"));
            // neither locked nor dismounted: its file system is still mounted, with other programs on it
            if (mustLock && !locked && !dismounted)
            {
                f.Dispose();
                throw new InvalidOperationException(L.T(
                    "Windows did not let the installer take the Hades system partition of that drive (error " + dismountError +
                    "). Nothing was written. Close the programs that use that drive, or restart the PC, and update before opening " +
                    "anything else.",
                    "Windows no ha dejado que el instalador tome la partición de sistema de Hades de esa unidad (error " +
                    dismountError + "). No se ha escrito nada. Cierra los programas que usen esa unidad, o reinicia el PC, y " +
                    "actualiza antes de abrir nada más."));
            }
            return f;
        }

        // Before a disk is erased whole, for each of its partitions: what its file system still had to write is
        // written out (its cache flushed through a handle of the volume), the volume is locked where Windows grants
        // it (nothing has it open), and dismounted, locked or not, as "format /X" does. A program that has a folder
        // or a file of it open (Explorer, WSL) can then no longer write through it, and the file system has nothing
        // left to write: nothing of the old volume can land on the partitions that are written in its place.
        // A partition without a file system answers these requests with errors, which are only written down. One
        // with a drive letter that cannot be dismounted stops the install, before anything is erased.
        public static void FlushAndDismount(int disk, PartitionInfo p)
        {
            string name = "partition " + disk + "/" + p.Number;
            FileStream f;
            try { f = Open(@"\\.\Harddisk" + disk + "Partition" + p.Number, true); }
            catch (Win32Exception e)
            {
                Log.Line(name + ": not opened to be dismounted (" + e.NativeErrorCode + ")");
                if (p.Letter != '\0') throw NotDismounted(p, e.NativeErrorCode);
                return;
            }
            using (f)
            {
                uint returned;
                IntPtr info = Marshal.AllocHGlobal(256);
                try
                {
                    bool asked = DeviceIoControl(f.SafeFileHandle, IoctlDiskGetPartitionInfoEx, IntPtr.Zero, 0, info, 256, out returned, IntPtr.Zero);
                    if (!asked || Marshal.ReadInt64(info, 8) != p.Offset || Marshal.ReadInt64(info, 16) != p.Size)
                        throw new InvalidOperationException(L.T("The disk has changed since you chose it. Nothing was written: choose it again.",
                                                                "El disco ha cambiado desde que lo elegiste. No se ha escrito nada: vuelve a elegirlo."));
                }
                finally { Marshal.FreeHGlobal(info); }
                bool flushed = true;
                try { f.Flush(true); }
                catch (IOException) { flushed = false; }
                bool locked = false;
                int lockError = 0;
                for (int attempt = 0; attempt < 4 && !locked; ++attempt)
                {
                    if (attempt > 0) System.Threading.Thread.Sleep(500);
                    locked = DeviceIoControl(f.SafeFileHandle, FsctlLockVolume, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, IntPtr.Zero);
                    lockError = locked ? 0 : Marshal.GetLastWin32Error();
                    if (!locked && lockError != ErrorAccessDenied) break; // not "in use": waiting does not help
                }
                bool dismounted = DeviceIoControl(f.SafeFileHandle, FsctlDismountVolume, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, IntPtr.Zero);
                int dismountError = dismounted ? 0 : Marshal.GetLastWin32Error();
                Log.Line(name + (p.Letter != '\0' ? " (" + p.Letter + ":)" : "") + ", before the disk is erased: flushed " + flushed +
                         ", locked " + locked + (locked ? "" : " (" + lockError + ")") + ", dismounted " + dismounted +
                         (dismounted ? "" : " (" + dismountError + ")") + (!locked && dismounted ? ", taken from what had it open" : ""));
                if (!dismounted && p.Letter != '\0') throw NotDismounted(p, dismountError);
            }
        }

        static InvalidOperationException NotDismounted(PartitionInfo p, int error)
        {
            return new InvalidOperationException(L.T(
                "Windows did not let the installer close partition " + p.Letter + ": of that disk before erasing it (error " + error +
                "). Nothing was written. Close the programs that use that drive, or restart the PC, and try again.",
                "Windows no ha dejado que el instalador cierre la partición " + p.Letter + ": de ese disco antes de borrarlo (error " +
                error + "). No se ha escrito nada. Cierra los programas que usen esa unidad, o reinicia el PC, y vuelve a intentarlo."));
        }

        // Whether this PC was started by UEFI (the other way is the BIOS of older PCs)
        public static bool Uefi
        {
            get
            {
                try { uint type; return GetFirmwareType(out type) && type == 2; }
                catch (Exception) { return false; }
            }
        }
    }

    // A partition with a file system that Windows has mounted and some program has open: it was left as it
    // was. The window then asks whether to take it from that program (MainForm.AskTake).
    sealed class PartitionInUseException : InvalidOperationException
    {
        public PartitionInUseException(string message) : base(message) { }
    }

    // WSL, the Linux of Windows. While it runs, the program that serves the files of Windows to it keeps a
    // folder open on every disk drive that was there when WSL started (Linux sees them as /mnt/<letter>),
    // whether anything in Linux uses them or not. Such a drive cannot be locked, however long one waits,
    // and nothing on the screen says why: so the window says it.
    static class Wsl
    {
        // That program is a DllHost.exe that Windows starts for the class Plan9FileSystem (vp9fs.dll); its
        // command line names the class by this identifier.
        const string FileServer = "DFB65C4C-B34F-435D-AFE9-A86218684AA8";

        public static bool IsFileServer(string commandLine)
        {
            return commandLine != null && commandLine.IndexOf("dllhost", StringComparison.OrdinalIgnoreCase) >= 0 &&
                   commandLine.IndexOf(FileServer, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        // Whether it is running now. false when that cannot be known.
        public static bool Running
        {
            get
            {
                try
                {
                    // (Windows is given three seconds to answer: this is asked from the window)
                    var patience = new EnumerationOptions();
                    patience.Timeout = TimeSpan.FromSeconds(3);
                    using (var search = new ManagementObjectSearcher(@"root\cimv2",
                               "SELECT CommandLine FROM Win32_Process WHERE Name = 'dllhost.exe'", patience))
                    using (ManagementObjectCollection found = search.Get())
                        foreach (ManagementBaseObject process in found)
                            using (process)
                                if (IsFileServer(process["CommandLine"] as string)) return true;
                }
                catch (Exception) { }
                return false;
            }
        }
    }

    static class Pc
    {
        public static bool Elevated
        {
            get { return new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator); }
        }

        // 1: on, 0: off, -1: not known (a PC that does not start by UEFI has no such value)
        public static int SecureBoot
        {
            get
            {
                try
                {
                    object v = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\SecureBoot\State",
                                                 "UEFISecureBootEnabled", null);
                    return v == null ? -1 : Convert.ToInt32(v) != 0 ? 1 : 0;
                }
                catch (Exception) { return -1; }
            }
        }
    }
}
