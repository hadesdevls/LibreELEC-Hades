// LibreELEC Hades installer for Windows: Secure Boot. The key of this PC (made here, kept only in the key slot of the
// Hades storage), the X.509 certificate of that key, the Authenticode signature of GRUB and of the kernel (the same
// bytes that sbsign 0.9.4 writes for the same file, key, certificate and signing time: tools/hades-secureboot of the
// build compares them), the three variables with which Hades asks the firmware to enrol the key (MokNew, MokAuth and
// MokTimeout, shim's own), and the read-only look at the firmware that decides whether the box is offered.
// Built with the C# compiler of the .NET Framework that Windows brings (build.ps1): C# 5 only.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;

namespace Hades.Installer
{
    // ---- DER: what is written here, and what is read back to check it

    static class Der
    {
        public sealed class Node
        {
            public int Tag, At, Start, End; // At: where the element starts; Start, End: its content
        }

        public static byte[] Cat(params byte[][] parts)
        {
            int n = 0;
            foreach (byte[] p in parts) n += p.Length;
            var all = new byte[n];
            int at = 0;
            foreach (byte[] p in parts) { Buffer.BlockCopy(p, 0, all, at, p.Length); at += p.Length; }
            return all;
        }

        public static byte[] Len(int n)
        {
            if (n < 0x80) return new[] { (byte)n };
            var l = new List<byte>();
            for (int v = n; v > 0; v >>= 8) l.Insert(0, (byte)(v & 0xFF));
            l.Insert(0, (byte)(0x80 | l.Count));
            return l.ToArray();
        }

        public static byte[] Tlv(byte tag, byte[] content) { return Cat(new[] { tag }, Len(content.Length), content); }
        public static byte[] Seq(params byte[][] parts) { return Tlv(0x30, Cat(parts)); }
        public static byte[] Set(params byte[][] parts) { return Tlv(0x31, Cat(parts)); }
        public static readonly byte[] Null = { 5, 0 };

        public static byte[] Oid(string dotted)
        {
            string[] s = dotted.Split('.');
            var n = new long[s.Length];
            for (int i = 0; i < s.Length; ++i) n[i] = long.Parse(s[i]);
            var body = new List<byte>();
            body.Add((byte)(40 * n[0] + n[1]));
            for (int i = 2; i < n.Length; ++i)
            {
                var chunk = new List<byte>();
                long v = n[i];
                chunk.Add((byte)(v & 0x7F));
                v >>= 7;
                while (v > 0) { chunk.Add((byte)(0x80 | (v & 0x7F))); v >>= 7; }
                chunk.Reverse();
                body.AddRange(chunk);
            }
            return Tlv(6, body.ToArray());
        }

        // INTEGER of a number that is not negative
        public static byte[] Int(long v)
        {
            var b = new List<byte>();
            long x = v;
            do { b.Insert(0, (byte)(x & 0xFF)); x >>= 8; } while (x > 0);
            if ((b[0] & 0x80) != 0) b.Insert(0, 0);
            return Tlv(2, b.ToArray());
        }

        // INTEGER of a big number, as its bytes (most significant first, not negative)
        public static byte[] Big(byte[] value)
        {
            int skip = 0;
            while (skip < value.Length - 1 && value[skip] == 0) ++skip;
            var b = new List<byte>();
            if ((value[skip] & 0x80) != 0) b.Add(0);
            for (int i = skip; i < value.Length; ++i) b.Add(value[i]);
            return Tlv(2, b.ToArray());
        }

        public static byte[] Utf8(string text) { return Tlv(0x0C, Encoding.UTF8.GetBytes(text)); }
        public static byte[] Octets(byte[] data) { return Tlv(4, data); }

        public static int Compare(byte[] a, byte[] b)
        {
            int n = Math.Min(a.Length, b.Length);
            for (int i = 0; i < n; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
            return a.Length == b.Length ? 0 : a.Length < b.Length ? -1 : 1;
        }

        public static Node Read(byte[] buf, int at, int limit)
        {
            if (at + 2 > limit) throw new InvalidDataException("DER: truncated");
            var n = new Node();
            n.At = at;
            n.Tag = buf[at];
            int len = buf[at + 1];
            at += 2;
            if ((len & 0x80) != 0)
            {
                int k = len & 0x7F;
                if (k == 0 || k > 4 || at + k > limit) throw new InvalidDataException("DER: bad length");
                len = 0;
                for (int i = 0; i < k; ++i) len = (len << 8) | buf[at + i];
                at += k;
            }
            if (len < 0 || (long)at + len > limit) throw new InvalidDataException("DER: element longer than its container");
            n.Start = at;
            n.End = at + len;
            return n;
        }

        public static List<Node> Kids(byte[] buf, Node parent)
        {
            var list = new List<Node>();
            int at = parent.Start;
            while (at < parent.End)
            {
                Node k = Read(buf, at, parent.End);
                list.Add(k);
                at = k.End;
            }
            return list;
        }

        public static byte[] Slice(byte[] buf, int from, int to)
        {
            var r = new byte[to - from];
            Buffer.BlockCopy(buf, from, r, 0, r.Length);
            return r;
        }

        public static byte[] Whole(byte[] buf, Node n) { return Slice(buf, n.At, n.End); }
        public static byte[] Content(byte[] buf, Node n) { return Slice(buf, n.Start, n.End); }

        public static byte[] Unsigned(byte[] buf, Node integer)
        {
            byte[] c = Content(buf, integer);
            int skip = 0;
            while (skip < c.Length - 1 && c[skip] == 0) ++skip;
            return Slice(c, skip, c.Length);
        }
    }

    // ---- the PE file whose signature is made (sbsigntools, src/image.c: the same regions, the same padding, the same
    // "endjunk", the same checksum)

    sealed class PeFile
    {
        public byte[] Buf;
        int sections, checksumAt, headerSize, sigdirAt, scnAt;
        public uint CertAddr, CertSize;
        public List<long[]> Regions;
        public long DataSize;

        // data is taken over (it is padded, when its regions end past its size, in a copy)
        public PeFile(byte[] data)
        {
            Buf = data;
            Parse();
            FindRegions();
            // an image whose regions end past its size (they are rounded to 8 bytes) is padded with zeros and read again, and
            // the padding is hashed (sbsign's image_load(): "reparse")
            if (DataSize > Buf.Length)
            {
                Array.Resize(ref Buf, (int)DataSize);
                Parse();
                FindRegions();
            }
        }

        uint U16(int at) { return BitConverter.ToUInt16(Buf, at); }
        uint U32(int at) { return BitConverter.ToUInt32(Buf, at); }

        void Parse()
        {
            byte[] b = Buf;
            if (b.Length < 64 || b[0] != (byte)'M' || b[1] != (byte)'Z') throw new InvalidDataException("not a PE file (no MZ header)");
            long pe = U32(0x3C);
            if (pe + 24 > b.Length || b[pe] != (byte)'P' || b[pe + 1] != (byte)'E' || b[pe + 2] != 0 || b[pe + 3] != 0)
                throw new InvalidDataException("not a PE file (no PE header)");
            int p = (int)pe;
            uint machine = U16(p + 4);
            sections = (int)U16(p + 6);
            int optSize = (int)U16(p + 20);
            int opt = p + 24;
            uint magic = U16(opt);
            int dd, minSize;
            if (machine == 0x8664 || machine == 0xAA64)
            {
                if (magic != 0x20B) throw new InvalidDataException("invalid a.out machine type");
                dd = opt + 112; minSize = 112;
            }
            else if (machine == 0x14C || machine == 0x1C2)
            {
                if (magic != 0x10B) throw new InvalidDataException("invalid a.out machine type");
                dd = opt + 96; minSize = 96;
            }
            else throw new InvalidDataException("invalid PE header magic");
            if (optSize < minSize + 8 * 5) throw new InvalidDataException("PE optional header too small for the certificate table entry");
            checksumAt = opt + 64;
            headerSize = (int)U32(opt + 60);
            sigdirAt = dd + 8 * 4;
            CertAddr = U32(sigdirAt);
            CertSize = U32(sigdirAt + 4);
            scnAt = opt + optSize;
            if (scnAt + 40L * sections > b.Length) throw new InvalidDataException("section table past the end of the file");
        }

        void FindRegions()
        {
            var regions = new List<long[]>();
            long nbytes = 0;
            regions.Add(new long[] { 0, checksumAt });
            nbytes += checksumAt + 4;
            regions.Add(new long[] { checksumAt + 4, sigdirAt - (checksumAt + 4) });
            nbytes += sigdirAt - (checksumAt + 4) + 8;
            regions.Add(new long[] { sigdirAt + 8, headerSize - (sigdirAt + 8) });
            nbytes += headerSize - (sigdirAt + 8);
            for (int i = 0; i < sections; ++i)
            {
                int s = scnAt + 40 * i;
                long size = U32(s + 16), offset = U32(s + 20);
                if (size == 0) continue;
                regions.Add(new long[] { offset, size });
                nbytes += size;
            }
            // (a stable sort by offset, as Python's sort is)
            regions = regions.OrderBy(delegate(long[] r) { return r[0]; }).ToList();
            if (nbytes + CertSize < Buf.Length)
                // (sbsign starts the region of what is left at the sum of the sizes, not at the end of the last region)
                regions.Add(new long[] { nbytes, Buf.Length - nbytes - CertSize });
            long[] last = regions[regions.Count - 1];
            Regions = regions;
            DataSize = (last[0] + last[1] + 7) & ~7L;
            foreach (long[] r in regions)
                if (r[0] < 0 || r[1] < 0 || r[0] + r[1] > Math.Max(Buf.Length, DataSize))
                    throw new InvalidDataException("a region of the file is past its end");
        }

        public byte[] Digest()
        {
            using (var h = SHA256.Create())
            {
                foreach (long[] r in Regions) h.TransformBlock(Buf, (int)r[0], (int)r[1], null, 0);
                h.TransformFinalBlock(new byte[0], 0, 0);
                return h.Hash;
            }
        }

        // the PKCS#7 of every WIN_CERTIFICATE (revision 2.0, type 2) of the certificate table
        public List<byte[]> Signatures()
        {
            var list = new List<byte[]>();
            if (CertSize == 0) return list;
            long at = CertAddr, end = (long)CertAddr + CertSize;
            if (end > Buf.Length) throw new InvalidDataException("the certificate table is past the end of the file");
            while (at + 8 <= end)
            {
                uint length = U32((int)at);
                uint revision = U16((int)at + 4), type = U16((int)at + 6);
                if (length < 8 || at + length > end) break;
                if (revision == 0x200 && type == 2) list.Add(Der.Slice(Buf, (int)at + 8, (int)(at + length)));
                at += (length + 7) & ~7L;
            }
            return list;
        }

        // The file with the signature appended, as sbsign's image_add_signature() and image_write() do.
        public byte[] SignedBytes(byte[] pkcs7)
        {
            if (CertSize != 0) throw new InvalidDataException("the file is signed already");
            int total = pkcs7.Length + 8, aligned = (total + 7) & ~7;
            var sig = new byte[aligned];
            Buffer.BlockCopy(BitConverter.GetBytes(total), 0, sig, 0, 4);
            sig[4] = 0x00; sig[5] = 0x02; sig[6] = 0x02; sig[7] = 0x00;
            Buffer.BlockCopy(pkcs7, 0, sig, 8, pkcs7.Length);
            int data = (int)DataSize;
            var all = new byte[data + sig.Length];
            Buffer.BlockCopy(Buf, 0, all, 0, data);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)data), 0, all, sigdirAt, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)sig.Length), 0, all, sigdirAt + 4, 4);
            Buffer.BlockCopy(sig, 0, all, data, sig.Length);
            uint csum = Sum(0, all, 0, checksumAt);
            csum = Sum(csum, all, checksumAt + 4, data - (checksumAt + 4));
            csum = Sum(csum, all, data, sig.Length);
            csum = (uint)(((ulong)csum + (ulong)data + (ulong)sig.Length) & 0xFFFFFFFFUL);
            Buffer.BlockCopy(BitConverter.GetBytes(csum), 0, all, checksumAt, 4);
            return all;
        }

        // the PE checksum over a piece of the file (image_pecoff_update_checksum())
        static uint Sum(uint csum, byte[] data, int at, int count)
        {
            int n = count / 2;
            ulong c = csum;
            for (int i = 0; i < n; ++i)
            {
                c += (ulong)(data[at + 2 * i] | (data[at + 2 * i + 1] << 8));
                c = (c >> 16) + (c & 0xFFFF);
            }
            if ((count & 1) != 0)
            {
                c += data[at + count - 1];
                c = (c >> 16) + (c & 0xFFFF);
            }
            return (uint)c;
        }
    }

    // ---- the key of this PC

    // An RSA key of 2048 bits that is made here, with the public exponent 65537. CngKey.Create with no name makes an
    // ephemeral key: Windows keeps it in memory of this process alone (no key container, no file of the user's profile, not
    // under ProgramData or the machine's keys), and it is gone when the process ends. Its one lasting copy is the PEM
    // that goes into the key slot of the Hades storage (SbSlot).
    sealed class SbKey : IDisposable
    {
        const string OidRsa = "1.2.840.113549.1.1.1";
        readonly RSACng rsa;
        readonly CngKey cng;
        RSAParameters parameters;

        SbKey(RSACng rsa, CngKey cng, RSAParameters parameters)
        {
            this.rsa = rsa;
            this.cng = cng;
            this.parameters = parameters;
        }

        public static SbKey Generate()
        {
            var make = new CngKeyCreationParameters();
            make.ExportPolicy = CngExportPolicies.AllowPlaintextExport;
            make.KeyCreationOptions = CngKeyCreationOptions.None;
            make.Parameters.Add(new CngProperty("Length", BitConverter.GetBytes(2048), CngPropertyOptions.None));
            CngKey key = CngKey.Create(CngAlgorithm.Rsa, null, make);
            var rsa = new RSACng(key);
            return new SbKey(rsa, key, rsa.ExportParameters(true));
        }

        // False for a key that Windows keeps by a name (in a container): this one is not.
        public bool Ephemeral { get { return cng != null && cng.KeyName == null && !cng.IsMachineKey; } }
        public bool HasPrivate { get { return parameters.D != null; } }
        public int Bits { get { return parameters.Modulus.Length * 8; } }
        public byte[] Modulus { get { return parameters.Modulus; } }
        public byte[] Exponent { get { return parameters.Exponent; } }

        public static SbKey FromParameters(RSAParameters p)
        {
            var rsa = new RSACng();
            rsa.ImportParameters(p);
            return new SbKey(rsa, null, p);
        }

        // PKCS#8 as DER, from the PEM of the key slot (nothing before it: SbSlot gives only the block)
        public static SbKey FromPem(string pem)
        {
            const string head = "-----BEGIN PRIVATE KEY-----", tail = "-----END PRIVATE KEY-----";
            int a = pem.IndexOf(head, StringComparison.Ordinal), b = pem.IndexOf(tail, StringComparison.Ordinal);
            if (a < 0 || b < a) throw new InvalidDataException("the key is not a PKCS#8 PEM");
            string body = pem.Substring(a + head.Length, b - a - head.Length);
            byte[] der;
            try { der = Convert.FromBase64String(string.Concat(body.Split(new[] { '\r', '\n', ' ' }, StringSplitOptions.RemoveEmptyEntries))); }
            catch (FormatException) { throw new InvalidDataException("the key is not valid base64"); }
            Der.Node top = Der.Read(der, 0, der.Length);
            if (top.Tag != 0x30 || top.End != der.Length) throw new InvalidDataException("the key is not a DER sequence");
            List<Der.Node> k = Der.Kids(der, top);
            if (k.Count < 3 || k[0].Tag != 2 || k[1].Tag != 0x30 || k[2].Tag != 4) throw new InvalidDataException("the key is not a PrivateKeyInfo");
            if (Der.Compare(Der.Whole(der, Der.Kids(der, k[1])[0]), Der.Oid(OidRsa)) != 0)
                throw new InvalidDataException("the key is not an RSA key");
            Der.Node inner = Der.Read(der, k[2].Start, k[2].End);
            List<Der.Node> f = Der.Kids(der, inner);
            if (inner.Tag != 0x30 || f.Count < 9) throw new InvalidDataException("the key is not an RSAPrivateKey");
            byte[] n = Der.Unsigned(der, f[1]);
            int half = (n.Length + 1) / 2;
            var p = new RSAParameters();
            p.Modulus = n;
            p.Exponent = Der.Unsigned(der, f[2]);
            p.D = Pad(Der.Unsigned(der, f[3]), n.Length);
            p.P = Pad(Der.Unsigned(der, f[4]), half);
            p.Q = Pad(Der.Unsigned(der, f[5]), half);
            p.DP = Pad(Der.Unsigned(der, f[6]), half);
            p.DQ = Pad(Der.Unsigned(der, f[7]), half);
            p.InverseQ = Pad(Der.Unsigned(der, f[8]), half);
            if (n.Length < 256) throw new InvalidDataException("the key is smaller than 2048 bits");
            return FromParameters(p);
        }

        static byte[] Pad(byte[] v, int size)
        {
            if (v.Length > size) throw new InvalidDataException("a number of the key is longer than its modulus");
            var r = new byte[size];
            Buffer.BlockCopy(v, 0, r, size - v.Length, v.Length);
            return r;
        }

        public byte[] Sign(byte[] sha256)
        {
            return rsa.SignHash(sha256, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        }

        public byte[] Pkcs8()
        {
            RSAParameters p = parameters;
            byte[] rsaKey = Der.Seq(Der.Int(0), Der.Big(p.Modulus), Der.Big(p.Exponent), Der.Big(p.D), Der.Big(p.P), Der.Big(p.Q),
                                    Der.Big(p.DP), Der.Big(p.DQ), Der.Big(p.InverseQ));
            return Der.Seq(Der.Int(0), Der.Seq(Der.Oid(OidRsa), Der.Null), Der.Octets(rsaKey));
        }

        // "-----BEGIN PRIVATE KEY-----", the base64 in lines of 64, "-----END PRIVATE KEY-----", each line ending in LF
        public string Pem()
        {
            string b64 = Convert.ToBase64String(Pkcs8());
            var sb = new StringBuilder("-----BEGIN PRIVATE KEY-----\n");
            for (int i = 0; i < b64.Length; i += 64) sb.Append(b64, i, Math.Min(64, b64.Length - i)).Append('\n');
            return sb.Append("-----END PRIVATE KEY-----\n").ToString();
        }

        // SubjectPublicKeyInfo
        public byte[] Spki()
        {
            return Der.Seq(Der.Seq(Der.Oid(OidRsa), Der.Null),
                           Der.Tlv(3, Der.Cat(new byte[] { 0 }, Der.Seq(Der.Big(parameters.Modulus), Der.Big(parameters.Exponent)))));
        }

        // The first 4 bytes of the SHA-256 of the public key (its SubjectPublicKeyInfo), as 8 hex digits: what the certificate
        // says in its name, so that it can be told on the blue screen.
        public string Short()
        {
            using (var h = SHA256.Create()) return Words.Hex(h.ComputeHash(Spki())).Substring(0, 8);
        }

        public void Dispose()
        {
            if (rsa != null) rsa.Dispose();
            if (cng != null) cng.Dispose();
            foreach (byte[] a in new[] { parameters.D, parameters.P, parameters.Q, parameters.DP, parameters.DQ, parameters.InverseQ })
                if (a != null) Array.Clear(a, 0, a.Length);
        }
    }

    // ---- its certificate

    static class SbCert
    {
        const string OidSha256Rsa = "1.2.840.113549.1.1.11";
        public const string NameStart = "LibreELEC Hades Secure Boot ";

        // A self-signed X.509 v3 certificate of the key: serial of 16 random bytes (not negative), SHA-256 with RSA, the same
        // issuer and subject "CN=LibreELEC Hades Secure Boot <YYYY-MM-DD> <8 hex>", valid from now (UTCTime) to 9999-12-31
        // 23:59:59 (GeneralizedTime); basicConstraints CA:FALSE (critical), keyUsage digitalSignature (critical),
        // extendedKeyUsage codeSigning, subjectKeyIdentifier. serial: for the tests, the bytes of it; null: random.
        public static byte[] Make(SbKey key, DateTime utcNow, byte[] serial)
        {
            if (serial == null)
            {
                serial = new byte[16];
                using (var rng = new RNGCryptoServiceProvider()) rng.GetBytes(serial);
            }
            if (serial.Length != 16) throw new ArgumentException("the serial is 16 bytes");
            serial = (byte[])serial.Clone();
            serial[0] = (byte)((serial[0] & 0x7F) | 0x40); // positive, and 16 bytes long when written
            string cn = NameStart + utcNow.ToString("yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture) + " " + key.Short();
            byte[] name = Der.Seq(Der.Set(Der.Seq(Der.Oid("2.5.4.3"), Der.Utf8(cn))));
            byte[] alg = Der.Seq(Der.Oid(OidSha256Rsa), Der.Null);
            byte[] spki = key.Spki();
            byte[] spkBits = Der.Slice(spki, Der.Kids(spki, Der.Read(spki, 0, spki.Length))[1].Start + 1, spki.Length);
            byte[] ski;
            using (var sha1 = SHA1.Create()) ski = sha1.ComputeHash(spkBits);
            byte[] validity = Der.Seq(Der.Tlv(0x17, Encoding.ASCII.GetBytes(utcNow.ToString("yyMMddHHmmss", System.Globalization.CultureInfo.InvariantCulture) + "Z")),
                                      Der.Tlv(0x18, Encoding.ASCII.GetBytes("99991231235959Z")));
            Func<string, bool, byte[], byte[]> ext = delegate(string oid, bool critical, byte[] value)
            {
                return critical ? Der.Seq(Der.Oid(oid), Der.Tlv(1, new byte[] { 0xFF }), Der.Octets(value))
                                : Der.Seq(Der.Oid(oid), Der.Octets(value));
            };
            byte[] extensions = Der.Seq(
                ext("2.5.29.19", true, Der.Seq()),                                                       // basicConstraints: CA false
                ext("2.5.29.15", true, new byte[] { 0x03, 0x02, 0x07, 0x80 }),                           // keyUsage: digitalSignature
                ext("2.5.29.37", false, Der.Seq(Der.Oid("1.3.6.1.5.5.7.3.3"))),                          // extendedKeyUsage: codeSigning
                ext("2.5.29.14", false, Der.Octets(ski)));                                               // subjectKeyIdentifier
            byte[] tbs = Der.Seq(Der.Tlv(0xA0, Der.Int(2)), Der.Tlv(2, serial), alg, name, validity, name, spki,
                                 Der.Tlv(0xA3, extensions));
            byte[] sig = key.Sign(Hash(tbs));
            return Der.Seq(tbs, alg, Der.Tlv(3, Der.Cat(new byte[] { 0 }, sig)));
        }

        public static byte[] Hash(byte[] data)
        {
            using (var h = SHA256.Create()) return h.ComputeHash(data);
        }

        // The parts of a certificate: its issuer and serial (each as the whole DER element), its subject, its SubjectPublicKeyInfo.
        public sealed class Parts
        {
            public byte[] Tbs, Issuer, Serial, Subject, Spki, SignatureAlgorithm, Signature;
        }

        public static Parts Parse(byte[] cert)
        {
            Der.Node top = Der.Read(cert, 0, cert.Length);
            if (top.Tag != 0x30 || top.End != cert.Length) throw new InvalidDataException("not one X.509 certificate");
            List<Der.Node> k = Der.Kids(cert, top);
            if (k.Count != 3 || k[0].Tag != 0x30 || k[2].Tag != 3) throw new InvalidDataException("not an X.509 certificate");
            List<Der.Node> t = Der.Kids(cert, k[0]);
            int i = t.Count > 0 && t[0].Tag == 0xA0 ? 1 : 0;
            if (t.Count < i + 6) throw new InvalidDataException("a certificate with too few fields");
            var p = new Parts();
            p.Tbs = Der.Whole(cert, k[0]);
            p.Serial = Der.Whole(cert, t[i]);
            p.Issuer = Der.Whole(cert, t[i + 2]);
            p.Subject = Der.Whole(cert, t[i + 4]);
            p.Spki = Der.Whole(cert, t[i + 5]);
            p.SignatureAlgorithm = Der.Whole(cert, k[1]);
            byte[] bits = Der.Content(cert, k[2]);
            p.Signature = Der.Slice(bits, 1, bits.Length);
            return p;
        }

        // null where the certificate is one that this program made for that key: its own name, its public key, its signature
        // (checked by Windows, which reads it with its own parser), its validity and its extensions. Else what is wrong.
        public static string Check(byte[] cert, SbKey key)
        {
            try
            {
                Parts p = Parse(cert);
                if (Der.Compare(p.Spki, key.Spki()) != 0) return "its public key is not the key's";
                if (Der.Compare(p.Issuer, p.Subject) != 0) return "it is not self-signed (issuer and subject differ)";
                using (var x = new X509Certificate2(cert))
                {
                    if (!x.Subject.StartsWith("CN=" + NameStart, StringComparison.Ordinal)) return "its subject is " + x.Subject;
                    if (!x.Subject.EndsWith(" " + key.Short(), StringComparison.Ordinal)) return "its subject does not end with the short name of the key";
                    if (x.NotAfter.ToUniversalTime().Year != 9999) return "it does not last to the year 9999";
                    if (x.NotBefore.ToUniversalTime() > DateTime.UtcNow.AddMinutes(5)) return "it starts in the future";
                    if (x.SerialNumber.Length != 32) return "its serial is not 16 bytes";
                    if (x.SignatureAlgorithm.Value != OidSha256Rsa) return "its signature is not SHA-256 with RSA";
                    bool basic = false, usage = false, eku = false, ski = false;
                    foreach (X509Extension e in x.Extensions)
                    {
                        var b = e as X509BasicConstraintsExtension;
                        if (b != null) basic = !b.CertificateAuthority && b.Critical;
                        var u = e as X509KeyUsageExtension;
                        if (u != null) usage = u.KeyUsages == X509KeyUsageFlags.DigitalSignature && u.Critical;
                        var k = e as X509EnhancedKeyUsageExtension;
                        if (k != null) eku = k.EnhancedKeyUsages.Count == 1 && k.EnhancedKeyUsages[0].Value == "1.3.6.1.5.5.7.3.3";
                        if (e is X509SubjectKeyIdentifierExtension) ski = true;
                    }
                    if (!basic || !usage || !eku || !ski) return "its extensions are not the ones that were meant";
                    RSA pub = x.GetRSAPublicKey();
                    if (pub == null || !pub.VerifyHash(Hash(p.Tbs), p.Signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1))
                        return "its signature does not verify";
                }
                return null;
            }
            catch (Exception e) { return e.Message; }
        }
    }

    // ---- Authenticode, as sbsign writes it

    static class Authenticode
    {
        const string OidSha256 = "2.16.840.1.101.3.4.2.1", OidSignedData = "1.2.840.113549.1.7.2", OidContentType = "1.2.840.113549.1.9.3",
                     OidMessageDigest = "1.2.840.113549.1.9.4", OidSigningTime = "1.2.840.113549.1.9.5", OidSmimeCaps = "1.2.840.113549.1.9.15",
                     OidSpcIndirect = "1.3.6.1.4.1.311.2.1.4", OidSpcPeImage = "1.3.6.1.4.1.311.2.1.15", OidRsa = "1.2.840.113549.1.1.1";

        // "<<<Obsolete>>>" in UTF-16BE, as sbsigntools writes it (idc.c, obsolete[])
        static readonly byte[] Obsolete = Encoding.BigEndianUnicode.GetBytes("<<<Obsolete>>>");

        // The SMIMECapabilities that OpenSSL 3 puts in a signature made by PKCS7_sign_add_signer() without PKCS7_NOSMIMECAP
        // (what sbsign does): AES-256, -192 and -128 in CBC, 3DES, RC2 with 128, 64 and 40 bits, DES.
        static byte[] SmimeCaps()
        {
            return Der.Seq(
                Der.Seq(Der.Oid("2.16.840.1.101.3.4.1.42")),
                Der.Seq(Der.Oid("2.16.840.1.101.3.4.1.22")),
                Der.Seq(Der.Oid("2.16.840.1.101.3.4.1.2")),
                Der.Seq(Der.Oid("1.2.840.113549.3.7")),
                Der.Seq(Der.Oid("1.2.840.113549.3.2"), Der.Int(128)),
                Der.Seq(Der.Oid("1.2.840.113549.3.2"), Der.Int(64)),
                Der.Seq(Der.Oid("1.3.14.3.2.7")),
                Der.Seq(Der.Oid("1.2.840.113549.3.2"), Der.Int(40)));
        }

        static byte[] SpcIndirect(byte[] peDigest)
        {
            byte[] peImage = Der.Seq(Der.Tlv(3, new byte[] { 0 }), Der.Tlv(0xA0, Der.Tlv(0xA2, Der.Tlv(0x80, Obsolete))));
            return Der.Seq(Der.Seq(Der.Oid(OidSpcPeImage), peImage), Der.Seq(Der.Seq(Der.Oid(OidSha256), Der.Null), Der.Octets(peDigest)));
        }

        static byte[] SignedAttributes(byte[] idc, string signingTime)
        {
            // (the messageDigest is the hash of the SEQUENCE without its tag and length: sbsign's idc.c writes buf+2, len-2)
            Der.Node top = Der.Read(idc, 0, idc.Length);
            byte[] content = Der.Content(idc, top);
            var attrs = new List<byte[]>();
            attrs.Add(Der.Seq(Der.Oid(OidContentType), Der.Set(Der.Oid(OidSpcIndirect))));
            attrs.Add(Der.Seq(Der.Oid(OidSigningTime), Der.Set(Der.Tlv(0x17, Encoding.ASCII.GetBytes(signingTime)))));
            attrs.Add(Der.Seq(Der.Oid(OidMessageDigest), Der.Set(Der.Octets(SbCert.Hash(content)))));
            attrs.Add(Der.Seq(Der.Oid(OidSmimeCaps), Der.Set(SmimeCaps())));
            // DER: the elements of a SET OF in the order of their encodings
            attrs.Sort(Der.Compare);
            return Der.Cat(attrs.ToArray());
        }

        public static bool IsSigningTime(string t)
        {
            if (t == null || t.Length != 13 || t[12] != 'Z') return false;
            for (int i = 0; i < 12; ++i) if (t[i] < '0' || t[i] > '9') return false;
            return true;
        }

        public static string Now() { return DateTime.UtcNow.ToString("yyMMddHHmmss", System.Globalization.CultureInfo.InvariantCulture) + "Z"; }

        static byte[] MakePkcs7(byte[] peDigest, byte[] cert, SbKey key, string signingTime)
        {
            byte[] idc = SpcIndirect(peDigest);
            byte[] attrs = SignedAttributes(idc, signingTime);
            byte[] sig = key.Sign(SbCert.Hash(Der.Set(attrs)));
            SbCert.Parts parts = SbCert.Parse(cert);
            byte[] sha = Der.Seq(Der.Oid(OidSha256), Der.Null);
            byte[] signer = Der.Seq(Der.Int(1), Der.Seq(parts.Issuer, parts.Serial), sha, Der.Tlv(0xA0, attrs), Der.Seq(Der.Oid(OidRsa), Der.Null),
                                    Der.Octets(sig));
            byte[] signed = Der.Seq(Der.Int(1), Der.Set(sha), Der.Seq(Der.Oid(OidSpcIndirect), Der.Tlv(0xA0, idc)), Der.Tlv(0xA0, cert), Der.Set(signer));
            return Der.Seq(Der.Oid(OidSignedData), Der.Tlv(0xA0, signed));
        }

        public sealed class Parsed
        {
            public byte[] PeDigest, Attributes, Signature, Cert;
        }

        // What a signature says, read back: the hash of the image, the signed attributes as a SET, the signature, the certificate.
        public static Parsed Parse(byte[] p7)
        {
            Der.Node top = Der.Read(p7, 0, p7.Length);
            List<Der.Node> t = Der.Kids(p7, top);
            if (t.Count != 2 || Der.Compare(Der.Whole(p7, t[0]), Der.Oid(OidSignedData)) != 0 || t[1].Tag != 0xA0)
                throw new InvalidDataException("not a PKCS#7 signedData");
            List<Der.Node> sd = Der.Kids(p7, Der.Kids(p7, t[1])[0]);
            // version, digest algorithms, content, [0] certificates, signer infos
            if (sd.Count != 5 || sd[3].Tag != 0xA0) throw new InvalidDataException("a signedData with an unexpected shape");
            List<Der.Node> ci = Der.Kids(p7, sd[2]);
            if (Der.Compare(Der.Whole(p7, ci[0]), Der.Oid(OidSpcIndirect)) != 0) throw new InvalidDataException("not Authenticode content");
            Der.Node idcNode = Der.Kids(p7, ci[1])[0];
            List<Der.Node> idc = Der.Kids(p7, idcNode);
            List<Der.Node> dig = Der.Kids(p7, idc[1]);
            if (Der.Compare(Der.Whole(p7, Der.Kids(p7, dig[0])[0]), Der.Oid(OidSha256)) != 0) throw new InvalidDataException("the image hash is not SHA-256");
            var r = new Parsed();
            r.PeDigest = Der.Content(p7, dig[1]);
            r.Cert = Der.Whole(p7, Der.Kids(p7, sd[3])[0]);
            List<Der.Node> signers = Der.Kids(p7, sd[4]);
            if (signers.Count != 1) throw new InvalidDataException("not one signer");
            List<Der.Node> si = Der.Kids(p7, signers[0]);
            if (si.Count != 6 || si[3].Tag != 0xA0 || si[5].Tag != 4) throw new InvalidDataException("a signer with an unexpected shape");
            r.Attributes = Der.Cat(new byte[] { 0x31 }, Der.Len(si[3].End - si[3].Start), Der.Content(p7, si[3]));
            r.Signature = Der.Content(p7, si[5]);
            byte[] md = null, ctype = null;
            foreach (Der.Node a in Der.Kids(p7, si[3]))
            {
                List<Der.Node> parts = Der.Kids(p7, a);
                byte[] name = Der.Whole(p7, parts[0]);
                Der.Node value = Der.Kids(p7, parts[1])[0];
                if (Der.Compare(name, Der.Oid(OidMessageDigest)) == 0) md = Der.Content(p7, value);
                if (Der.Compare(name, Der.Oid(OidContentType)) == 0) ctype = Der.Whole(p7, value);
            }
            if (ctype == null || Der.Compare(ctype, Der.Oid(OidSpcIndirect)) != 0) throw new InvalidDataException("the contentType attribute is not Authenticode");
            if (md == null || Der.Compare(md, SbCert.Hash(Der.Content(p7, idcNode))) != 0)
                throw new InvalidDataException("the messageDigest attribute is not the hash of the content");
            return r;
        }

        // null where the file has exactly one signature, made with this certificate, over this very file; else what is wrong.
        public static string Verify(byte[] file, byte[] cert)
        {
            try
            {
                var pe = new PeFile((byte[])file.Clone());
                List<byte[]> sigs = pe.Signatures();
                if (sigs.Count != 1) return sigs.Count + " signatures, one expected";
                Parsed p = Parse(sigs[0]);
                if (Der.Compare(p.PeDigest, pe.Digest()) != 0) return "the signature is not of this file";
                if (Der.Compare(p.Cert, cert) != 0) return "signed with another certificate";
                using (var x = new X509Certificate2(cert))
                {
                    RSA pub = x.GetRSAPublicKey();
                    if (pub == null || !pub.VerifyHash(SbCert.Hash(p.Attributes), p.Signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1))
                        return "the signature does not verify with the certificate";
                }
                return null;
            }
            catch (Exception e) { return e.Message; }
        }

        // The file, signed. It is not signed already; and the signature is checked (its hash, and its RSA signature with the public
        // key of the certificate) before it is given. file is not changed. signingTime: YYMMDDHHMMSSZ (the tests fix it).
        public static byte[] Sign(byte[] file, SbKey key, byte[] cert, string signingTime)
        {
            if (!IsSigningTime(signingTime)) throw new ArgumentException("signing time: YYMMDDHHMMSSZ");
            var pe = new PeFile((byte[])file.Clone());
            if (pe.CertSize != 0) throw new InvalidDataException("the file is signed already");
            byte[] digest = pe.Digest();
            byte[] signed = pe.SignedBytes(MakePkcs7(digest, cert, key, signingTime));
            string wrong = Verify(signed, cert);
            if (wrong != null) throw new InvalidDataException("the signature that was made does not check: " + wrong);
            return signed;
        }
    }

    // ---- shim's variables: MokNew, MokAuth, MokTimeout

    static class Mok
    {
        public static readonly Guid ShimGuid = new Guid("605dab50-e046-4300-abb6-3dd810dd8b23");
        public static readonly Guid X509Guid = new Guid("a5c059a1-94e4-4aa7-87b5-ab155c2bf072");
        public static readonly Guid GlobalGuid = new Guid("8be4df61-93ca-11d2-aa0d-00e098032b8c"); // EFI_GLOBAL_VARIABLE
        public static readonly Guid DbGuid = new Guid("d719b2cb-3d3a-4596-a3bc-dad00e67656f");      // the image security database
        public const string New = "MokNew", Auth = "MokAuth", Timeout = "MokTimeout";
        public const uint Attributes = 7; // non-volatile, boot services, runtime
        // The SHA-256 of the certificates of Microsoft that shim is signed by (Microsoft Corporation UEFI CA 2011, Microsoft UEFI CA 2023)
        public static readonly string[] MicrosoftThirdParty =
        {
            "48e99b991f57fc52f76149599bff0a58c47154229b9f8d603ac40d3500248507",
            "f6124e34125bee3fe6d79a574eaa7b91c0e7bd9d929c1a321178efd611dad901"
        };

        // An EFI_SIGNATURE_LIST of one X.509 certificate: the type, the sizes (28 + 16 + the certificate; no header; 16 + the
        // certificate), the owner of the signature (shim's GUID, as mokutil writes it) and the certificate.
        public static byte[] MokNew(byte[] cert)
        {
            int size = 16 + cert.Length;
            return Der.Cat(X509Guid.ToByteArray(), BitConverter.GetBytes(28 + size), BitConverter.GetBytes(0), BitConverter.GetBytes(size),
                           ShimGuid.ToByteArray(), cert);
        }

        // SHA-256 of MokNew and the password in UTF-16LE, with no terminator (MokManager: compute_pw_hash)
        public static byte[] MokAuth(byte[] mokNew, string password)
        {
            using (var h = SHA256.Create()) return h.ComputeHash(Der.Cat(mokNew, Encoding.Unicode.GetBytes(password)));
        }

        // INT32 -1: MokManager goes to its menu with no countdown to lose
        public static byte[] MokTimeout() { return BitConverter.GetBytes(-1); }

        // A code of four digits, from the digits of the top row, which read the same with any layout of the keyboard of the firmware.
        public static string NewCode()
        {
            using (var rng = new RNGCryptoServiceProvider())
            {
                var b = new byte[4];
                while (true)
                {
                    rng.GetBytes(b);
                    uint v = BitConverter.ToUInt32(b, 0);
                    if (v < 4294960000u) return (v % 10000).ToString("0000");
                }
            }
        }

        // The signatures that a signature database holds: for each list of type X.509, the SHA-256 of each certificate.
        public static List<string> CertHashes(byte[] database)
        {
            var found = new List<string>();
            int at = 0;
            while (at + 28 <= database.Length)
            {
                int size = BitConverter.ToInt32(database, at + 16), header = BitConverter.ToInt32(database, at + 20), each = BitConverter.ToInt32(database, at + 24);
                if (size < 28 || at + size > database.Length || header < 0 || each < 16 || 28 + header > size) break;
                var type = new byte[16];
                Buffer.BlockCopy(database, at, type, 0, 16);
                if (new Guid(type) == X509Guid)
                    for (int s = at + 28 + header; s + each <= at + size; s += each)
                        using (var h = SHA256.Create()) found.Add(Words.Hex(h.ComputeHash(Der.Slice(database, s + 16, s + each))));
                at += size;
            }
            return found;
        }
    }

    // ---- the firmware: what is read, and the three variables that may be written

    interface IFirmware
    {
        // null: it does not exist (or cannot be read)
        byte[] Get(string name, Guid guid);
        void Set(string name, Guid guid, byte[] data, uint attributes);
        void Delete(string name, Guid guid);
    }

    // The firmware of this PC, through the calls of Windows. It writes (and deletes) nothing but MokNew, MokAuth and MokTimeout
    // in shim's GUID, and only once the window has said that writing is allowed (Allowed): the tests never do.
    sealed class WindowsFirmware : IFirmware
    {
        public static bool Allowed;
        static bool privileged;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern uint GetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, out uint attributes);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool SetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, uint attributes);
        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool LookupPrivilegeValueW(string system, string name, out long luid);
        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges state, uint length, IntPtr previous, IntPtr returned);
        [DllImport("kernel32.dll")]
        static extern IntPtr GetCurrentProcess();
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr handle);

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct TokenPrivileges { public uint Count; public long Luid; public uint Attributes; }

        // SeSystemEnvironmentPrivilege, which Windows asks for to read or write a variable of the firmware
        static bool Privilege()
        {
            if (privileged) return true;
            IntPtr token;
            if (!OpenProcessToken(GetCurrentProcess(), 0x28 /* adjust privileges, query */, out token)) return false;
            try
            {
                var state = new TokenPrivileges();
                state.Count = 1;
                state.Attributes = 2; // enabled
                if (!LookupPrivilegeValueW(null, "SeSystemEnvironmentPrivilege", out state.Luid)) return false;
                AdjustTokenPrivileges(token, false, ref state, 0, IntPtr.Zero, IntPtr.Zero);
                privileged = Marshal.GetLastWin32Error() == 0;
                return privileged;
            }
            finally { CloseHandle(token); }
        }

        static string Braced(Guid g) { return "{" + g.ToString() + "}"; }

        public byte[] Get(string name, Guid guid)
        {
            if (!Privilege()) return null;
            int room = 4096;
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                var buffer = new byte[room];
                uint attributes;
                uint got = GetFirmwareEnvironmentVariableExW(name, Braced(guid), buffer, (uint)buffer.Length, out attributes);
                if (got > 0) return Der.Slice(buffer, 0, (int)got);
                if (Marshal.GetLastWin32Error() != 122 /* not enough room */) return null;
                room *= 4;
            }
            return null;
        }

        static void Allow(string name, Guid guid)
        {
            if (!Allowed) throw new InvalidOperationException("firmware variables are not written from here");
            if (guid != Mok.ShimGuid || (name != Mok.New && name != Mok.Auth && name != Mok.Timeout))
                throw new InvalidOperationException("only MokNew, MokAuth and MokTimeout of shim are written");
            if (!Privilege()) throw new InvalidOperationException("Windows did not give the privilege to write firmware variables");
        }

        public void Set(string name, Guid guid, byte[] data, uint attributes)
        {
            Allow(name, guid);
            if (data == null || data.Length == 0) throw new InvalidOperationException("an empty variable is a deletion: use Delete");
            if (!SetFirmwareEnvironmentVariableExW(name, Braced(guid), data, (uint)data.Length, attributes))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }

        public void Delete(string name, Guid guid)
        {
            Allow(name, guid);
            if (!SetFirmwareEnvironmentVariableExW(name, Braced(guid), null, 0, Mok.Attributes))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
    }

#if HADES_TESTS
    // A firmware in memory, for the tests: it keeps its variables, and can be told to refuse or to give a wrong byte back.
    sealed class FakeFirmware : IFirmware
    {
        public readonly Dictionary<string, byte[]> Vars = new Dictionary<string, byte[]>();
        public readonly List<string> Log = new List<string>();
        public string RefuseName;      // Set of this name fails
        public string CorruptName;     // Get of this name returns it with a bit changed
        static string Key(string name, Guid guid) { return guid.ToString() + "/" + name; }
        public byte[] Get(string name, Guid guid)
        {
            byte[] v;
            if (!Vars.TryGetValue(Key(name, guid), out v)) return null;
            v = (byte[])v.Clone();
            if (name == CorruptName) v[v.Length - 1] ^= 1;
            return v;
        }
        public void Set(string name, Guid guid, byte[] data, uint attributes)
        {
            Log.Add("set " + name + " " + data.Length);
            if (name == RefuseName) throw new InvalidOperationException("refused");
            Vars[Key(name, guid)] = (byte[])data.Clone();
        }
        public void Delete(string name, Guid guid)
        {
            Log.Add("delete " + name);
            Vars.Remove(Key(name, guid));
        }
    }
#endif

    // What is asked of the firmware before the box is offered, all of it reading, and the request to enrol the key.
    static class Enroll
    {
        public sealed class Look
        {
            public int SecureBoot = -1;      // 1 on, 0 off, -1 not known
            public int ThirdPartyCa = -1;    // 1: the database has a Microsoft third-party CA, 0: it has none, -1: not known
            public int BitLocker = -1;       // the Windows drive: 1 on, 0 off, -1 not known
        }

        public static Look Examine(IFirmware fw, int secureBoot, int bitLocker)
        {
            var look = new Look();
            look.SecureBoot = secureBoot;
            look.BitLocker = bitLocker;
            byte[] db = fw == null ? null : fw.Get("db", Mok.DbGuid);
            if (db != null)
            {
                look.ThirdPartyCa = 0;
                foreach (string h in Mok.CertHashes(db))
                    if (Array.IndexOf(Mok.MicrosoftThirdParty, h) >= 0) look.ThirdPartyCa = 1;
            }
            return look;
        }

        public sealed class Result
        {
            public bool Written;            // the three variables are there and read back as written
            public string Code;             // the four digits that the blue screen asks for (null: nothing was written)
            public string Note = "";        // what happened, for the log (English)
            public bool OtherPending;       // a request that is not ours was there: it was left alone
            public List<string> Steps = new List<string>();
        }

        // The three variables, written one after the other and read back; if any of them fails, those that were written are deleted
        // (size 0) and nothing is left. A MokNew that is there and is not Hades's own (somebody else's pending request) is left alone.
        public static Result Write(IFirmware fw, byte[] cert, string code)
        {
            var r = new Result();
            byte[] pending = fw.Get(Mok.New, Mok.ShimGuid);
            if (pending != null && IndexOf(pending, Encoding.ASCII.GetBytes(SbCert.NameStart)) < 0)
            {
                r.OtherPending = true;
                r.Note = "a MokNew that is not Hades's is pending (" + pending.Length + " bytes): left as it is";
                return r;
            }
            byte[] mokNew = Mok.MokNew(cert), mokAuth = Mok.MokAuth(mokNew, code), timeout = Mok.MokTimeout();
            var written = new List<string>();
            try
            {
                foreach (KeyValuePair<string, byte[]> v in new[] { new KeyValuePair<string, byte[]>(Mok.New, mokNew),
                                                                  new KeyValuePair<string, byte[]>(Mok.Auth, mokAuth),
                                                                  new KeyValuePair<string, byte[]>(Mok.Timeout, timeout) })
                {
                    fw.Set(v.Key, Mok.ShimGuid, v.Value, Mok.Attributes);
                    written.Add(v.Key);
                    byte[] back = fw.Get(v.Key, Mok.ShimGuid);
                    bool same = back != null && back.Length == v.Value.Length;
                    for (int i = 0; same && i < back.Length; ++i) same = back[i] == v.Value[i];
                    r.Steps.Add(v.Key + ": " + v.Value.Length + " bytes, " + (same ? "read back the same" : "NOT the same when read back"));
                    if (!same) throw new IOException(v.Key + " is not what was written when it is read back");
                }
                r.Written = true;
                r.Code = code;
                r.Note = "MokNew, MokAuth and MokTimeout written and read back";
            }
            catch (Exception e)
            {
                r.Note = "the request to enrol the key failed (" + e.Message + ")";
                foreach (string name in written)
                {
                    try { fw.Delete(name, Mok.ShimGuid); r.Steps.Add(name + ": deleted"); }
                    catch (Exception again) { r.Steps.Add(name + ": could not be deleted (" + again.Message + ")"); }
                }
            }
            return r;
        }

        // The three variables, deleted where they are Hades's: what "Remove LibreELEC Hades" offers when a request is still pending.
        public static bool Pending(IFirmware fw)
        {
            byte[] n = fw.Get(Mok.New, Mok.ShimGuid);
            return n != null && IndexOf(n, Encoding.ASCII.GetBytes(SbCert.NameStart)) >= 0;
        }

        public static string Clear(IFirmware fw)
        {
            var steps = new List<string>();
            foreach (string name in new[] { Mok.New, Mok.Auth, Mok.Timeout })
            {
                if (fw.Get(name, Mok.ShimGuid) == null) continue;
                try { fw.Delete(name, Mok.ShimGuid); steps.Add(name + " deleted"); }
                catch (Exception e) { steps.Add(name + " not deleted (" + e.Message + ")"); }
            }
            return string.Join(", ", steps.ToArray());
        }

        internal static int IndexOf(byte[] data, byte[] what)
        {
            for (int i = 0; i + what.Length <= data.Length; ++i)
            {
                int k = 0;
                while (k < what.Length && data[i + k] == what[k]) ++k;
                if (k == what.Length) return i;
            }
            return -1;
        }
    }
}
