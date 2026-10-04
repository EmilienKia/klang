/*
 * FileChannel fully Future/Sync forms.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module file_channel_fully;

main() : int throws(Throwable) {
    deadline : Duration = Duration::ofSeconds(5L);
    path : k::io::file::Path("/tmp/klang-file-channel-fully.bin");
    channel : k::io::file::FileChannel = k::io::file::FileChannel::openSync(
        path,
        k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
        k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
    buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(2u);
    buffer->put((byte) 79); buffer->put((byte) 75); buffer->flip();
    written : Future<int>! = channel.writeFullyAsync(buffer, deadline);
    count : int = written->get(deadline);
    buffer->clear();
    channel.readFullySync(buffer, 0L, deadline);
    channel.closeSync();
    delete written; delete buffer;
    return count == 2 ? 0 : 1;
}
