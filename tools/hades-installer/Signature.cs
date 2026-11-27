// LibreELEC Hades installer for Windows: whether an image is the one that LibreELEC Hades published. Since 1.2.3 each
// release has SHA256SUMS signed with the keys of the update channel (SHA256SUMS.sig, next to it): the signature is checked
// with the two public keys that are written here, and then the SHA-256 of the image is looked for in that list.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace Hades.Installer
{
    // Ed25519 (RFC 8032), the verification: the arithmetic of its section 5.1 written out with BigInteger, as the reference
    // code of its section 6 does. It is not constant time, which a check of public data does not need.
    static partial class Ed25519
    {
        static readonly BigInteger P = BigInteger.Pow(2, 255) - 19;
        static readonly BigInteger L = BigInteger.Pow(2, 252) + BigInteger.Parse("27742317777372353535851937790883648493");
        static readonly BigInteger D = Mod(-121665 * Inverse(121666));
        static readonly BigInteger SqrtM1 = BigInteger.ModPow(2, (P - 1) / 4, P);
        static readonly BigInteger[] B = BasePoint();

        static BigInteger Mod(BigInteger a)
        {
            BigInteger r = BigInteger.Remainder(a, P);
            return r.Sign < 0 ? r + P : r;
        }

        static BigInteger Inverse(BigInteger a) { return BigInteger.ModPow(Mod(a), P - 2, P); }

        // A number of the curve from its 32 bytes, little-endian, read as positive
        static BigInteger Number(byte[] bytes, int at, int count)
        {
            var le = new byte[count + 1];
            Buffer.BlockCopy(bytes, at, le, 0, count);
            return new BigInteger(le);
        }

        // x from y and the bit of its sign; -1 when there is none (RFC 8032, 5.1.3)
        static BigInteger RecoverX(BigInteger y, int sign)
        {
            if (y >= P) return BigInteger.MinusOne;
            BigInteger x2 = Mod((y * y - 1) * Inverse(D * y * y + 1));
            if (x2.IsZero) return sign != 0 ? BigInteger.MinusOne : BigInteger.Zero;
            BigInteger x = BigInteger.ModPow(x2, (P + 3) / 8, P);
            if (!Mod(x * x - x2).IsZero) x = Mod(x * SqrtM1);
            if (!Mod(x * x - x2).IsZero) return BigInteger.MinusOne;
            if ((x.IsEven ? 0 : 1) != sign) x = P - x;
            return x;
        }

        static BigInteger[] BasePoint()
        {
            BigInteger y = Mod(4 * Inverse(5));
            BigInteger x = RecoverX(y, 0);
            return new[] { x, y, BigInteger.One, Mod(x * y) };
        }

        // A point from its 32 bytes (extended coordinates X, Y, Z, T), or null when they are not one
        static BigInteger[] Decode(byte[] bytes, int at)
        {
            var le = new byte[33];
            Buffer.BlockCopy(bytes, at, le, 0, 32);
            int sign = le[31] >> 7;
            le[31] &= 0x7f;
            BigInteger y = new BigInteger(le);
            BigInteger x = RecoverX(y, sign);
            if (x.Sign < 0) return null;
            return new[] { x, y, BigInteger.One, Mod(x * y) };
        }

        static BigInteger[] Add(BigInteger[] p, BigInteger[] q)
        {
            BigInteger a = Mod((p[1] - p[0]) * (q[1] - q[0]));
            BigInteger b = Mod((p[1] + p[0]) * (q[1] + q[0]));
            BigInteger c = Mod(2 * p[3] * q[3] * D);
            BigInteger d = Mod(2 * p[2] * q[2]);
            BigInteger e = b - a, f = d - c, g = d + c, h = b + a;
            return new[] { Mod(e * f), Mod(g * h), Mod(f * g), Mod(e * h) };
        }

        static BigInteger[] Multiply(BigInteger s, BigInteger[] p)
        {
            BigInteger[] q = { BigInteger.Zero, BigInteger.One, BigInteger.One, BigInteger.Zero };
            while (s.Sign > 0)
            {
                if (!s.IsEven) q = Add(q, p);
                p = Add(p, p);
                s >>= 1;
            }
            return q;
        }

        static bool Same(BigInteger[] p, BigInteger[] q)
        {
            return Mod(p[0] * q[2] - q[0] * p[2]).IsZero && Mod(p[1] * q[2] - q[1] * p[2]).IsZero;
        }

        // SHA-512 of the parts, as a number modulo L
        static BigInteger HashModL(params byte[][] parts)
        {
            using (SHA512 sha = SHA512.Create())
            {
                for (int i = 0; i < parts.Length - 1; ++i) sha.TransformBlock(parts[i], 0, parts[i].Length, null, 0);
                byte[] last = parts[parts.Length - 1];
                sha.TransformFinalBlock(last, 0, last.Length);
                return BigInteger.Remainder(Number(sha.Hash, 0, 64), L);
            }
        }

        // Whether the 64 bytes of signature are those of the message by the key of those 32 bytes: [S]B = R + [k]A, with
        // k = SHA-512(R || A || message) modulo L, S below L, and R and A points of the curve (RFC 8032, 5.1.7)
        public static bool Verify(byte[] key, byte[] message, byte[] signature)
        {
            if (key == null || message == null || signature == null || key.Length != 32 || signature.Length != 64) return false;
            BigInteger[] a = Decode(key, 0);
            if (a == null) return false;
            BigInteger[] r = Decode(signature, 0);
            if (r == null) return false;
            BigInteger s = Number(signature, 32, 32);
            if (s >= L) return false;
            var rBytes = new byte[32];
            Buffer.BlockCopy(signature, 0, rBytes, 0, 32);
            BigInteger k = HashModL(rBytes, key, message);
            return Same(Multiply(s, B), Add(r, Multiply(k, a)));
        }
    }

    // What the check of an image against the signed list of its release found
    enum Origin
    {
        Verified,   // SHA256SUMS.sig holds for one of the two keys, and the SHA-256 of the image is in that SHA256SUMS
        NotSigned,  // nothing signed says anything of this image: no SHA256SUMS.sig (or no SHA256SUMS) next to it, or a signed
                    // list that does not name it (an image of someone else, a build of one's own)
        NoMatch,    // something signed is there and does not hold: the signature is not one of those keys, or not of that
                    // SHA256SUMS, or SHA256SUMS names this file with another SHA-256 (it changed after it was signed)
    }

    // What exactly was found, for what the window says (MainForm.OriginText)
    enum Found
    {
        Listed,            // Verified
        NoSignature,       // NotSigned: no SHA256SUMS.sig next to the image
        NoList,            // NotSigned: SHA256SUMS.sig without SHA256SUMS
        NotListed,         // NotSigned: the signed list does not name the image
        NotHashed,         // NotSigned: the SHA-256 of the image was not worked out (not in the window: it always is)
        NotASignature,     // NoMatch: SHA256SUMS.sig is not a signature file of LibreELEC Hades
        OtherKey,          // NoMatch: it names a key that is not one of LibreELEC Hades
        Changed,           // NoMatch: the signature does not hold for SHA256SUMS
        OtherHash,         // NoMatch: the signed list names the image with another SHA-256
        Unreadable,        // NoMatch: the files of the signature could not be read, or the list is too large
    }

    sealed class OriginCheck
    {
        public Origin Result;
        public Found What;
        public int Key;           // the key that signed (1 or 2), when Verified
        public string Line = "";  // the name that the signed list gives the image, when Verified
        public string Why = "";   // in English, for the log and the tests: what was found
        // The list and the signature that verified, when they are copies of a second download ("SHA256SUMS (1)"); else empty
        public string Files = "";
        // Not verified, and there are copies of a second download next to the image too, none of them of this image
        public bool OtherCopies;

        public static OriginCheck Of(Origin result, Found what, string why)
        {
            var c = new OriginCheck();
            c.Result = result;
            c.What = what;
            c.Why = why;
            return c;
        }
    }

    static class Release
    {
        // What is signed is this label and then SHA256SUMS, byte for byte, so that a signature of the list can never be taken
        // for one of a manifest of the update channel ("hades-update-manifest-1\n" and the manifest), nor the other way round.
        public const string Label = "hades-sha256sums-1\n";
        public const string Format = "hades-sha256sums-signature-1";
        public const string SumsName = "SHA256SUMS", SignatureName = "SHA256SUMS.sig";
        const int SumsMax = 1 << 20, SignatureMax = 4096;

        // The two keys of the publisher, as the update channel of the system has them (packages/sysutils/hades-update-channel/
        // keys): key 1, the one in use (id 444378a0ca23e565), and key 2, kept apart for when the first has to be replaced
        // (id da5e1e82124a210e).
        static readonly string[] Pinned = { "xNiCz47zKLov6jSe9VicYhfNCA/v+rPBjEAzQsnwcww=", "XPICry2F28eC7zSIT5rfVPhSCjBTBUE7Zeb0jnu9qzs=" };

#if HADES_TESTS
        // (only in the program of the tests: the window trusts the key of the tests, for the screenshots of a signed image)
        public static byte[][] TestKeys;
#endif

        public static byte[][] PublisherKeys()
        {
#if HADES_TESTS
            if (TestKeys != null) return TestKeys;
#endif
            var keys = new byte[Pinned.Length][];
            for (int i = 0; i < Pinned.Length; ++i) keys[i] = Convert.FromBase64String(Pinned[i]);
            return keys;
        }

        // The id that a signature file gives its key: the first 8 bytes of the SHA-256 of the key, in hexadecimal
        public static string KeyId(byte[] key)
        {
            using (SHA256 sha = SHA256.Create()) return Words.Hex(sha.ComputeHash(key)).Substring(0, 16);
        }

        // Whether there is a signature next to the image (also a copy of a second download, CopyNames()), so that its SHA-256
        // has to be worked out to check it
        public static bool Present(string imagePath)
        {
            string dir = Path.GetDirectoryName(Path.GetFullPath(imagePath));
            if (File.Exists(Path.Combine(dir, SignatureName))) return true;
            try
            {
                return CopyNames(dir, SignatureName, OtherSignature).Length > 0;
            }
            catch (Exception)
            {
                return false;
            }
        }

        // The names that a browser gives a second download of a file with the same name: "SHA256SUMS (1)" (Chrome, Edge) or
        // "SHA256SUMS(1)" (Firefox), and "SHA256SUMS (1).sig" for the signature. An old list or signature of another release
        // can be left in the folder of the downloads under the plain name, the new ones then get these.
        static readonly Regex OtherSums = new Regex("^SHA256SUMS ?\\([0-9]{1,3}\\)$", RegexOptions.IgnoreCase);
        static readonly Regex OtherSignature = new Regex("^SHA256SUMS ?\\([0-9]{1,3}\\)\\.sig$", RegexOptions.IgnoreCase);
        const int OtherMax = 8;

        // The plain name (when it is there) and then the copies of a second download in dir, in the order of their names, at
        // most OtherMax of them
        static string[] CopyNames(string dir, string plain, Regex copy)
        {
            var names = new List<string>();
            if (File.Exists(Path.Combine(dir, plain))) names.Add(plain);
            var copies = new List<string>();
            foreach (string path in Directory.GetFiles(dir))
            {
                string name = Path.GetFileName(path);
                if (copy.IsMatch(name)) copies.Add(name);
            }
            copies.Sort(StringComparer.Ordinal);
            for (int i = 0; i < copies.Count && i < OtherMax; ++i) names.Add(copies[i]);
            return names.ToArray();
        }

        static readonly Regex SignatureFile = new Regex(
            "^\\{\\s*\"format\"\\s*:\\s*\"" + Format + "\"\\s*,\\s*\"key\"\\s*:\\s*\"([0-9a-f]{16})\"\\s*,\\s*\"signature\"\\s*:\\s*\"([A-Za-z0-9+/]{86}==)\"\\s*\\}\\s*$");
        static readonly Regex SumsLine = new Regex("^([0-9a-f]{64}) [ *](.+)$");

        static byte[] ReadAtMost(string path, int max)
        {
            using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                if (f.Length > max) return null;
                var b = new byte[f.Length];
                int at = 0, read;
                while (at < b.Length && (read = f.Read(b, at, b.Length - at)) > 0) at += read;
                return at == b.Length ? b : null;
            }
        }

        // The image against SHA256SUMS and SHA256SUMS.sig in its folder. imageHash: the SHA-256 of the image file, in lowercase
        // hexadecimal (needed only when SHA256SUMS.sig is there). keys: the public keys that are trusted, in their order (the
        // publisher's: PublisherKeys()). Never throws: a file that cannot be read is what was found.
        //
        // When those two do not verify the image, the copies of a second download next to it ("SHA256SUMS (1)",
        // "SHA256SUMS (1).sig": CopyNames()) are tried too, every list with every signature: a pair whose signature holds for one
        // of the keys and whose list has the SHA-256 of this image verifies it, exactly as the two under their plain names
        // would. Nothing else is taken from them. Else what the plain names gave is what was found, and OtherCopies says that
        // copies were there too.
        public static OriginCheck Check(string imagePath, string imageHash, byte[][] keys)
        {
            string dir;
            try
            {
                dir = Path.GetDirectoryName(Path.GetFullPath(imagePath));
            }
            catch (Exception e)
            {
                return OriginCheck.Of(Origin.NoMatch, Found.Unreadable, "the files of the signature could not be read: " + e.Message);
            }
            OriginCheck plain = CheckPair(imagePath, imageHash, keys, Path.Combine(dir, SignatureName), Path.Combine(dir, SumsName));
            if (plain.Result == Origin.Verified || imageHash == null) return plain;
            string[] signatures, lists;
            try
            {
                signatures = CopyNames(dir, SignatureName, OtherSignature);
                lists = CopyNames(dir, SumsName, OtherSums);
            }
            catch (Exception)
            {
                return plain;
            }
            bool copies = false;
            foreach (string signature in signatures)
                foreach (string list in lists)
                {
                    if (signature == SignatureName && list == SumsName) continue;
                    copies = true;
                    OriginCheck c = CheckPair(imagePath, imageHash, keys, Path.Combine(dir, signature), Path.Combine(dir, list));
                    if (c.Result != Origin.Verified) continue;
                    c.Files = list + ", " + signature;
                    c.Why += " (" + list + " with " + signature + "; " + SumsName + " with " + SignatureName + ": " + plain.Why + ")";
                    return c;
                }
            plain.OtherCopies = copies;
            return plain;
        }

        // The image against one list and one signature
        static OriginCheck CheckPair(string imagePath, string imageHash, byte[][] keys, string sigPath, string sumsPath)
        {
            string sigName = Path.GetFileName(sigPath), sumsName = Path.GetFileName(sumsPath);
            try
            {
                if (!File.Exists(sigPath))
                    return OriginCheck.Of(Origin.NotSigned, Found.NoSignature, "there is no " + sigName + " next to the image");
                if (!File.Exists(sumsPath))
                    return OriginCheck.Of(Origin.NotSigned, Found.NoList, "there is " + sigName + " but no " + sumsName + " next to the image");
                byte[] sigBytes = ReadAtMost(sigPath, SignatureMax);
                Match m = sigBytes == null ? Match.Empty : SignatureFile.Match(Encoding.ASCII.GetString(sigBytes));
                if (!m.Success)
                    return OriginCheck.Of(Origin.NoMatch, Found.NotASignature, sigName + " is not a signature of a list of LibreELEC Hades");
                int key = -1;
                for (int i = 0; i < keys.Length; ++i)
                    if (KeyId(keys[i]) == m.Groups[1].Value) key = i;
                if (key < 0)
                    return OriginCheck.Of(Origin.NoMatch, Found.OtherKey, sigName + " names the key " + m.Groups[1].Value +
                                                          ", which is not one of LibreELEC Hades");
                byte[] sums = ReadAtMost(sumsPath, SumsMax);
                if (sums == null) return OriginCheck.Of(Origin.NoMatch, Found.Unreadable, sumsName + " is larger than a list of a release");
                byte[] label = Encoding.ASCII.GetBytes(Label);
                var signed = new byte[label.Length + sums.Length];
                Buffer.BlockCopy(label, 0, signed, 0, label.Length);
                Buffer.BlockCopy(sums, 0, signed, label.Length, sums.Length);
                if (!Ed25519.Verify(keys[key], signed, Convert.FromBase64String(m.Groups[2].Value)))
                    return OriginCheck.Of(Origin.NoMatch, Found.Changed, "the signature in " + sigName + " does not hold for " + sumsName +
                                                          ": one of the two was changed after it was signed");
                if (imageHash == null)
                    return OriginCheck.Of(Origin.NotSigned, Found.NotHashed, "the SHA-256 of the image was not worked out");
                string name = Path.GetFileName(imagePath), named = null;
                foreach (string raw in Encoding.UTF8.GetString(sums).Split('\n'))
                {
                    Match line = SumsLine.Match(raw.TrimEnd('\r'));
                    if (!line.Success) continue;
                    if (line.Groups[1].Value == imageHash)
                    {
                        var ok = OriginCheck.Of(Origin.Verified, Found.Listed, "the signature holds for key " + (key + 1) + " (" + m.Groups[1].Value +
                                                                 "), and " + sumsName + " lists the image as " + line.Groups[2].Value);
                        ok.Key = key + 1;
                        ok.Line = line.Groups[2].Value;
                        return ok;
                    }
                    if (string.Equals(line.Groups[2].Value, name, StringComparison.OrdinalIgnoreCase)) named = line.Groups[1].Value;
                }
                if (named != null)
                    return OriginCheck.Of(Origin.NoMatch, Found.OtherHash, "the signed " + sumsName + " gives " + name + " another SHA-256 (" +
                                                          named.Substring(0, 16) + "...): the image changed after it was signed");
                return OriginCheck.Of(Origin.NotSigned, Found.NotListed, "the signed " + sumsName + " next to the image does not list it");
            }
            catch (Exception e)
            {
                return OriginCheck.Of(Origin.NoMatch, Found.Unreadable, "the files of the signature could not be read: " + e.Message);
            }
        }
    }
}
