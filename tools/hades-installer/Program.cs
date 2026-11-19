// LibreELEC Hades installer for Windows: start.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace Hades.Installer
{
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            string here = Path.GetDirectoryName(Application.ExecutablePath);
            Application.EnableVisualStyles();
            Ui.Dark = Ui.WindowsIsDark();
            Application.SetCompatibleTextRenderingDefault(false);
            // The words of the command line. None of them asks for administrator rights by itself. (The ones that have a
            // first, Spanish, name take it too.)
            string test = args.Length > 0 ? args[0] : "";
            // "Restart into LibreELEC Hades", from its shortcut on the desktop (asks for administrator rights itself), and what
            // it reads of the firmware (read only, as administrator)
            if (args.Length >= 1 && FirmwareBoot.IsSwitch(test)) return FirmwareBoot.Run(here);
            if (args.Length >= 2 && test == "--boot-entries") return FirmwareBoot.ListEntries(args[1]);
            // how the disks of this PC are classified, into a text file (nothing is read from the disks themselves)
            if (args.Length >= 2 && (test == "--list" || test == "--listar"))
                return Disks.ListToFile(args[1], args.Length > 2 ? args[2] : null);
#if HADES_TESTS
            // The tests, only in LibreELEC-Hades-Installer-tests.exe (build.ps1 builds it from the same sources with
            // /define:HADES_TESTS): their data (a throw-away key, certificates and small disk images) is no part of the
            // program that is published.
            if (args.Length >= 2 && test == "--capture-restart") return SelfTest.CaptureRestart(args[1]);
            if (args.Length >= 2 && (test == "--selftest" || test == "--autoprueba"))
                return SelfTest.Run(args[1], args.Length > 2 ? args[2] : null);
            if (args.Length >= 2 && (test == "--capture" || test == "--captura"))
                return SelfTest.Capture(args[1], args.Length > 2 ? args[2] : null);
            if (args.Length >= 3 && (test == "--vhd-test" || test == "--prueba-vhd")) return VhdTest.Run(args[1], args[2]);
            if (args.Length >= 4 && (test == "--simulate-update" || test == "--simular-actualizar"))
            {
                // after the three that are needed, in any order: "internal" (a disk inside the PC), "diagnostics", and
                // "keep=<file>": the system partition that the update replaces, from which the folder hades-diag is
                // kept. Any other word is a mistake: nothing is written for it.
                bool inside = false, diagnostics = false;
                string old = null;
                for (int i = 4; i < args.Length; ++i)
                {
                    if (args[i] == "internal" || args[i] == "interno") inside = true;
                    else if (args[i] == "diagnostics" || args[i] == "diagnostico") diagnostics = true;
                    else if (args[i].StartsWith("keep=", StringComparison.Ordinal) && args[i].Length > 5) old = args[i].Substring(5);
                    else return 2;
                }
                return SelfTest.SimulateUpdate(args[1], args[2], args[3], inside, diagnostics, old);
            }
            if (args.Length >= 3 && test == "--simulate-secureboot")
            {
                // after the two that are needed, in any order: "code=NNNN" (the code of the blue screen), "time=YYMMDDHHMMSSZ" (the signing time),
                // "internal" (for a disk inside the PC), "diagnostics", and "testkey" (the throw-away key of the tests, for comparing with sbsign).
                // Writes the files into the folder; no disk and no firmware variable is touched.
                string code = null, time = null;
                bool testKey = false, inside = false, diagnostics = false;
                for (int i = 3; i < args.Length; ++i)
                {
                    if (args[i].StartsWith("code=", StringComparison.Ordinal)) code = args[i].Substring(5);
                    else if (args[i].StartsWith("time=", StringComparison.Ordinal)) time = args[i].Substring(5);
                    else if (args[i] == "testkey") testKey = true;
                    else if (args[i] == "internal") inside = true;
                    else if (args[i] == "diagnostics") diagnostics = true;
                    else return 2;
                }
                return SelfTest.SimulateSecureBoot(args[1], args[2], code, time, testKey, inside, diagnostics);
            }
            if (args.Length == 4 && test == "--simulate-update-secureboot") return SelfTest.SimulateUpdateSecureBoot(args[1], args[2], args[3]);
            if (args.Length >= 2 && test == "--make-test-image") return SelfTest.MakeTestImage(args[1]);
#else
            // In the program that is published, the words of the tests say where the tests are, and nothing is done.
            if (Array.IndexOf(TestWords, test) >= 0)
            {
                MessageBox.Show(L.T("\"" + test + "\" is one of the tests of the installer, and the tests are not in this program: " +
                                    "they are in LibreELEC-Hades-Installer-tests.exe, which is built from the same source code " +
                                    "(build.ps1, in the folder tools/hades-installer of the project). Nothing has been done.",
                                    "\"" + test + "\" es una de las pruebas del instalador, y las pruebas no están en este programa: " +
                                    "están en LibreELEC-Hades-Installer-tests.exe, que se compila del mismo código fuente " +
                                    "(build.ps1, en la carpeta tools/hades-installer del proyecto). No se ha hecho nada."),
                                L.T("LibreELEC Hades Installer", "Instalador de LibreELEC Hades"), MessageBoxButtons.OK,
                                MessageBoxIcon.Information);
                return 2;
            }
#endif
            if (!Pc.Elevated)
            {
                // Writing a disk needs administrator rights: start again with them (Windows asks).
                try
                {
                    var again = new ProcessStartInfo(Application.ExecutablePath);
                    again.UseShellExecute = true;
                    again.Verb = "runas";
                    Process.Start(again);
                }
                catch (Win32Exception)
                {
                    MessageBox.Show(L.T("Writing to a disk needs the administrator permission that Windows has just asked for. " +
                                        "Without it, nothing can be installed.",
                                        "Para escribir en un disco hace falta el permiso de administrador que Windows acaba de " +
                                        "pedir. Sin él no se puede instalar."),
                                    L.T("LibreELEC Hades Installer", "Instalador de LibreELEC Hades"), MessageBoxButtons.OK,
                                    MessageBoxIcon.Information);
                }
                return 0;
            }
            // (only here, in the window: the tests never get to write a variable of the firmware)
            WindowsFirmware.Allowed = true;
            Log.Open(here);
            Log.Line("started; Secure Boot " + Pc.SecureBoot + ", UEFI " + Native.Uefi);
            Application.Run(new MainForm(MainForm.FindImage(here)));
            return 0;
        }
#if !HADES_TESTS

        // The words of the tests, which only LibreELEC-Hades-Installer-tests.exe takes
        static readonly string[] TestWords = { "--selftest", "--autoprueba", "--capture", "--captura", "--capture-restart",
                                               "--vhd-test", "--prueba-vhd", "--simulate-update", "--simular-actualizar",
                                               "--simulate-secureboot", "--simulate-update-secureboot", "--make-test-image" };
#endif
    }
}
