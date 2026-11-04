/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026-present Hades
 *
 * BD-Live in the isolated BD-J service: the names that the Java code of a disc looks up are
 * resolved by the player (libbluray's BD-J host asks it), which answers only with addresses of
 * the Internet. Only in the JVM of hades-bdj-net@.service, with BD-Live on (libbluray puts this
 * module on the module path then). It exports nothing: the disc's code reaches it only through
 * java.net.InetAddress.
 */
module org.videolan.hades.bdlive {
    provides java.net.spi.InetAddressResolverProvider
        with org.videolan.hades.bdlive.ResolverProvider;
}
