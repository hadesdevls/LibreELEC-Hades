// LibreELEC Hades installer for Windows: the window of Secure Boot drawn with made-up disks (--capture), with checks of what it does.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.IO;

namespace Hades.Installer
{
    static partial class SelfTest
    {
        // The box of Secure Boot and what goes with it: an image that cannot (the box says so), one that can, the explanation that opens when
        // it is ticked (with every paragraph, and with the plain one), a Hades that has its certificate (the box starts ticked), the questions
        // before writing, and the last panel in its forms. "stick": a drive without Hades; "has": one with it.
        static void CaptureSecure(string directory, DiskInfo stick, DiskInfo has)
        {
            var data = new MainForm.TestData();
            data.Spare = 700L << 30;
            data.Disks.Add(has);
            data.Disks.Add(stick);
            data.Secure.Add(has);
            data.Look.SecureBoot = 1; data.Look.ThirdPartyCa = 0; data.Look.BitLocker = 1;
            MainForm.Test = data;
            string without = Path.Combine(directory, "sb-without.img");
            long[] unused;
            byte[] made = MakeImage(4, true, out unused, 4, true, 1);
            File.WriteAllBytes(without, made);
            File.WriteAllText(without + ".sha256", Sha(made, 0, made.Length) + "  sb-without.img\n");
            bool offWithout, texted;
            using (var form = new MainForm(without))
            {
                form.Show();
                Pump(delegate { return !form.Scanning; }, 180);
                form.SelectDisk(stick);
                Pump(delegate { return true; }, 1);
                offWithout = !form.SecureKnown && !form.SecureTicked && !form.SecureCanBeSet;
                texted = form.SecureText.Contains(L.T("this image cannot", "esta imagen no sabe"));
                Save(form, Path.Combine(directory, "20-secure-image-cannot.png"));
            }
            File.Delete(without);
            File.Delete(without + ".sha256");
            string path = captureSecureImage.Path;
            bool offered, ticksWithYes, staysWithNo, followsDisk, keepsUnticked, wording = true, askedOnce;
            bool noticeOff, noticeOn;
            int asked = 0;
            using (var form = new MainForm(path))
            {
                form.Show();
                Pump(delegate { return !form.Scanning; }, 180);
                form.SelectDisk(stick);
                Pump(delegate { return true; }, 1);
                // offered, and off to begin with (a drive that has no Hades); the notice says what Secure Boot has to be
                offered = form.SecureKnown && form.SecureCanBeSet && !form.SecureTicked;
                noticeOff = Pc.SecureBoot != 1 || form.NoticeText.Contains(L.T("Without the Secure Boot option", "Sin la opción de Secure Boot"));
                Save(form, Path.Combine(directory, "21-secure-off.png"));
                // the disk that has Hades with its certificate: the box starts ticked there (nothing was set yet), and off again on the other
                form.SelectDisk(has);
                Pump(delegate { return true; }, 1);
                followsDisk = form.SecureTicked;
                Save(form, Path.Combine(directory, "23-secure-update.png"));
                form.SelectDisk(stick);
                Pump(delegate { return true; }, 1);
                followsDisk &= !form.SecureTicked;
                // a No in the explanation leaves it as it was
                MainForm.SecureAnswer = delegate { ++asked; return false; };
                form.ClickSecure();
                staysWithNo = !form.SecureTicked && asked == 1;
                // a Yes ticks it, and the image is read through for it (the button waits for that)
                MainForm.SecureAnswer = delegate { ++asked; return true; };
                form.ClickSecure();
                Pump(delegate { return !form.Scanning; }, 180);
                Pump(delegate { return true; }, 1);
                ticksWithYes = form.SecureTicked && asked == 2;
                noticeOn = Pc.SecureBoot != 1 || form.NoticeText.Contains(L.T("Hades will start with it", "Hades arrancará con él"));
                Save(form, Path.Combine(directory, "22-secure-on.png"));
                // unticked by the user (no question for that), it stays unticked on the disk that has Hades with its certificate
                form.ClickSecure();
                keepsUnticked = !form.SecureTicked;
                form.SelectDisk(has);
                Pump(delegate { return true; }, 1);
                keepsUnticked &= !form.SecureTicked;
                form.SelectDisk(stick);
                Pump(delegate { return true; }, 1);
                MainForm.SecureAnswer = null;
                askedOnce = asked == 2;
                // the questions before writing say what Secure Boot does
                using (ConfirmForm c = MainForm.AskErase(stick, L.T("Secure Boot: the installer makes a key for this PC and, when it is done, shows you a 4-digit code to type on the blue screen of the first start.",
                                                                    "Secure Boot: el instalador crea una clave para este PC y, al terminar, te muestra un código de 4 cifras para escribir en la pantalla azul del primer arranque.")))
                {
                    c.Show();
                    Save(c, Path.Combine(directory, "24-secure-confirm-erase.png"));
                    wording &= c.Said.Contains("4-digit") || c.Said.Contains("4 cifras");
                }
                using (ConfirmForm c = MainForm.AskUpdate(has, "image.img.gz", false, L.T("Secure Boot is turned off for this update: the boot of Hades goes back to the one without Secure Boot, and the PC needs Secure Boot turned off in the BIOS until you update with the option ticked again.",
                                                                                          "Secure Boot está desmarcado en esta actualización: el arranque de Hades vuelve a ser el de sin Secure Boot, y el PC necesita Secure Boot desactivado en la BIOS hasta que actualices con la opción marcada otra vez.")))
                {
                    c.Show();
                    Save(c, Path.Combine(directory, "25-secure-confirm-update-off.png"));
                    wording &= c.Said.Contains("Secure Boot");
                }
                // the explanation itself: with every paragraph, and the plain one
                var all = new Enroll.Look();
                all.SecureBoot = 1; all.ThirdPartyCa = 0; all.BitLocker = 1;
                var plain = new Enroll.Look();
                plain.SecureBoot = 1; plain.ThirdPartyCa = 1; plain.BitLocker = 0;
                using (ConfirmForm c = MainForm.AskSecureForm(all))
                {
                    c.Show();
                    Save(c, Path.Combine(directory, "26-secure-explain-all.png"));
                    wording &= c.Said.Contains("BitLocker") && c.Said.Contains("3rd Party") && c.Said.Contains("MOK") && c.ActionReady;
                }
                using (ConfirmForm c = MainForm.AskSecureForm(plain))
                {
                    c.Show();
                    Save(c, Path.Combine(directory, "27-secure-explain.png"));
                    wording &= !c.Said.Contains("BitLocker") && !c.Said.Contains("3rd Party") && c.Said.Contains("4");
                }
                // the last panel: the code, the key by hand, the key that was kept, and no Secure Boot
                var withCode = new SbJob();
                withCode.Enabled = true; withCode.NewKey = true; withCode.Enrolled = true; withCode.Code = "4821";
                var byHand = new SbJob();
                byHand.Enabled = true; byHand.NewKey = true; byHand.Manual = true;
                var kept = new SbJob();
                kept.Enabled = true; kept.KeyKept = true;
                form.ShowDone(stick, Place.Usb, false, null, withCode);
                Save(form, Path.Combine(directory, "28-done-secure-code.png"));
                form.ShowDone(stick, Place.Internal, false, null, byHand);
                Save(form, Path.Combine(directory, "29-done-secure-by-hand.png"));
                form.ShowDone(has, Place.Usb, true, null, kept);
                Save(form, Path.Combine(directory, "30-done-secure-kept.png"));
                form.ShowDone(stick, Place.Usb, false, null, null);
                Save(form, Path.Combine(directory, "31-done-no-secure.png"));
                form.HideDone();
                bool ok = offWithout && texted && offered && noticeOff && noticeOn && staysWithNo && ticksWithYes && followsDisk && keepsUnticked && askedOnce && wording;
                if (!ok) ++captureFailures;
                File.WriteAllText(Path.Combine(directory, "20-secure-state.txt"),
                                  (ok ? "ok:   " : "FAIL: ") + "the box of Secure Boot. An image that cannot: off, disabled, and says so: " + (offWithout && texted) +
                                  "; one that can: offered and off for a drive without Hades: " + offered + "; the notice of this PC says what Secure Boot has to be: " +
                                  (noticeOff && noticeOn) + "; a No leaves it unticked: " + staysWithNo + "; a Yes ticks it and the image is read through: " + ticksWithYes +
                                  "; the Hades that has its certificate starts ticked: " + followsDisk + "; unticked by the user it stays so: " + keepsUnticked +
                                  "; the explanation is asked once for each tick: " + askedOnce + "; the texts say what they have to: " + wording + "\r\n");
            }
        }
    }
}
