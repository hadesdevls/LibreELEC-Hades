// LibreELEC Hades installer for Windows: the part of the test of the virtual disk (--vhd-test) that is about Secure Boot. It installs with the box ticked on the
// virtual disk and then updates it, and reads back from the disk, with the readers of the tests, what was written. The firmware of this PC is never written
// here: the request to enrol the key goes to a firmware in memory, and writing to the real one is not allowed in a test (WindowsFirmware.Allowed).
// It needs administrator rights like the rest of the test, so it has not been run by the person who wrote it: the user runs it (test-installer.cmd).
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;

namespace Hades.Installer
{
    static partial class VhdTest
    {
        // plan: the image, read through. Runs on the disk of the test (Find()); the disk has Hades on it (the blocks before this one leave it there) or is empty:
        // the install erases it in any case, as the test of the whole disk does.
        static void SecureBoot(ImagePlan plan, Action<string, bool> check, Action<string> say, ref int notRun)
        {
            if (plan.SecureState != 1 || plan.Sb == null || !plan.Sb.Known)
            {
                ++notRun;
                say("NOT RUN: this image does not have /secureboot (" + (plan.Sb == null ? "not read through" : plan.Sb.Why) + "): nothing about Secure Boot on the disk.");
                return;
            }
            check("the firmware of this PC is not written by this test", !WindowsFirmware.Allowed);
            var fake = new FakeFirmware();
            var install = new SbJob();
            install.Enabled = true;
            install.Firmware = fake;
            DiskInfo disk = Find();
            Operations.InstallWhole(disk, plan, Place.Internal, false, Quiet, install);
            check("Secure Boot, whole disk: the request to enrol went to the firmware in memory and not to the one of this PC (three variables, the code kept)",
                  install.Enrolled && !install.Manual && install.Code != null && install.Code.Length == 4 && fake.Vars.Count == 3 && Enroll.Pending(fake));
            byte[] cert = install.Cert;
            DiskInfo now = Find();
            PartitionInfo system, storage;
            bool has = Disks.FindHades(now, out system, out storage);
            check("Secure Boot, whole disk: Hades is on the disk", has);
            if (!has || cert == null) return;
            string problem;
            string signed1 = Read(now, system, storage, cert, plan, out problem);
            check("Secure Boot, whole disk, read back from the disk: the FATs are alike and sound, bootx64.efi is shim, mmx64.efi is MokManager, grubx64.efi and KERNEL are signed " +
                  "with the key of the PC, HADES-SB.CER is its certificate, and the slot of the storage holds the key" + (problem.Length > 0 ? " (" + problem + ")" : ""),
                  problem.Length == 0);
            // an update of that Hades with the box ticked: the key of the disk is read from its slot, the certificate from its system partition, and nothing is enrolled again
            var update = new SbJob();
            update.Enabled = true;
            update.Firmware = new FakeFirmware();
            Find();
            Operations.Update(Find(), plan, Place.Internal, false, Quiet, false, update);
            check("Secure Boot, update: the key of the disk is kept (read from its slot), its certificate is the one that was there, and nothing is asked of the firmware",
                  update.KeyKept && !update.NewKey && update.Code == null && !update.Enrolled && ((FakeFirmware)update.Firmware).Log.Count == 0 &&
                  update.Cert != null && HexOf(update.Cert) == HexOf(cert));
            now = Find();
            has = Disks.FindHades(now, out system, out storage);
            string after = has ? Read(now, system, storage, cert, plan, out problem) : "";
            check("Secure Boot, update, read back from the disk: all as after the install, with the same key and certificate" + (problem.Length > 0 ? " (" + problem + ")" : ""),
                  has && problem.Length == 0);
            // an update without the box: the boot goes back to GRUB alone, the slot keeps the key, nothing is asked of the firmware
            Find();
            Operations.Update(Find(), plan, Place.Internal, false, Quiet, false, null);
            now = Find();
            has = Disks.FindHades(now, out system, out storage);
            byte[] gx = null, bx = null;
            SbSlot slot = null;
            if (has)
            {
                using (FileStream raw = Native.OpenDisk(now.Number, false))
                {
                    var view = new DiskSlice(raw, system.Offset, system.Size);
                    Func<long, int, byte[]> read = DiagFolder.Reader(view, system.Size);
                    bx = SelfTest.ReadFatFile(read, system.Size, "EFI/BOOT/bootx64.efi");
                    gx = SelfTest.ReadFatFile(read, system.Size, "EFI/BOOT/grubx64.efi");
                }
                slot = SbDisk.SlotOnDisk(now.Number, storage);
            }
            check("Secure Boot, an update without the box: /EFI/BOOT/bootx64.efi is GRUB as the image has it (not shim), there is no grubx64.efi and no HADES-SB.CER, and the key is still in the slot",
                  has && bx != null && HexOf(bx) == HexOf(plan.Sb.GrubBytes) && gx == null && SbDisk.CertOnDisk(now.Number, system) == null && slot != null && slot.Valid && !slot.Empty);
            say("   Secure Boot, read back from the disk: after the install, " + signed1 + "; after the update with the box, " + (after.Length > 0 ? after : "no Hades on the disk"));
        }

        static string HexOf(byte[] b) { return Words.Hex(b); }

        // What is on the disk, read through the physical drive: "" when it is all as it has to be, in problem what is not.
        static string Read(DiskInfo now, PartitionInfo system, PartitionInfo storage, byte[] cert, ImagePlan plan, out string problem)
        {
            problem = "";
            using (FileStream raw = Native.OpenDisk(now.Number, false))
            {
                var view = new DiskSlice(raw, system.Offset, system.Size);
                Func<long, int, byte[]> read = DiagFolder.Reader(view, system.Size);
                string sound = SelfTest.FatProblems(read, system.Size);
                if (sound != null) { problem = sound; return "not sound"; }
                byte[] bx = SelfTest.ReadFatFile(read, system.Size, "EFI/BOOT/bootx64.efi"), mx = SelfTest.ReadFatFile(read, system.Size, "EFI/BOOT/mmx64.efi");
                byte[] gx = SelfTest.ReadFatFile(read, system.Size, "EFI/BOOT/grubx64.efi"), kx = SelfTest.ReadFatFile(read, system.Size, "KERNEL");
                byte[] cx = SelfTest.ReadFatFile(read, system.Size, "HADES-SB.CER");
                if (bx == null || HexOf(bx) != HexOf(plan.Sb.ShimBytes)) problem += " bootx64.efi is not shim;";
                if (mx == null || HexOf(mx) != HexOf(plan.Sb.MmBytes)) problem += " mmx64.efi is not MokManager;";
                if (gx == null || Authenticode.Verify(gx, cert) != null) problem += " grubx64.efi is not signed with the certificate;";
                if (kx == null || Authenticode.Verify(kx, cert) != null) problem += " KERNEL is not signed with the certificate;";
                if (cx == null || HexOf(cx) != HexOf(cert)) problem += " HADES-SB.CER is not the certificate;";
                SbSlot slot = SbSlot.Read(DiagFolder.Reader(new DiskSlice(raw, storage.Offset, storage.Size), storage.Size), storage.Size);
                if (!slot.Valid || slot.KeyPem == null) problem += " the slot has no key;";
                else
                {
                    using (SbKey key = SbKey.FromPem(slot.KeyPem))
                        if (SbCert.Check(cert, key) != null) problem += " the key of the slot is not the certificate's;";
                }
            }
            problem = problem.Trim();
            return problem.Length == 0 ? "as it has to be" : "wrong";
        }
    }
}
