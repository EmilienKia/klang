/*
 * Ephemeral loopback listener with accept/transfer/close Future joins.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

class Application : public k::Application {
    private server : k::io::ServerSocket!;
    private peer : k::io::SocketChannel;
    private buffer : k::io::ByteBuffer!;
    private accepting : Future<k::io::SocketChannel>!;
    private transfer : Future<int>!;
    private closingPeer : Future<k::io::IoDone>!;
    private closingServer : Future<k::io::IoDone>!;

    public main(args: const String[]) : int {
        status : int = 0;
        deadline : Duration = Duration::ofSeconds(30L);
        joinTimeout : Duration = Duration::ofSeconds(35L);
        try {
            if (args.size > 2) return usage(args[0], "[port] (port: 0..65535)");
            port : int = 0;
            if (args.size == 2) port = parsePort(args[1], 0);
            if (port < 0) return usage(args[0], "[port] (port: 0..65535)");
            address : k::io::NetworkAddress("127.0.0.1", port);
            buffer = k::io::ByteBuffer::allocateDirect(4u);
            server = k::io::ServerSocket::bind(address);
            announcePort(server->localPort());
            accepting = server->acceptAsync(deadline);
            peer = accepting->get(joinTimeout);
            transfer = peer.readFullyAsync(buffer, deadline);
            count : int = transfer->get(joinTimeout);
            checkPing(buffer, count);
            buffer->flip();
            transfer = peer.writeFullyAsync(buffer, deadline);
            written : int = transfer->get(joinTimeout);
            if (written != 4) throw IllegalStateException();
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        try {
            if (peer.isOpen()) {
                closingPeer = peer.closeAsync();
                closingPeer->get(joinTimeout);
            }
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        try {
            if (server != null && server->isOpen()) {
                closingServer = server->closeAsync();
                closingServer->get(joinTimeout);
            }
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        return status;
    }
}
