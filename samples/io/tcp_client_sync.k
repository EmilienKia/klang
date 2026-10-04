/*
 * One four-byte TCP exchange through the Sync bridge.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

main(args: const String[]) : int {
    channel : k::io::SocketChannel;
    status : int = 0;
    try {
        if (args.size < 2 || args.size > 3) return usage(args[0], "port [host]");
        port : int = parsePort(args[1], 1);
        if (port < 0 || (args.size == 3 && args[2].size() == 0u))
            return usage(args[0], "port [host] (port: 1..65535)");
        address : k::io::NetworkAddress = clientAddress(args, port);
        deadline : Duration = Duration::ofSeconds(30L);
        buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
        channel = k::io::SocketChannel::connectSync(address, deadline);
        fillPing(buffer);
        written : int = channel.writeFullySync(buffer, deadline);
        if (written != 4) throw IllegalStateException();
        buffer->clear();
        count : int = channel.readFullySync(buffer, deadline);
        checkPing(buffer, count);
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    try {
        if (channel.isOpen()) channel.closeSync();
        if (status == 0) announcePing();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    return status;
}
