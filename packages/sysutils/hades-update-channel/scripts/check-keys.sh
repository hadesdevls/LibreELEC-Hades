#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Hades: the two keys of the update channel, checked before an image carries them (package.mk) and before a
# release is published. In <keys>: hades-1.pub and hades-2.pub, each one line "ed25519 <the 32 bytes of the
# public key, base64>" (lines that start with # are comments), and hades-1.proof and hades-2.proof, each the
# signature (Ed25519, base64, one line) of the key over "hades-update-key-proof-1" and a new line: it shows
# that the key is a whole one and that its private half exists and is held. Nothing else is in <keys>.
# Refused: a key that is missing, malformed or without a proof that holds; the same key twice; a key of
# <test keys> (keys whose private half anyone can make: the tests use them).
# With <pinned> (the two key lines that the publisher pinned when the keys came in, kept outside the tree), the two
# keys have to be exactly those, in their order: a change of the keys in the tree alone stops the build.
# Usage: check-keys.sh <keys> <test keys> [<pinned>]   (OPENSSL: the openssl that checks the proofs, 3.0 or newer)
set -u
K=${1:-}
T=${2:-}
PIN=${3:-}
O=${OPENSSL:-openssl}
fail() {
  echo "check-keys: $*" >&2
  exit 1
}
[ -f "$T" ] || fail "no list of test keys ($T)"
[ -d "$K" ] || fail "no keys ($K): the two public keys of LibreELEC Hades (hades-1.pub and hades-2.pub, with their proofs) are not in the tree. A build that is never published can be made without a channel: HADES_UPDATE_CHANNEL=no"
other=$(ls -A "$K" | grep -v -x -E 'hades-[12]\.(pub|proof)')
[ -z "$other" ] || fail "in $K, files that are not the keys: $(echo $other)"
W=$(mktemp -d) || fail "no temporary folder"
trap 'rm -rf "$W"' EXIT
printf 'hades-update-key-proof-1\n' >"$W/proof.msg"
first=""
lines_found=""
for n in 1 2; do
  pub=$K/hades-$n.pub
  proof=$K/hades-$n.proof
  [ -f "$pub" ] || fail "$pub is missing"
  [ -f "$proof" ] || fail "$proof is missing"
  lines=$(grep -c -v -E '^[[:space:]]*(#|$)' "$pub")
  [ "$lines" = 1 ] || fail "$pub: $lines lines that are not comments (one: ed25519 <key>)"
  read -r kind b64 more <<<"$(grep -v -E '^[[:space:]]*(#|$)' "$pub")"
  [ "$kind" = ed25519 ] && [ -z "$more" ] || fail "$pub: not \"ed25519 <key>\""
  [[ $b64 =~ ^[A-Za-z0-9+/]{43}=$ ]] || fail "$pub: the key is not 32 bytes in base64"
  [ "$(printf '%s' "$b64" | base64 -d 2>/dev/null | wc -c)" = 32 ] || fail "$pub: the key is not 32 bytes in base64"
  if grep -v -E '^[[:space:]]*(#|$)' "$T" | grep -q -x -F "ed25519 $b64"; then
    fail "$pub is a TEST key (in $(basename "$T")): its private half is public, so an image never trusts it"
  fi
  [ "$b64" != "$first" ] || fail "hades-1.pub and hades-2.pub are the same key: the second has to be another, kept apart"
  first=$b64
  lines_found="$lines_found$kind $b64
"
  # (the key as OpenSSL reads it: the 12 bytes that say "Ed25519 public key" are 16 characters of base64)
  printf -- '-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEA%s\n-----END PUBLIC KEY-----\n' "$b64" >"$W/key.pem"
  s=$(grep -v -E '^[[:space:]]*(#|$)' "$proof")
  [[ $s =~ ^[A-Za-z0-9+/]{86}==$ ]] || fail "$proof: not one line of 64 bytes in base64"
  printf '%s' "$s" | base64 -d >"$W/proof.sig" 2>/dev/null
  [ "$(wc -c <"$W/proof.sig")" = 64 ] || fail "$proof: not 64 bytes"
  "$O" pkeyutl -verify -rawin -pubin -inkey "$W/key.pem" -in "$W/proof.msg" -sigfile "$W/proof.sig" >/dev/null 2>&1 ||
    fail "$proof does not hold for $pub: the key is not whole, or it is not the one that signed its proof"
  echo "check-keys: hades-$n.pub: ed25519 ${b64:0:12}..., its proof holds"
done
if [ -n "$PIN" ]; then
  [ -f "$PIN" ] || fail "no pinned keys ($PIN)"
  [ "$(printf '%s' "$lines_found")" = "$(grep -v -E '^[[:space:]]*(#|$)' "$PIN")" ] ||
    fail "the keys of the tree are not the pinned ones of $PIN (both, in their order): a key changed in the tree alone is never trusted"
  echo "check-keys: the two keys are the pinned ones"
fi
echo "check-keys: OK"
