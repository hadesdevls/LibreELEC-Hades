/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026-present Hades
 */
package org.videolan.hades.bdlive;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.net.spi.InetAddressResolver;
import java.net.spi.InetAddressResolverProvider;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

/**
 * The resolver of the JVM of the BD-J service with BD-Live (JEP 418). The service has no name
 * server (the one of a home network is on that network or on this system, which the service may
 * not reach): each name goes to the player, which resolves it and answers only with IPv4
 * addresses of the Internet. Nothing else is resolved here: no reverse lookups, no IPv6, and no
 * fallback to the JVM's own resolver.
 */
public final class ResolverProvider extends InetAddressResolverProvider {

    @Override
    public InetAddressResolver get(Configuration configuration) {
        return new Resolver();
    }

    @Override
    public String name() {
        return "LibreELEC Hades BD-Live";
    }

    private static final class Resolver implements InetAddressResolver {

        @Override
        public Stream<InetAddress> lookupByName(String host, LookupPolicy policy) throws UnknownHostException {
            if (host == null || host.isEmpty() || host.length() > 253) {
                throw new UnknownHostException(String.valueOf(host));
            }
            if ((policy.characteristics() & LookupPolicy.IPV4) == 0) {
                throw new UnknownHostException(host + ": BD-Live resolves IPv4 addresses only");
            }
            byte[][] found = Natives.resolve0(host);
            if (found == null || found.length == 0) {
                throw new UnknownHostException(host + ": no address that BD-Live may reach");
            }
            List<InetAddress> addresses = new ArrayList<>(found.length);
            for (byte[] a : found) {
                if (a == null || a.length != 4) {
                    throw new UnknownHostException(host);
                }
                addresses.add(InetAddress.getByAddress(host, a));
            }
            return addresses.stream();
        }

        @Override
        public String lookupByAddress(byte[] addr) throws UnknownHostException {
            throw new UnknownHostException("BD-Live: no reverse lookups");
        }
    }
}

/** The one native method, registered by libbluray's BD-J host (bdr_net_server.c). */
final class Natives {
    private Natives() {
    }

    static native byte[][] resolve0(String host);
}
