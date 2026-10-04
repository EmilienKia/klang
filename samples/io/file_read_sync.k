/*
 * Read a caller-selected file through the interruptible Sync bridge.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

main(args: const String[]) : int {
    channel : k::io::file::FileChannel;
    status : int = 0;
    try {
        if (args.size != 2 || args[1].size() == 0u) return usage(args[0], "filepath");
        path : k::io::file::Path(args[1]);
        deadline : Duration = Duration::ofSeconds(30L);
        buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4096u);
        scratch : byte[4096];
        channel = k::io::file::FileChannel::openSync(path, k::io::file::OPEN_READ, deadline);
        while (true) {
            count : int = channel.readSync(buffer, deadline);
            if (count == 0) break;
            emitBytes(buffer, scratch, count);
            buffer->clear();
        }
        k::io::stdoutBytes.flush();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    try {
        if (channel.isOpen()) channel.closeSync();
    } catch (error: Throwable&) {
        status = failureStatus(error);
    }
    return status;
}
