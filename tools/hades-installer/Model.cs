// LibreELEC Hades installer for Windows: what it knows of the disks of the PC, the two languages of its
// texts, and small helpers.
// Built with the C# compiler of the .NET Framework that Windows brings (build.ps1): C# 5 only.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Hades.Installer
{
    // The language of every text that a user sees: English, or Spanish where Windows is in Spanish. The
    // window can change it while it runs, but not while a disk is being worked on. A text is worded when
    // it is made: one that is kept and shown later is made again when the language changes
    // (MainForm.SwitchLanguage). Nothing but texts depends on it. Log lines are in English; an error that
    // they quote is in the language that was in use.
    static class L
    {
        public static bool Spanish = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "es";

        public static string T(string english, string spanish)
        {
            return Spanish ? spanish : english;
        }
    }

    sealed class VolumeInfo
    {
        public char Letter;
        public string Label = "";
        public string FileSystem = "";
        public long Size;
        public long Used;
        public bool Ready;
    }

    sealed class PartitionInfo
    {
        public int Number;
        public long Offset;
        public long Size;
        public string GptType = ""; // "{guid}", lower case; empty on an MBR disk
        public char Letter;
        public long End { get { return Offset + Size; } }
    }

    // A stretch of a disk that a volume is on.
    sealed class Extent
    {
        public int Disk;
        public long Offset;
        public long Length;
    }

    // What must never be erased, and where it is: Windows, the files of the user, this program, the image
    // that is chosen and temporary files (Disks.Keeping). Each of them is known twice: by the drive letter
    // of its path, and by the place on a disk of the volume that it is really on, which is what counts when
    // the path does not say it (a folder that is a mount point or a junction, a SUBST letter).
    sealed class Keep
    {
        public List<char> Letters = new List<char>();
        public List<Extent> Extents = new List<Extent>();
        // Windows itself. Its disk is not used in any way, whatever Windows says of that disk.
        public char Windows;
        public List<int> WindowsDisks = new List<int>();
    }

    sealed class DiskInfo
    {
        public int Number;
        public string Name = ""; // as Windows gives it: it says which disk this is, so it is in no language
        public string Serial = "";
        public string UniqueId = "";
        public string Location = "";
        public long Size;
        public int BusType;
        public bool IsBoot, IsSystem, IsOffline, IsReadOnly;
        public int PartitionStyle; // 0: none (raw), 1: MBR, 2: GPT
        public List<PartitionInfo> Partitions = new List<PartitionInfo>();
        public List<VolumeInfo> Volumes = new List<VolumeInfo>();
        public string Excluded; // why it is not offered; null when it is
        public string Kept;     // where on it something is that must not be erased (Disks.Kept): "D:", or "" for a
                                // volume without a letter; null: nowhere
        public bool HasHades;   // a system partition of Hades is on it

        public bool SameAs(DiskInfo other)
        {
            return other != null && Number == other.Number && UniqueId == other.UniqueId && Serial == other.Serial &&
                   Size == other.Size && BusType == other.BusType && Name == other.Name;
        }

        public string BusName
        {
            get
            {
                switch (BusType)
                {
                    case 7: return "USB";
                    case 12: return L.T("SD card", "tarjeta SD");
                    case 13: return L.T("MMC card", "tarjeta MMC");
                    case 17: return "NVMe";
                    case 11: return "SATA";
                    case 3: return "ATA";
                    case 8: return "RAID";
                    case 10: return "SAS";
                    case 15: return L.T("virtual disk", "disco virtual");
                    default: return "bus " + BusType;
                }
            }
        }

        public bool Removable { get { return BusType == 7 || BusType == 12 || BusType == 13; } }
        public bool Fixed { get { return BusType == 17 || BusType == 11 || BusType == 3 || BusType == 10 || BusType == 8; } }
        public bool Virtual { get { return BusType == 15; } }

        public long UsedBytes
        {
            get { long n = 0; foreach (VolumeInfo v in Volumes) n += v.Used; return n; }
        }

        // The name to show: a disk that Windows gives no name is called by its number
        public string ShownName { get { return Name.Length > 0 ? Name : L.T("Drive ", "Unidad ") + Number; } }

        public string Title { get { return ShownName + "  ·  " + Words.Size(Size); } }

        public string Detail
        {
            get
            {
                var sb = new StringBuilder(BusName);
                if (HasHades) sb.Append(L.T("  ·  has LibreELEC Hades", "  ·  tiene LibreELEC Hades"));
                if (Volumes.Count == 0)
                    sb.Append(Partitions.Count == 0 ? L.T("  ·  empty, no partitions", "  ·  vacío, sin particiones")
                                                    : L.T("  ·  no drive letter in Windows", "  ·  sin letra de unidad en Windows"));
                foreach (VolumeInfo v in Volumes)
                {
                    sb.Append("  ·  ").Append(v.Letter).Append(':');
                    if (v.Label.Length > 0) sb.Append(L.T(" “", " «")).Append(v.Label).Append(L.T("”", "»"));
                    if (v.Ready) sb.Append(' ').Append(Words.Size(v.Used)).Append(L.T(" used", " ocupados"));
                }
                return sb.ToString();
            }
        }
    }

    static class Words
    {
        static readonly CultureInfo Comma = CultureInfo.GetCultureInfo("es-ES");

        // "29 GB", "1.0 GB", "512 MB"; in Spanish with a decimal comma
        public static string Size(long bytes)
        {
            CultureInfo numbers = L.Spanish ? Comma : CultureInfo.InvariantCulture;
            double gb = bytes / 1073741824.0;
            if (gb >= 10) return Math.Round(gb).ToString("0", numbers) + " GB";
            if (gb >= 1) return gb.ToString("0.0", numbers) + " GB";
            return Math.Round(bytes / 1048576.0).ToString("0", numbers) + " MB";
        }

        public static string Hex(byte[] hash)
        {
            var sb = new StringBuilder(hash.Length * 2);
            foreach (byte b in hash) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }
    }

    static class Log
    {
        static string path;
        static readonly object gate = new object();

        public static void Open(string directory)
        {
            try
            {
                path = Path.Combine(directory, "LibreELEC-Hades-Installer.log");
                File.AppendAllText(path, "");
            }
            catch (Exception)
            {
                path = Path.Combine(Path.GetTempPath(), "LibreELEC-Hades-Installer.log");
            }
        }

        public static void Line(string text)
        {
            if (path == null) return;
            lock (gate)
            {
                try { File.AppendAllText(path, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + text + "\r\n"); }
                catch (Exception) { }
            }
        }
    }
}
