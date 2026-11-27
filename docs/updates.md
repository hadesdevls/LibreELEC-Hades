# How updates are kept safe

An update replaces the whole system of LibreELEC Hades, so nothing that reaches your PC can change more. This page says, first in plain words and then in detail, how LibreELEC Hades makes sure that what it installs is a version that its maintainer published, and what you can choose.

## In plain words

**There are three ways a new version gets in:**

1. **From the system itself** (*Settings > LibreELEC > Updates*). Every six hours, while nothing plays, LibreELEC Hades looks on GitHub for a newer version, tells you ("New version: Settings > LibreELEC > Updates"), and installs it only after you say yes. This is the default.
2. **A file that you copy into the `Update` folder** (`/storage/.update`, a share on the network when file sharing is on), as in LibreELEC.
3. **The installer for Windows**, which writes the new version from Windows, after it checks that the image is the one that was published (below).

**Who signs a version.** Each version is signed by the maintainer of LibreELEC Hades with a private key that is never stored on GitHub or on any server. Your system carries, inside its read-only system image, the two public keys that may sign: the one in use and a spare one kept apart, so that the first can be replaced without a new installation. Nothing else is trusted: not GitHub, not the network, not a file's name.

**What is checked before anything is installed.** A version from the system itself is taken only when:

- its signature holds for one of those two keys;
- it is newer than the version you run (an update never takes you back to an older one);
- it is for this kind of system;
- the update file arrives whole, with exactly the size and fingerprint (SHA-256) that the signature covers.

Anything else is refused, and nothing of it is kept. It is downloaded over HTTPS only, and looking sends nothing about your system: no identifier, no hardware, no version.

**And again at the next start.** A version is installed by the system itself at its next start. Before that start reads or runs anything of the update, it checks the update file again, with the programs and the keys of the version you run, not those of the update (unless you turned off the question for files copied by hand, below).

**A file that you copy by hand.** From 1.2.3, an update copied into the `Update` folder is installed without a question only when it is signed:

- the `.tar` file of a release copied together with its two small files `hades-manifest.json` and `hades-manifest.json.sig` (they are published with every release);
- or the `.tar` file alone, when it is the newest release and the system can reach GitHub: LibreELEC Hades recognises it and keeps its signature next to it.

The version you run can be put back the same way, to repair it (for example when Kodi no longer starts): its own `.tar` with its two files is installed again at the next start. An older version is not.

Anything else (another build, an older version, a changed copy, an image file) is installed only after you say yes on the TV. As soon as LibreELEC Hades sees the file, it shows its name and asks: *Install*, *Not now* (preselected) or *Remove it*. If you restart before answering, the file is not installed and stays in the folder, and the question comes once LibreELEC Hades has started. This way a file that someone else put there, or that is not what it claims to be, never replaces your system without you knowing.

**Your choices**, in *Settings > LibreELEC > Updates*:

- *Automatic Updates*: "manual" (the default) tells you and asks; "auto" downloads a new version by itself and installs it at the next start, as LibreELEC does.
- To stop looking for new versions: keep "manual" and turn off *Show Update Notifications*. *Available Versions* still looks when you choose it.
- *Ask before installing an update placed by hand*: on by default. Turned off, any update in the `Update` folder is installed at the next start without a question, as in LibreELEC. It says what that opens and asks before it turns off.

**The image that the installer for Windows writes.** From 1.2.3 every release has `SHA256SUMS`, the list of its files with their fingerprints, and `SHA256SUMS.sig`, its signature by the same keys. Download them with the image and keep the three files in one folder: the installer checks the signature and looks for the image in the list before it writes anything, and says what it found:

- **signed by LibreELEC Hades** (green): the image is the one that was published, and it arrived whole;
- **not signed** (orange): there is no signature next to it (an image of an older release, a download without those two files, a build of your own). It is written only after a question that explains this and asks you to type a word;
- **not the image that LibreELEC Hades signed** (red): the list or its signature was changed, or the list gives the image another fingerprint. It is not written; download the release again.

**With Secure Boot** (the box in the installer for Windows), only a signed update is installed from the `Update` folder, whatever that setting says, because the system signs the new version for your PC as it installs it. Install anything else with the installer for Windows.

**What a signature does not cover:**

- **The installer for Windows itself.** From 1.2.3 the list of the files of each release, `SHA256SUMS`, is signed with the same keys (`SHA256SUMS.sig`), and the installer checks it before it writes an image (below). The installer cannot vouch for itself, though, and it carries no paid Windows signature: download it only from https://github.com/hadesdevls/LibreELEC-Hades/releases, and check its SHA-256 against the signed list as shown below.
- **Programs that already run on your system.** Kodi and its add-ons run with full rights, and so does anyone who can sign in over SSH or write to the shares of file sharing (`Configfiles`, for one, holds what the system runs at start). Such a program or person can change the system in other ways than an update. Install add-ons only from sources you trust, and keep file sharing with a password, as *Network access: Recommended* has it.
- **Being told about a new version.** Someone who controls your network can keep you from reaching GitHub, and then you are not told of a new version. They cannot make the system install an older or a changed one.

## In detail

### The update channel

The image carries, in `/usr/share/hades/update` (inside the read-only system image):

- `channel.conf`: the address of the manifest, `https://github.com/hadesdevls/LibreELEC-Hades/releases/latest/download/hades-manifest.json`, and the channel, `stable`;
- `keys/hades-1.pub` and `keys/hades-2.pub`: the two Ed25519 public keys;
- `release-serial`: the number of this version (MAJOR times 1000000, plus MINOR times 10000, plus PATCH times 100, plus 99 for a final version; 1.2.3 is 1020399).

Every release on GitHub carries `hades-manifest.json` and its signature `hades-manifest.json.sig`. The manifest is a small JSON file: format, channel, its own serial, when it was issued and when it expires, and the release: version, serial, the oldest serial it can be installed over (`min_serial`), architecture (`Generic.x86_64`), and the update file with its name, address, size and SHA-256.

The client (`hades_update.py` in the LibreELEC settings add-on) checks, in this order:

1. **The signature, before the manifest is read.** Ed25519 (RFC 8032), verified with pycryptodome, over the fixed label `hades-update-manifest-1` followed by the bytes of the manifest, so that a signature made for anything else does not fit. The signature file names its key; a key that is not one of the two of the image is refused.
2. **The form.** Exactly the fields of its format: none missing, none more, none twice; whole numbers only; a manifest of 64 KiB and a signature of 4 KiB at most.
3. **Channel and architecture** must be this system's.
4. **No going back.** A manifest older (by its serial) than the newest one this system has taken is refused; that serial is kept on the system and never lowered. The release is offered only when its serial is higher than the image's, and installed from the system only when the image's is at least its `min_serial`.
5. **Expiry.** A manifest expires 365 days after it is signed. Whether it has expired is judged only once the clock has been set over the network in that start; before that, nothing is taken.
6. **The file.** Downloaded over HTTPS only, with the certificates of the system, never more than its signed size, and kept only when its size and SHA-256 match, both as it arrives and as it was written to the disk. It goes to Kodi's temporary folder first and into `/storage/.update` only once checked, together with the signed manifest and its signature, and with no other update left there that the next start could take instead.

Requests are plain GETs with the user agent `LibreELEC-Hades-update`, without cookies, without a proxy from the environment and without anything about the system. A redirect is followed only over HTTPS, at most four in a row, and only to the same host and port or to GitHub's hosts of release files on port 443 (`github.com`, `release-assets.githubusercontent.com`, `objects.githubusercontent.com`); anything else is refused before it is asked. A manifest has to arrive whole within 60 seconds, and a download that brings less than 64 KiB in two minutes is given up.

### The list of a release (`SHA256SUMS.sig`)

From 1.2.3, `SHA256SUMS` lists every file of the release (the image, the update file, the installer, the source code and its lists) with its SHA-256, and `SHA256SUMS.sig` is its signature: Ed25519 over the fixed label `hades-sha256sums-1` and a line break, followed by the bytes of `SHA256SUMS`. The label is not the manifest's, so neither signature can be passed off as the other. The list leaves out itself, its signature and the manifest of the update channel with its signature: the manifest holds by its own signature, and a renewed one can replace it later without touching the list. `SHA256SUMS.sig` is a line of JSON, like the manifest's: `{"format": "hades-sha256sums-signature-1", "key": "<id>", "signature": "<base64>"}`, where the id is the first 16 hexadecimal digits of the SHA-256 of the public key (`444378a0ca23e565` for key 1, `da5e1e82124a210e` for key 2).

The installer for Windows carries the two public keys in its code (`tools/hades-installer/Signature.cs`) and checks the signature itself (Ed25519 written out with .NET's big integers, and tested against RFC 8032, against the signature of the manifest of 1.2.2 and against lists signed by the publication tools). Then it looks for the SHA-256 of the image in the list, by its value, so a renamed copy of a published image is still recognised. It does not use the network.

**To check a download yourself** (Linux or macOS with OpenSSL 3, or WSL on Windows), in the folder with the files:

```sh
# key 1 of LibreELEC Hades (for key 2, da5e1e82124a210e: XPICry2F28eC7zSIT5rfVPhSCjBTBUE7Zeb0jnu9qzs= in place of the last part)
printf -- '-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEAxNiCz47zKLov6jSe9VicYhfNCA/v+rPBjEAzQsnwcww=\n-----END PUBLIC KEY-----\n' > hades-1.pem
{ printf 'hades-sha256sums-1\n'; cat SHA256SUMS; } > SHA256SUMS.tosign
sed -n 's/.*"signature": "\([A-Za-z0-9+/=]*\)".*/\1/p' SHA256SUMS.sig | openssl base64 -d -A > SHA256SUMS.sig.bin
openssl pkeyutl -verify -rawin -pubin -inkey hades-1.pem -in SHA256SUMS.tosign -sigfile SHA256SUMS.sig.bin
sha256sum -c --ignore-missing SHA256SUMS
```

The fourth line must say `Signature Verified Successfully`, and the last one `OK` for each file you downloaded. On Windows alone, `Get-FileHash <file>` gives a file's SHA-256 to compare with the list; the signature itself is what the installer checks.

### At the next start

The start of the system (the `init` of its initramfs) installs what it finds in `/storage/.update`: the first `.tar` by name, else an `.img.gz` or `.img`, else a `KERNEL` and `SYSTEM`. Before it reads or runs anything of it, it runs, from the system that is installed now (its Python, its client and its keys, never those of the update):

- **without Secure Boot:** `hades_update.py check-update`. The update is installed when it is the `.tar` named by a manifest signed for this system, for a newer release that can be installed over it or for the release that runs (the same signed bytes, put back to repair it), with that file's name, size and SHA-256 (the manifest and its signature next to it); or when it is, byte for byte and file for file, the update approved on this system. An approval holds the name, size and SHA-256 of each file, is written only when you say *Install* on the TV or choose a version from a channel that you added by hand, is kept outside the folders that file sharing offers, and is used once. Otherwise the update is not installed and is left where it is, and the screen and the kernel log say why (lines that start with `hades-update:`). With *Ask before installing an update placed by hand* off, every update is installed, as in LibreELEC;
- **with Secure Boot:** `hades_update.py check-kept`, which takes only a signed `.tar`, as above; no approval counts. Anything else is removed and not installed (kernel log: `hades-secureboot:`).

The expiry and the newest manifest taken are not checked again at start, because the clock is not set over the network yet; the client checked both before it downloaded the file.

### What is not covered, and why

- **Root.** Kodi, its add-ons, SSH and the shares of file sharing all act with full rights on the system, as in LibreELEC. What they can do is not limited to the `Update` folder, so the check at start is a guard against a file that is not what it claims to be, not a wall against a program that already runs as root.
- **The installer for Windows.** It checks the signed list before it writes an image, but nothing checks the installer before it runs but you: its SHA-256 is in the signed list (below). Releases before 1.2.3 have no signed list, and the installer says so of their images.
- **The clock.** Network time is not authenticated: someone who controls the network could move the clock back so that an expired manifest seems current. They still cannot make a system take a manifest older than the newest one it has seen, a version that is not newer, or a file that was not signed.
- **Forgetting.** A reset to defaults forgets the newest manifest seen; the signature, the expiry and the version of the image still hold.
- **A key that leaks** stays valid on systems that have not yet installed a version without it. The spare key exists so that the next version can be signed with it and drop the other.
