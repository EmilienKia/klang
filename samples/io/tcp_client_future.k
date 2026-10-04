/*
 * One four-byte TCP exchange using Async methods and Future owners.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

class Application : public k::Application {
    private channel : k::io::SocketChannel;
    private buffer : k::io::ByteBuffer!;
    private connecting : Future<k::io::SocketChannel>!;
    private transfer : Future<int>!;
    private closing : Future<k::io::IoDone>!;

    public main(args: const String[]) : int {
        status : int = 0;
        deadline : Duration = Duration::ofSeconds(30L);
        joinTimeout : Duration = Duration::ofSeconds(35L);
        try {
            if (args.size < 2 || args.size > 3) return usage(args[0], "port [host]");
            port : int = parsePort(args[1], 1);
            if (port < 0 || (args.size == 3 && args[2].size() == 0u))
                return usage(args[0], "port [host] (port: 1..65535)");
            address : k::io::NetworkAddress = clientAddress(args, port);
            buffer = k::io::ByteBuffer::allocateDirect(4u);
            connecting = k::io::SocketChannel::connectAsync(address, deadline);
            channel = connecting->get(joinTimeout);
            fillPing(buffer);
            transfer = channel.writeFullyAsync(buffer, deadline);
            written : int = transfer->get(joinTimeout);
            if (written != 4) throw IllegalStateException();
            buffer->clear();
            transfer = channel.readFullyAsync(buffer, deadline);
            count : int = transfer->get(joinTimeout);
            checkPing(buffer, count);
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        try {
            if (channel.isOpen()) {
                closing = channel.closeAsync();
                closing->get(joinTimeout);
            }
            if (status == 0) announcePing();
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        return status;
    }
}
