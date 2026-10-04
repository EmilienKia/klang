/*
 * Async submission followed by bounded, blocking Future joins.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

class Application : public k::Application {
    private channel : k::io::file::FileChannel;
    private buffer : k::io::ByteBuffer!;
    private opened : Future<k::io::file::FileChannel>!;
    private reading : Future<int>!;
    private closing : Future<k::io::IoDone>!;

    public main(args: const String[]) : int {
        status : int = 0;
        deadline : Duration = Duration::ofSeconds(30L);
        joinTimeout : Duration = Duration::ofSeconds(35L);
        try {
            if (args.size != 2 || args[1].size() == 0u) return usage(args[0], "filepath");
            path : k::io::file::Path(args[1]);
            buffer = k::io::ByteBuffer::allocateDirect(4096u);
            scratch : byte[4096];
            opened = k::io::file::FileChannel::openAsync(path, k::io::file::OPEN_READ, deadline);
            channel = opened->get(joinTimeout);
            while (true) {
                reading = channel.readAsync(buffer, deadline);
                count : int = reading->get(joinTimeout);
                if (count == 0) break;
                emitBytes(buffer, scratch, count);
                buffer->clear();
            }
            k::io::stdoutBytes.flush();
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        try {
            if (channel.isOpen()) {
                closing = channel.closeAsync();
                closing->get(joinTimeout);
            }
        } catch (error: Throwable&) {
            status = failureStatus(error);
        }
        return status;
    }
}
