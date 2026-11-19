// LibreELEC Hades installer for Windows: what Explorer shows in the properties of the program.
// SPDX-License-Identifier: GPL-2.0-or-later
using System.Reflection;

#if HADES_TESTS
[assembly: AssemblyTitle("LibreELEC Hades Installer, with its tests")]
[assembly: AssemblyDescription("The LibreELEC Hades installer with its self-tests, for whoever works on it (not the program that is published)")]
#else
[assembly: AssemblyTitle("LibreELEC Hades Installer")]
[assembly: AssemblyDescription("Installs, updates and removes LibreELEC Hades (Dolby Vision for your PC) on a USB drive or a secondary disk")]
#endif
[assembly: AssemblyCompany("LibreELEC Hades")]
[assembly: AssemblyProduct("LibreELEC Hades Installer")]
[assembly: AssemblyCopyright("Copyright (C) 2026 Hades. GPL-2.0-or-later")]
[assembly: AssemblyVersion("1.1.0.0")]
[assembly: AssemblyFileVersion("1.1.0.0")]
