// LibreELEC Hades installer for Windows: the tests of the signature of a release (Signature.cs). Only in
// LibreELEC-Hades-Installer-tests.exe (build.ps1, /define:HADES_TESTS): the signing below, with a throw-away key, exists
// for the tests alone; the published program only verifies.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.IO;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;

namespace Hades.Installer
{
    static partial class Ed25519
    {
        static byte[] Encode(BigInteger[] p)
        {
            BigInteger zi = Inverse(p[2]);
            BigInteger x = Mod(p[0] * zi), y = Mod(p[1] * zi);
            byte[] le = y.ToByteArray();
            var b = new byte[32];
            Buffer.BlockCopy(le, 0, b, 0, Math.Min(32, le.Length));
            if (!x.IsEven) b[31] |= 0x80;
            return b;
        }

        static BigInteger Scalar(byte[] seed, out byte[] prefix)
        {
            byte[] h;
            using (SHA512 sha = SHA512.Create()) h = sha.ComputeHash(seed);
            h[0] &= 248;
            h[31] &= 127;
            h[31] |= 64;
            prefix = new byte[32];
            Buffer.BlockCopy(h, 32, prefix, 0, 32);
            return Number(h, 0, 32);
        }

        // The public key of a seed of 32 bytes, and its signature of a message (RFC 8032, 5.1.5 and 5.1.6)
        public static byte[] PublicKey(byte[] seed)
        {
            byte[] prefix;
            return Encode(Multiply(Scalar(seed, out prefix), B));
        }

        public static byte[] Sign(byte[] seed, byte[] message)
        {
            byte[] prefix;
            BigInteger a = Scalar(seed, out prefix);
            byte[] pub = Encode(Multiply(a, B));
            BigInteger r = HashModL(prefix, message);
            byte[] rb = Encode(Multiply(r, B));
            BigInteger k = HashModL(rb, pub, message);
            BigInteger s = BigInteger.Remainder(r + k * a, L);
            var sig = new byte[64];
            Buffer.BlockCopy(rb, 0, sig, 0, 32);
            byte[] sl = s.ToByteArray();
            Buffer.BlockCopy(sl, 0, sig, 32, Math.Min(32, sl.Length));
            return sig;
        }
    }

    static partial class SelfTest
    {
        static byte[] FromHex(string hex)
        {
            var b = new byte[hex.Length / 2];
            for (int i = 0; i < b.Length; ++i) b[i] = Convert.ToByte(hex.Substring(2 * i, 2), 16);
            return b;
        }

        static byte[] Concat(byte[] a, byte[] b)
        {
            var c = new byte[a.Length + b.Length];
            Buffer.BlockCopy(a, 0, c, 0, a.Length);
            Buffer.BlockCopy(b, 0, c, a.Length, b.Length);
            return c;
        }

        // The signature file as hades-channel.py sums-wrap writes it
        static string SignatureFile(byte[] key, byte[] signature)
        {
            return "{\"format\": \"" + Release.Format + "\", \"key\": \"" + Release.KeyId(key) + "\", \"signature\": \"" +
                   Convert.ToBase64String(signature) + "\"}\n";
        }

        // The image of a release against its signed list: Ed25519 itself (RFC 8032), the keys of the publisher (a real
        // signature of key 1), a list signed by hades-channel.py, and each outcome of Release.Check.
        static void Signatures(string directory)
        {
            // RFC 8032, section 7.1, tests 1 to 3: the public key and the signature from the seed, and the check
            string[][] rfc =
            {
                new[] { "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
                        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
                        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
                new[] { "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
                        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
                        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
                new[] { "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
                        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
                        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
            };
            bool vectors = true, refused = true;
            foreach (string[] v in rfc)
            {
                byte[] seed = FromHex(v[0]), pub = FromHex(v[1]), msg = FromHex(v[2]), sig = FromHex(v[3]);
                vectors &= Words.Hex(Ed25519.PublicKey(seed)) == v[1] && Words.Hex(Ed25519.Sign(seed, msg)) == v[3] &&
                           Ed25519.Verify(pub, msg, sig);
                // one bit of the signature, of the message or of the key changed: refused
                for (int bit = 0; bit < 512; bit += 37)
                {
                    var s2 = (byte[])sig.Clone();
                    s2[bit / 8] ^= (byte)(1 << (bit % 8));
                    refused &= !Ed25519.Verify(pub, msg, s2);
                }
                refused &= !Ed25519.Verify(pub, Concat(msg, new byte[] { 0 }), sig);
                var p2 = (byte[])pub.Clone();
                p2[5] ^= 1;
                refused &= !Ed25519.Verify(p2, msg, sig);
            }
            Check("Ed25519: the public keys and signatures of RFC 8032 (tests 1 to 3) are made and verified", vectors);
            Check("... and a signature, message or key changed by one bit is refused", refused);
            // S at or above L (the signature plus L in its second half): refused, not taken as the same signature
            {
                byte[] sig = FromHex(rfc[0][3]);
                BigInteger l = BigInteger.Pow(2, 252) + BigInteger.Parse("27742317777372353535851937790883648493");
                var sb = new byte[33];
                Buffer.BlockCopy(sig, 32, sb, 0, 32);
                byte[] big = (new BigInteger(sb) + l).ToByteArray();
                var s3 = (byte[])sig.Clone();
                Array.Clear(s3, 32, 32);
                Buffer.BlockCopy(big, 0, s3, 32, Math.Min(32, big.Length));
                Check("Ed25519: a signature whose S is not below L is refused (S + L is no second form of it)",
                      big.Length <= 33 && !Ed25519.Verify(FromHex(rfc[0][1]), new byte[0], s3));
            }

            // the keys of the publisher: their ids, and the signature of key 1 of the manifest of 1.2.2 as it was published
            byte[][] keys = Release.PublisherKeys();
            Check("the two keys of the publisher are those of the update channel (ids 444378a0ca23e565 and da5e1e82124a210e)",
                  keys.Length == 2 && Release.KeyId(keys[0]) == "444378a0ca23e565" && Release.KeyId(keys[1]) == "da5e1e82124a210e");
            byte[] manifest = Convert.FromBase64String(SignatureVectors.Manifest122);
            byte[] signed = Concat(Encoding.ASCII.GetBytes("hades-update-manifest-1\n"), manifest);
            byte[] real = Convert.FromBase64String(SignatureVectors.Manifest122Signature);
            Check("the signature of the manifest of 1.2.2 (by key 1, made with openssl) holds for key 1, and not for key 2, nor " +
                  "for the manifest without its prefix",
                  Ed25519.Verify(keys[0], signed, real) && !Ed25519.Verify(keys[1], signed, real) && !Ed25519.Verify(keys[0], manifest, real));

            // Release.Check in a folder of its own: an image, its list, the signature
            string dir = Path.Combine(directory, "release-signature");
            if (Directory.Exists(dir)) Directory.Delete(dir, true);
            Directory.CreateDirectory(dir);
            try
            {
                string image = Path.Combine(dir, "LibreELEC-Hades-9.9.9-Generic.x86_64.img.gz");
                string sums = Path.Combine(dir, Release.SumsName), sigPath = Path.Combine(dir, Release.SignatureName);
                byte[] seed = FromHex(SignatureVectors.TestSeed);
                byte[] testKey = Ed25519.PublicKey(seed);
                byte[][] testKeys = { testKey };

                // a list signed by hades-channel.py sums-wrap with a throw-away key (the shape and the prefix that the
                // publication writes): its image is "image\n"
                File.WriteAllBytes(image, Encoding.ASCII.GetBytes("image\n"));
                File.WriteAllBytes(sums, Convert.FromBase64String(SignatureVectors.PySums));
                File.WriteAllText(sigPath, SignatureVectors.PySignature, new UTF8Encoding(false));
                string hash = Engine.FileHash(image, null, null);
                OriginCheck py = Release.Check(image, hash, new[] { Convert.FromBase64String(SignatureVectors.PyKey) });
                Check("a list signed by the publication's tool (hades-channel.py sums-wrap) is verified, and the image found in it: " + py.Why,
                      py.Result == Origin.Verified && py.Key == 1 && py.Line == "LibreELEC-Hades-9.9.9-Generic.x86_64.img.gz");
                Check("... and with the keys of the publisher it does not match (another key signed it): " +
                      Release.Check(image, hash, keys).Why,
                      Release.Check(image, hash, keys).Result == Origin.NoMatch && Release.Check(image, hash, keys).What == Found.OtherKey);

                // signed here with the test key
                string list = hash + "  LibreELEC-Hades-9.9.9-Generic.x86_64.img.gz\n" +
                              "0000000000000000000000000000000000000000000000000000000000000001  LibreELEC-Hades-9.9.9-Generic.x86_64.tar\n";
                byte[] listBytes = Encoding.ASCII.GetBytes(list);
                File.WriteAllBytes(sums, listBytes);
                byte[] good = Ed25519.Sign(seed, Concat(Encoding.ASCII.GetBytes(Release.Label), listBytes));
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                OriginCheck c = Release.Check(image, hash, testKeys);
                Check("verified: the signature holds and the list has the SHA-256 of the image (" + c.Why + ")",
                      c.Result == Origin.Verified && c.Key == 1);
                string renamed = Path.Combine(dir, "my-copy.img.gz");
                File.Copy(image, renamed);
                c = Release.Check(renamed, hash, testKeys);
                Check("verified under another file name: the image is found by its SHA-256", c.Result == Origin.Verified);
                File.Delete(renamed);

                c = Release.Check(image, Words.Hex(new byte[32]), testKeys);
                Check("does not match: the list names the image with another SHA-256 (it changed after it was signed): " + c.Why,
                      c.Result == Origin.NoMatch && c.What == Found.OtherHash);
                string other = Path.Combine(dir, "own-build.img.gz");
                File.WriteAllBytes(other, Encoding.ASCII.GetBytes("my own build\n"));
                c = Release.Check(other, Engine.FileHash(other, null, null), testKeys);
                Check("not signed: an image that the signed list next to it does not name: " + c.Why, c.Result == Origin.NotSigned && c.What == Found.NotListed);
                File.Delete(other);

                File.WriteAllBytes(sums, Encoding.ASCII.GetBytes(list.Replace("0000000000000000000000000000000000000000000000000000000000000001",
                                                                                   "0000000000000000000000000000000000000000000000000000000000000002")));
                c = Release.Check(image, hash, testKeys);
                Check("does not match: a list changed after it was signed: " + c.Why, c.Result == Origin.NoMatch && c.What == Found.Changed);
                File.WriteAllBytes(sums, listBytes);

                byte[] asManifest = Ed25519.Sign(seed, Concat(Encoding.ASCII.GetBytes("hades-update-manifest-1\n"), listBytes));
                File.WriteAllText(sigPath, SignatureFile(testKey, asManifest), new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("does not match: the same bytes signed as a manifest of the update channel are no signature of the list",
                      c.Result == Origin.NoMatch && c.What == Found.Changed);
                byte[] other64 = Ed25519.Sign(FromHex(rfc[1][0]), Concat(Encoding.ASCII.GetBytes(Release.Label), listBytes));
                File.WriteAllText(sigPath, SignatureFile(testKey, other64), new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("does not match: a signature by another key, under the name of the trusted one", c.Result == Origin.NoMatch && c.What == Found.Changed);
                File.WriteAllText(sigPath, SignatureFile(testKey, good).Replace(Release.Format, "hades-update-signature-1"), new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("does not match: a signature file of another format", c.Result == Origin.NoMatch && c.What == Found.NotASignature);
                File.WriteAllText(sigPath, "not a signature\n", new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("does not match: a file that is not a signature", c.Result == Origin.NoMatch && c.What == Found.NotASignature);

                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                File.Delete(sums);
                c = Release.Check(image, hash, testKeys);
                Check("not signed: SHA256SUMS.sig without its SHA256SUMS", c.Result == Origin.NotSigned && c.What == Found.NoList);
                File.Delete(sigPath);
                File.WriteAllBytes(sums, listBytes);
                c = Release.Check(image, hash, testKeys);
                Check("not signed: SHA256SUMS without SHA256SUMS.sig (" + c.Why + ")", c.Result == Origin.NotSigned && c.What == Found.NoSignature && !Release.Present(image));
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                Check("the signature next to the image is seen before the image is read", Release.Present(image));

                // copies of a second download, as a browser names them ("SHA256SUMS (1)", "SHA256SUMS(1).sig"): the list and the
                // signature of another release under the plain names, those of this image's release under the names of copies
                byte[] oldList = Encoding.ASCII.GetBytes(
                    "1111111111111111111111111111111111111111111111111111111111111111  LibreELEC-Hades-9.9.8-Generic.x86_64.img.gz\n");
                byte[] oldGood = Ed25519.Sign(seed, Concat(Encoding.ASCII.GetBytes(Release.Label), oldList));
                Action clearCopies = () =>
                {
                    foreach (string f in Directory.GetFiles(dir, "SHA256SUMS*"))
                        if (Path.GetFileName(f) != Release.SumsName && Path.GetFileName(f) != Release.SignatureName) File.Delete(f);
                };
                File.WriteAllBytes(sums, oldList);
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS (1)"), listBytes);
                c = Release.Check(image, hash, testKeys);
                Check("verified: an old SHA256SUMS next to the new signature, the new list saved as \"SHA256SUMS (1)\" (" + c.Why + ")",
                      c.Result == Origin.Verified && c.Files == "SHA256SUMS (1), SHA256SUMS.sig" && !c.OtherCopies);
                clearCopies();
                File.WriteAllText(sigPath, SignatureFile(testKey, oldGood), new UTF8Encoding(false));
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS(1)"), listBytes);
                File.WriteAllText(Path.Combine(dir, "SHA256SUMS(1).sig"), SignatureFile(testKey, good), new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("verified: the pair of another release under the plain names, this release's as copies without a space (" + c.Why + ")",
                      c.Result == Origin.Verified && c.Files == "SHA256SUMS(1), SHA256SUMS(1).sig");
                clearCopies();
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS (1)"), oldList);
                c = Release.Check(image, hash, testKeys);
                Check("does not match: an old list, the new signature, and a copy that is not of this image either: what the plain " +
                      "names gave, with the copies said (" + c.Why + ")",
                      c.Result == Origin.NoMatch && c.What == Found.Changed && c.OtherCopies && c.Files == "" &&
                          MainForm.OriginText(c).Contains("SHA256SUMS (1)") && MainForm.OriginText(c).Contains("same release"));
                clearCopies();
                File.WriteAllText(sigPath, SignatureFile(testKey, oldGood), new UTF8Encoding(false));
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS (2)"), listBytes);
                File.WriteAllText(Path.Combine(dir, "SHA256SUMS (2).sig"), SignatureFile(testKey, other64), new UTF8Encoding(false));
                c = Release.Check(image, hash, testKeys);
                Check("not signed: a copy of the list with the image, under a signature of another key with the name of the trusted one (" +
                      c.Why + ")", c.Result == Origin.NotSigned && c.What == Found.NotListed && c.OtherCopies);
                clearCopies();
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS.old"), listBytes);
                File.WriteAllBytes(Path.Combine(dir, "SHA256SUMS (1).txt"), listBytes);
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));
                File.WriteAllBytes(sums, oldList);
                c = Release.Check(image, hash, testKeys);
                Check("does not match: a file that is no copy of a download (SHA256SUMS.old, SHA256SUMS (1).txt) is not taken (" + c.Why + ")",
                      c.Result == Origin.NoMatch && c.What == Found.Changed && !c.OtherCopies);
                clearCopies();
                File.Delete(sigPath);
                File.WriteAllText(Path.Combine(dir, "SHA256SUMS (1).sig"), SignatureFile(testKey, good), new UTF8Encoding(false));
                File.WriteAllBytes(sums, listBytes);
                Check("a copy of the signature alone is seen before the image is read", Release.Present(image));
                c = Release.Check(image, hash, testKeys);
                Check("verified: the list under its plain name, the signature saved as \"SHA256SUMS (1).sig\" (" + c.Why + ")",
                      c.Result == Origin.Verified && c.Files == "SHA256SUMS, SHA256SUMS (1).sig");
                clearCopies();
                File.WriteAllText(sigPath, SignatureFile(testKey, good), new UTF8Encoding(false));

                // what the window says of each, and the question before an image that is not signed
                using (ConfirmForm ask = MainForm.AskUnsigned("own-build.img.gz", OriginCheck.Of(Origin.NotSigned, Found.NoSignature, "x"), false))
                using (ConfirmForm askSecure = MainForm.AskUnsigned("own-build.img.gz", OriginCheck.Of(Origin.NotSigned, Found.NoSignature, "x"), true))
                    Check("an image that is not signed is written only after a question that needs a word typed, whose Enter key " +
                          "does not act before it, and that speaks of Secure Boot only when the box is ticked",
                          !ask.ActionReady && !ask.EnterCancels && ask.Said.Contains("own-build.img.gz") && !ask.Said.Contains("Secure Boot") &&
                              askSecure.Said.Contains("Secure Boot"));
            }
            finally
            {
                Directory.Delete(dir, true);
            }
        }
    }

    static partial class SelfTest
    {
        // The window with an image and its signed list: "33-image-signed" (the key of the tests trusted, as the keys of the
        // publisher are in the program), "34-image-signature-does-not-match" (the same list with the keys of the publisher),
        // and "35-confirm-unsigned", the question before an image that is not signed.
        static void CaptureSignatures(string directory, DiskInfo stick)
        {
            long[] unused;
            string dir = Path.Combine(directory, "signed");
            if (Directory.Exists(dir)) Directory.Delete(dir, true);
            Directory.CreateDirectory(dir);
            try
            {
                string image = Path.Combine(dir, "LibreELEC-Hades-9.9.9-Generic.x86_64.img");
                byte[] made = MakeImage(4, true, out unused);
                File.WriteAllBytes(image, made);
                string hash = Sha(made, 0, made.Length);
                File.WriteAllText(image + ".sha256", hash + "  " + Path.GetFileName(image) + "\n");
                byte[] seed = FromHex(SignatureVectors.TestSeed);
                byte[] list = Encoding.ASCII.GetBytes(hash + "  " + Path.GetFileName(image) + "\n");
                File.WriteAllBytes(Path.Combine(dir, Release.SumsName), list);
                File.WriteAllText(Path.Combine(dir, Release.SignatureName),
                                  SignatureFile(Ed25519.PublicKey(seed), Ed25519.Sign(seed, Concat(Encoding.ASCII.GetBytes(Release.Label), list))),
                                  new UTF8Encoding(false));
                foreach (bool trusted in new[] { true, false })
                {
                    Release.TestKeys = trusted ? new[] { Ed25519.PublicKey(seed) } : null;
                    using (var form = new MainForm(image))
                    {
                        form.Show();
                        Pump(delegate { return !form.Scanning; }, 180);
                        form.SelectDisk(stick);
                        Pump(delegate { return true; }, 1);
                        Save(form, Path.Combine(directory, trusted ? "33-image-signed.png" : "34-image-signature-does-not-match.png"));
                    }
                }
            }
            finally
            {
                Release.TestKeys = null;
                Directory.Delete(dir, true);
            }
            using (ConfirmForm ask = MainForm.AskUnsigned("LibreELEC-Hades-my-build.img.gz", OriginCheck.Of(Origin.NotSigned, Found.NoSignature, ""), false))
            {
                ask.Show();
                Save(ask, Path.Combine(directory, "35-confirm-unsigned.png"));
            }
        }
    }

    // The data of the tests of the signature
    static class SignatureVectors
    {
        // The seed of a throw-away key of these tests (never a key of a release)
        public const string TestSeed = "6a1c0d4e3b29f8875ad6c2e0b1f4398d7e5a60c2b9d8f1e03a4c7b6d5e2f1a09";
        // hades-manifest.json of 1.2.2 as it was published, and its signature by key 1 (hades-manifest.json.sig)
        public const string Manifest122 =
            "ewogImZvcm1hdCI6ICJoYWRlcy11cGRhdGUtbWFuaWZlc3QtMSIsCiAiY2hhbm5lbCI6ICJzdGFibGUiLAogInNlcmlhbCI6IDEwMjAyOTkwMDAsCiAiaXNzdWVkIjog" +
            "IjIwMjYtMTEtMjZUMTM6NDE6MTVaIiwKICJleHBpcmVzIjogIjIwMjctMTEtMjZUMTM6NDE6MTVaIiwKICJyZWxlYXNlIjogewogICJ2ZXJzaW9uIjogIjEuMi4yIiwK" +
            "ICAic2VyaWFsIjogMTAyMDI5OSwKICAibWluX3NlcmlhbCI6IDEwMDAwOTksCiAgImFyY2giOiAiR2VuZXJpYy54ODZfNjQiLAogICJub3RlcyI6ICJodHRwczovL2dp" +
            "dGh1Yi5jb20vaGFkZXNkZXZscy9MaWJyZUVMRUMtSGFkZXMvcmVsZWFzZXMvdGFnL3YxLjIuMiIsCiAgImZpbGUiOiB7CiAgICJuYW1lIjogIkxpYnJlRUxFQy1IYWRl" +
            "cy0xLjIuMi1HZW5lcmljLng4Nl82NC50YXIiLAogICAidXJsIjogImh0dHBzOi8vZ2l0aHViLmNvbS9oYWRlc2RldmxzL0xpYnJlRUxFQy1IYWRlcy9yZWxlYXNlcy9k" +
            "b3dubG9hZC92MS4yLjIvTGlicmVFTEVDLUhhZGVzLTEuMi4yLUdlbmVyaWMueDg2XzY0LnRhciIsCiAgICJzaXplIjogNzIwMDc2ODAwLAogICAic2hhMjU2IjogImRm" +
            "MTkxYWZlY2JiYjIzMGIxYmZhNTNjYTI4MGI3MTYxMjE2MDUyZTY5OTY1ZmU3ODZiMTk1ODM1NmFiZmE3MTYiCiAgfQogfQp9Cg==";
        public const string Manifest122Signature = "ZMT4HUi5Zz1DrR9Zz+tKAuZW1GBduo5J//wyf0qtEymfoZV/AZjRj+j4xkgPz0ux+1s9lCN7xuvmSC6rthwWBQ==";
        // A SHA256SUMS signed by hades-channel.py sums-wrap (a throw-away key made by openssl): the list, its signature file
        // and the public key
        public const string PySums =
            "MjU0ZWRkZjE1ZDk1MzRlM2IyMGM1NTQ2OTA3N2FhMmYyNGYxNjdhYTRiODk3YTM2MzgxZDNlMjUxZTQ4MjljMiAgTGlicmVFTEVDLUhhZGVzLTkuOS45LUdlbmVyaWMu" +
            "eDg2XzY0LmltZy5negpjZjVmMjNmOGM1NDEyZmFjNDRmNzQ5NmZmNmNjMGI4OTljNWIyNjJlMTUyMmQ0MDg4Y2M4NjcyMzYyODc5YjgzICBMaWJyZUVMRUMtSGFkZXMt" +
            "OS45LjktR2VuZXJpYy54ODZfNjQudGFyCjkzNjM1Mzk2NWI0YmE5MTgwZTNhY2I3ODFkODFhYTYzNDM5MGFjOTVmZDI4NzRhOWNiYzRjNDg0NmI0OWZiZDQgIGhhZGVz" +
            "LW1hbmlmZXN0Lmpzb24K";
        public const string PySignature =
            "{\"format\": \"hades-sha256sums-signature-1\", \"key\": \"a7dd8c40711c0eca\", \"signature\": " +
            "\"8BRTFvHBcSKWvmVNnZwbixzz6Po/GDnY2enSj4BhPXwPnGPSlGLUpeeKLj3qJjTlh8Yhh+CxWrzEMYqJTnKECA==\"}\n";
        public const string PyKey = "Uf0IP98cVCIVJ4LXyMoNj2cndNxz/348zwJCIwzjK50=";
    }
}
