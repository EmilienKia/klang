/*
 * Ephemeral loopback Future/Sync channels. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
module stream_channel_loopback;

main() : int throws(Throwable) {
    deadline : Duration = Duration::ofSeconds(5L);
    local : k::io::NetworkAddress("127.0.0.1", 0);
    server : k::io::ServerSocket! = k::io::ServerSocket::bind(local);
    remote : k::io::NetworkAddress("localhost", server->localPort());
    accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline);
    client : k::io::SocketChannel = k::io::SocketChannel::connectSync(remote, deadline);
    peer : k::io::SocketChannel = accepted->get(deadline);
    buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(2u);
    buffer->put((byte) 79); buffer->put((byte) 75); buffer->flip();
    count : int = client.writeFullySync(buffer, deadline);
    buffer->clear();
    peer.readFullySync(buffer, deadline);
    valid : bool = count == 2 && buffer->get(0u) == (byte) 79 && buffer->get(1u) == (byte) 75;
    client.closeSync(); peer.closeSync(); server->closeSync();
    delete buffer; delete accepted; delete server;
    return valid ? 0 : 1;
}
