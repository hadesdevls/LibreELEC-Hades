// LibreELEC Hades installer for Windows: the tests of Secure Boot (--selftest), and the two commands that make, without touching a disk or the
// firmware, what an install and an update with Secure Boot write (--simulate-secureboot, --simulate-update-secureboot). Written in English: it is
// for whoever works on the program.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace Hades.Installer
{
    static partial class SelfTest
    {
        static byte[] B64(string s) { return Convert.FromBase64String(s); }

        // the action throws, and what it says has this text
        static bool ThrowsText(Action action, string text)
        {
            try { action(); }
            catch (Exception e) { return e.Message.Contains(text); }
            return false;
        }
        static string HexOf(byte[] b) { return Words.Hex(b); }
        static string Sha256Of(byte[] b) { using (var h = SHA256.Create()) return Words.Hex(h.ComputeHash(b)); }

        static byte[] Gunzip(string base64)
        {
            using (var gz = new GZipStream(new MemoryStream(B64(base64)), CompressionMode.Decompress))
            using (var o = new MemoryStream())
            {
                gz.CopyTo(o);
                return o.ToArray();
            }
        }

        // a made-up PE (x86-64): headers of 0x400, sections one after the other, and "tail" bytes after the last one (what sbsign calls endjunk)
        public static byte[] MakePe(int[] sections, int tail, int seed)
        {
            int at = 0x400;
            foreach (int n in sections) at += n;
            var b = new byte[at + tail];
            uint x = (uint)seed;
            for (int i = 0x400; i < b.Length; ++i) { x = x * 1664525 + 1013904223; b[i] = (byte)(x >> 24); }
            b[0] = (byte)'M'; b[1] = (byte)'Z';
            Buffer.BlockCopy(BitConverter.GetBytes(0x80), 0, b, 0x3C, 4);
            Buffer.BlockCopy(Encoding.ASCII.GetBytes("PE\0\0"), 0, b, 0x80, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)0x8664), 0, b, 0x84, 2);
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)sections.Length), 0, b, 0x86, 2);
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)0xF0), 0, b, 0x94, 2);
            int opt = 0x98;
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)0x20B), 0, b, opt, 2);
            Buffer.BlockCopy(BitConverter.GetBytes(0x400), 0, b, opt + 60, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(16), 0, b, opt + 108, 4);
            int scn = opt + 0xF0, raw = 0x400;
            for (int i = 0; i < sections.Length; ++i)
            {
                Buffer.BlockCopy(Encoding.ASCII.GetBytes(".s" + i), 0, b, scn + 40 * i, 3);
                Buffer.BlockCopy(BitConverter.GetBytes(sections[i]), 0, b, scn + 40 * i + 16, 4);
                Buffer.BlockCopy(BitConverter.GetBytes(raw), 0, b, scn + 40 * i + 20, 4);
                raw += sections[i];
            }
            return b;
        }

        // The two made-up programs whose signatures are compared with sbsign (sbsign-vectors.sh signs them): one with 49 bytes after its last
        // section, one whose last section ends at an odd place so that it is padded to 8 bytes
        public static byte[] PeA() { return MakePe(new[] { 0x1000, 0x600, 0x100 }, 49, 11); }
        public static byte[] PeB() { return MakePe(new[] { 0x1000, 0x600, 0xFD }, 0, 12); }

        static SbKey TestKey() { return SbKey.FromPem(SbVectors.TestKeyPem.Replace("\\n", "\n")); }

        // ---- the parts

        static void SecureBootParts(string directory, string realImage)
        {
            Say("---- Secure Boot");
            MainForm.SecureAnswer = null;
            CryptoParts(directory);
            MokParts();
            FirmwareParts();
            Ext4Parts(directory);
            FatParts(directory);
            if (realImage != null) SecureBootReal(directory, realImage);
        }

        // ---- keys, certificates, signatures

        static void CryptoParts(string directory)
        {
            Check("DER: INTEGER 128 is 02 02 00 80, 65 is 02 01 41, 65536 is 02 03 01 00 00, a big number loses its leading zeros and gains one when it needs it",
                  HexOf(Der.Int(128)) == "02020080" && HexOf(Der.Int(65)) == "020141" && HexOf(Der.Int(65536)) == "02030100" + "00" &&
                  HexOf(Der.Big(new byte[] { 0, 0, 0x7F })) == "02017f" && HexOf(Der.Big(new byte[] { 0, 0x80, 1 })) == "02030080" + "01" &&
                  HexOf(Der.Oid("1.2.840.113549.1.1.11")) == "06092a864886f70d01010b" && HexOf(Der.Len(300)) == "82012c");
            // an ephemeral key leaves nothing in the places where Windows keeps keys: the folders of the key storage of the user and of the machine
            Func<int> keyFiles = delegate
            {
                int n = 0;
                foreach (string root in new[] { Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData) })
                {
                    string keys = Path.Combine(root, @"Microsoft\Crypto");
                    try
                    {
                        if (Directory.Exists(keys)) n += Directory.GetFiles(keys, "*", SearchOption.AllDirectories).Length;
                    }
                    catch (Exception) { }
                }
                return n;
            };
            int filesBefore = keyFiles();
            using (SbKey t1 = SbKey.Generate()) using (SbKey t2 = SbKey.Generate()) using (SbKey t3 = SbKey.Generate())
                t1.Sign(new byte[32]);
            Check("the keys that are made leave no file in the key storage of Windows (" + filesBefore + " files in Microsoft\\Crypto of the user and of the machine before, " + keyFiles() + " after three keys)",
                  keyFiles() == filesBefore);
            using (SbKey a = SbKey.Generate())
            using (SbKey b = SbKey.Generate())
            {
                Check("the key of the PC: RSA of 2048 bits, exponent 65537, and not kept by Windows in any container (an ephemeral key has no name)",
                      a.Bits == 2048 && HexOf(a.Exponent).TrimStart('0') == "10001" && a.Ephemeral);
                Check("two keys are not the same", a.Short() != b.Short() && HexOf(a.Modulus) != HexOf(b.Modulus));
                string pem = a.Pem();
                string[] lines = pem.Split('\n');
                bool shape = lines[0] == "-----BEGIN PRIVATE KEY-----" && lines[lines.Length - 2] == "-----END PRIVATE KEY-----" && lines[lines.Length - 1] == "" &&
                             !pem.Contains("\r");
                for (int i = 1; i < lines.Length - 3; ++i) shape &= lines[i].Length == 64;
                Check("its PEM is PKCS#8 with the base64 in lines of 64 and every line ending in LF", shape && pem.Length < 4096 - SbSlot.FirstLine.Length);
                using (SbKey again = SbKey.FromPem(pem))
                    Check("... and reading it back gives the same key (public part and signatures)",
                          HexOf(again.Spki()) == HexOf(a.Spki()) && HexOf(again.Sign(new byte[32])) == HexOf(a.Sign(new byte[32])) && again.Pem() == pem);
                Check("a text that is not a PKCS#8 key is refused",
                      Throws(delegate { SbKey.FromPem("-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n"); }) &&
                      Throws(delegate { SbKey.FromPem("hello"); }));
                byte[] serial = new byte[16];
                for (int i = 0; i < 16; ++i) serial[i] = (byte)(0xF0 + i);
                DateTime now = new DateTime(2026, 11, 17, 12, 30, 45, DateTimeKind.Utc);
                byte[] cert = SbCert.Make(a, now, serial), cert2 = SbCert.Make(a, now, serial);
                string name = SbCert.NameStart + "2026-11-17 " + a.Short();
                Say("      (check: " + (SbCert.Check(cert, a) ?? "none") + ")");
                Check("the certificate: it is read by Windows, self-signed with the key, named \"" + SbCert.NameStart + "<date> <8 hex>\", CA false, " +
                      "digitalSignature, code signing, until the year 9999",
                      SbCert.Check(cert, a) == null && new System.Security.Cryptography.X509Certificates.X509Certificate2(cert).Subject == "CN=" + name);
                Check("... the same inputs give the same bytes (RSA with PKCS#1 v1.5 has no randomness), and a different key does not verify it",
                      HexOf(cert) == HexOf(cert2) && SbCert.Check(cert, b) != null);
                // the validity as written: UTCTime from now, GeneralizedTime 99991231235959Z
                Check("... its validity starts at the time given, as a UTCTime, and ends with the GeneralizedTime 99991231235959Z",
                      Encoding.ASCII.GetString(cert).Contains("261117123045Z") && Encoding.ASCII.GetString(cert).Contains("99991231235959Z"));
                byte[] flipped = (byte[])cert.Clone();
                flipped[flipped.Length - 5] ^= 1;
                Check("a certificate with a changed byte, or one that is not a certificate, does not check",
                      SbCert.Check(flipped, a) != null && SbCert.Check(new byte[40], a) != null);
                File.WriteAllBytes(Path.Combine(directory, "sb-cert-sample.der"), cert);
                // two fresh certificates differ in their serials, which are 16 bytes and not negative
                byte[] c1 = SbCert.Make(a, DateTime.UtcNow, null), c2 = SbCert.Make(a, DateTime.UtcNow, null);
                SbCert.Parts p1 = SbCert.Parse(c1), p2 = SbCert.Parse(c2);
                Say("      (check: " + (SbCert.Check(c1, a) ?? "none") + ")");
                Check("a certificate with no serial given takes 16 random bytes: two are not alike, the serial is 16 bytes and positive",
                      HexOf(p1.Serial) != HexOf(p2.Serial) && p1.Serial.Length == 2 + 16 && p1.Serial[2] < 0x80 && p1.Serial[2] != 0 && SbCert.Check(c1, a) == null);
            }

            // Authenticode against sbsign
            using (SbKey test = TestKey())
            {
                byte[] cert = B64(SbVectors.TestCertDer);
                Check("the test key and the test certificate go together", HexOf(test.Spki()) == HexOf(SbCert.Parse(cert).Spki));
                byte[] a = PeA(), b = PeB();
                File.WriteAllBytes(Path.Combine(directory, "sb-pe-a.efi"), a);
                File.WriteAllBytes(Path.Combine(directory, "sb-pe-b.efi"), b);
                var pa = new PeFile((byte[])a.Clone());
                var pb = new PeFile((byte[])b.Clone());
                Check("the made-up programs: A has an endjunk region of 49 bytes (its length is 1 more than a multiple of 8) and is padded to 8; B ends in the middle of a word and is padded with zeros (its padding is hashed as an endjunk of its own)",
                      pa.Regions.Count == 7 && pa.DataSize == ((a.Length + 7) & ~7) && pb.Buf.Length > b.Length && pb.DataSize == pb.Buf.Length &&
                      pb.Regions.Count == 7);
                bool haveVectors = SbVectors.PeAHash.Length == 64 && SbVectors.PeBHash.Length == 64;
                if (!haveVectors) { Say("NOT RUN: the vectors of sbsign are not in TestVectors.cs"); ++notRun; }
                else
                {
                    Check("the programs are the ones that sbsign signed for the vectors (their SHA-256)",
                          Sha256Of(a) == SbVectors.PeAHash && Sha256Of(b) == SbVectors.PeBHash);
                    byte[] sa = Authenticode.Sign(a, test, cert, SbVectors.PeATime), sb = Authenticode.Sign(b, test, cert, SbVectors.PeBTime);
                    Check("Authenticode: the file signed by this program is, byte for byte, the one that sbsign 0.9.4 wrote (same key, certificate and signing time) " +
                          "(program A, with its endjunk)",
                          Sha256Of(sa) == SbVectors.PeASigned);
                    Check("... and program B, which is padded", Sha256Of(sb) == SbVectors.PeBSigned);
                }
                byte[] signed = Authenticode.Sign(a, test, cert, "261118123045Z");
                Check("the signature that was made verifies with its certificate, and the original is left alone",
                      Authenticode.Verify(signed, cert) == null && !a.SequenceEqual(signed) && a.Length == 0x1B00 + 49);
                Check("a file that is signed already is refused (it is not signed twice), and so is one that is not a PE",
                      Throws(delegate { Authenticode.Sign(signed, test, cert, "261118123045Z"); }) &&
                      Throws(delegate { Authenticode.Sign(new byte[300], test, cert, "261118123045Z"); }));
                var damaged = (byte[])signed.Clone();
                damaged[0x500] ^= 1;
                var damagedSig = (byte[])signed.Clone();
                damagedSig[damagedSig.Length - 40] ^= 1;
                Check("a changed byte of the program, or of the signature, is found by the check; and so is a signature made with another certificate",
                      Authenticode.Verify(damaged, cert) != null && Authenticode.Verify(damagedSig, cert) != null &&
                      Authenticode.Verify(signed, SbCert.Make(test, DateTime.UtcNow, null)) != null);
                Check("the signing time must be YYMMDDHHMMSSZ", Throws(delegate { Authenticode.Sign(a, test, cert, "2026-11-18"); }));
                // the PE checksum: what the signed file says is the checksum over it
                var back = new PeFile(signed);
                Check("the signed file has its certificate table at the end, aligned to 8, and its PE checksum is the one that is computed",
                      back.CertSize > 0 && back.CertAddr + back.CertSize == signed.Length && back.CertAddr % 8 == 0 &&
                      PeChecksum(signed, back) == BitConverter.ToUInt32(signed, 0x98 + 64));
            }
        }

        // the checksum of a PE as the loader computes it (the field itself counts as zero), 16-bit words folded, plus the length
        static uint PeChecksum(byte[] file, PeFile pe)
        {
            ulong sum = 0;
            int skip = 0x98 + 64;
            for (int i = 0; i + 1 < file.Length; i += 2)
            {
                if (i == skip || i == skip + 2) continue;
                sum += (ulong)(file[i] | (file[i + 1] << 8));
                sum = (sum >> 16) + (sum & 0xFFFF);
            }
            if ((file.Length & 1) != 0) sum += file[file.Length - 1];
            sum = (sum >> 16) + (sum & 0xFFFF);
            return (uint)(sum + (ulong)file.Length);
        }

        // ---- the variables of shim

        static void MokParts()
        {
            byte[] cert = B64(SbVectors.TestCertDer);
            byte[] mokNew = Mok.MokNew(cert), auth = Mok.MokAuth(mokNew, "4821"), timeout = Mok.MokTimeout();
            Check("MokNew: an EFI_SIGNATURE_LIST of one X.509 certificate: its type, sizes 28+16+n, 0 and 16+n, the owner (shim's GUID) and the certificate",
                  mokNew.Length == 28 + 16 + cert.Length &&
                  new Guid(Der.Slice(mokNew, 0, 16)) == new Guid("a5c059a1-94e4-4aa7-87b5-ab155c2bf072") &&
                  BitConverter.ToInt32(mokNew, 16) == mokNew.Length && BitConverter.ToInt32(mokNew, 20) == 0 && BitConverter.ToInt32(mokNew, 24) == 16 + cert.Length &&
                  new Guid(Der.Slice(mokNew, 28, 44)) == new Guid("605dab50-e046-4300-abb6-3dd810dd8b23") &&
                  Der.Slice(mokNew, 44, mokNew.Length).SequenceEqual(cert));
            Check("MokTimeout is the INT32 -1, little-endian, and MokAuth is 32 bytes", timeout.Length == 4 && HexOf(timeout) == "ffffffff" && auth.Length == 32);
            if (SbVectors.MokNewHash.Length != 64) { Say("NOT RUN: the vectors of mok-vectors.py are not in TestVectors.cs"); ++notRun; }
            else
                Check("MokNew and MokAuth (code 4821) are the bytes that an independent program (Python's hashlib) computes: SHA-256 of MokNew||password in UTF-16LE",
                      Sha256Of(mokNew) == SbVectors.MokNewHash && HexOf(auth) == SbVectors.MokAuthHash && mokNew.Length.ToString() == SbVectors.MokNewLength);
            Check("a different code gives a different MokAuth, and the same code the same one",
                  HexOf(Mok.MokAuth(mokNew, "4822")) != HexOf(auth) && HexOf(Mok.MokAuth(mokNew, "4821")) == HexOf(auth));
            bool digits = true, notAllAlike = false;
            var seen = new HashSet<string>();
            for (int i = 0; i < 400; ++i)
            {
                string c = Mok.NewCode();
                digits &= c.Length == 4;
                foreach (char ch in c) digits &= ch >= '0' && ch <= '9';
                seen.Add(c);
            }
            notAllAlike = seen.Count > 300;
            var firsts = new HashSet<char>();
            foreach (string c in seen) firsts.Add(c[0]);
            Check("the code is four digits, from the top row of the keyboard, and 400 codes are not alike, and their first digits take (almost) every value (" + firsts.Count + " of 10)",
                  digits && notAllAlike && firsts.Count >= 8);
            // the database of the firmware: Microsoft's certificate of third parties is found by its hash, and only by it
            byte[] ms = B64(SbVectors.Microsoft2011Der);
            Check("the SHA-256 of Microsoft's certificate (UEFI CA 2011) is the one that the check of the database looks for",
                  Sha256Of(ms) == Mok.MicrosoftThirdParty[0] && Mok.MicrosoftThirdParty.Length == 2 && Mok.MicrosoftThirdParty[1].Length == 64);
            Func<byte[], byte[]> list = delegate(byte[] c) { return Mok.MokNew(c); };
            byte[] withMs = Der.Cat(list(cert), list(ms)), without = list(cert);
            Check("the database is read list by list: the hashes of its certificates are found, and the Microsoft one among them",
                  Mok.CertHashes(withMs).Count == 2 && Mok.CertHashes(withMs).Contains(Mok.MicrosoftThirdParty[0]) &&
                  Mok.CertHashes(without).Count == 1 && !Mok.CertHashes(without).Contains(Mok.MicrosoftThirdParty[0]) && Mok.CertHashes(new byte[10]).Count == 0);
            var fw = new FakeFirmware();
            fw.Vars[Mok.DbGuid + "/db"] = withMs;
            Enroll.Look yes = Enroll.Examine(fw, 1, 0);
            fw.Vars[Mok.DbGuid + "/db"] = without;
            Enroll.Look no = Enroll.Examine(fw, 1, 1);
            Enroll.Look unknown = Enroll.Examine(new FakeFirmware(), -1, -1);
            Check("the look at the firmware: a database with Microsoft's CA, one without it, and one that cannot be read",
                  yes.ThirdPartyCa == 1 && yes.SecureBoot == 1 && yes.BitLocker == 0 && no.ThirdPartyCa == 0 && no.BitLocker == 1 && unknown.ThirdPartyCa == -1);
        }

        static void FirmwareParts()
        {
            // (a certificate that carries the name that this program gives its own: a request with it is Hades's)
            byte[] cert;
            using (SbKey k = SbKey.Generate()) cert = SbCert.Make(k, new DateTime(2026, 11, 17, 0, 0, 0, DateTimeKind.Utc), null);
            var fw = new FakeFirmware();
            Enroll.Result r = Enroll.Write(fw, cert, "4821");
            Check("the request to enrol: the three variables are written and read back as written, and the code is kept",
                  r.Written && r.Code == "4821" && fw.Vars.Count == 3 && fw.Log.Count == 3 &&
                  HexOf(fw.Get(Mok.Auth, Mok.ShimGuid)) == HexOf(Mok.MokAuth(Mok.MokNew(cert), "4821")));
            Check("... a request of this installer that is pending is known, and the three variables are deleted by Clear",
                  Enroll.Pending(fw) && Enroll.Clear(fw).Contains("MokNew") && fw.Vars.Count == 0 && !Enroll.Pending(fw));
            var refusing = new FakeFirmware();
            refusing.RefuseName = Mok.Auth;
            Enroll.Result bad = Enroll.Write(refusing, cert, "4821");
            Check("if the firmware refuses the second variable, the first is deleted and nothing is left; no code is given",
                  !bad.Written && bad.Code == null && refusing.Vars.Count == 0 && refusing.Log.Contains("delete MokNew") && !refusing.Log.Any(l => l.StartsWith("set MokTimeout")));
            var wrong = new FakeFirmware();
            wrong.CorruptName = Mok.Timeout;
            Enroll.Result flipped = Enroll.Write(wrong, cert, "4821");
            Check("if a variable is not what was written when it is read back, the three are deleted",
                  !flipped.Written && wrong.Vars.Count == 0 && wrong.Log.Count(l => l.StartsWith("delete")) == 3);
            var other = new FakeFirmware();
            byte[] foreign = Der.Cat(new byte[44], B64(Convert.ToBase64String(Encoding.ASCII.GetBytes("CN=Some other tool "))));
            other.Vars[Mok.ShimGuid + "/" + Mok.New] = foreign;
            Enroll.Result left = Enroll.Write(other, cert, "4821");
            Check("a MokNew that is not Hades's (somebody else's pending request) is left alone: nothing is written, and it is said",
                  !left.Written && left.OtherPending && other.Log.Count == 0 && other.Vars[Mok.ShimGuid + "/" + Mok.New].SequenceEqual(foreign) &&
                  !Enroll.Pending(other));
            // the real firmware: it writes nothing but those three variables, and nothing at all unless the window says so
            var real = new WindowsFirmware();
            bool allowedBefore = WindowsFirmware.Allowed;
            Check("the firmware of this PC is not written by the tests (writing is allowed only by the window), and only MokNew, MokAuth and MokTimeout of shim could be",
                  !allowedBefore && ThrowsText(delegate { real.Set(Mok.New, Mok.ShimGuid, new byte[] { 1 }, 7); }, "not written from here") &&
                  ThrowsText(delegate { real.Delete(Mok.New, Mok.ShimGuid); }, "not written from here"));
            WindowsFirmware.Allowed = true;
            const string only = "only MokNew, MokAuth and MokTimeout";
            bool onlyThose = ThrowsText(delegate { real.Set("db", Mok.DbGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Set("KEK", Mok.GlobalGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Set("PK", Mok.GlobalGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Set("BootOrder", Mok.GlobalGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Set("BootNext", Mok.GlobalGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Delete("dbx", Mok.DbGuid); }, only) &&
                             ThrowsText(delegate { real.Delete("Boot0001", Mok.GlobalGuid); }, only) &&
                             ThrowsText(delegate { real.Set(Mok.New, Mok.GlobalGuid, new byte[] { 1 }, 7); }, only) &&
                             ThrowsText(delegate { real.Set("MokList", Mok.ShimGuid, new byte[] { 1 }, 7); }, only);
            WindowsFirmware.Allowed = allowedBefore;
            Check("... even where writing is allowed, db, KEK, PK, dbx, Boot####, BootOrder, BootNext, MokList and shim's name in another GUID are refused before Windows is asked",
                  onlyThose);
        }

        // ---- ext4 and the key slot

        static void Ext4Parts(string directory)
        {
            foreach (string which in new[] { "1 KiB blocks (a directory with an index, an extent tree of depth 1, the key in two stretches)", "4 KiB blocks" })
            {
                bool small = which.StartsWith("1");
                byte[] fs = Gunzip(small ? SbVectors.Ext4Small : SbVectors.Ext4Big);
                SbSlot slot = SbSlot.Read(DiagFolder.Reader(fs), fs.Length);
                // what debugfs said when the image was made (mk-ext4.sh)
                long[][] expected = small ? new[] { new[] { 2179L * 1024, 3072L }, new[] { 2212L * 1024, 1024L } } : new[] { new[] { 1083L * 4096, 4096L } };
                bool runs = slot.File != null && slot.File.Runs.Count == expected.Length;
                for (int i = 0; runs && i < expected.Length; ++i) runs &= slot.File.Runs[i][0] == expected[i][0] && slot.File.Runs[i][1] == expected[i][1];
                Check("ext4 with " + which + ": the key slot is found by its path, empty, in a file system that is clean, in the stretches that debugfs gives",
                      slot.Valid && slot.Empty && slot.Clean && runs && slot.Content.Length == 4096);
                if (!slot.Valid) continue;
                // writing a key: through the patches, as an install does
                using (SbKey key = SbKey.Generate())
                {
                    string pem = key.Pem();
                    byte[] content = SbSlot.Fill(pem);
                    bool shaped = Encoding.ASCII.GetString(content).StartsWith(SbSlot.FirstLine + "-----BEGIN PRIVATE KEY-----\n", StringComparison.Ordinal) &&
                                  content.Length == 4096;
                    for (int i = SbSlot.FirstLine.Length + pem.Length; i < 4096; ++i) shaped &= content[i] == (byte)'\n';
                    Check("the slot with a key: the first line, the PEM, and LF up to 4096 bytes", shaped);
                    var copy = (byte[])fs.Clone();
                    foreach (Patch p in slot.Patches(content, 0)) Buffer.BlockCopy(p.Bytes, 0, copy, (int)p.Offset, p.Bytes.Length);
                    SbSlot after = SbSlot.Read(DiagFolder.Reader(copy), copy.Length);
                    long changed = 0;
                    for (int i = 0; i < fs.Length; ++i) changed += fs[i] != copy[i] ? 1 : 0;
                    Check("... as patches into those stretches, it reads back as that key, and nothing else of the file system changes (" + changed + " bytes, all in the slot)",
                          after.Valid && !after.Empty && after.KeyPem == pem && changed <= 4096 && changed > 1000);
                    // writing it to a disk-like stream
                    var stream = new ProbeStream(new MemoryStream((byte[])fs.Clone()));
                    SbSlot.WriteTo(stream, fs.Length, pem);
                    var written = new byte[fs.Length];
                    stream.Position = 0;
                    stream.Read(written, 0, written.Length);
                    bool aligned = stream.Writes.Count > 0;
                    foreach (long w in stream.Writes) aligned &= w % 4096 == 0;
                    Check("... written to a disk, it goes in whole pieces of 4096 bytes at places that are multiples of 4096, and reads back as the same slot",
                          SbSlot.Read(DiagFolder.Reader(written), written.Length).KeyPem == pem && aligned &&
                          written.Take(2048).SequenceEqual(fs.Take(2048)));
                    Check("... a slot that is not empty is not written again",
                          Throws(delegate { SbSlot.WriteTo(new MemoryStream(written), written.Length, pem); }));
                    // not clean: state without its valid bit, or a journal to replay
                    foreach (int mutation in new[] { 0, 1 })
                    {
                        var dirty = (byte[])fs.Clone();
                        if (mutation == 0) dirty[1024 + 0x3A] = 0; else dirty[1024 + 0x60] |= 4;
                        var s = new MemoryStream((byte[])dirty.Clone());
                        bool refused = Throws(delegate { SbSlot.WriteTo(s, dirty.Length, pem); });
                        Check("a file system that was not shut down cleanly (" + (mutation == 0 ? "no valid bit in its state" : "its journal needs replaying") +
                              ") is not written: refused, and not a byte changes", refused && s.ToArray().SequenceEqual(dirty));
                    }
                    // what the disk gives back is not what was written: the old bytes are put back
                    var flaky = new LateFlipStream(new MemoryStream((byte[])fs.Clone()));
                    flaky.FlipAt = slot.File.Runs[0][0] + 100;
                    bool threw = Throws(delegate { SbSlot.WriteTo(flaky, fs.Length, pem); });
                    var restored = new byte[fs.Length];
                    flaky.Disarm();
                    flaky.Position = 0;
                    flaky.Read(restored, 0, restored.Length);
                    Check("a disk that gives a changed byte back: it is found, the old bytes are put back, and the slot is empty again", threw && restored.SequenceEqual(fs));
                }
                // what is refused
                if (small) Ext4Refusals(fs, slot);
            }
            // a file that is not an ext4, and one that is too small
            Check("something that is not an ext4 (zeros), or a partition that is too small, has no slot",
                  !SbSlot.Read(DiagFolder.Reader(new byte[1 << 20]), 1 << 20).Valid && !SbSlot.Read(DiagFolder.Reader(new byte[4096]), 4096).Valid);
            File.WriteAllBytes(Path.Combine(directory, "sb-ext4-1k.img"), Gunzip(SbVectors.Ext4Small));
            File.WriteAllBytes(Path.Combine(directory, "sb-ext4-4k.img"), Gunzip(SbVectors.Ext4Big));
        }

        // A stream of a disk that, once something was written to it, gives one byte back wrong.
        sealed class LateFlipStream : Stream
        {
            readonly Stream inner;
            public long FlipAt = -1;
            bool armed;
            public LateFlipStream(Stream inner) { this.inner = inner; }
            public override bool CanRead { get { return true; } }
            public override bool CanSeek { get { return true; } }
            public override bool CanWrite { get { return true; } }
            public override long Length { get { return inner.Length; } }
            public override long Position { get { return inner.Position; } set { inner.Position = value; } }
            public override void Flush() { inner.Flush(); }
            public override long Seek(long offset, SeekOrigin origin) { return inner.Seek(offset, origin); }
            public override void SetLength(long value) { inner.SetLength(value); }
            public override int Read(byte[] buffer, int offset, int count)
            {
                long at = inner.Position;
                int read = inner.Read(buffer, offset, count);
                if (armed && FlipAt >= at && FlipAt < at + read) buffer[offset + (int)(FlipAt - at)] ^= 0x40;
                return read;
            }
            public override void Write(byte[] buffer, int offset, int count)
            {
                armed = true;
                inner.Write(buffer, offset, count);
            }
            public void Disarm() { armed = false; }
        }

        static void Ext4Refusals(byte[] fs, SbSlot slot)
        {
            string whyNot;
            Ext4 e = Ext4.Open(DiagFolder.Reader(fs), fs.Length, out whyNot);
            long inode = e.InodeOffset(slot.File.Inode);
            Action<string, Action<byte[]>> refuse = delegate(string what, Action<byte[]> mutate)
            {
                var m = (byte[])fs.Clone();
                mutate(m);
                SbSlot s = SbSlot.Read(DiagFolder.Reader(m), m.Length);
                Check("a slot that is not as it has to be is refused: " + what + (s.Why != null ? " (" + s.Why + ")" : ""), !s.Valid);
            };
            refuse("it is 4095 bytes long", m => m[inode + 4] = 0xFF);
            refuse("it is 4097 bytes long", m => { m[inode + 4] = 0x01; m[inode + 5] = 0x10; });
            refuse("it is a directory, not a regular file", m => m[inode + 1] = 0x41);
            refuse("its data is in the inode (inline data)", m => m[inode + 0x20 + 3] |= 0x10);
            refuse("it has no extents", m => m[inode + 0x20 + 2] &= 0xF7);
            refuse("its extent is not initialized", m => m[inode + 0x28 + 12 + 4 + 1] = 0x80);
            refuse("its first line is another", m => m[slot.File.Runs[0][0] + 7] = (byte)'X');
            refuse("it holds something that is not a key and not empty", m => m[slot.File.Runs[0][0] + 100] = (byte)'X');
            refuse("it holds a key that does not end", m => { byte[] c = Encoding.ASCII.GetBytes("-----BEGIN PRIVATE KEY-----\nAAAA\n"); Buffer.BlockCopy(c, 0, m, (int)slot.File.Runs[0][0] + 24, c.Length); });
            refuse("its extent tree has no magic", m => m[inode + 0x28] = 0);
            refuse("its superblock has no magic", m => m[1024 + 0x38] = 0);
        }

        // ---- the system partition: FAT16 with the files of an image that knows Secure Boot

        sealed class SbFatMaker
        {
            public readonly byte[] P;
            public const int Spc = 4, Reserved = 4, RootEntries = 512;
            public readonly int Cb = Spc * 512, FatSectors, Clusters;
            readonly ushort[] fat;
            int next = 2;
            public readonly long FatAt, RootAt, DataAt;

            public SbFatMaker(long size)
            {
                long total = size / 512;
                int rootSectors = RootEntries * 32 / 512;
                int fatSectors = 1;
                for (int i = 0; i < 6; ++i)
                {
                    long clusters = (total - Reserved - 2 * fatSectors - rootSectors) / Spc;
                    fatSectors = (int)(((clusters + 2) * 2 + 511) / 512);
                }
                FatSectors = fatSectors;
                Clusters = (int)((total - Reserved - 2 * fatSectors - rootSectors) / Spc);
                P = new byte[size];
                FatAt = Reserved * 512L;
                RootAt = FatAt + 2L * fatSectors * 512;
                DataAt = RootAt + RootEntries * 32L;
                fat = new ushort[Clusters + 2];
                fat[0] = 0xFFF8; fat[1] = 0xFFFF;
                P[510] = 0x55; P[511] = 0xAA;
                P[11] = 0; P[12] = 2; P[13] = Spc; P[14] = Reserved; P[16] = 2; P[17] = (byte)(RootEntries & 0xFF); P[18] = (byte)((RootEntries >> 8) & 0xFF);
                Buffer.BlockCopy(BitConverter.GetBytes((ushort)0), 0, P, 19, 2);
                P[21] = 0xF8;
                Buffer.BlockCopy(BitConverter.GetBytes((ushort)fatSectors), 0, P, 22, 2);
                Buffer.BlockCopy(BitConverter.GetBytes((uint)total), 0, P, 32, 4);
                Buffer.BlockCopy(Encoding.ASCII.GetBytes("LIBREELEC  "), 0, P, 43, 11);
            }

            public long ClusterAt(int c) { return DataAt + (long)(c - 2) * Cb; }

            // clusters for a file, one after the other from the next free one (skipping "gap" clusters first, which are left free)
            public int Alloc(int bytes, int gap = 0)
            {
                next += gap;
                int n = Math.Max(1, (bytes + Cb - 1) / Cb);
                int first = next;
                for (int i = 0; i < n; ++i) fat[next + i] = (ushort)(i + 1 < n ? next + i + 1 : 0xFFFF);
                next += n;
                return first;
            }

            // a file whose chain is in two pieces with free clusters between them (it is not in one run)
            public int AllocFragmented(int bytes)
            {
                int n = Math.Max(2, (bytes + Cb - 1) / Cb), n1 = n / 2, n2 = n - n1;
                int a = next;
                for (int i = 0; i < n1; ++i) fat[a + i] = (ushort)(a + i + 1);
                next += n1 + 7;
                int b = next;
                for (int i = 0; i < n2; ++i) fat[b + i] = (ushort)(i + 1 < n2 ? b + i + 1 : 0xFFFF);
                fat[a + n1 - 1] = (ushort)b;
                next += n2;
                return a;
            }

            // everything but "keep" clusters is used (by a file that is nobody's)
            public void FillAllBut(int keep)
            {
                int n = Clusters - (next - 2) - keep;
                for (int i = 0; i < n; ++i) fat[next + i] = (ushort)(i + 1 < n ? next + i + 1 : 0xFFFF);
                next += n;
            }

            public void Put(int first, byte[] data)
            {
                int c = first, at = 0;
                while (at < data.Length)
                {
                    int n = Math.Min(Cb, data.Length - at);
                    Buffer.BlockCopy(data, at, P, (int)ClusterAt(c), n);
                    at += n;
                    c = fat[c];
                }
            }

            public void PutDir(int first, byte[] entries)
            {
                Put(first, entries);
            }

            public void Seal(byte[] root)
            {
                Buffer.BlockCopy(root, 0, P, (int)RootAt, root.Length);
                for (int copy = 0; copy < 2; ++copy)
                    for (int c = 0; c < fat.Length; ++c)
                    {
                        P[FatAt + copy * FatSectors * 512L + 2 * c] = (byte)fat[c];
                        P[FatAt + copy * FatSectors * 512L + 2 * c + 1] = (byte)(fat[c] >> 8);
                    }
            }
        }

        static byte[] DirBytes(int size, params byte[][] entries)
        {
            var d = new byte[size];
            int at = 0;
            foreach (byte[] e in entries) { Buffer.BlockCopy(e, 0, d, at, e.Length); at += e.Length; }
            return d;
        }

        static byte[] FileEntry(string name11, byte small, int first, long size, string longName = null, byte attr = 0x20)
        {
            byte[] e = ShortEntry(name11, attr, small, size);
            Buffer.BlockCopy(BitConverter.GetBytes((ushort)first), 0, e, 26, 2);
            return longName == null ? e : Der.Cat(LongNames(longName, e), e);
        }

        sealed class SbSynth
        {
            public byte[] Image, Shim, Mm, Grub, Kernel, Decoy, System;
            public string Path;
        }

        // An image of 88 MiB like those of the tests, with a FAT16 system partition that has what an image with Secure Boot has, and an ext4
        // storage partition with the key slot. dirsLast: the folders are put after the files in the partition (so that the scan meets a cluster
        // before the entry that names it). longNames: the names carry long names. fragmented: the kernel is in two pieces.
        static SbSynth MakeSbImage(string directory, string name, bool dirsLast, bool longNames, bool fragmented, bool bigStorage, bool withSecureboot = true,
                                   string extra = null, bool full = false)
        {
            var s = new SbSynth();
            s.Shim = MakePe(new[] { 0x9000, 0x3000, 0x400 }, 24, 21);
            s.Mm = MakePe(new[] { 0x7000, 0x2000, 0x200 }, 0, 22);
            s.Grub = MakePe(new[] { 0xC000, 0x4000, 0x140 }, 0, 23);
            s.Kernel = MakePe(new[] { 0x31000, 0x9000, 0x100 }, 3, 24);
            s.Decoy = Noise(3000, 25);
            long[] unused;
            byte[] img = MakeImage(0, false, out unused);
            var fat = new SbFatMaker(P1Size * S);
            // allocation: folders first or last
            int efi = 0, boot = 0, sec = 0;
            if (!dirsLast) { efi = fat.Alloc(1); boot = fat.Alloc(1); sec = fat.Alloc(1); }
            int kernel = fragmented ? fat.AllocFragmented(s.Kernel.Length) : fat.Alloc(s.Kernel.Length);
            int grub = fat.Alloc(s.Grub.Length), cfg = fat.Alloc(s.Decoy.Length);
            int shim = fat.Alloc(s.Shim.Length), mm = fat.Alloc(s.Mm.Length);
            int system = fat.Alloc(1500000), diag = fat.Alloc(10);
            if (full) fat.FillAllBut(10);
            if (dirsLast) { efi = fat.Alloc(1); boot = fat.Alloc(1); sec = fat.Alloc(1); }
            fat.Put(kernel, s.Kernel);
            fat.Put(grub, s.Grub);
            fat.Put(cfg, s.Decoy);
            fat.Put(shim, s.Shim);
            fat.Put(mm, s.Mm);
            fat.Put(system, Noise(1500000, 26));
            fat.Put(diag, Noise(10, 27));
            Func<string, string> lng = delegate(string n) { return longNames ? n : null; };
            byte[] rootDir = DirBytes(512 * 32,
                ShortEntry("LIBREELEC  ", 0x08, 0, 0),
                withSecureboot ? FileEntry("EFI        ", 0, efi, 0, null, 0x10) : FileEntry("EFI        ", 0, efi, 0, null, 0x10),
                FileEntry("KERNEL     ", 0, kernel, s.Kernel.Length),
                FileEntry("SYSTEM     ", 0, system, 1500000),
                FileEntry("DIAG-OFFTXT", 0, diag, 10),
                extra == "HADES-SB.CER" ? FileEntry("HADES-SBCER", 0, diag, 10) : new byte[0],
                withSecureboot ? FileEntry("SECURE~1   ", 0, sec, 0, "secureboot", 0x10) : new byte[0]);
            fat.PutDir(efi, DirBytes(fat.Cb, ShortEntry(".          ", 0x10, 0, 0), ShortEntry("..         ", 0x10, 0, 0),
                                     FileEntry("BOOT       ", 0, boot, 0, null, 0x10)));
            fat.PutDir(boot, DirBytes(fat.Cb, ShortEntry(".          ", 0x10, 0, 0), ShortEntry("..         ", 0x10, 0, 0),
                                      FileEntry("BOOTX64 EFI", longNames ? (byte)0 : (byte)0x18, grub, s.Grub.Length, lng("bootx64.efi")),
                                      FileEntry("GRUB    CFG", 0x18, cfg, s.Decoy.Length),
                                      extra == "fbx64.efi" ? FileEntry("FBX64   EFI", 0x18, cfg, 100) : new byte[0],
                                      extra == "grubx64.efi" ? FileEntry("GRUBX64 EFI", 0x18, cfg, 100) : new byte[0],
                                      extra == "mmx64.efi" ? FileEntry("MMX64   EFI", 0x18, cfg, 100) : new byte[0]));
            if (withSecureboot)
                fat.PutDir(sec, DirBytes(fat.Cb, ShortEntry(".          ", 0x10, 0, 0), ShortEntry("..         ", 0x10, 0, 0),
                                         FileEntry("SHIMX64 EFI", longNames ? (byte)0 : (byte)0x18, shim, s.Shim.Length, lng("shimx64.efi")),
                                         FileEntry("MMX64   EFI", 0x18, mm, s.Mm.Length)));
            fat.Seal(rootDir);
            s.System = fat.P;
            Buffer.BlockCopy(fat.P, 0, img, (int)(P1 * S), fat.P.Length);
            byte[] ext = Gunzip(bigStorage ? SbVectors.Ext4Big : SbVectors.Ext4Small);
            Array.Clear(img, (int)(P2 * S), (int)(P2Size * S));
            Buffer.BlockCopy(ext, 0, img, (int)(P2 * S), ext.Length);
            s.Image = img;
            s.Path = Path.Combine(directory, name);
            File.WriteAllBytes(s.Path, img);
            return s;
        }

        // The system partition as an install writes it, read back from the stream that it went to.
        static byte[] WrittenSystem(ImagePlan plan, List<Patch> patches, out WriteResult result)
        {
            var part = new Engine.Target();
            var stream = new MemoryStream();
            stream.SetLength(plan.System.Size);
            part.Stream = stream; part.Capacity = plan.System.Size; part.From = plan.System.Start; part.Length = plan.System.Size;
            Engine.Write(plan, new[] { part }, patches, null, null);
            result = part.Result;
            return stream.ToArray();
        }

        static byte[] WrittenStorage(ImagePlan plan, List<Patch> patches)
        {
            var part = new Engine.Target();
            var stream = new MemoryStream();
            stream.SetLength(plan.Storage.Size);
            part.Stream = stream; part.Capacity = plan.Storage.Size; part.From = plan.Storage.Start; part.Length = plan.Storage.Size;
            Engine.Write(plan, new[] { part }, patches, null, null);
            return stream.ToArray();
        }

        // Reads a file by its path from a FAT16 (long names where they are), through its chain. null: not there or broken.
        static byte[] ReadFatFile(byte[] partition, string path) { return ReadFatFile(DiagFolder.Reader(partition), partition.Length, path); }
        static string FatProblems(byte[] partition) { return FatProblems(DiagFolder.Reader(partition), partition.Length); }

        internal static byte[] ReadFatFile(Func<long, int, byte[]> read, long size, string path)
        {
            string why;
            Fat16 f = Fat16.Open(read, size, out why);
            if (f == null) return null;
            byte[] dir = f.Root;
            string[] parts = path.Split('/');
            for (int i = 0; i < parts.Length; ++i)
            {
                DirEnt e = SbDir.Find(SbDir.List(dir), parts[i]);
                if (e == null) return null;
                if (i == parts.Length - 1)
                {
                    if (e.Folder) return null;
                    int need = (int)((e.Size + f.ClusterBytes - 1) / f.ClusterBytes);
                    List<int> chain = e.Size == 0 ? new List<int>() : f.Chain(e.First, need);
                    if (chain == null || chain.Count != need) return null;
                    var bytes = new byte[e.Size];
                    for (int k = 0; k < chain.Count; ++k)
                        Buffer.BlockCopy(f.Cluster(chain[k]), 0, bytes, k * f.ClusterBytes, (int)Math.Min(f.ClusterBytes, bytes.Length - k * f.ClusterBytes));
                    return bytes;
                }
                if (!e.Folder) return null;
                List<int> dirChain = f.Chain(e.First, 64);
                if (dirChain == null) return null;
                dir = new byte[dirChain.Count * f.ClusterBytes];
                for (int k = 0; k < dirChain.Count; ++k) Buffer.BlockCopy(f.Cluster(dirChain[k]), 0, dir, k * f.ClusterBytes, f.ClusterBytes);
            }
            return null;
        }

        internal static string FatProblems(Func<long, int, byte[]> read, long size)
        {
            string why;
            Fat16 f = Fat16.Open(read, size, out why);
            if (f == null) return "not a FAT16: " + why;
            if (!f.SameFats) return "the two FATs differ";
            // no cluster is the next of two, and every chain reachable from a directory entry ends
            var pointed = new HashSet<int>();
            for (int c = 2; c < f.Table.Length; ++c)
            {
                int n = f.Table[c];
                if (n >= 2 && n < 0xFFF0 && !pointed.Add(n)) return "cluster " + n + " is the next of two";
            }
            return null;
        }

        static void FatParts(string directory)
        {
            DateTime stamp = new DateTime(2026, 11, 17, 12, 0, 0, DateTimeKind.Utc);
            int variant = 0;
            foreach (bool dirsLast in new[] { false, true })
                foreach (bool longNames in new[] { false, true })
                {
                    ++variant;
                    bool fragmented = variant == 4;
                    SbSynth s = MakeSbImage(directory, "sb-synthetic-" + variant + ".img", dirsLast, longNames, fragmented, variant % 2 == 0);
                    string label = "synthetic image " + variant + " (" + (dirsLast ? "folders after the files" : "folders first") + ", " +
                                   (longNames ? "with long names" : "short names only") + ")";
                    ImagePlan quick = Engine.Plan(s.Path, false, null, null);
                    Engine.ProbeSecureBoot(quick);
                    ImagePlan plan = Engine.Plan(s.Path, true, null, null);
                    Check(label + ": the quick look finds /secureboot with shim and MokManager, and the read through finds everything that the change needs: " +
                          "bootx64.efi, KERNEL, shim, MokManager, the storage",
                          quick.SecureState == 1 && plan.SecureState == 1 && plan.Sb.Known && plan.Sb.GrubBytes.SequenceEqual(s.Grub) &&
                          plan.Sb.KernelBytes.SequenceEqual(s.Kernel) && plan.Sb.ShimBytes.SequenceEqual(s.Shim) && plan.Sb.MmBytes.SequenceEqual(s.Mm) &&
                          plan.Sb.Storage != null && plan.Sb.Storage.Length == plan.Storage.Size);
                    // what came by before it was wanted is read again, in a pass of its own, when the folders are after the files, and not otherwise
                    Check(label + ": " + (dirsLast ? "the clusters that came by before the entry that names them are read again (" + plan.Sb.Rereads + " clusters in " + plan.Sb.Passes + " passes)"
                                                    : "nothing has to be read again"),
                          dirsLast ? plan.Sb.Rereads > 0 && plan.Sb.Passes >= 1 : plan.Sb.Rereads == 0 && plan.Sb.Passes == 0);
                    Check(label + ": the storage partition is collected in the same pass (it comes after the system partition and is not read again)", !plan.Sb.StorageReadAgain);
                    // the gzip of the same image gives the same reading (it is read forward as it is unpacked)
                    if (variant == 3)
                    {
                        string gz = s.Path + ".gz";
                        using (var f = File.Create(gz)) using (var z = new GZipStream(f, CompressionMode.Compress)) z.Write(s.Image, 0, s.Image.Length);
                        ImagePlan pz = Engine.Plan(gz, true, null, null);
                        Check(label + ": the compressed image is read to the same result", pz.Sb.Known && pz.Sb.KernelBytes.SequenceEqual(s.Kernel) && pz.Sb.GrubBytes.SequenceEqual(s.Grub));
                        File.Delete(gz);
                    }
                    using (SbKey key = SbKey.Generate())
                    {
                        byte[] cert = SbCert.Make(key, stamp, null);
                        SbBuild build = SecureBoot.Build(plan, key, cert, true, "261118120000Z");
                        WriteResult wr;
                        List<Patch> patches = Engine.Patches(plan, false, false, null, false, null, build);
                        byte[] sys = WrittenSystem(plan, patches, out wr);
                        byte[] sto = WrittenStorage(plan, patches);
                        Check(label + ": written, read back the same", wr.Verified);
                        Check(label + ": the FAT is sound: both FATs alike, no chain crossed or broken", FatProblems(sys) == null);
                        byte[] bx = ReadFatFile(sys, "EFI/BOOT/bootx64.efi"), gx = ReadFatFile(sys, "EFI/BOOT/grubx64.efi"), mx = ReadFatFile(sys, "EFI/BOOT/mmx64.efi"),
                               kx = ReadFatFile(sys, "KERNEL"), cx = ReadFatFile(sys, "HADES-SB.CER");
                        Check(label + ": bootx64.efi is shim, grubx64.efi is GRUB signed with the key of the PC, mmx64.efi is MokManager, KERNEL is the kernel signed, " +
                              "HADES-SB.CER is the certificate",
                              bx != null && bx.SequenceEqual(s.Shim) && gx != null && Authenticode.Verify(gx, cert) == null && mx != null && mx.SequenceEqual(s.Mm) &&
                              kx != null && Authenticode.Verify(kx, cert) == null && cx != null && cx.SequenceEqual(cert) && !gx.SequenceEqual(s.Grub) && !kx.SequenceEqual(s.Kernel));
                        Check(label + ": what was signed is the program of the image (the signed file, with its signature taken off, hashes as the original)",
                              new PeFile((byte[])gx.Clone()).Digest().SequenceEqual(new PeFile((byte[])s.Grub.Clone()).Digest()) &&
                              new PeFile((byte[])kx.Clone()).Digest().SequenceEqual(new PeFile((byte[])s.Kernel.Clone()).Digest()));
                        byte[] before = ReadFatFile(s.System, "SYSTEM"), after = ReadFatFile(sys, "SYSTEM");
                        Check(label + ": nothing else of the partition changes: SYSTEM, grub.cfg, DIAG-OFF.TXT and /secureboot read the same, and no fbx64.efi is there",
                              before != null && before.SequenceEqual(after) && ReadFatFile(sys, "EFI/BOOT/grub.cfg").SequenceEqual(s.Decoy) &&
                              ReadFatFile(sys, "secureboot/shimx64.efi").SequenceEqual(s.Shim) && ReadFatFile(sys, "secureboot/mmx64.efi").SequenceEqual(s.Mm) &&
                              ReadFatFile(sys, "DIAG-OFF.TXT") != null && ReadFatFile(sys, "EFI/BOOT/fbx64.efi") == null);
                        // the NT bits: GRUBX64.EFI and MMX64.EFI show in lower case, as the names that the image has
                        string whyNot;
                        Fat16 fr = Fat16.Open(DiagFolder.Reader(sys), sys.Length, out whyNot);
                        List<int> bootChain = null;
                        {
                            DirEnt efiEnt = SbDir.Find(SbDir.List(fr.Root), "EFI");
                            DirEnt bootEnt = SbDir.Find(SbDir.List(fr.Cluster(efiEnt.First)), "BOOT");
                            bootChain = fr.Chain(bootEnt.First, 8);
                        }
                        byte[] bootDirBytes = fr.Cluster(bootChain[0]);
                        int grubEntry = -1, mmEntry = -1, bootEntry = -1;
                        for (int e = 0; e < bootDirBytes.Length; e += 32)
                        {
                            string n = Encoding.ASCII.GetString(bootDirBytes, e, 11);
                            if (n == "GRUBX64 EFI") grubEntry = e;
                            if (n == "MMX64   EFI") mmEntry = e;
                            if (n == "BOOTX64 EFI") bootEntry = e;
                        }
                        Check(label + ": the new entries of /EFI/BOOT are GRUBX64 EFI and MMX64   EFI with the lower-case bits 0x18, and take the times of bootx64.efi's entry",
                              grubEntry >= 0 && mmEntry >= 0 && bootDirBytes[grubEntry + 12] == 0x18 && bootDirBytes[mmEntry + 12] == 0x18 && bootEntry >= 0 &&
                              Der.Slice(bootDirBytes, grubEntry + 13, grubEntry + 20).SequenceEqual(Der.Slice(bootDirBytes, bootEntry + 13, bootEntry + 20)) &&
                              Der.Slice(bootDirBytes, grubEntry + 22, grubEntry + 26).SequenceEqual(Der.Slice(bootDirBytes, bootEntry + 22, bootEntry + 26)) &&
                              bootDirBytes[grubEntry + 11] == 0x20);
                        // the key slot in the storage partition: the key, and nothing else of that file system changed
                        SbSlot slot = SbSlot.Read(DiagFolder.Reader(sto), sto.Length);
                        byte[] stoBefore = (byte[])plan.Sb.Storage.Clone();
                        long changed = 0;
                        for (int i = 0; i < sto.Length; ++i) changed += sto[i] != stoBefore[i] ? 1 : 0;
                        Check(label + ": the storage partition has the key in its slot (and the key is the one that signed) and nothing else changes in it (" + changed + " bytes)",
                              slot.Valid && !slot.Empty && slot.KeyPem == key.Pem() && changed <= 4096 && changed > 1000);
                        if (variant == 1 || variant == 4)
                        {
                            File.WriteAllBytes(Path.Combine(directory, "sb-synth" + variant + "-system.bin"), sys);
                            File.WriteAllBytes(Path.Combine(directory, "sb-synth" + variant + "-storage.bin"), sto);
                            File.WriteAllBytes(Path.Combine(directory, "sb-synth" + variant + "-cert.der"), cert);
                            File.WriteAllText(Path.Combine(directory, "sb-synth" + variant + "-expected.txt"),
                                              "EFI/BOOT/bootx64.efi " + Sha256Of(bx) + "\nEFI/BOOT/grubx64.efi " + Sha256Of(gx) + "\nEFI/BOOT/mmx64.efi " + Sha256Of(mx) +
                                              "\nEFI/BOOT/grub.cfg " + Sha256Of(s.Decoy) + "\nKERNEL " + Sha256Of(kx) + "\nHADES-SB.CER " + Sha256Of(cx) + "\nSYSTEM " + Sha256Of(after) +
                                              "\nsecureboot/shimx64.efi " + Sha256Of(s.Shim) + "\nsecureboot/mmx64.efi " + Sha256Of(s.Mm) + "\n", new UTF8Encoding(false));
                        }

                        if (variant == 1) CompatParts(directory, s, plan, key, cert, build);
                    }
                    if (variant == 1) UpdateParts(directory, s);
                }
            // an image without /secureboot: the box stays off and says why; one with the folder but without a file of it, or with a name taken
            SbSynth none = MakeSbImage(directory, "sb-synthetic-none.img", false, false, false, false, false);
            ImagePlan np = Engine.Plan(none.Path, false, null, null);
            Engine.ProbeSecureBoot(np);
            ImagePlan ns = Engine.Plan(none.Path, true, null, null);
            Check("an image with no /secureboot: the quick look says no, the read through says why, and nothing is collected",
                  np.SecureState == 2 && ns.SecureState == 2 && !ns.Sb.Known && ns.Sb.Why.Contains("/secureboot") && ns.Sb.KernelBytes == null && ns.Sb.Storage == null);
            Check("... and a change for Secure Boot cannot be built for it",
                  Throws(delegate { using (SbKey k = SbKey.Generate()) SecureBoot.Build(ns, k, SbCert.Make(k, DateTime.UtcNow, null), true, "261118120000Z"); }));
            // what an image has to be like: not one that has the names that the change takes, nor one where it does not fit, nor a FAT that is not FAT16
            foreach (string name in new[] { "fbx64.efi", "grubx64.efi", "mmx64.efi", "HADES-SB.CER" })
            {
                SbSynth has = MakeSbImage(directory, "sb-synthetic-has.img", false, false, false, false, true, name);
                ImagePlan hp = Engine.Plan(has.Path, true, null, null);
                Check("an image that has " + (name == "HADES-SB.CER" ? "/" : "/EFI/BOOT/") + name + " already is refused (" + hp.Sb.Why + "): nothing is built for it",
                      !hp.Sb.Known && hp.Sb.Why.Contains(name) && Throws(delegate { using (SbKey k = SbKey.Generate()) SecureBoot.Build(hp, k, SbCert.Make(k, DateTime.UtcNow, null), true, "261118120000Z"); }));
                File.Delete(has.Path);
            }
            SbSynth fullImage = MakeSbImage(directory, "sb-synthetic-full.img", false, false, false, false, true, null, true);
            ImagePlan fp = Engine.Plan(fullImage.Path, true, null, null);
            bool noRoom = false;
            string roomWhy = null;
            using (SbKey k = SbKey.Generate())
            {
                try { SecureBoot.Build(fp, k, SbCert.Make(k, DateTime.UtcNow, null), true, "261118120000Z"); }
                catch (InvalidDataException e) { noRoom = true; roomWhy = e.Message; }
            }
            Check("an image whose system partition has no room for the new contents is refused before anything is written (" + roomWhy + ")", fp.Sb.Known && noRoom);
            File.Delete(fullImage.Path);
            SbSynth notFat16 = MakeSbImage(directory, "sb-synthetic-not16.img", false, false, false, false);
            byte[] bytes = File.ReadAllBytes(notFat16.Path);
            bytes[P1 * S + 13] = 1; // one sector in a cluster: four times the clusters, more than a FAT16 has
            File.WriteAllBytes(notFat16.Path, bytes);
            ImagePlan n16 = Engine.Plan(notFat16.Path, true, null, null);
            Check("a system partition that is not FAT16 is not changed (" + n16.Sb.Why + ")", !n16.Sb.Known && n16.Sb.Why.Contains("FAT16"));
            File.Delete(notFat16.Path);
            Check("an image that is not a FAT at all (the random one of the other tests) is not an image for Secure Boot, and its scan is not broken by that",
                  FakeScan(directory));
        }

        static bool FakeScan(string directory)
        {
            long[] unused;
            string path = Path.Combine(directory, "sb-random.img");
            File.WriteAllBytes(path, MakeImage(4, true, out unused));
            ImagePlan p = Engine.Plan(path, true, null, null);
            ImagePlan q = Engine.Plan(path, false, null, null);
            Engine.ProbeSecureBoot(q);
            File.Delete(path);
            return p.Scanned && p.SecureState == 2 && q.SecureState == 2 && p.HideOffsets.Count == 4;
        }

        // The change together with the other changes of an install and of an update: the switch of the diagnostics (a root entry that the change writes too),
        // the hades-diag folder that an update keeps (which takes free clusters and free entries from the partition with the change made), the other disks hidden.
        static void CompatParts(string directory, SbSynth s, ImagePlan plan, SbKey key, byte[] cert, SbBuild build)
        {
            WriteResult wr;
            List<Patch> withDiag = Engine.Patches(plan, false, false, null, true, null, build);
            byte[] sys = WrittenSystem(plan, withDiag, out wr);
            long at;
            Engine.Switch sw = Engine.ReadSwitch(new MemoryStream(sys), sys.Length, out at);
            Check("with the diagnostics on: the switch is on and the files of Secure Boot are all there (the later patch wins where both write the root directory)",
                  wr.Verified && sw == Engine.Switch.On && FatProblems(sys) == null && ReadFatFile(sys, "KERNEL") != null &&
                  Authenticode.Verify(ReadFatFile(sys, "KERNEL"), cert) == null && ReadFatFile(sys, "HADES-SB.CER").SequenceEqual(cert));
            // the folder hades-diag of the old partition goes into the new one next to the change
            byte[] old = WithDiag(Fat16Partition(6000, 100), DiagTree());
            DiagKeep kept = KeepFrom(old, s.System, plan.System.Start, build.System, 4L << 20);
            Check("an update that keeps hades-diag, with the change for Secure Boot made first: the folder is kept (" + kept.State + ")",
                  kept.State == DiagKeep.Outcome.Kept);
            if (kept.State == DiagKeep.Outcome.Kept)
            {
                var all = new List<Patch>(build.System);
                all.AddRange(kept.Patches);
                byte[] both = WrittenSystem(plan, all, out wr);
                DiagItem back = Reread(both);
                string where;
                Check("... both are in the partition that is written: its FAT is sound, the folder reads back as the one that was kept, and every file of Secure Boot reads as before",
                      wr.Verified && FatProblems(both) == null && back != null && DiagFolder.Same(kept.Folder, back, out where) &&
                      ReadFatFile(both, "KERNEL").SequenceEqual(ReadFatFile(sys, "KERNEL")) && ReadFatFile(both, "EFI/BOOT/grubx64.efi").SequenceEqual(ReadFatFile(sys, "EFI/BOOT/grubx64.efi")) &&
                      ReadFatFile(both, "HADES-SB.CER").SequenceEqual(cert));
                File.WriteAllBytes(Path.Combine(directory, "sb-synth1-with-diag-system.bin"), both);
                // the change does not take a cluster of the folder, nor the folder one of the change
                string wrongOrder = null;
                var swapped = new List<Patch>(kept.Patches);
                swapped.AddRange(build.System);
                try
                {
                    byte[] bad = WrittenSystem(plan, swapped, out wr);
                    wrongOrder = FatProblems(bad) == null && Reread(bad) != null && ReadFatFile(bad, "KERNEL") != null ? null : "damaged";
                }
                catch (Exception) { wrongOrder = "threw"; }
                Say("      (the two sets of patches in the other order: " + (wrongOrder ?? "also sound") + ")");
            }
            // the patches of the change are marked as layered where they write the FAT or the root directory, and only there
            int layered = build.System.Count(p => p.Layered);
            Check("the patches that write the FATs and the root directory are marked, and no other (" + layered + " of " + build.System.Count + ")",
                  layered == plan.Sb.FatCount + 2 && build.System.Where(p => p.Layered).All(p => p.Offset - plan.System.Start < plan.Sb.DataAt) &&
                  build.System.Where(p => !p.Layered).All(p => p.Offset - plan.System.Start >= plan.Sb.DataAt));
        }

        // The update of a Hades that has Secure Boot, and its four ways of not having its key.
        static void UpdateParts(string directory, SbSynth s)
        {
            ImagePlan plan = Engine.Plan(s.Path, true, null, null);
            using (SbKey key = SbKey.Generate())
            {
                byte[] cert = SbCert.Make(key, DateTime.UtcNow, null);
                byte[] emptySlot = (byte[])plan.Sb.Storage.Clone();
                SbSlot empty = SbSlot.Read(DiagFolder.Reader(emptySlot), emptySlot.Length);
                var withKey = (byte[])emptySlot.Clone();
                foreach (Patch p in empty.Patches(SbSlot.Fill(key.Pem()), 0)) Buffer.BlockCopy(p.Bytes, 0, withKey, (int)p.Offset, p.Bytes.Length);
                SbSlot has = SbSlot.Read(DiagFolder.Reader(withKey), withKey.Length);
                SbSlot hasOther;
                using (SbKey other = SbKey.Generate())
                {
                    var w = (byte[])emptySlot.Clone();
                    foreach (Patch p in empty.Patches(SbSlot.Fill(other.Pem()), 0)) Buffer.BlockCopy(p.Bytes, 0, w, (int)p.Offset, p.Bytes.Length);
                    hasOther = SbSlot.Read(DiagFolder.Reader(w), w.Length);
                }
                bool writeSlot;
                // (a) the Hades has its certificate and its key: the key of the disk signs, and nothing is enrolled again
                using (var job = new SbJob())
                {
                    job.Enabled = true; job.SigningTime = "261118120000Z";
                    SbBuild b = job.PrepareUpdate(plan, cert, has, out writeSlot);
                    WriteResult wrote;
                    byte[] kernel = ReadFatFile(WrittenSystem(plan, b.System, out wrote), "KERNEL");
                    Check("an update of a Hades that has its certificate and its key in the slot: the key of the disk signs, the certificate is the one that is enrolled, " +
                          "nothing is enrolled again and the slot is not written",
                          job.KeyKept && !job.NewKey && job.Code == null && !writeSlot && b.Cert.SequenceEqual(cert) && Authenticode.Verify(kernel, cert) == null);
                    var fake = new FakeFirmware();
                    job.Firmware = fake;
                    job.Finish();
                    Check("... and the request to the firmware is not made", !job.Enrolled && !job.Manual && fake.Log.Count == 0);
                }
                // (b) it has its certificate and the slot is empty: a new key, only if the user says yes
                using (var job = new SbJob())
                {
                    job.Enabled = true; job.SigningTime = "261118120000Z";
                    SecureKeyException asked = null;
                    try { job.PrepareUpdate(plan, cert, empty, out writeSlot); } catch (SecureKeyException e) { asked = e; }
                    Check("a certificate and an empty slot: the update asks first (nothing is made)", asked != null && asked.Kind == 1 && job.Key == null);
                    job.AllowNewKey = true;
                    SbBuild b = job.PrepareUpdate(plan, cert, empty, out writeSlot);
                    Check("... and with a yes a new key is made, to be written into the slot, with a code to enrol it", writeSlot && job.NewKey && job.Code != null && job.Code.Length == 4 && !b.Cert.SequenceEqual(cert));
                    var fake = new FakeFirmware();
                    job.Firmware = fake;
                    job.Finish();
                    Check("... and once everything is written the request to the firmware is made: three variables, the code kept for the final panel",
                          job.Enrolled && !job.Manual && job.Code != null && fake.Vars.Count == 3 && Enroll.Pending(fake));
                    var nofw = new FakeFirmware();
                    nofw.RefuseName = Mok.New;
                    var again = new SbJob();
                    again.Enabled = true; again.AllowNewKey = true;
                    again.PrepareUpdate(plan, cert, empty, out writeSlot);
                    again.Firmware = nofw;
                    again.Finish();
                    Check("... where the firmware refuses it, nothing is left in the firmware, no code is shown, and the key is enrolled from the disk instead",
                          !again.Enrolled && again.Manual && again.Code == null && nofw.Vars.Count == 0);
                    again.Dispose();
                }
                // (c) its slot holds another key than the certificate's: it is never overwritten; the update does not go on with Secure Boot
                using (var job = new SbJob())
                {
                    job.Enabled = true;
                    SecureKeyException refused = null;
                    try { job.PrepareUpdate(plan, cert, hasOther, out writeSlot); } catch (SecureKeyException e) { refused = e; }
                    Check("a slot with a key that is not the certificate's: refused, and the slot is not touched", refused != null && refused.Kind == 2);
                }
                // (d) no certificate, an empty slot: a new key; (e) no certificate, a key in the slot: that key, with a new certificate
                using (var job = new SbJob())
                {
                    job.Enabled = true;
                    job.PrepareUpdate(plan, null, empty, out writeSlot);
                    Check("a Hades that has no certificate and an empty slot: a new key, to be written into the slot, and enrolled", writeSlot && job.NewKey && job.Code != null);
                }
                using (var job = new SbJob())
                {
                    job.Enabled = true;
                    job.PrepareUpdate(plan, null, has, out writeSlot);
                    Check("... one that has no certificate and a key in the slot (an earlier try): that key is used, with a certificate of its own, and enrolled",
                          !writeSlot && job.NewKey && job.Code != null && SbCert.Check(job.Cert, key) == null);
                }
                // (f) no slot at all, and (g) a file system that was not shut down cleanly
                var dirty = (byte[])emptySlot.Clone();
                dirty[1024 + 0x3A] = 0;
                SbSlot notClean = SbSlot.Read(DiagFolder.Reader(dirty), dirty.Length);
                SbSlot none = SbSlot.Read(DiagFolder.Reader(new byte[1 << 20]), 1 << 20);
                foreach (SbSlot bad in new[] { none, notClean })
                    using (var job = new SbJob())
                    {
                        job.Enabled = true;
                        SecureKeyException refused = null;
                        try { job.PrepareUpdate(plan, null, bad, out writeSlot); } catch (SecureKeyException e) { refused = e; }
                        Check("a storage with no key slot (start Hades once with this version), or one that was not shut down cleanly: nothing is written, and it is said (" +
                              (bad == none ? "no slot" : "not clean") + ")",
                              refused != null && refused.Kind == 2 && refused.Message.Contains(bad == none ? "key slot" : "shut down cleanly"));
                    }
                // the commands that write what an install and an update write, with a fixed code and time, and the key of the test for the one that sbsign repeats
                string install = Path.Combine(directory, "sim-install"), update = Path.Combine(directory, "sim-update");
                int code1 = SimulateSecureBoot(install, s.Path, "4821", "261118120000Z", false, false, false);
                Check("--simulate-secureboot: system.bin, storage.bin, MokNew.bin, MokAuth.bin, MokTimeout.bin, cert.der and code.txt are written, and they agree",
                      code1 == 0 && SimulationAgrees(install, "4821"));
                if (code1 == 0)
                {
                    int code2 = SimulateUpdateSecureBoot(update, s.Path, Path.Combine(install, "storage.bin"));
                    byte[] cert1 = File.ReadAllBytes(Path.Combine(install, "cert.der"));
                    byte[] sys2 = code2 == 0 ? File.ReadAllBytes(Path.Combine(update, "system.bin")) : null;
                    Check("--simulate-update-secureboot: the key is read from that storage.bin, the certificate from the system.bin beside it, the image is signed with them, and the " +
                          "partition that is written has the same certificate",
                          code2 == 0 && FatProblems(sys2) == null && ReadFatFile(sys2, "HADES-SB.CER").SequenceEqual(cert1) &&
                          Authenticode.Verify(ReadFatFile(sys2, "KERNEL"), cert1) == null && Authenticode.Verify(ReadFatFile(sys2, "EFI/BOOT/grubx64.efi"), cert1) == null);
                }
            }
        }

        static bool SimulationAgrees(string folder, string code)
        {
            byte[] sys = File.ReadAllBytes(Path.Combine(folder, "system.bin")), cert = File.ReadAllBytes(Path.Combine(folder, "cert.der"));
            byte[] mokNew = File.ReadAllBytes(Path.Combine(folder, "MokNew.bin")), auth = File.ReadAllBytes(Path.Combine(folder, "MokAuth.bin"));
            byte[] timeout = File.ReadAllBytes(Path.Combine(folder, "MokTimeout.bin"));
            byte[] sto = File.ReadAllBytes(Path.Combine(folder, "storage.bin"));
            SbSlot slot = SbSlot.Read(DiagFolder.Reader(sto), sto.Length);
            return File.ReadAllText(Path.Combine(folder, "code.txt")).Trim() == code && mokNew.SequenceEqual(Mok.MokNew(cert)) && auth.SequenceEqual(Mok.MokAuth(mokNew, code)) &&
                   timeout.SequenceEqual(Mok.MokTimeout()) && FatProblems(sys) == null && ReadFatFile(sys, "HADES-SB.CER").SequenceEqual(cert) &&
                   Authenticode.Verify(ReadFatFile(sys, "KERNEL"), cert) == null && slot.Valid && !slot.Empty;
        }

        // ---- the real image

        static void SecureBootReal(string directory, string image)
        {
            ImagePlan plan = Engine.Plan(image, true, null, null);
            ImagePlan quick = Engine.Plan(image, false, null, null);
            var clock = System.Diagnostics.Stopwatch.StartNew();
            Engine.ProbeSecureBoot(quick);
            clock.Stop();
            Say("real image: the quick look for Secure Boot (what the window does in the background when an image is chosen) took " + clock.ElapsedMilliseconds + " ms");
            Check("real image: the quick look and the read through agree on whether the image can start with Secure Boot (" + quick.SecureState + ")", quick.SecureState == plan.SecureState);
            Say("real image for Secure Boot: state " + plan.SecureState + (plan.Sb.Known ? ", complete" : ", " + plan.Sb.Why));
            if (plan.SecureState != 1 || !plan.Sb.Known)
            {
                Check("real image without /secureboot: the box of Secure Boot is off, and the image says why (" + plan.Sb.Why + ")",
                      plan.SecureState == 2 && !plan.Sb.Known && plan.Sb.Why != null && plan.Sb.Why.Contains("/secureboot") &&
                      Throws(delegate { using (SbKey k = SbKey.Generate()) SecureBoot.Build(plan, k, SbCert.Make(k, DateTime.UtcNow, null), true, "261118120000Z"); }));
                Say("NOT RUN: the whole path of Secure Boot over the real image (it has no /secureboot).");
                ++notRun;
                return;
            }
            Check("real image with /secureboot: GRUB and the kernel are PE files that are not signed, and shim and MokManager are the signed programs of Debian",
                  new PeFile((byte[])plan.Sb.GrubBytes.Clone()).CertSize == 0 && new PeFile((byte[])plan.Sb.KernelBytes.Clone()).CertSize == 0 &&
                  new PeFile((byte[])plan.Sb.ShimBytes.Clone()).CertSize > 0 && new PeFile((byte[])plan.Sb.MmBytes.Clone()).CertSize > 0);
            SbSlot slot = SbSlot.Read(DiagFolder.Reader(plan.Sb.Storage), plan.Sb.Storage.Length);
            Check("... and its storage has the key slot, empty", slot.Valid && slot.Empty && slot.Clean);
            using (SbKey key = SbKey.Generate())
            {
                byte[] cert = SbCert.Make(key, DateTime.UtcNow, null);
                SbBuild build = SecureBoot.Build(plan, key, cert, true, Authenticode.Now());
                WriteResult wr;
                byte[] sys = WrittenSystem(plan, Engine.Patches(plan, false, false, null, false, null, build), out wr);
                byte[] sto = WrittenStorage(plan, Engine.Patches(plan, false, false, null, false, null, build));
                byte[] gx = ReadFatFile(sys, "EFI/BOOT/grubx64.efi"), kx = ReadFatFile(sys, "KERNEL");
                Check("real image: written to the system partition and read back; the FAT is sound; grubx64.efi and KERNEL verify with the certificate of the PC; shim is bootx64.efi",
                      wr.Verified && FatProblems(sys) == null && gx != null && kx != null && Authenticode.Verify(gx, cert) == null && Authenticode.Verify(kx, cert) == null &&
                      ReadFatFile(sys, "EFI/BOOT/bootx64.efi").SequenceEqual(plan.Sb.ShimBytes) && ReadFatFile(sys, "EFI/BOOT/mmx64.efi").SequenceEqual(plan.Sb.MmBytes));
                SbSlot after = SbSlot.Read(DiagFolder.Reader(sto), sto.Length);
                Check("... and the slot of its storage holds the key that signed", after.Valid && after.KeyPem == key.Pem());
                File.WriteAllBytes(Path.Combine(directory, "real-sb-system.bin"), sys);
                File.WriteAllBytes(Path.Combine(directory, "real-sb-storage.bin"), sto);
                File.WriteAllBytes(Path.Combine(directory, "real-sb-cert.der"), cert);
                File.WriteAllBytes(Path.Combine(directory, "real-sb-grub-original.efi"), plan.Sb.GrubBytes);
                File.WriteAllBytes(Path.Combine(directory, "real-sb-kernel-original.efi"), plan.Sb.KernelBytes);
                using (SbKey test = TestKey())
                {
                    byte[] testCert = SbCert.Make(test, new DateTime(2026, 11, 18, 0, 0, 0, DateTimeKind.Utc), new byte[16]);
                    File.WriteAllBytes(Path.Combine(directory, "real-sb-test-cert.der"), testCert);
                    SbBuild t = SecureBoot.Build(plan, test, testCert, true, "261118120000Z");
                    File.WriteAllBytes(Path.Combine(directory, "real-sb-test-grub-signed.efi"), t.Grub);
                    File.WriteAllBytes(Path.Combine(directory, "real-sb-test-kernel-signed.efi"), t.Kernel);
                }
            }
        }

        // ---- the commands

        // --simulate-secureboot: what an install with Secure Boot writes, as files. testKey: the throw-away key of the tests (for the comparison with sbsign);
        // inside: for a disk inside the PC; diagnostics: with the diagnostics on.
        public static int SimulateSecureBoot(string directory, string image, string code, string time, bool testKey, bool inside, bool diagnostics)
        {
            Directory.CreateDirectory(directory);
            ImagePlan plan = Engine.Plan(image, true, null, null);
            if (!plan.Sb.Known) { Console.Error.WriteLine("this image cannot start with Secure Boot: " + plan.Sb.Why); return 1; }
            if (code != null && (code.Length != 4 || !code.All(char.IsDigit))) return 2;
            if (time != null && !Authenticode.IsSigningTime(time)) return 2;
            using (SbKey key = testKey ? TestKey() : SbKey.Generate())
            {
                byte[] cert = testKey ? SbCert.Make(key, new DateTime(2026, 11, 18, 0, 0, 0, DateTimeKind.Utc), new byte[16]) : SbCert.Make(key, DateTime.UtcNow, null);
                SbBuild build = SecureBoot.Build(plan, key, cert, true, time ?? Authenticode.Now());
                List<Patch> patches = Engine.Patches(plan, inside, false, null, diagnostics && plan.SwitchOffset >= 0, null, build);
                WriteResult wr;
                File.WriteAllBytes(Path.Combine(directory, "system.bin"), WrittenSystem(plan, patches, out wr));
                bool ok = wr.Verified;
                File.WriteAllBytes(Path.Combine(directory, "storage.bin"), WrittenStorage(plan, patches));
                code = code ?? Mok.NewCode();
                byte[] mokNew = Mok.MokNew(cert);
                File.WriteAllBytes(Path.Combine(directory, "MokNew.bin"), mokNew);
                File.WriteAllBytes(Path.Combine(directory, "MokAuth.bin"), Mok.MokAuth(mokNew, code));
                File.WriteAllBytes(Path.Combine(directory, "MokTimeout.bin"), Mok.MokTimeout());
                File.WriteAllBytes(Path.Combine(directory, "cert.der"), cert);
                File.WriteAllText(Path.Combine(directory, "code.txt"), code + "\n");
                File.WriteAllText(Path.Combine(directory, "simulate.txt"), "image " + Path.GetFileName(image) + "; key " + key.Short() + "; " + build.Summary + "; verified " + ok + "\n");
                return ok ? 0 : 1;
            }
        }

        // --simulate-update-secureboot: the update of a Hades that has Secure Boot: its key is read from the storage.bin, its certificate from the system.bin beside
        // it (or from "old=<file>"), and the new system.bin is written. Nothing is written to a disk or to the firmware.
        public static int SimulateUpdateSecureBoot(string directory, string image, string storageFile)
        {
            Directory.CreateDirectory(directory);
            ImagePlan plan = Engine.Plan(image, true, null, null);
            if (!plan.Sb.Known) { Console.Error.WriteLine("this image cannot start with Secure Boot: " + plan.Sb.Why); return 1; }
            byte[] storage = File.ReadAllBytes(storageFile);
            string oldFile = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(storageFile)), "system.bin");
            byte[] oldSystem = File.Exists(oldFile) ? File.ReadAllBytes(oldFile) : null;
            byte[] oldCert = oldSystem == null ? null : SbDisk.Cert(DiagFolder.Reader(oldSystem), oldSystem.Length);
            using (var job = new SbJob())
            {
                job.Enabled = true;
                bool writeSlot;
                SbBuild build = job.PrepareUpdate(plan, oldCert, SbSlot.Read(DiagFolder.Reader(storage), storage.Length), out writeSlot);
                string existing = Engine.Ext4Uuid(Der.Slice(storage, 0, 2048));
                List<Patch> patches = Engine.Patches(plan, false, false, plan.StorageUuidOffsets.Count == 4 && existing != null ? existing : null, false, null, build);
                WriteResult wr;
                File.WriteAllBytes(Path.Combine(directory, "system.bin"), WrittenSystem(plan, patches, out wr));
                File.WriteAllText(Path.Combine(directory, "update.txt"), "image " + Path.GetFileName(image) + "; the key of the disk " + (job.KeyKept ? "kept" : "NOT kept") + "; " +
                                  build.Summary + "; verified " + wr.Verified + "\n");
                return wr.Verified && job.KeyKept && !writeSlot ? 0 : 1;
            }
        }
    }
}
