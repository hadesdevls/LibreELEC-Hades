// LibreELEC Hades installer for Windows: the three ways of putting Hades on a disk, and taking it off again.
// Nothing here asks the user anything: the window does that before it calls these. Each of them looks the
// disk up again and refuses when it is not the one that was chosen or is no longer one that may be written.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;

namespace Hades.Installer
{
    static class Operations
    {
        public delegate void Report(int permille, string text);

        const long MiB = 1024 * 1024;
        public const long StorageLeast = 8 * 1024 * MiB; // the least room for /storage next to the data of a disk

        static long Down(long value) { return value / MiB * MiB; }
        static long Up(long value) { return (value + MiB - 1) / MiB * MiB; }

        // The disk as it is now, when it is still the chosen one and may still be changed in the way that is
        // meant. keep: what must not be erased, as it is now (Disks.Keeping, with the image that is used).
        // erases: the whole disk is about to be erased, which a disk that holds this program, the image or
        // the files of the user never is (Disks.Refusal).
        public static DiskInfo Current(DiskInfo chosen, Place place, long imageSize, Keep keep, bool erases)
        {
            List<string> guarded = Disks.Protected();
            foreach (DiskInfo d in Disks.List())
            {
                if (d.Number != chosen.Number) continue;
                if (!d.SameAs(chosen))
                    throw new InvalidOperationException(L.T("The disk has changed since you chose it. Nothing was written: choose it again.",
                                                            "El disco ha cambiado desde que lo elegiste. No se ha escrito nada: vuelve a elegirlo."));
                string why = Disks.Refusal(d, place, imageSize, keep, guarded, erases);
                if (why != null)
                    throw new InvalidOperationException(L.T("The disk can no longer be used because ", "El disco ya no se puede usar porque ") +
                                                        why + ".");
                return d;
            }
            throw new InvalidOperationException(L.T("The disk is no longer connected. Nothing was written.",
                                                    "El disco ya no está conectado. No se ha escrito nada."));
        }

        static string StageText(string stage)
        {
            if (stage == Engine.Verifying) return L.T("Verifying", "Verificando");
            if (stage == Engine.Writing) return L.T("Writing", "Escribiendo");
            if (stage == Engine.Reading) return L.T("Reading the image", "Leyendo la imagen");
            return L.T("Checking the image", "Comprobando la imagen");
        }

        static Engine.Progress Bar(Report report)
        {
            return delegate(string stage, long done, long total)
            {
                double part = total > 0 ? (double)done / total : 0;
                // writing is the first 60 % of the bar, reading back the rest
                int permille = (int)(stage == Engine.Verifying ? 600 + 400 * part : 50 + 550 * part);
                report(permille, StageText(stage) + "…  " + Words.Size(done) + L.T(" of ", " de ") + Words.Size(total));
            };
        }

        // The whole disk for Hades: everything on it is erased and the image is written as it is (USB), or
        // with its boot menus and its partition table changed for a disk inside the PC. diagnostics: with
        // the switch of its diagnostics turned on.
        public static WriteResult InstallWhole(DiskInfo disk, ImagePlan plan, Place place, bool diagnostics, Report report)
        {
            report(0, L.T("Preparing the disk…", "Preparando el disco…"));
            List<Patch> patches = Engine.Patches(plan, place == Place.Internal, true, null, diagnostics);
            DiskInfo now = Current(disk, place, plan.Size, Disks.Keeping(plan.Path), true);
            // Each volume of the disk written out and dismounted first, taken from the programs that have it open: Windows
            // erases a disk whose volume is in use all the same, and what that volume still had to write must not land on
            // the partitions that are written in its place (Native.FlushAndDismount). Then the disk is erased at once.
            foreach (PartitionInfo p in now.Partitions) Native.FlushAndDismount(disk.Number, p);
            Disks.Clear(disk.Number);
            System.Threading.Thread.Sleep(1500);
            // erasing leaves a disk without partitions; what identifies it has to be the same
            DiskInfo after = null;
            foreach (DiskInfo d in Disks.List()) if (d.Number == disk.Number) after = d;
            if (after == null || !after.SameAs(disk) || after.Partitions.Count != 0)
                throw new InvalidOperationException(L.T("The disk changed while it was being prepared. Start again.",
                                                        "El disco ha cambiado mientras se preparaba. Vuelve a empezar."));
            WriteResult result;
            using (FileStream target = Native.OpenDisk(disk.Number, true))
                result = Engine.WriteWhole(plan, target, disk.Size, patches, Bar(report), null);
            Disks.Refresh(disk.Number);
            Log.Line("whole " + place + (diagnostics ? ", diagnostics on" : "") + ": " + result.Bytes + " bytes, written " + result.Written +
                     ", read back " + result.ReadBack);
            if (!result.Verified) throw new IOException(Mismatch);
            return result;
        }

        public static string Mismatch
        {
            get
            {
                return L.T("What was written does NOT match the image. Do not start the PC from that disk. " +
                           "The usual cause is a faulty USB stick or a failing USB port: try another port or another drive.",
                           "Lo que quedó escrito NO coincide con la imagen. No arranques desde ese disco. " +
                           "Suele ser una memoria defectuosa o un puerto USB que falla: prueba otro puerto u otra unidad.");
            }
        }

        // (the window says it too, before it lets the user ask)
        public static string AlreadyThere
        {
            get
            {
                return L.T("That disk already has Hades: update it, or remove it before installing it again next to your files.",
                           "Ese disco ya tiene Hades: actualízalo, o quítalo antes de volver a instalarlo junto a tus archivos.");
            }
        }

        static string NotFound
        {
            get
            {
                return L.T("Hades was not found on that disk. Nothing was touched.",
                           "En ese disco no se encuentra Hades. No se ha tocado nada.");
            }
        }

        // Neither of the two partitions of Hades is written over or deleted while this program or the image
        // that is used is on it (Disks.KeptOnHades): what is said then.
        static InvalidOperationException OnHades(PartitionInfo held)
        {
            string where = held.Letter != '\0' ? " (" + held.Letter + ":)" : "";
            return new InvalidOperationException(L.T(
                "This program or the image is on a partition of the Hades of that disk" + where + ", which would be lost. " +
                "Copy them to another disk and start this program from there. Nothing was touched.",
                "Este programa o la imagen están en una partición del Hades de ese disco" + where + ", que se perdería. " +
                "Cópialos a otro disco y abre este programa desde ahí. No se ha tocado nada."));
        }

        public enum Removal { Refused, WholeDisk, Partitions }

        // How Hades is taken off a disk: nothing else decides it. Refused: this program or the image that
        // is chosen is on one of its two partitions (held). WholeDisk: the disk has nothing but Hades and
        // is erased. Partitions: its two partitions are deleted and "before", the volume with a letter that
        // ends where they start, gets the room back (null: there is none, and the room is left unallocated).
        public static Removal HowToRemove(DiskInfo now, PartitionInfo system, PartitionInfo storage, Keep keep,
                                          out PartitionInfo held, out PartitionInfo before)
        {
            before = null;
            held = Disks.KeptOnHades(now, system, storage, keep);
            if (held != null) return Removal.Refused;
            if (Disks.OnlyHades(now, system, storage, keep)) return Removal.WholeDisk;
            foreach (PartitionInfo p in now.Partitions)
                if (p.Letter != '\0' && p.End <= system.Offset && system.Offset - p.End <= 16 * MiB) before = p;
            return Removal.Partitions;
        }

        // What a volume could give to Hades, in bytes (0: nothing). least: what the volume has to keep.
        public static long Spare(int disk, PartitionInfo volume, ImagePlan plan, out long least)
        {
            long most;
            Disks.SupportedSize(disk, volume.Number, out least, out most);
            long spare = Down(volume.Size - least) - 2 * MiB;
            return spare >= plan.System.Size + StorageLeast ? spare : 0;
        }

        // Hades next to what a disk already has: one of its volumes is made smaller, and two partitions
        // are added in the room that leaves. No file of the disk is touched.
        public static void InstallBeside(DiskInfo disk, PartitionInfo volume, long storageSize, ImagePlan plan, bool diagnostics,
                                         Report report)
        {
            if (!plan.Scanned || !plan.StorageAnywhere)
                throw new InvalidOperationException(L.T(
                    "This Hades image cannot be installed next to the data of a disk. A more recent one is needed.",
                    "Esta imagen de Hades no sabe instalarse junto a los datos de un disco. Hace falta una más reciente."));
            if (!Native.Uefi)
                throw new InvalidOperationException(L.T("This PC does not start by UEFI: Hades can only be installed on a USB drive.",
                                                        "Este PC no arranca por UEFI: Hades solo se puede instalar en una unidad USB."));
            List<Patch> patches = Engine.Patches(plan, true, false, null, diagnostics);
            // (nothing is erased: the disk may be the one that this program or the image is on)
            Keep keep = Disks.Keeping(plan.Path);
            DiskInfo now = Current(disk, Place.Internal, plan.Size, keep, false);
            if (now.PartitionStyle != 2)
                throw new InvalidOperationException(L.T("The disk does not use a GPT partition table.",
                                                        "El disco no usa una tabla de particiones GPT."));
            PartitionInfo hadSystem, hadStorage;
            if (Disks.FindHades(now, out hadSystem, out hadStorage))
                throw new InvalidOperationException(AlreadyThere);
            PartitionInfo target = null;
            foreach (PartitionInfo p in now.Partitions)
                if (p.Number == volume.Number && p.Offset == volume.Offset && p.Size == volume.Size && p.Letter == volume.Letter) target = p;
            if (target == null || target.Letter == '\0')
                throw new InvalidOperationException(L.T("The chosen partition has changed. Nothing was touched: choose it again.",
                                                        "La partición elegida ha cambiado. No se ha tocado nada: vuelve a elegirla."));
            VolumeInfo info = Disks.Volume(target.Letter);
            if (!info.Ready || info.FileSystem != "NTFS")
                throw new InvalidOperationException(L.T("The chosen partition is not NTFS.", "La partición elegida no es NTFS."));
            if (Disks.Encrypted(target.Letter) == 1)
                throw new InvalidOperationException(
                    L.T("Partition " + target.Letter + ": is encrypted with BitLocker. Decrypt it first",
                        "La partición " + target.Letter + ": está cifrada con BitLocker. Descífrala antes") +
                    // (a disk that holds what must be kept cannot be used whole)
                    (Disks.Kept(now, keep) == null ? L.T(", or use the whole disk.", ", o usa el disco entero.") : "."));
            storageSize = Down(storageSize);
            long need = plan.System.Size + storageSize;
            long least;
            long spare = Spare(disk.Number, target, plan, out least);
            if (storageSize < StorageLeast || need > spare)
                throw new InvalidOperationException(L.T(
                    "Partition " + target.Letter + ": cannot give up that much space (it can give up " + Words.Size(spare) + ").",
                    "La partición " + target.Letter + ": no puede ceder ese espacio (puede ceder " + Words.Size(spare) + ")."));

            long oldSize = target.Size, oldEnd = target.End;
            PartitionInfo system = null, storage = null;
            bool shrunk = false;
            try
            {
                report(10, L.T("Making room on " + target.Letter + ": (Windows moves files if it needs to; this can take a while)…",
                               "Haciendo sitio en " + target.Letter + ": (Windows mueve archivos si hace falta; puede tardar)…"));
                Disks.Resize(disk.Number, target.Number, Down(oldSize - need));
                shrunk = true;
                PartitionInfo small = null;
                foreach (DiskInfo d in Disks.List())
                    if (d.Number == disk.Number)
                        foreach (PartitionInfo p in d.Partitions)
                            if (p.Number == target.Number && p.Offset == target.Offset) small = p;
                if (small == null || small.End > oldEnd - need)
                    throw new InvalidOperationException(L.T("The partition did not end up the size that was asked for.",
                                                            "La partición no quedó del tamaño pedido."));
                long systemAt = Up(small.End);
                long storageAt = systemAt + plan.System.Size;
                long storageRoom = Down(oldEnd - storageAt);
                if (storageRoom < plan.Storage.Size)
                    throw new InvalidOperationException(L.T("Not enough room was left.", "No quedó sitio suficiente."));

                report(30, L.T("Creating the Hades partitions…", "Creando las particiones de Hades…"));
                system = Disks.Create(disk.Number, systemAt, plan.System.Size, Disks.EfiType);
                storage = Disks.Create(disk.Number, storageAt, storageRoom, Disks.LinuxType);
                if (system.Size < plan.System.Size || storage.Size < plan.Storage.Size || system.Offset < small.End ||
                    storage.Offset < system.End || storage.End > oldEnd)
                    throw new InvalidOperationException(L.T("The new partitions are not where they were asked for.",
                                                            "Las particiones creadas no están donde se pidieron."));
                Log.Line("beside" + (diagnostics ? ", diagnostics on" : "") + ": volume " + target.Letter + ": " + oldSize + " -> " + small.Size +
                         "; system #" + system.Number + " at " +
                         system.Offset + " (" + system.Size + "); storage #" + storage.Number + " at " + storage.Offset + " (" + storage.Size + ")");

                var parts = new Engine.Target[2];
                using (FileStream storageStream = Native.OpenPartition(disk.Number, storage.Number, storage.Offset, storage.Size))
                using (FileStream systemStream = Native.OpenPartition(disk.Number, system.Number, system.Offset, system.Size))
                {
                    parts[0] = new Engine.Target();
                    parts[0].Stream = systemStream;
                    parts[0].Capacity = system.Size;
                    parts[0].From = plan.System.Start;
                    parts[0].Length = plan.System.Size;
                    parts[1] = new Engine.Target();
                    parts[1].Stream = storageStream;
                    parts[1].Capacity = storage.Size;
                    parts[1].From = plan.Storage.Start;
                    parts[1].Length = plan.Storage.Size;
                    Engine.Write(plan, parts, patches, Bar(report), null);
                }
                Log.Line("beside: system written " + parts[0].Result.Written + ", read back " + parts[0].Result.ReadBack +
                         "; storage written " + parts[1].Result.Written + ", read back " + parts[1].Result.ReadBack);
                if (!parts[0].Result.Verified || !parts[1].Result.Verified) throw new IOException(Mismatch);
                Disks.Refresh(disk.Number);
            }
            catch (Exception e)
            {
                Log.Line("beside failed: " + e);
                string undone = "";
                try
                {
                    report(0, L.T("It could not be completed. Undoing the changes…", "No se pudo completar. Deshaciendo los cambios…"));
                    if (storage != null) Disks.Delete(disk.Number, storage.Number, storage.Offset);
                    if (system != null) Disks.Delete(disk.Number, system.Number, system.Offset);
                    if (shrunk) Disks.Resize(disk.Number, target.Number, oldSize);
                    undone = shrunk ? L.T(" It has been undone: " + target.Letter + ": is back to its size and your files are as they were.",
                                          " Se ha deshecho: " + target.Letter + ": vuelve a tener su tamaño y tus archivos están como estaban.")
                                    : L.T(" Nothing on the disk was changed.", " No se llegó a cambiar nada del disco.");
                }
                catch (Exception again)
                {
                    Log.Line("undo failed: " + again);
                    undone = L.T(" It could not be undone completely (" + again.Message + "): your files are still on " + target.Letter +
                                 ":, but some space may have been left unallocated. Use “Remove LibreELEC Hades” or Disk Management " +
                                 "in Windows.",
                                 " No se pudo deshacer del todo (" + again.Message + "): tus archivos siguen en " + target.Letter +
                                 ":, pero puede haber quedado espacio sin asignar. Usa «Quitar LibreELEC Hades» o la Administración de " +
                                 "discos de Windows.");
                }
                throw new InvalidOperationException(e.Message + undone, e);
            }
        }

        // Hades is already on the disk: its system partition is replaced by the one of the image, and its
        // storage partition, with what the user has there, is not touched. The boot menus of the image are
        // made to name that storage, and on a disk inside the PC they are changed as when it is installed
        // there (Engine.ForInternalDisk). diagnostics: with the switch of its diagnostics turned on. What the
        // diagnostics had saved on the old system partition (the folder hades-diag) is read from it before it
        // is written over and goes into the new one (DiagFolder); what became of it is returned, and whatever
        // goes wrong with it leaves the update as it would be without it.
        // A system partition that some program has open is left alone (PartitionInUseException), unless
        // take says to take it from that program (Native.OpenPartition): the user is asked first.
        public static DiagKeep Update(DiskInfo disk, ImagePlan plan, Place place, bool diagnostics, Report report, bool take = false)
        {
            report(0, L.T("Preparing the update…", "Preparando la actualización…"));
            // (only the system partition of Hades is written, through a handle that cannot write outside it)
            Keep keep = Disks.Keeping(plan.Path);
            DiskInfo now = Current(disk, place, 0, keep, false);
            PartitionInfo system, storage;
            if (!Disks.FindHades(now, out system, out storage))
                throw new InvalidOperationException(NotFound);
            PartitionInfo held = Disks.KeptOnHades(now, system, storage, keep);
            if (held != null) throw OnHades(held);
            if (system.Size < plan.System.Size)
                throw new InvalidOperationException(L.T(
                    "The system partition of that disk is " + Words.Size(system.Size) + " and the one of this image is " +
                    Words.Size(plan.System.Size) + ": it cannot be updated, it has to be installed from scratch. Nothing was touched.",
                    "La partición de sistema de ese disco mide " + Words.Size(system.Size) + " y la de esta imagen " +
                    Words.Size(plan.System.Size) + ": no se puede actualizar, hay que instalar de cero. No se ha tocado nada."));
            string existing = Disks.StorageUuid(now.Number, storage);
            if (existing == null)
                throw new InvalidOperationException(L.T(
                    "The Hades storage on that disk cannot be read (the partition that keeps your settings does not have the " +
                    "expected format). Nothing was touched.",
                    "No se puede leer el almacenamiento de Hades de ese disco (la partición que guarda tus ajustes no tiene el " +
                    "formato esperado). No se ha tocado nada."));
            List<Patch> patches = Engine.Patches(plan, !now.Removable, false, existing, diagnostics);
            Log.Line("update" + (diagnostics ? ", diagnostics on" : "") + (take ? ", taking the partition if it is in use" : "") +
                     ": system #" + system.Number + " at " + system.Offset + "; the storage is " + existing +
                     " (the image names " + plan.StorageUuid + ")");
            var part = new Engine.Target();
            DiagKeep kept;
            using (FileStream stream = Native.OpenPartition(now.Number, system.Number, system.Offset, system.Size, true, take))
            {
                report(30, L.T("Reading what the diagnostics saved…", "Leyendo lo que guardó el diagnóstico…"));
                kept = DiagFolder.Prepare(plan, stream, system.Size, patches);
                Log.Line("update: hades-diag before writing: " + kept.Summary());
                var all = new List<Patch>(patches);
                all.AddRange(kept.Patches);
                part.Stream = stream;
                part.Capacity = system.Size;
                part.From = plan.System.Start;
                part.Length = plan.System.Size;
                Engine.Write(plan, new[] { part }, all, Bar(report), null);
                if (part.Result.Verified) DiagFolder.Confirm(kept, DiagFolder.Reader(stream, system.Size), system.Size);
            }
            Disks.Refresh(now.Number);
            Log.Line("update: written " + part.Result.Written + ", read back " + part.Result.ReadBack);
            Log.Line("update: hades-diag: " + kept.Summary());
            if (!part.Result.Verified)
                throw new IOException(L.T("What was written does NOT match the image. Update again before starting Hades: " +
                                          "your settings are still on the disk, but the system was left half written.",
                                          "Lo que quedó escrito NO coincide con la imagen. Vuelve a actualizar antes de arrancar Hades: " +
                                          "tus ajustes siguen en el disco, pero el sistema quedó a medias.") +
                                      (kept.State == DiagKeep.Outcome.Kept
                                           ? L.T(" What its diagnostics had saved (the folder hades-diag) is lost as well.",
                                                 " Lo que había guardado su diagnóstico (la carpeta hades-diag) también se pierde.")
                                           : ""));
            return kept;
        }

        // Takes Hades off an internal disk. Next to anything else: its two partitions go, and the volume
        // before them gets the room back. A disk with nothing but Hades: it is erased and left with one
        // empty NTFS volume. imagePath: the image that is chosen in the window, if any, which is kept too.
        public static string Uninstall(DiskInfo disk, string imagePath, Report report)
        {
            Keep keep = Disks.Keeping(imagePath);
            DiskInfo now = Current(disk, Place.Internal, 0, keep, false);
            PartitionInfo system, storage;
            if (!Disks.FindHades(now, out system, out storage))
                throw new InvalidOperationException(NotFound);
            PartitionInfo held, before;
            Removal how = HowToRemove(now, system, storage, keep, out held, out before);
            if (how == Removal.Refused) throw OnHades(held);
            // (never a disk with anything else on it, nor one that holds what must be kept)
            if (how == Removal.WholeDisk)
            {
                report(200, L.T("Erasing the disk…", "Borrando el disco…"));
                // (as before an install on the whole disk: nothing of its volumes may land on the one that is made)
                foreach (PartitionInfo p in now.Partitions) Native.FlushAndDismount(disk.Number, p);
                Disks.Clear(disk.Number);
                System.Threading.Thread.Sleep(1500);
                Disks.InitializeGpt(disk.Number);
                report(600, L.T("Creating an empty partition for Windows…", "Creando una partición vacía para Windows…"));
                // (the label of the new volume, which is written to the disk)
                char letter = Disks.MakeDataVolume(disk.Number, L.T("Data", "Datos"));
                Disks.Refresh(disk.Number);
                Log.Line("uninstall: whole disk " + disk.Number + " -> " + letter + ":");
                return L.T("The disk is empty and Windows sees it as " + letter + ":.",
                           "El disco está vacío y Windows lo ve como " + letter + ":.");
            }
            report(200, L.T("Deleting the Hades partitions…", "Borrando las particiones de Hades…"));
            Disks.Delete(disk.Number, storage.Number, storage.Offset);
            Disks.Delete(disk.Number, system.Number, system.Offset);
            // the volume that gets the room back, as Windows numbers it now
            if (before != null)
            {
                PartitionInfo meant = before;
                before = null;
                foreach (DiskInfo d in Disks.List())
                    if (d.Number == disk.Number)
                        foreach (PartitionInfo p in d.Partitions)
                            if (p.Offset == meant.Offset && p.Size == meant.Size && p.Letter == meant.Letter) before = p;
            }
            if (before == null)
            {
                Disks.Refresh(disk.Number);
                Log.Line("uninstall: partitions deleted, no volume before them to grow");
                return L.T("The Hades partitions have been deleted. The space is left unallocated: you can add it to a volume from " +
                           "Disk Management in Windows.",
                           "Las particiones de Hades se han borrado. El espacio queda sin asignar: puedes añadirlo a un volumen desde " +
                           "la Administración de discos de Windows.");
            }
            report(600, L.T("Giving the space back to " + before.Letter + ":…", "Devolviendo el espacio a " + before.Letter + ":…"));
            long least, most;
            Disks.SupportedSize(disk.Number, before.Number, out least, out most);
            if (most > before.Size) Disks.Resize(disk.Number, before.Number, most);
            Disks.Refresh(disk.Number);
            Log.Line("uninstall: " + before.Letter + ": " + before.Size + " -> " + most);
            string size = Words.Size(Math.Max(most, before.Size));
            return L.T("LibreELEC Hades has been removed and " + before.Letter + ": has its space back (" + size + ").",
                       "LibreELEC Hades se ha quitado y " + before.Letter + ": ha recuperado su espacio (" + size + ").");
        }
    }
}
