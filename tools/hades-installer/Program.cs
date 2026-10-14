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
            Application.SetCompatibleTextRenderingDefault(false);
            // the tests: none of them asks for administrator rights by itself. (Each has its first, Spanish, name too.)
            string test = args.Length > 0 ? args[0] : "";
            if (args.Length >= 2 && (test == "--selftest" || test == "--autoprueba"))
                return SelfTest.Run(args[1], args.Length > 2 ? args[2] : null);
            if (args.Length >= 2 && (test == "--capture" || test == "--captura"))
                return SelfTest.Capture(args[1], args.Length > 2 ? args[2] : null);
            if (args.Length >= 2 && (test == "--list" || test == "--listar"))
                return SelfTest.ListDisks(args[1], args.Length > 2 ? args[2] : null);
            if (args.Length >= 3 && (test == "--vhd-test" || test == "--prueba-vhd")) return VhdTest.Run(args[1], args[2]);
            if (args.Length >= 4 && (test == "--simulate-update" || test == "--simular-actualizar"))
            {
                // after the three that are needed, in any order: "internal" (a disk inside the PC) and "diagnostics".
                // Any other word is a mistake: nothing is written for it.
                bool inside = false, diagnostics = false;
                for (int i = 4; i < args.Length; ++i)
                {
                    if (args[i] == "internal" || args[i] == "interno") inside = true;
                    else if (args[i] == "diagnostics" || args[i] == "diagnostico") diagnostics = true;
                    else return 2;
                }
                return SelfTest.SimulateUpdate(args[1], args[2], args[3], inside, diagnostics);
            }
            if (args.Length >= 2 && test == "--make-test-image") return SelfTest.MakeTestImage(args[1]);
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
                                    L.T("LibreELEC Hades · Installer", "LibreELEC Hades · Instalador"), MessageBoxButtons.OK,
                                    MessageBoxIcon.Information);
                }
                return 0;
            }
            Log.Open(here);
            Log.Line("started; Secure Boot " + Pc.SecureBoot + ", UEFI " + Native.Uefi);
            Application.Run(new MainForm(MainForm.FindImage(here)));
            return 0;
        }
    }
}
