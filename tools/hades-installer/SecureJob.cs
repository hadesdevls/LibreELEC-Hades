// LibreELEC Hades installer for Windows: the Secure Boot choice of one install or update, from the box of the window to the request to
// the firmware: which key is used (a new one made here, or the one that the Hades of the disk has in its key slot), what is built from it,
// and, once everything has been written and read back, the request that makes the blue screen of shim offer the key.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;

namespace Hades.Installer
{
    // The key of this Hades cannot be used as it is: Kind 1, it is not in its storage but a new one could be made (the user is asked); Kind 2,
    // nothing is changed on the disk and the update has to go on without Secure Boot, or be done from inside Hades.
    sealed class SecureKeyException : Exception
    {
        public int Kind;
        public SecureKeyException(int kind, string message) : base(message) { Kind = kind; }
    }

    // A read-only window on a part of a disk (or of a file): what is read at 0 is the first byte of the part.
    sealed class DiskSlice : Stream
    {
        readonly Stream inner;
        readonly long start, length;
        long position;
        public DiskSlice(Stream inner, long start, long length) { this.inner = inner; this.start = start; this.length = length; }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return true; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { return length; } }
        public override long Position { get { return position; } set { position = value; } }
        public override void Flush() { }
        public override long Seek(long offset, SeekOrigin origin)
        {
            position = origin == SeekOrigin.Begin ? offset : origin == SeekOrigin.Current ? position + offset : length + offset;
            return position;
        }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
        public override int Read(byte[] buffer, int offset, int count)
        {
            if (position < 0 || position >= length) return 0;
            count = (int)Math.Min(count, length - position);
            inner.Seek(start + position, SeekOrigin.Begin);
            int read = inner.Read(buffer, offset, count);
            position += Math.Max(read, 0);
            return read;
        }
    }

    static class SbDisk
    {
        public const string CertName = "HADES-SB.CER";

        // The certificate in the root directory of a system partition of Hades (HADES-SB.CER), read through "read"; null: it has none, or it is
        // not one DER certificate. The partition is not changed.
        public static byte[] Cert(Func<long, int, byte[]> read, long size)
        {
            try
            {
                string why;
                Fat16 f = Fat16.Open(read, size, out why);
                if (f == null) return null;
                DirEnt e = SbDir.Find(SbDir.List(f.Root), CertName);
                if (e == null || e.Folder || e.Size < 100 || e.Size > 16384) return null;
                int need = (int)((e.Size + f.ClusterBytes - 1) / f.ClusterBytes);
                List<int> chain = f.Chain(e.First, need);
                if (chain == null || chain.Count != need) return null;
                var bytes = new byte[e.Size];
                for (int i = 0; i < chain.Count; ++i)
                {
                    byte[] c = f.Cluster(chain[i]);
                    Buffer.BlockCopy(c, 0, bytes, i * f.ClusterBytes, (int)Math.Min(f.ClusterBytes, bytes.Length - i * f.ClusterBytes));
                }
                Der.Node top = Der.Read(bytes, 0, bytes.Length);
                return top.Tag == 0x30 && top.End == bytes.Length ? bytes : null;
            }
            catch (Exception) { return null; }
        }

        // From a disk of this PC, read-only through the physical drive.
        public static byte[] CertOnDisk(int disk, PartitionInfo system)
        {
            try
            {
                using (FileStream raw = Native.OpenDisk(disk, false))
                    return Cert(DiagFolder.Reader(new DiskSlice(raw, system.Offset, system.Size), system.Size), system.Size);
            }
            catch (Exception) { return null; }
        }

        public static SbSlot SlotOnDisk(int disk, PartitionInfo storage)
        {
            using (FileStream raw = Native.OpenDisk(disk, false))
                return SbSlot.Read(DiagFolder.Reader(new DiskSlice(raw, storage.Offset, storage.Size), storage.Size), storage.Size);
        }
    }

    sealed class SbJob : IDisposable
    {
        public bool Enabled;               // the box was ticked
        public bool AllowNewKey;           // an update: a new key may be made where the Hades of the disk has its certificate but not its key
#pragma warning disable 649 // (set only by the tests, which the published program does not carry: there they stay null)
        public string SigningTime;         // for the tests: the time that the signatures say
        public IFirmware Firmware;         // for the tests: a firmware in memory; null: the one of this PC (only where WindowsFirmware.Allowed)
#pragma warning restore 649

        // what came of it
        public byte[] Cert;
        public bool NewKey;                // a key was made (or its certificate renewed): it has to be enrolled
        public bool KeyKept;               // the key of the disk was used and its certificate is the one that is enrolled: nothing to enrol
        public string Code;                // the four digits that shim asks for; null when none was set
        public bool Enrolled;              // the three variables are in the firmware, read back
        public bool Manual;                // they could not be written: the key is enrolled from the disk, from the blue screen
        public string Note = "";           // for the log
        public SbKey Key;

        string signingTime { get { return SigningTime ?? Authenticode.Now(); } }

        // An install: a new key, a certificate, and everything that the image needs, with the key slot of the image's storage written.
        public SbBuild PrepareInstall(ImagePlan plan)
        {
            Key = SbKey.Generate();
            if (!Key.Ephemeral) throw new InvalidOperationException("the key of this PC is kept by a name: it is not used");
            Cert = SbCert.Make(Key, DateTime.UtcNow, null);
            NewKey = true;
            Code = Mok.NewCode();
            SbBuild b = SecureBoot.Build(plan, Key, Cert, true, signingTime);
            Log.Line("secure boot, new key " + Key.Short() + ": " + b.Summary);
            return b;
        }

        // An update. oldCert: HADES-SB.CER of the system partition that is replaced (null: it has none); slot: the key slot of the storage that
        // is kept. writeSlot: the key has to be written into the slot of the disk, which is empty. See SecureKeyException.
        public SbBuild PrepareUpdate(ImagePlan plan, byte[] oldCert, SbSlot slot, out bool writeSlot)
        {
            writeSlot = false;
            if (slot == null || !slot.Valid)
                throw new SecureKeyException(2, L.T(
                    "The storage of this Hades does not have its key slot yet. Start Hades once with this version, shut it down, and come back to update " +
                    "with Secure Boot. Nothing was changed.",
                    "El almacenamiento de este Hades todavía no tiene su hueco para la clave. Arranca Hades una vez con esta versión, apágalo y vuelve a " +
                    "actualizar con Secure Boot. No se ha cambiado nada.") + (slot != null && slot.Why != null ? " (" + slot.Why + ")" : ""));
            if (oldCert != null && slot.KeyPem != null)
            {
                SbKey old = SbKey.FromPem(slot.KeyPem);
                string bad = SbCert.Check(oldCert, old);
                if (bad != null)
                {
                    old.Dispose();
                    throw new SecureKeyException(2, L.T(
                        "The key that this Hades keeps is not the one of its certificate (" + bad + "). Nothing was changed. Untick Secure Boot to update " +
                        "without it, or update from inside Hades.",
                        "La clave que guarda este Hades no es la de su certificado (" + bad + "). No se ha cambiado nada. Desmarca Secure Boot para " +
                        "actualizar sin él, o actualiza desde dentro de Hades."));
                }
                Key = old;
                Cert = oldCert;
                KeyKept = true;
                Log.Line("secure boot, update: the key of the disk (" + old.Short() + ") is kept; its certificate is already enrolled");
            }
            else if (slot.KeyPem != null)
            {
                // a key that an earlier try put in the slot, with no certificate on the system partition: it is used, with a certificate of its own
                Key = SbKey.FromPem(slot.KeyPem);
                Cert = SbCert.Make(Key, DateTime.UtcNow, null);
                NewKey = true;
                Code = Mok.NewCode();
                Log.Line("secure boot, update: the key of the slot (" + Key.Short() + ") has no certificate on the disk: a new one is made and enrolled");
            }
            else
            {
                if (oldCert != null && !AllowNewKey)
                    throw new SecureKeyException(1, L.T(
                        "The Hades on this drive has a Secure Boot certificate, but its key is no longer in its storage. A new key can be made for it, " +
                        "which has to be enrolled again on the blue screen.",
                        "El Hades de esta unidad tiene un certificado de Secure Boot, pero su clave ya no está en su almacenamiento. Se puede hacer una " +
                        "clave nueva, que hay que volver a inscribir en la pantalla azul."));
                if (!slot.Clean)
                    throw new SecureKeyException(2, L.T(
                        "The storage of this Hades was not shut down cleanly: start Hades, shut it down, and update again. Nothing was changed.",
                        "El almacenamiento de este Hades no se apagó limpiamente: arranca Hades, apágalo y vuelve a actualizar. No se ha cambiado nada."));
                Key = SbKey.Generate();
                if (!Key.Ephemeral) throw new InvalidOperationException("the key of this PC is kept by a name: it is not used");
                Cert = SbCert.Make(Key, DateTime.UtcNow, null);
                NewKey = true;
                writeSlot = true;
                Code = Mok.NewCode();
                Log.Line("secure boot, update: a new key (" + Key.Short() + ")" + (oldCert != null ? " for a Hades whose slot was empty" : ""));
            }
            SbBuild b = SecureBoot.Build(plan, Key, Cert, false, signingTime);
            Log.Line("secure boot, update: " + b.Summary);
            return b;
        }

        // After everything was written and read back: the request that makes shim offer the key (only for a key that has to be enrolled).
        // It never throws: where the firmware does not take it, the key is enrolled from the disk instead, and it says so.
        public void Finish()
        {
            if (!NewKey) return;
            try
            {
                IFirmware fw = Firmware;
                if (fw == null)
                {
                    if (!Native.Uefi) { Manual = true; Note = "this PC does not start by UEFI: the variables are not written"; Code = null; return; }
                    fw = new WindowsFirmware();
                }
                Enroll.Result r = Enroll.Write(fw, Cert, Code);
                Enrolled = r.Written;
                Manual = !r.Written;
                if (!r.Written) Code = null;
                Note = r.Note;
                Log.Line("secure boot: " + r.Note + (r.Steps.Count > 0 ? " (" + string.Join("; ", r.Steps.ToArray()) + ")" : "") +
                         (r.Written ? "; code " + r.Code : ""));
            }
            catch (Exception e)
            {
                Manual = true;
                Code = null;
                Note = "the request to enrol the key could not be made (" + e.Message + ")";
                Log.Line("secure boot: " + Note);
            }
        }

        public void Dispose()
        {
            if (Key != null) { Key.Dispose(); Key = null; }
        }
    }
}
