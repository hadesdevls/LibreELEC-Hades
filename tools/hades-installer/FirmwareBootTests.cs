// LibreELEC Hades installer for Windows: the tests of "Restart into LibreELEC Hades" (FirmwareBoot), run by --selftest.
// The boot entries are built the way AMI firmware and Windows build them; the first set is the PC of the user as its
// "bcdedit /enum firmware" showed it on 2026-11-18 (ASUS ROG STRIX X670E-E, Hades on a USB SSD). Nothing here reads or
// writes the firmware of this PC.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Hades.Installer
{
    static partial class SelfTest
    {
        static byte[] Join(params byte[][] parts)
        {
            var m = new MemoryStream();
            foreach (byte[] p in parts) m.Write(p, 0, p.Length);
            return m.ToArray();
        }

        static byte[] U16(int v) { return new[] { (byte)(v & 0xFF), (byte)(v >> 8) }; }
        static byte[] Ucs2z(string s) { return Join(Encoding.Unicode.GetBytes(s), new byte[2]); }

        static byte[] Node(int type, int sub, byte[] payload)
        {
            return Join(new[] { (byte)type, (byte)sub }, U16(4 + payload.Length), payload);
        }

        static readonly byte[] EndNode = Node(0x7F, 0xFF, new byte[0]);

        static byte[] HdNode(uint number, ulong start, ulong size, Guid guid)
        {
            return Node(4, 1, Join(BitConverter.GetBytes(number), BitConverter.GetBytes(start), BitConverter.GetBytes(size), guid.ToByteArray(),
                                   new byte[] { 2, 2 }));
        }

        static byte[] MbrNode(uint number, ulong start, ulong size, uint signature)
        {
            return Node(4, 1, Join(BitConverter.GetBytes(number), BitConverter.GetBytes(start), BitConverter.GetBytes(size),
                                   BitConverter.GetBytes(signature), new byte[12], new byte[] { 1, 1 }));
        }

        static byte[] FileNode(string path) { return Node(4, 4, Ucs2z(path)); }

        static byte[] LoadOption(uint attributes, string description, byte[] path, byte[] optional)
        {
            return Join(BitConverter.GetBytes(attributes), U16(path.Length), Ucs2z(description), path, optional ?? new byte[0]);
        }

        static FirmwareBoot.Firmware Fw(int current, int[] order, params object[] entries)
        {
            var fw = new FirmwareBoot.Firmware();
            fw.Order = new List<int>(order);
            fw.Current = current;
            for (int i = 0; i + 1 < entries.Length; i += 2)
                fw.Entries[(int)entries[i]] = FirmwareBoot.Parse((int)entries[i], (byte[])entries[i + 1]);
            return fw;
        }

        static FirmwareBoot.HadesDisk Hd(int disk, Guid guid)
        {
            var h = new FirmwareBoot.HadesDisk();
            h.Disk = disk;
            h.Name = "Disk " + disk;
            h.Size = 117L << 30;
            h.Bus = "USB";
            h.Partition = 1;
            h.Offset = 4 << 20;
            h.PartitionGuid = guid;
            return h;
        }

        static void FirmwareBootChecks()
        {
            var windowsGuid = new Guid("6d0a3b8e-4c1f-4e2a-9b77-0c5e2f1a9d31");
            var hadesGuid = new Guid("c47d2a90-5e6b-4f1d-8a3c-2b9e7d6f4a12");
            var otherGuid = new Guid("0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0");
            byte[] winOptional = Join(Encoding.ASCII.GetBytes("WINDOWS\0"), BitConverter.GetBytes(1u), BitConverter.GetBytes(0x88u),
                                      Ucs2z("BCDOBJECT={9dea862c-5cdd-4e70-acc1-f32b344d4795}"));
            byte[] ami = { 0x4E, 0xAC, 0x08, 0x81, 0x11, 0x9F, 0x59, 0x4D, 0x85, 0x0E, 0xE2, 0x1A, 0x52, 0x2C, 0x59, 0xB2 };
            byte[] acpi = Node(2, 1, Join(BitConverter.GetBytes(0x0a0341d0u), new byte[4]));
            byte[] windows = LoadOption(1, "Windows Boot Manager",
                                        Join(HdNode(1, 2048, 204800, windowsGuid), FileNode(@"\EFI\Microsoft\Boot\bootmgfw.efi"), EndNode), winOptional);
            byte[] hiksemi = LoadOption(1, "UEFI: HIKSEMI, Partition 1",
                                        Join(acpi, Node(1, 1, new byte[] { 3, 8 }), Node(3, 5, new byte[] { 13, 0 }),
                                             HdNode(1, 8192, 2097152, hadesGuid), EndNode), ami);
            byte[] cd = LoadOption(1, "UEFI:CD/DVD Drive", Node(5, 1, Join(U16(3), U16(0), new byte[1])), ami);
            byte[] removable = LoadOption(1, "UEFI:Removable Device", Node(5, 1, Join(U16(5), U16(0), new byte[1])), ami);
            byte[] network = LoadOption(1, "UEFI:Network Device", Node(5, 1, Join(U16(6), U16(0), new byte[1])), ami);
            var hades = new List<FirmwareBoot.HadesDisk> { Hd(5, hadesGuid) };

            // the entries, read as they are
            FirmwareBoot.Entry w = FirmwareBoot.Parse(0, windows), u = FirmwareBoot.Parse(3, hiksemi);
            Check("restart into Hades: the entries of the PC of the user are read as the firmware keeps them (description, file, partition)",
                  w != null && w.Description == "Windows Boot Manager" && w.File == @"\EFI\Microsoft\Boot\bootmgfw.efi" && w.Active &&
                      w.BootCategory && w.Partitions.Count == 1 && w.Partitions[0].PartitionGuid == windowsGuid &&
                      u != null && u.Description == "UEFI: HIKSEMI, Partition 1" && u.File == "" && u.Partitions.Count == 1 &&
                      u.Partitions[0].PartitionGuid == hadesGuid && u.Partitions[0].Start == 8192 && u.Name == "Boot0003");

            // the PC of the user: Windows first, Hades on its USB SSD
            FirmwareBoot.Firmware asus = Fw(3, new[] { 0, 3, 1, 2, 4 }, 0, windows, 3, hiksemi, 1, cd, 2, removable, 4, network);
            FirmwareBoot.Found chosen;
            List<FirmwareBoot.Found> all;
            FirmwareBoot.Outcome o = FirmwareBoot.Decide(true, asus, hades, out chosen, out all);
            Check("restart into Hades: on the PC of the user the entry of its SSD is chosen (Boot0003), by the identifier of its partition",
                  o == FirmwareBoot.Outcome.Ready && chosen != null && chosen.Entry.Number == 3 && chosen.Hades.Disk == 5 && all.Count == 1);
            Check("... and when Hades is somewhere else (another identifier), the firmware does not list it",
                  FirmwareBoot.Decide(true, asus, new List<FirmwareBoot.HadesDisk> { Hd(5, otherGuid) }, out chosen, out all) ==
                      FirmwareBoot.Outcome.NotListed && chosen == null);
            Check("... with no Hades on this PC, and on a PC that does not start by UEFI, or whose boot order cannot be read, it says so",
                  FirmwareBoot.Decide(true, asus, new List<FirmwareBoot.HadesDisk>(), out chosen, out all) == FirmwareBoot.Outcome.NoHades &&
                      FirmwareBoot.Decide(false, asus, hades, out chosen, out all) == FirmwareBoot.Outcome.NotUefi &&
                      FirmwareBoot.Decide(true, null, hades, out chosen, out all) == FirmwareBoot.Outcome.CannotRead &&
                      FirmwareBoot.Decide(true, new FirmwareBoot.Firmware(), hades, out chosen, out all) == FirmwareBoot.Outcome.CannotRead);

            // what must not be taken
            byte[] off = LoadOption(0, "UEFI: HIKSEMI, Partition 1", Join(HdNode(1, 8192, 2097152, hadesGuid), EndNode), ami);
            byte[] app = LoadOption(0x101, "Hades as an application", Join(HdNode(1, 8192, 2097152, hadesGuid), EndNode), null);
            byte[] mbr = LoadOption(1, "UEFI: some MBR drive", Join(MbrNode(1, 8192, 2097152, 0xC47D2A90), EndNode), null);
            // (an MBR node whose 16 bytes of signature are those of the identifier of Hades: it is no GPT partition all the same)
            byte[] fake = LoadOption(1, "UEFI: an MBR node with the bytes of a GUID",
                                     Join(Node(4, 1, Join(BitConverter.GetBytes(1u), BitConverter.GetBytes(8192UL), BitConverter.GetBytes(2097152UL),
                                                          hadesGuid.ToByteArray(), new byte[] { 1, 1 })), EndNode), null);
            o = FirmwareBoot.Decide(true, Fw(0, new[] { 0, 7 }, 0, windows, 7, off), hades, out chosen, out all);
            Check("restart into Hades: an entry of Hades that is turned off in the firmware is not used, and that is said",
                  o == FirmwareBoot.Outcome.TurnedOff && chosen != null && chosen.Entry.Number == 7 && all.Count == 0);
            o = FirmwareBoot.Decide(true, Fw(0, new[] { 0, 8, 9, 14 }, 0, windows, 8, app, 9, mbr, 14, fake), hades, out chosen, out all);
            Check("... nor an application entry, nor an MBR partition whatever its numbers and its bytes", o == FirmwareBoot.Outcome.NotListed);
            o = FirmwareBoot.Decide(true, Fw(0, new[] { 0 }, 0, windows, 3, hiksemi), hades, out chosen, out all);
            Check("... nor an entry that is not in the boot order", o == FirmwareBoot.Outcome.NotListed);

            // two drives with Hades, odd names, an entry twice in the order, broken entries before the good one
            byte[] second = LoadOption(1, "UEFI OS (Kingston DataTraveler 3.0) ñ",
                                       Join(HdNode(1, 8192, 2097152, otherGuid), FileNode(@"\EFI\BOOT\BOOTX64.EFI"), EndNode), null);
            byte[] cut = Join(BitConverter.GetBytes(1u), U16(200), Ucs2z("cut short"), HdNode(1, 8192, 2097152, hadesGuid));
            byte[] noEnd = Join(BitConverter.GetBytes(1u), U16(4), Encoding.Unicode.GetBytes("no end"));
            byte[] zero = Join(BitConverter.GetBytes(1u), U16(8), Ucs2z("zero node"), new byte[] { 4, 1, 0, 0 }, EndNode);
            var both = new List<FirmwareBoot.HadesDisk> { Hd(5, hadesGuid), Hd(6, otherGuid) };
            FirmwareBoot.Firmware odd = Fw(0, new[] { 0, 10, 11, 12, 13, 13, 3, 0 }, 0, windows, 10, cut, 11, noEnd, 12, zero, 13, second, 3, hiksemi);
            o = FirmwareBoot.Decide(true, odd, both, out chosen, out all);
            Check("restart into Hades: broken entries are passed over without harm, each entry counts once, and of two drives with Hades " +
                  "the one first in the boot order is used (and the text says that there are two)",
                  odd.Entries[10] == null && odd.Entries[11] == null && odd.Entries[12] == null && o == FirmwareBoot.Outcome.Ready &&
                      chosen.Entry.Number == 13 && chosen.Hades.Disk == 6 && all.Count == 2 &&
                      chosen.Entry.Description == "UEFI OS (Kingston DataTraveler 3.0) ñ" &&
                      FirmwareBoot.Text(o, chosen, all, "F8", null, false).Contains("More than one drive"));
            Check("restart into Hades: a boot order that is not made of whole numbers is none",
                  FirmwareBoot.Numbers(new byte[] { 0, 0, 3 }) == null && FirmwareBoot.Numbers(new byte[0]) == null &&
                      FirmwareBoot.Numbers(null) == null && FirmwareBoot.Numbers(new byte[] { 3, 0, 0, 0 }).Count == 2 &&
                      FirmwareBoot.Numbers(new byte[] { 0x0C, 0x01 })[0] == 0x010C);
            Check("restart into Hades: a file in two nodes is one path, and every way of cutting an entry short is no entry",
                  FirmwareBoot.Parse(1, LoadOption(1, "x", Join(FileNode(@"\EFI\Microsoft"), FileNode(@"\Boot\bootmgfw.efi"), EndNode), null)).File ==
                      @"\EFI\Microsoft\Boot\bootmgfw.efi" &&
                      FirmwareBoot.Parse(1, new byte[7]) == null && FirmwareBoot.Parse(1, null) == null &&
                      FirmwareBoot.Parse(1, Join(BitConverter.GetBytes(1u), U16(2), Ucs2z("short path"), new byte[] { 0x7F, 0xFF })) == null);

            // the key of the boot menu
            Check("restart into Hades: the key of the boot menu by the maker of the PC (ASUS F8, its laptops Esc, MSI F11, Gigabyte F12, " +
                  "ASRock F11, HP F9), and none for one that is not known",
                  FirmwareBoot.MenuKey("ASUSTeK COMPUTER INC.", false) == "F8" && FirmwareBoot.MenuKey("ASUSTeK COMPUTER INC.", true) == "Esc" &&
                      FirmwareBoot.MenuKey("Micro-Star International Co., Ltd.", false) == "F11" &&
                      FirmwareBoot.MenuKey("Gigabyte Technology Co., Ltd.", false) == "F12" && FirmwareBoot.MenuKey("ASRock", false) == "F11" &&
                      FirmwareBoot.MenuKey("HP", true) == "F9" && FirmwareBoot.MenuKey("Hewlett-Packard", false) == "F9" &&
                      FirmwareBoot.MenuKey("Sharp", false) == null && FirmwareBoot.MenuKey("", false) == null &&
                      FirmwareBoot.MenuKey(null, false) == null && FirmwareBoot.MenuKey("To Be Filled By O.E.M.", false) == null);

            // the texts and the window
            bool texts = true;
            foreach (bool spanish in new[] { false, true })
            {
                L.Spanish = spanish;
                FirmwareBoot.Decide(true, asus, hades, out chosen, out all);
                string ready = FirmwareBoot.Text(FirmwareBoot.Outcome.Ready, chosen, all, "F8", null, false);
                texts &= ready.Contains("UEFI: HIKSEMI, Partition 1") && ready.Contains("Disk 5") && !ready.Contains("More than one");
                foreach (FirmwareBoot.Outcome k in new[] { FirmwareBoot.Outcome.NotUefi, FirmwareBoot.Outcome.CannotRead, FirmwareBoot.Outcome.NotListed })
                    texts &= FirmwareBoot.Text(k, null, null, "F8", "error 5", false).Contains("F8") &&
                             FirmwareBoot.Text(k, null, null, null, "error 5", false).Contains("ASUS F8");
                texts &= FirmwareBoot.Text(FirmwareBoot.Outcome.TurnedOff, chosen, all, "F11", null, false).Contains("F11");
                // (a shortcut that is on this desktop already shows ticked and cannot be set: nothing would be made)
                bool made = FirmwareBoot.ShortcutMade;
                using (var form = new RestartForm(ready, true, true))
                using (var ticked = new RestartForm(ready, true, true, true))
                using (var info = new RestartForm(FirmwareBoot.Text(FirmwareBoot.Outcome.NotListed, null, null, "F8", null, false), false, false))
                    texts &= form.Restarts && form.EnterCancels && form.OffersShortcut && !info.Restarts && !info.OffersShortcut &&
                             form.Said == ready && !form.Shortcut && form.ShortcutTicked == made && form.ShortcutCanBeSet == !made &&
                             ticked.Shortcut == !made && ticked.ShortcutTicked && !info.Shortcut;
            }
            L.Spanish = false;
            Check("restart into Hades: in both languages the question names the entry and the drive, Enter does not restart, the shortcut " +
                  "is offered only from the window, its box is off unless it was ticked in the window, and every text that cannot restart " +
                  "says the key of the boot menu", texts);
            UsbRemovalChecks();
        }

        // Taking Hades off a USB drive (Operations.Uninstall with Place.Usb): what decides it is HowToRemove, tested above with a
        // drive written as a USB drive; here the question, which is the one of an erase (the word has to be typed), and the drive
        // that holds this program or the image, which is not offered for it.
        static void UsbRemovalChecks()
        {
            DiskInfo stick = Make(7, "DataTraveler 3.0", 7, 29, '\0');
            stick.HasHades = true;
            bool asked = true;
            foreach (bool spanish in new[] { false, true })
            {
                L.Spanish = spanish;
                using (ConfirmForm usb = MainForm.AskRemove(stick, true))
                using (ConfirmForm inside = MainForm.AskRemove(stick))
                    asked &= !usb.ActionReady && usb.EnterActs && usb.Said.Contains("exFAT") && usb.Said.Contains(stick.Title) &&
                             !inside.Said.Contains("exFAT") && !inside.ActionReady;
            }
            L.Spanish = false;
            var keep = new Keep();
            keep.Letters.Add('H');
            DiskInfo holding = Make(8, "USB stick with the program", 7, 29, 'H');
            Check("remove from a USB drive: the question asks for the word, says that the drive is left as an exFAT one, in both " +
                  "languages; and a USB drive that holds this program or the image is not offered",
                  asked && Disks.Classify(holding, Place.Usb, 0, keep, null) != null && Disks.Classify(stick, Place.Usb, 0, new Keep(), null) == null);
            Check("remove from a USB drive: it is left with an MBR partition table, as Windows formats a removable drive (up to " +
                  "2 TiB, what MBR can hold); a disk inside the PC, and a larger USB drive, with GPT",
                  Operations.RemovalMbr(Place.Usb, 29L << 30) && Operations.RemovalMbr(Place.Usb, 1L << 41) &&
                      !Operations.RemovalMbr(Place.Usb, (1L << 41) + (1L << 20)) && !Operations.RemovalMbr(Place.Usb, 4L << 40) &&
                      !Operations.RemovalMbr(Place.Internal, 29L << 30) && !Operations.RemovalMbr(Place.Usb, 0));
        }

        // Drawn for the guide: the question and what is said when the PC does not list the drive, in both languages
        public static int CaptureRestart(string directory)
        {
            Ui.FixedScale = true;
            Ui.Scale = 1f;
            Ui.Apply();
            var windowsGuid = new Guid("6d0a3b8e-4c1f-4e2a-9b77-0c5e2f1a9d31");
            var hadesGuid = new Guid("c47d2a90-5e6b-4f1d-8a3c-2b9e7d6f4a12");
            byte[] windows = LoadOption(1, "Windows Boot Manager",
                                        Join(HdNode(1, 2048, 204800, windowsGuid), FileNode(@"\EFI\Microsoft\Boot\bootmgfw.efi"), EndNode), null);
            byte[] hiksemi = LoadOption(1, "UEFI: HIKSEMI, Partition 1", Join(HdNode(1, 8192, 2097152, hadesGuid), EndNode), null);
            FirmwareBoot.Firmware asus = Fw(3, new[] { 0, 3 }, 0, windows, 3, hiksemi);
            var disk = Hd(5, hadesGuid);
            disk.Name = "HIKSEMI HS-USB-SYNC C PRO";
            foreach (bool spanish in new[] { false, true })
            {
                L.Spanish = spanish;
                string dir = Path.Combine(directory, spanish ? "es" : "en");
                Directory.CreateDirectory(dir);
                FirmwareBoot.Found chosen;
                List<FirmwareBoot.Found> all;
                FirmwareBoot.Decide(true, asus, new List<FirmwareBoot.HadesDisk> { disk }, out chosen, out all);
                var shots = new[] {
                    new { Name = "restart-1-ask.png", Text = FirmwareBoot.Text(FirmwareBoot.Outcome.Ready, chosen, all, "F8", null, false), Ready = true, Box = true },
                    new { Name = "restart-2-from-shortcut.png", Text = FirmwareBoot.Text(FirmwareBoot.Outcome.Ready, chosen, all, "F8", null, true), Ready = true, Box = false },
                    new { Name = "restart-3-not-listed.png", Text = FirmwareBoot.Text(FirmwareBoot.Outcome.NotListed, null, null, "F8", null, false), Ready = false, Box = false },
                    new { Name = "restart-4-no-drive.png", Text = FirmwareBoot.Text(FirmwareBoot.Outcome.NoHades, null, null, "F8", null, false), Ready = false, Box = false } };
                foreach (var s in shots)
                    using (var form = new RestartForm(s.Text, s.Ready, s.Box))
                    {
                        form.StartPosition = System.Windows.Forms.FormStartPosition.Manual;
                        form.Location = new System.Drawing.Point(-4000, -4000);
                        form.ShowInTaskbar = false;
                        form.Show();
                        Save(form, Path.Combine(dir, s.Name));
                        form.Close();
                    }
            }
            L.Spanish = false;
            return 0;
        }
    }
}
