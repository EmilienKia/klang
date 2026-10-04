/*
 * Ephemeral loopback listener, one four-byte exchange, then close.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

main(args: const String[]) : int {
    server : k::io::ServerSocket!;
    peer : k::io::SocketChannel;
    status : int = 0;
    try {
        if (args.size > 2) return usage(args[0], "[port] (port: 0..65535)");
        port : int = 0;
        if (args.size == 2) port = parsePort(args[1], 0);
        if (port < 0) return usage(args[0], "[port] (port: 0..65535)");
        address : k::io::NetworkAddress("127.0.0.1", port);
        deadline : Duration = Duration::ofSeconds(30L);
        buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
        server = k::io::ServerSocket::bind(address);
        announcePort(server->localPort());
        peer = server->acceptSync(deadline);
        count : int = peer.readFullySync(buffer, deadline);
        checkPing(buffer, count);
        buffer->flip();
        written : int = peer.writeFullySync(buffer, deadline);
        if (written != 4) throw IllegalStateException();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    try {
        if (peer.isOpen()) peer.closeSync();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    try {
        if (server != null && server->isOpen()) server->closeSync();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    return status;
}
