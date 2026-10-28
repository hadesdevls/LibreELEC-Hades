# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Hades diagnostics: remove credentials from logs before they are written to the boot partition (busybox sed -E).
# redact.sh runs it after redact-ids.awk, which has put markers in the place of the addresses of the network,
# serial numbers, UUIDs, SSH keys, network names and the name of the system, and has cut down an address or a
# path of media that ends with the name of a media file over the spaces in it.
# Network and add-on URLs keep only their scheme and host (also with their slashes escaped, as in JSON; of one
# that is percent-encoded inside something else, only the scheme); one with a query goes up to the end of its
# line, or to the first quote or angle bracket (add-ons write a search into a query with its spaces). Paths of
# media (under /var/media, /media, /mnt, /run/media and the media folders of /storage) keep only that folder.
# E-mail addresses go (but the names of systemd units, as modprobe@drm.service); the users of a configuration
# of Samba; a line from its authorization or cookie header on, and from anything named like a password on;
# the values of anything named like a key, a token, a secret, a session or a code (also between tags, and with
# the quotes escaped as in JSON inside a log line); and long token-like strings are replaced.
# What it does not catch: a secret under a name that says nothing, shorter than 32 characters, outside an
# address; standard base64, whose "/" cuts it into shorter runs (a rule for it would eat file paths); a
# long run without a single digit, which is taken for a name (of random tokens, one in some hundreds); and
# the rest of an address or of a path of media after a space in it, where nothing in its part of the line
# ends with the extension of a media file and it has no query (a folder named "Some Film (2001)", unquoted).
# Three rules leave a whole line out and say so in its place: first of all, a line of 32767 characters or
# more and a line with 256 letters in a row (the rules for addresses follow a run of letters to its end
# from every "rtmp", "rtsp" or "mms" in it); and, after the rules for addresses, a line that still has a
# run of 256 or more of the characters
# that a name, a mail address or a token is written with (no file name is that long: it is a token or a
# blob of data). Three of the rules below start with a repetition that sed tries again from every
# character of such a run, so that without this the time grows with the square of its length (128 KiB of
# letters and digits took 44 s). Nothing of a line that is left out can be let through; the other lines
# go through the rules as they always did.
/^.{32767}/s~.*~[left out, a line of 32767 characters or more]~
/[A-Za-z]{256,}/s~.*~[left out, a line with a run of 256 characters or more]~
# (an address or a path of media that ends with the name of a media file, over the spaces in it, redact-ids.awk
# has cut down already, with "{//}" or "{/}" where "//" or "/" goes: the rules for other addresses and paths
# do not take it again, and the last two of these rules put the slashes back. An address with a query goes up
# to the end of its line, or to the first quote or angle bracket: a query can have spaces in it)
s~(https?|rtmp[a-z]*|rtsp[a-z]*|mms[a-z]*|smb|nfs|s?ftps?|ssh|davs?|webdavs?|wss?|udp|rtp|upnp|plugin):(\\?/){2}([^/@ "'<>\\]*@)?([^/:? "'<>\\]+)[^ "'<>]*\?[^"'<>]*~\1://\4/[redacted]~Ig
s~(https?|rtmp[a-z]*|rtsp[a-z]*|mms[a-z]*|smb|nfs|s?ftps?|ssh|davs?|webdavs?|wss?|udp|rtp|upnp|plugin):(\\?/){2}([^/@ "'<>\\]*@)?([^/:? "'<>\\]+)[^ "'<>]*~\1://\4/[redacted]~Ig
s~(https?|rtmp[a-z]*|rtsp[a-z]*|mms[a-z]*|smb|nfs|s?ftps?|ssh|davs?|webdavs?|wss?|udp|rtp|upnp|plugin)%(25)*3A%(25)*2F%(25)*2F[^ "'<>&]*~\1%3A%2F%2F[redacted]~Ig
s~(^|[^A-Za-z0-9_.-])/(var/media/[^/"'<>]+|media/[^/"'<>]+|mnt/[^/"'<>]+|run/media/[^/"'<>]+/[^/"'<>]+|storage/(videos|music|pictures|tvshows|movies|downloads|recordings|emulators|roms))/[^ "'<>]+~\1/\2/[redacted]~Ig
s~:\{//\}~://~g
s~\{/\}~/~g
/[A-Za-z0-9_.%+=-]{256,}/s~.*~[left out, a line with a run of 256 characters or more]~
s~@([][A-Za-z0-9_.:\\-]*\.(service|socket|target|mount|automount|timer|path|slice|scope|device|swap))~{@}\1~g
s~[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}~[email]~g
s~\{@\}~@~g
s~^([ \t]*(valid users|invalid users|admin users|read list|write list|username map|username|user name)[ \t]*=).*~\1 [redacted]~I
s~((proxy-)?authorization|(set-)?cookies?|[a-z0-9_.-]*(passw(or)?d|passwd|passphrase|pwd|psk)[a-z0-9_.-]*)(["'\\ ]*[=:]).*~\1\6 [redacted-line]~Ig
s~(Bearer|Basic|Digest|Negotiate) +[^ "'\\<>]+~\1 [redacted]~Ig
s~(^|[^A-Za-z0-9])(([A-Za-z0-9_.-]*(key|token|secret|pin|sig(nature)?|session([_-]?id)?|sid|credentials?|(device|user)[_-]?code|otp)([_-]?[0-9]+)?|pass|auth[a-z_]*)["'\\ ]*[=:]["'\\ ]*)[^ "'\\&,;<>]+~\1\2[redacted]~Ig
s~(<[^<>]*(key|token|secret|passw(or)?d|passwd|passphrase|pwd|psk|auth|session|credential|cookie|sid)[^<>]*>)[^<>]+(</)~\1[redacted]\4~Ig
# Long runs: marked, then the ones that are names are let go (an extension of OpenGL, EGL or Vulkan, or a run
# without a digit), and what is still marked is replaced. A "=" ends a run, so that a name does not join its value.
s~[A-Za-z0-9][A-Za-z0-9_+-]{31,}={0,2}~{#&#}~g
s~\{#((E?GL|GLX|VK)_[A-Za-z0-9_]*|[A-Za-z_+-]*)#\}~\1~g
s~\{#[^#]*#\}~[redacted-long]~g
