// LibreELEC Hades installer for Windows: "Restart into LibreELEC Hades".
// The firmware of a PC (UEFI) keeps a list of boot entries and a boot order, Windows first. When a drive with Hades is
// plugged in, most firmware lists it as an entry of its own (on the PC of the user, an ASUS: "UEFI: HIKSEMI, Partition
// 1"). This finds, among the entries that are in the boot order of the firmware already, the one whose device is the
// system partition of a Hades on a disk of this PC, known by the identifier that its partition table gives that
// partition (GPT), never by a drive letter; and sets BootNext to it: the firmware starts that entry at the next start
// only, and deletes BootNext when it does, so the start after that one is Windows again, as always. Nothing else of the
// firmware is written: no boot entry is made, changed, deleted or moved, and the boot order stays as it is. When the
// firmware does not list the drive, it says so and how to start it from the boot menu of the PC.
// It needs administrator rights, which Windows asks for every time, and the privilege to change the variables of the
// firmware, which an administrator has (it is turned on for this program only, and for this run only).
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Windows.Forms;

namespace Hades.Installer
{
    static class FirmwareBoot
    {
        // the variables of the boot manager of UEFI (EFI_GLOBAL_VARIABLE)
        public const string Global = "{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";
        // the switch of a shortcut on the desktop (and its Spanish name, as the other switches have one)
        public const string Switch = "--restart-into-hades";
        const string SwitchEs = "--reiniciar-en-hades";

        public static bool IsSwitch(string arg) { return arg == Switch || arg == SwitchEs; }

        // ---- what a boot entry says (EFI_LOAD_OPTION)

        // A partition named in the device path of an entry (the "hard drive" node of UEFI)
        public sealed class HardDrive
        {
            public uint Number;
            public ulong Start, Size;
            public byte Format;          // 1 MBR, 2 GPT
            public byte SignatureType;   // 1 the 32-bit signature of an MBR disk, 2 the GUID of a GPT partition
            public byte[] Signature = new byte[16];
            public Guid PartitionGuid { get { return SignatureType == 2 ? new Guid(Signature) : Guid.Empty; } }
        }

        public sealed class Entry
        {
            public int Number;
            public uint Attributes;
            public string Description = "";
            public string File = "";     // the file of its device path, "" when the firmware starts the default one
            public List<HardDrive> Partitions = new List<HardDrive>();
            public bool Active { get { return (Attributes & 1) != 0; } }
            public bool BootCategory { get { return (Attributes & 0x1F00) == 0; } }
            public string Name { get { return "Boot" + Number.ToString("X4"); } }
            public override string ToString() { return Name + " \"" + Description + "\""; }
        }

        static string Ucs2(byte[] data, ref int at, int end, out bool ended)
        {
            var sb = new StringBuilder();
            ended = false;
            while (at + 1 < end)
            {
                int c = data[at] | data[at + 1] << 8;
                at += 2;
                if (c == 0) { ended = true; break; }
                sb.Append(c < 32 || c == 127 ? '?' : (char)c);
            }
            return sb.ToString();
        }

        // An entry from the bytes of its variable Boot####, or null when they do not hold together: a description
        // without its end, a device path longer than what is there, a node shorter than its own header.
        public static Entry Parse(int number, byte[] data)
        {
            if (data == null || data.Length < 8) return null;
            var e = new Entry();
            e.Number = number;
            e.Attributes = BitConverter.ToUInt32(data, 0);
            int pathLength = BitConverter.ToUInt16(data, 4);
            int i = 6;
            bool ended;
            e.Description = Ucs2(data, ref i, data.Length, out ended);
            if (!ended) return null;
            int last = i + pathLength; // (the end of the device path, not in it)
            if (pathLength < 4 || last > data.Length) return null;
            bool previousFile = false;
            while (i + 4 <= last)
            {
                int type = data[i], sub = data[i + 1], length = data[i + 2] | data[i + 3] << 8;
                if (length < 4 || i + length > last) return null;
                if (type == 0x7F && sub == 0xFF) break;
                if (type == 4 && sub == 1 && length == 42)
                {
                    var hd = new HardDrive();
                    hd.Number = BitConverter.ToUInt32(data, i + 4);
                    hd.Start = BitConverter.ToUInt64(data, i + 8);
                    hd.Size = BitConverter.ToUInt64(data, i + 16);
                    Buffer.BlockCopy(data, i + 24, hd.Signature, 0, 16);
                    hd.Format = data[i + 40];
                    hd.SignatureType = data[i + 41];
                    e.Partitions.Add(hd);
                }
                if (type == 4 && sub == 4)
                {
                    // (two file nodes in a row are one path)
                    int at = i + 4;
                    bool unused;
                    string part = Ucs2(data, ref at, i + length, out unused);
                    e.File = previousFile ? e.File + part : part;
                    previousFile = true;
                }
                else previousFile = false;
                i += length;
            }
            return e;
        }

        // A variable made of 16-bit numbers (BootOrder, BootCurrent, BootNext), or null
        public static List<int> Numbers(byte[] data)
        {
            if (data == null || data.Length == 0 || data.Length % 2 != 0) return null;
            var list = new List<int>();
            for (int i = 0; i < data.Length; i += 2) list.Add(data[i] | data[i + 1] << 8);
            return list;
        }

        // ---- what is looked for

        // The system partition of a Hades on a disk of this PC (a GPT disk: the images of Hades are)
        public sealed class HadesDisk
        {
            public int Disk;
            public string Name = "";
            public long Size;
            public string Bus = "";
            // how the texts name it: "HIKSEMI HS-USB-SYNC C PRO (117 GB, USB)"
            public string Shown { get { return Name + " (" + Words.Size(Size) + ", " + Bus + ")"; } }
            public int Partition;
            public long Offset;
            public Guid PartitionGuid;
        }

        public static bool Matches(HardDrive hd, HadesDisk h)
        {
            return hd.SignatureType == 2 && h.PartitionGuid != Guid.Empty && hd.PartitionGuid == h.PartitionGuid;
        }

        // What the firmware has: its boot order, the entry that this start of Windows came from, and the entries of
        // the boot order (null: there, but broken)
        public sealed class Firmware
        {
            public List<int> Order;
            public int Current = -1;
            public int Next = -1;
            public Dictionary<int, Entry> Entries = new Dictionary<int, Entry>();
        }

        public sealed class Found
        {
            public Entry Entry;
            public HadesDisk Hades;
        }

        // The entries of the boot order whose device is the system partition of a Hades, in the order of the firmware;
        // the ones that are turned off in the firmware (not active) go to "off". Only boot entries count (not the
        // applications that a firmware may list), and only entries that are in the boot order.
        public static List<Found> Find(Firmware fw, List<HadesDisk> hades, List<Found> off)
        {
            var found = new List<Found>();
            if (fw == null || fw.Order == null || hades == null) return found;
            var seen = new List<int>();
            foreach (int n in fw.Order)
            {
                Entry e;
                if (seen.Contains(n) || !fw.Entries.TryGetValue(n, out e) || e == null || !e.BootCategory) continue;
                seen.Add(n);
                HadesDisk match = null;
                foreach (HardDrive hd in e.Partitions)
                    foreach (HadesDisk h in hades)
                        if (match == null && Matches(hd, h)) match = h;
                if (match == null) continue;
                var f = new Found();
                f.Entry = e;
                f.Hades = match;
                if (e.Active) found.Add(f);
                else if (off != null) off.Add(f);
            }
            return found;
        }

        public enum Outcome { Ready, NotUefi, CannotRead, NoHades, NotListed, TurnedOff }

        // What to do, from what was found: nothing here asks Windows anything (the self-test runs it)
        public static Outcome Decide(bool uefi, Firmware fw, List<HadesDisk> hades, out Found chosen, out List<Found> all)
        {
            chosen = null;
            all = new List<Found>();
            if (!uefi) return Outcome.NotUefi;
            if (fw == null || fw.Order == null) return Outcome.CannotRead;
            if (hades == null || hades.Count == 0) return Outcome.NoHades;
            var off = new List<Found>();
            all = Find(fw, hades, off);
            if (all.Count > 0)
            {
                chosen = all[0];
                return Outcome.Ready;
            }
            if (off.Count > 0)
            {
                chosen = off[0];
                return Outcome.TurnedOff;
            }
            return Outcome.NotListed;
        }

        // The key of the boot menu, by the maker of the PC (or of its board), for the texts; null when not known.
        // laptop: the PC says that it is a portable one (the makers of boards and of laptops differ in it).
        public static string MenuKey(string maker, bool laptop)
        {
            string m = (maker ?? "").Trim().ToLowerInvariant();
            if (m.Length == 0) return null;
            if (m.Contains("asus")) return laptop ? "Esc" : "F8";
            if (m.Contains("micro-star") || m == "msi" || m.StartsWith("msi ")) return "F11";
            if (m.Contains("gigabyte")) return "F12";
            if (m.Contains("asrock")) return "F11";
            if (m.Contains("dell")) return "F12";
            if (m.Contains("hewlett") || m == "hp" || m.StartsWith("hp ")) return "F9";
            if (m.Contains("lenovo")) return "F12";
            if (m.Contains("acer")) return "F12";
            if (m.Contains("intel")) return "F10";
            return null;
        }

        // ---- the firmware of this PC (administrator)

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern uint GetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, out uint attributes);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool SetFirmwareEnvironmentVariableExW(string name, string guid, byte[] value, uint size, uint attributes);

        [StructLayout(LayoutKind.Sequential)]
        struct Luid { public uint Low; public int High; }

        [StructLayout(LayoutKind.Sequential)]
        struct TokenPrivileges { public uint Count; public Luid Luid; public uint Attributes; }

        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);

        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool LookupPrivilegeValue(string system, string name, out Luid luid);

        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges state, uint length, IntPtr previous,
                                                 IntPtr returned);

        [DllImport("kernel32.dll")]
        static extern IntPtr GetCurrentProcess();

        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr handle);

        [DllImport("user32.dll", SetLastError = true)]
        static extern bool ExitWindowsEx(uint flags, uint reason);

        const int ErrorEnvvarNotFound = 203, ErrorInsufficientBuffer = 122, ErrorNotAllAssigned = 1300;

        // A privilege of this process turned on, or the error of Windows
        static int Privilege(string name)
        {
            IntPtr token;
            if (!OpenProcessToken(GetCurrentProcess(), 0x0020 | 0x0008 /* adjust, query */, out token)) return Marshal.GetLastWin32Error();
            try
            {
                var tp = new TokenPrivileges();
                tp.Count = 1;
                tp.Attributes = 2; // enabled
                if (!LookupPrivilegeValue(null, name, out tp.Luid)) return Marshal.GetLastWin32Error();
                if (!AdjustTokenPrivileges(token, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero)) return Marshal.GetLastWin32Error();
                int error = Marshal.GetLastWin32Error();
                return error == ErrorNotAllAssigned ? error : 0;
            }
            finally { CloseHandle(token); }
        }

        // A variable of the firmware, null when it is not there; throws when it cannot be read
        static byte[] ReadVariable(string name)
        {
            foreach (int size in new[] { 4096, 65536 })
            {
                var buffer = new byte[size];
                uint attributes;
                uint got = GetFirmwareEnvironmentVariableExW(name, Global, buffer, (uint)size, out attributes);
                if (got > 0)
                {
                    var data = new byte[got];
                    Buffer.BlockCopy(buffer, 0, data, 0, (int)got);
                    return data;
                }
                int error = Marshal.GetLastWin32Error();
                if (error == ErrorEnvvarNotFound) return null;
                if (error != ErrorInsufficientBuffer) throw new Win32Exception(error);
            }
            throw new Win32Exception(ErrorInsufficientBuffer);
        }

        // What the firmware of this PC has, or null with the reason (in the words of Windows) in "why"
        public static Firmware ReadFirmware(out string why)
        {
            why = null;
            try
            {
                int p = Privilege("SeSystemEnvironmentPrivilege");
                if (p != 0) throw new Win32Exception(p);
                var fw = new Firmware();
                fw.Order = Numbers(ReadVariable("BootOrder"));
                if (fw.Order == null)
                {
                    why = "no BootOrder";
                    return null;
                }
                List<int> current = Numbers(ReadVariable("BootCurrent")), next = Numbers(ReadVariable("BootNext"));
                if (current != null && current.Count == 1) fw.Current = current[0];
                if (next != null && next.Count == 1) fw.Next = next[0];
                foreach (int n in fw.Order)
                    if (!fw.Entries.ContainsKey(n)) fw.Entries[n] = Parse(n, ReadVariable("Boot" + n.ToString("X4")));
                return fw;
            }
            catch (Win32Exception e)
            {
                why = e.Message + " (" + e.NativeErrorCode + ")";
                return null;
            }
        }

        // The disks of this PC with Hades on them, each by the identifier of its system partition. Only disks that the
        // installer would offer for Hades are looked at (read, as the window does): never the disk of Windows, nor one in the
        // list of protected disks of this PC.
        public static List<HadesDisk> HadesDisks()
        {
            var found = new List<HadesDisk>();
            Keep all = Disks.Keeping(null), windows = new Keep();
            windows.Windows = all.Windows;
            windows.WindowsDisks.AddRange(all.WindowsDisks);
            List<string> guarded = Disks.Protected();
            foreach (DiskInfo d in Disks.List())
            {
                string why = Disks.Classify(d, d.Removable ? Place.Usb : Place.Internal, 0, windows, guarded);
                if (why != null)
                {
                    Log.Line("restart into Hades: disk " + d.Number + " not looked at: " + why);
                    continue;
                }
                PartitionInfo system, storage;
                if (d.PartitionStyle != 2 || !Disks.FindHades(d, out system, out storage)) continue;
                var h = new HadesDisk();
                h.Disk = d.Number;
                h.Name = d.ShownName;
                h.Size = d.Size;
                h.Bus = d.BusName;
                h.Partition = system.Number;
                h.Offset = system.Offset;
                h.PartitionGuid = PartitionGuid(d.Number, system.Offset);
                if (h.PartitionGuid != Guid.Empty) found.Add(h);
                else Log.Line("restart into Hades: disk " + d.Number + " has Hades, but Windows gives no identifier of its partition");
            }
            return found;
        }

        // The GUID that the partition table gives the partition that starts there (Windows reads it from the disk)
        static Guid PartitionGuid(int disk, long offset)
        {
            try
            {
                using (var search = new ManagementObjectSearcher(@"\\.\root\Microsoft\Windows\Storage",
                                                                 "SELECT Offset, Guid FROM MSFT_Partition WHERE DiskNumber = " + disk))
                    foreach (ManagementObject p in search.Get())
                        if (Convert.ToInt64(p["Offset"] ?? -1) == offset && p["Guid"] != null)
                            return new Guid(Convert.ToString(p["Guid"]));
            }
            catch (Exception e) { Log.Line("restart into Hades: partition of disk " + disk + ": " + e.Message); }
            return Guid.Empty;
        }

        // BootNext = number, read back: true when the firmware gives it back
        static bool SetNext(int number, out string why)
        {
            why = null;
            try
            {
                int p = Privilege("SeSystemEnvironmentPrivilege");
                if (p != 0) throw new Win32Exception(p);
                var value = new[] { (byte)(number & 0xFF), (byte)(number >> 8) };
                // non-volatile, and seen by the boot manager and by the system, as the firmware keeps it
                if (!SetFirmwareEnvironmentVariableExW("BootNext", Global, value, 2, 7)) throw new Win32Exception(Marshal.GetLastWin32Error());
                List<int> back = Numbers(ReadVariable("BootNext"));
                if (back != null && back.Count == 1 && back[0] == number) return true;
                why = "BootNext is not what was written";
                return false;
            }
            catch (Win32Exception e)
            {
                why = e.Message + " (" + e.NativeErrorCode + ")";
                return false;
            }
        }

        // BootNext taken away again (when Windows does not restart after all): a variable of one start, which goes
        static void ClearNext()
        {
            try
            {
                if (Privilege("SeSystemEnvironmentPrivilege") == 0 && SetFirmwareEnvironmentVariableExW("BootNext", Global, null, 0, 7))
                    Log.Line("restart into Hades: BootNext taken away again");
            }
            catch (Exception e) { Log.Line("restart into Hades: BootNext not taken away: " + e.Message); }
        }

        static bool Restart(out string why)
        {
            why = null;
            int p = Privilege("SeShutdownPrivilege");
            // a restart that Windows plans, which lets the programs close as they do at any restart
            if (p == 0 && ExitWindowsEx(0x02 /* reboot */, 0x80000000 /* planned */)) return true;
            why = new Win32Exception(p != 0 ? p : Marshal.GetLastWin32Error()).Message;
            return false;
        }

        // The maker of this PC and whether it is a portable one (for the key of its boot menu)
        static void Maker(out string maker, out bool laptop)
        {
            maker = "";
            laptop = false;
            try
            {
                using (var search = new ManagementObjectSearcher("SELECT Manufacturer, PCSystemType FROM Win32_ComputerSystem"))
                    foreach (ManagementObject o in search.Get())
                    {
                        maker = Convert.ToString(o["Manufacturer"] ?? "");
                        laptop = Convert.ToInt32(o["PCSystemType"] ?? 0) == 2;
                    }
                // a PC that a person built: the maker of the board says the key
                if (MenuKey(maker, laptop) == null)
                    using (var search = new ManagementObjectSearcher("SELECT Manufacturer FROM Win32_BaseBoard"))
                        foreach (ManagementObject o in search.Get()) maker = Convert.ToString(o["Manufacturer"] ?? "");
            }
            catch (Exception) { }
        }

        // ---- the texts (made here, so that the self-test and the screenshots show them as they are said)

        static string KeyText(string key)
        {
            string keys = L.T("Common keys: ASUS F8 (laptops Esc), MSI F11, Gigabyte F12, ASRock F11, Dell F12, HP F9, Lenovo F12, Acer F12, " +
                              "Intel NUC F10. The manual of the PC says it for sure.",
                              "Teclas habituales: ASUS F8 (portátiles Esc), MSI F11, Gigabyte F12, ASRock F11, Dell F12, HP F9, Lenovo F12, " +
                              "Acer F12, Intel NUC F10. El manual del PC lo dice con seguridad.");
            if (key == null)
                return L.T("Restart the PC and, as soon as its logo shows, press the key of its boot menu; then choose the drive " +
                           "with LibreELEC Hades. ", "Reinicia el PC y, en cuanto salga su logotipo, pulsa la tecla de su menú de " +
                           "arranque; después elige la unidad con LibreELEC Hades. ") + keys;
            return L.T("Restart the PC and, as soon as its logo shows, press " + key + " (the key of the boot menu of this PC); then " +
                       "choose the drive with LibreELEC Hades. ", "Reinicia el PC y, en cuanto salga su logotipo, pulsa " + key +
                       " (la tecla del menú de arranque de este PC); después elige la unidad con LibreELEC Hades. ") + keys;
        }

        public static string Title { get { return L.T("Restart into LibreELEC Hades", "Reiniciar en LibreELEC Hades"); } }

        // What the window says for an outcome. why: what Windows said, for CannotRead.
        public static string Text(Outcome outcome, Found chosen, List<Found> all, string key, string why, bool shortcutRun)
        {
            switch (outcome)
            {
                case Outcome.Ready:
                    {
                        var sb = new StringBuilder();
                        sb.Append(L.T("The PC restarts and starts LibreELEC Hades from:\r\n", "El PC se reinicia y arranca LibreELEC Hades desde:\r\n"));
                        sb.Append("    ").Append(chosen.Hades.Shown).Append("\r\n");
                        sb.Append(L.T("    entry “", "    entrada «")).Append(chosen.Entry.Description).Append(L.T("” of the boot menu of the PC\r\n\r\n",
                                                                                                                   "» del menú de arranque del PC\r\n\r\n"));
                        if (all != null && all.Count > 1)
                            sb.Append(L.T("More than one drive has LibreELEC Hades: the one that comes first in the boot order of the PC is " +
                                          "used. Unplug the other one to use that one instead.\r\n\r\n",
                                          "Hay más de una unidad con LibreELEC Hades: se usa la primera en el orden de arranque del PC. " +
                                          "Desconecta la otra para usar esa.\r\n\r\n"));
                        sb.Append(L.T("Only for that start: the next time the PC starts, Windows starts as always. Nothing else of the PC is " +
                                      "changed.\r\n\r\n", "Solo para ese arranque: la próxima vez que el PC arranque, arrancará Windows como " +
                                      "siempre. No se cambia nada más del PC.\r\n\r\n"));
                        sb.Append(L.T("Save your work first: Windows closes your programs to restart. If it does not restart, nothing is " +
                                      "left set.", "Guarda antes tu trabajo: Windows cierra tus programas para reiniciar. Si no se reinicia, " +
                                      "no queda nada cambiado."));
                        return sb.ToString();
                    }
                case Outcome.NotUefi:
                    return L.T("This PC starts Windows in the old way (BIOS, not UEFI), so Windows cannot choose what it starts next.\r\n\r\n",
                               "Este PC arranca Windows a la manera antigua (BIOS, no UEFI), así que Windows no puede elegir qué arranca " +
                               "la próxima vez.\r\n\r\n") + KeyText(key);
                case Outcome.CannotRead:
                    return L.T("Windows did not let this program read the boot settings of the PC" + (why != null ? " (" + why + ")" : "") +
                               ". Nothing was changed.\r\n\r\n", "Windows no ha dejado que este programa lea la configuración de " +
                               "arranque del PC" + (why != null ? " (" + why + ")" : "") + ". No se ha cambiado nada.\r\n\r\n") + KeyText(key);
                case Outcome.NoHades:
                    return L.T("No drive with LibreELEC Hades is connected to this PC. Plug in the drive with LibreELEC Hades and try " +
                               "again.", "No hay ninguna unidad con LibreELEC Hades conectada a este PC. Conecta la unidad con LibreELEC " +
                               "Hades y vuelve a intentarlo.");
                case Outcome.TurnedOff:
                    return L.T("The boot menu of the PC has the drive with LibreELEC Hades (" + chosen.Hades.Shown + ", entry “" +
                               chosen.Entry.Description + "”), but it is turned off in the settings of the PC (its BIOS). Nothing was " +
                               "changed: this program does not change those settings.\r\n\r\n",
                               "El menú de arranque del PC tiene la unidad con LibreELEC Hades (" + chosen.Hades.Shown + ", entrada «" +
                               chosen.Entry.Description + "»), pero está desactivada en la configuración del PC (su BIOS). No se ha " +
                               "cambiado nada: este programa no cambia esa configuración.\r\n\r\n") + KeyText(key);
                default:
                    return L.T("The PC does not list the drive with LibreELEC Hades in its boot menu right now. Many PCs list a USB " +
                               "drive only if it was plugged in when the PC started, and an update of the BIOS can clear the list: " +
                               "leave the drive plugged in, restart Windows once, and try again from here. Nothing was changed.\r\n\r\n",
                               "El PC no tiene ahora la unidad con LibreELEC Hades en su menú de arranque. Muchos PC solo incluyen una " +
                               "unidad USB si estaba conectada cuando arrancaron, y una actualización de la BIOS puede vaciar la lista: " +
                               "deja la unidad conectada, reinicia Windows una vez y vuelve a intentarlo desde aquí. No se ha cambiado " +
                               "nada.\r\n\r\n") + KeyText(key);
            }
        }

        // ---- the shortcut on the desktop

        public static string ShortcutPath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                    L.T("Restart into LibreELEC Hades.lnk", "Reiniciar en LibreELEC Hades.lnk"));
            }
        }

        // whether the desktop has that shortcut already (it is only looked for: then no box offers to make it again)
        public static bool ShortcutMade { get { return File.Exists(ShortcutPath); } }

        // What a box for the shortcut says, in the window and in the question
        public static string ShortcutText(bool made)
        {
            return made ? L.T("The desktop has a shortcut for this already", "El escritorio ya tiene un acceso directo para esto")
                        : L.T("Also put a shortcut for this on the desktop", "Poner también un acceso directo en el escritorio");
        }

        [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
        class ShellLink { }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("000214F9-0000-0000-C000-000000000046")]
        interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int size, IntPtr data, uint flags);
            void GetIDList(out IntPtr list);
            void SetIDList(IntPtr list);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int size);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder dir, int size);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder args, int size);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
            void GetHotkey(out short key);
            void SetHotkey(short key);
            void GetShowCmd(out int show);
            void SetShowCmd(int show);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int size, out int icon);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int icon);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
            void Resolve(IntPtr window, uint flags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
        }

        // A shortcut that starts this program, where it is now, with the switch. true when it was written.
        public static bool MakeShortcut(string path, string program, out string why)
        {
            why = null;
            try
            {
                var link = (IShellLinkW)new ShellLink();
                link.SetPath(program);
                link.SetArguments(Switch);
                link.SetWorkingDirectory(Path.GetDirectoryName(program));
                link.SetDescription(L.T("Restarts the PC into LibreELEC Hades, once (asks for administrator rights)",
                                        "Reinicia el PC en LibreELEC Hades, una vez (pide permiso de administrador)"));
                link.SetIconLocation(program, 0);
                ((IPersistFile)link).Save(path, true);
                Marshal.ReleaseComObject(link);
                return File.Exists(path);
            }
            catch (Exception e)
            {
                why = e.Message;
                return false;
            }
        }

        // ---- the flow

        // From the window (owner) or from the shortcut (owner null): looks, asks, sets BootNext and restarts. shortcutWanted: the box
        // for a shortcut on the desktop starts ticked (the user ticked it in the window; it is off until then). The shortcut is made only
        // when the PC is restarted with that box ticked. Gives back what that box was left at (shortcutWanted when it was not offered).
        public static bool Ask(IWin32Window owner, bool shortcutRun, bool shortcutWanted = false)
        {
            string maker;
            bool laptop;
            Cursor.Current = Cursors.WaitCursor;
            Maker(out maker, out laptop);
            string key = MenuKey(maker, laptop);
            bool uefi = Native.Uefi;
            string why = null;
            Firmware fw = uefi ? ReadFirmware(out why) : null;
            List<HadesDisk> hades = null;
            if (fw != null)
                try { hades = HadesDisks(); }
                catch (Exception e) { why = e.Message; }
            Found chosen;
            List<Found> all;
            Outcome outcome = Decide(uefi, fw, hades, out chosen, out all);
            Cursor.Current = Cursors.Default;
            Log.Line("restart into Hades: maker \"" + maker + "\"" + (laptop ? " (portable)" : "") + ", UEFI " + uefi +
                     (fw != null ? ", boot order " + Order(fw.Order) + ", this start Boot" + (fw.Current >= 0 ? fw.Current.ToString("X4") : "?") +
                                   (fw.Next >= 0 ? ", BootNext " + fw.Next.ToString("X4") + " already" : "")
                                 : ", firmware not read" + (why != null ? " (" + why + ")" : "")) +
                     "; Hades on " + (hades == null ? "?" : hades.Count + " disk(s)" + Places(hades)) + "; " + outcome +
                     (chosen != null ? ": " + chosen.Entry : "") + (shortcutWanted ? "; the box for a shortcut was ticked in the window" : ""));
            if (fw != null)
                foreach (KeyValuePair<int, Entry> e in fw.Entries)
                    Log.Line("    Boot" + e.Key.ToString("X4") + (e.Value == null ? " (broken)" : " \"" + e.Value.Description + "\"" +
                             (e.Value.Active ? "" : " off") + (e.Value.BootCategory ? "" : " app") + Partitions(e.Value) +
                             (e.Value.File.Length > 0 ? " " + e.Value.File : "")));
            string text = Text(outcome, chosen, all, key, why, shortcutRun);
            using (var form = new RestartForm(text, outcome == Outcome.Ready, !shortcutRun && outcome == Outcome.Ready, shortcutWanted))
            {
                if (owner == null) form.StartPosition = FormStartPosition.CenterScreen;
                bool asked = form.ShowDialog(owner) == DialogResult.OK;
                if (form.OffersShortcut && form.ShortcutCanBeSet) shortcutWanted = form.ShortcutTicked;
                if (!asked || outcome != Outcome.Ready) return shortcutWanted;
                if (form.Shortcut)
                {
                    string where = ShortcutPath, failed;
                    bool made = MakeShortcut(where, Application.ExecutablePath, out failed);
                    Log.Line("restart into Hades: shortcut " + where + (made ? " made" : " not made: " + failed));
                }
                string notSet;
                if (!SetNext(chosen.Entry.Number, out notSet))
                {
                    Log.Line("restart into Hades: BootNext=" + chosen.Entry.Number.ToString("X4") + " not set: " + notSet);
                    Say(owner, L.T("The PC did not take it (" + notSet + "). Nothing was changed, and the PC is not restarted.\r\n\r\n",
                                   "El PC no lo ha aceptado (" + notSet + "). No se ha cambiado nada, y el PC no se reinicia.\r\n\r\n") +
                               KeyText(key));
                    return shortcutWanted;
                }
                Log.Line("restart into Hades: BootNext=" + chosen.Entry.Number.ToString("X4") + " set and read back; restarting");
                string notRestarted;
                if (!Restart(out notRestarted))
                {
                    ClearNext();
                    Log.Line("restart into Hades: Windows did not restart: " + notRestarted);
                    Say(owner, L.T("Windows did not restart (" + notRestarted + "). Nothing is left set: restart from here again when " +
                                   "you want.", "Windows no se ha reiniciado (" + notRestarted + "). No queda nada cambiado: vuelve a " +
                                   "reiniciar desde aquí cuando quieras."));
                }
            }
            return shortcutWanted;
        }

        static void Say(IWin32Window owner, string text)
        {
            using (var form = new RestartForm(text, false, false))
            {
                if (owner == null) form.StartPosition = FormStartPosition.CenterScreen;
                form.ShowDialog(owner);
            }
        }

        static string Order(List<int> order)
        {
            var sb = new StringBuilder();
            foreach (int n in order) sb.Append(sb.Length > 0 ? " " : "").Append(n.ToString("X4"));
            return sb.ToString();
        }

        static string Places(List<HadesDisk> hades)
        {
            var sb = new StringBuilder();
            foreach (HadesDisk h in hades)
                sb.Append(" [disk ").Append(h.Disk).Append(", partition ").Append(h.Partition).Append(' ').Append(h.PartitionGuid.ToString("B")).Append(']');
            return sb.ToString();
        }

        static string Partitions(Entry e)
        {
            var sb = new StringBuilder();
            foreach (HardDrive hd in e.Partitions)
                sb.Append(" HD(").Append(hd.Number).Append(',').Append(hd.SignatureType == 2 ? hd.PartitionGuid.ToString("B") : "MBR").Append(')');
            return sb.ToString();
        }

        // Started from the shortcut on the desktop: with administrator rights (Windows asks), the question, and the restart.
        public static int Run(string here)
        {
            if (!Pc.Elevated)
            {
                try
                {
                    var again = new ProcessStartInfo(Application.ExecutablePath, Switch);
                    again.UseShellExecute = true;
                    again.Verb = "runas";
                    Process.Start(again);
                }
                catch (Win32Exception)
                {
                    MessageBox.Show(L.T("Restarting into LibreELEC Hades needs the administrator permission that Windows has just asked for.",
                                        "Para reiniciar en LibreELEC Hades hace falta el permiso de administrador que Windows acaba de pedir."),
                                    Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                return 0;
            }
            Log.Open(here);
            if (!Ui.FixedScale) Ui.Scale = Ui.SystemScale();
            Ui.Apply();
            Ask(null, true);
            return 0;
        }

        // Read only (administrator): what the firmware lists and which entry starts which Hades, into <folder>\boot-entries.txt,
        // to compare with "bcdedit /enum firmware". Nothing is written to the firmware.
        public static int ListEntries(string directory)
        {
            Directory.CreateDirectory(directory);
            var sb = new StringBuilder();
            sb.AppendLine("UEFI: " + Native.Uefi + ", administrator: " + Pc.Elevated);
            string why = null;
            Firmware fw = Native.Uefi ? ReadFirmware(out why) : null;
            if (fw == null) sb.AppendLine("firmware not read" + (why != null ? ": " + why : ""));
            else
            {
                sb.AppendLine("BootOrder: " + Order(fw.Order) + "   BootCurrent: " + (fw.Current >= 0 ? fw.Current.ToString("X4") : "-") +
                              "   BootNext: " + (fw.Next >= 0 ? fw.Next.ToString("X4") : "-"));
                foreach (KeyValuePair<int, Entry> e in fw.Entries)
                    sb.AppendLine("Boot" + e.Key.ToString("X4") + (e.Value == null ? "  (broken)" : "  \"" + e.Value.Description + "\"" +
                                  (e.Value.Active ? "" : "  off") + (e.Value.BootCategory ? "" : "  application") + Partitions(e.Value) +
                                  (e.Value.File.Length > 0 ? "  " + e.Value.File : "")));
                List<HadesDisk> hades = HadesDisks();
                sb.AppendLine("Hades on" + (hades.Count == 0 ? " no disk" : Places(hades)));
                Found chosen;
                List<Found> all;
                Outcome o = Decide(true, fw, hades, out chosen, out all);
                sb.AppendLine("outcome: " + o + (chosen != null ? ", " + chosen.Entry + " on disk " + chosen.Hades.Disk : ""));
            }
            File.WriteAllText(Path.Combine(directory, "boot-entries.txt"), sb.ToString(), new UTF8Encoding(false));
            return 0;
        }
    }

    // The question before the restart, or what was found instead (then with one button)
    sealed class RestartForm : Form
    {
        readonly PillButton ok = new PillButton(), cancel = new PillButton();
        readonly Label what = new Label();
        readonly CheckBox shortcut = new ThemedCheck();
        readonly Label where = new Label();
        readonly Panel top = new Panel();
        readonly bool restarts, offers; // (what Visible says only once the form is on show)

        // ready: Windows restarts with the first button; offerShortcut: with the box for a shortcut on the desktop, which is off unless
        // shortcutWanted (ticked in the window)
        public RestartForm(string details, bool ready, bool offerShortcut, bool shortcutWanted = false)
        {
            Text = FirmwareBoot.Title;
            Font = Ui.F(13f);
            BackColor = Ui.Back;
            ForeColor = Ui.Text;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowIcon = false;
            StartPosition = FormStartPosition.CenterParent;
            AutoScaleMode = AutoScaleMode.None;
            int width = 600, inner = width - 48;
            int say = TextRenderer.MeasureText(details, Font, new Size(Ui.S(inner), 0), TextFormatFlags.WordBreak).Height;
            say = Math.Max(Ui.S(60), say + Ui.S(6));
            int y = Ui.S(56 + 18);
            top.SetBounds(0, 0, Ui.S(width), Ui.S(56));
            top.BackColor = Ui.Ink;
            top.Paint += PaintBanner;
            what.Text = details;
            what.ForeColor = Ui.Text;
            what.SetBounds(Ui.S(24), y, Ui.S(inner), say);
            y += say;
            restarts = ready;
            offers = offerShortcut;
            shortcut.Visible = offerShortcut;
            if (offerShortcut)
            {
                string folder = Path.GetDirectoryName(Application.ExecutablePath);
                // (off until the user ticks it; a shortcut that is there already shows ticked and cannot be set: nothing is made again)
                bool made = FirmwareBoot.ShortcutMade;
                shortcut.Text = FirmwareBoot.ShortcutText(made);
                shortcut.ForeColor = Ui.Text;
                shortcut.Checked = made || shortcutWanted;
                shortcut.Enabled = !made;
                shortcut.SetBounds(Ui.S(22), y + Ui.S(12), Ui.S(inner), Ui.S(26));
                y += Ui.S(12 + 26);
                where.Text = L.T("It opens this program from " + folder + ": keep that folder where it is.",
                                 "Abre este programa desde " + folder + ": deja esa carpeta donde está.");
                where.Font = Ui.F(12f);
                where.ForeColor = Ui.Soft;
                int tall = TextRenderer.MeasureText(where.Text, where.Font, new Size(Ui.S(inner - 30), 0), TextFormatFlags.WordBreak).Height;
                where.SetBounds(Ui.S(24 + 30), y, Ui.S(inner - 30), tall + Ui.S(4));
                y += tall + Ui.S(4);
            }
            where.Visible = offerShortcut;
            y += Ui.S(18);
            ClientSize = new Size(Ui.S(width), y + Ui.S(40 + 22));
            ok.Text = ready ? L.T("Restart now", "Reiniciar ahora") : L.T("OK", "Aceptar");
            ok.DialogResult = DialogResult.OK;
            ok.Font = Ui.F(14f, "semi");
            ok.BackColor = Ui.Primary;
            ok.ForeColor = Ui.PrimaryText;
            cancel.Text = L.T("Cancel", "Cancelar");
            cancel.DialogResult = DialogResult.Cancel;
            cancel.Font = Ui.F(14f);
            cancel.Outline = true;
            cancel.ForeColor = Ui.Text;
            cancel.Visible = ready;
            if (ready)
            {
                ok.SetBounds(Ui.S(width - 24 - 108 - 10 - 210), y, Ui.S(210), Ui.S(40));
                cancel.SetBounds(Ui.S(width - 24 - 108), y, Ui.S(108), Ui.S(40));
                // (a restart is not done with the Enter key)
                AcceptButton = cancel;
                CancelButton = cancel;
            }
            else
            {
                ok.SetBounds(Ui.S(width - 24 - 140), y, Ui.S(140), Ui.S(40));
                AcceptButton = ok;
                CancelButton = ok;
            }
            Controls.AddRange(new Control[] { what, shortcut, where, ok, cancel, top });
            if (ready) ActiveControl = cancel;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Ui.DarkTitleBar(Handle);
        }

        void PaintBanner(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            Ui.DrawMark(g, Ui.Sf(36), top.Height / 2f, Ui.Sf(11), Ui.Disc, true, false);
            TextRenderer.DrawText(g, FirmwareBoot.Title, Ui.F(17f, "semi"), new Rectangle(Ui.S(64), 0, top.Width - Ui.S(80), top.Height), Color.White,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
        }

        public bool Shortcut { get { return offers && shortcut.Enabled && shortcut.Checked; } }
        // what the box shows, and whether it can be set (for the boxes of the window, which say the same)
        public bool ShortcutTicked { get { return shortcut.Checked; } }
        public bool ShortcutCanBeSet { get { return shortcut.Enabled; } }

        // (for the tests)
        public string Said { get { return what.Text; } }
        public bool Restarts { get { return restarts; } }
        public bool EnterCancels { get { return AcceptButton == cancel && ActiveControl == cancel; } }
        public bool OffersShortcut { get { return offers; } }
    }
}
