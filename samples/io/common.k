/*
 * Shared CLI, diagnostics and byte framing for the I/O examples.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

parsePort(text: const String&, minimum: int) : int {
    if (text.size() == 0u) return -1;
    port : int = 0;
    index : unsigned int = 0u;
    while (index < text.size()) {
        digit : int = (int) text.at(index) - 48;
        if (digit < 0 || digit > 9) return -1;
        if (port > (65535 - digit) / 10) return -1;
        port = port * 10 + digit;
        ++index;
    }
    return port < minimum ? -1 : port;
}

usage(program: const String&, arguments: const char[]) : int throws(Throwable) {
    programChars : char[]! = program.toUtf32();
    k::io::stderrPrint.print("Usage: ");
    k::io::stderrPrint.print(programChars);
    k::io::stderrPrint.print(" ");
    k::io::stderrPrint.println(arguments);
    k::io::stderrPrint.flush();
    return 2;
}

clientAddress(args: const String[], port: int) : k::io::NetworkAddress throws(Throwable) {
    if (args.size == 2) return k::io::NetworkAddress("127.0.0.1", port);
    host : char[]! = new char[args[2].size() + 1u];
    index : unsigned int = 0u;
    while (index < args[2].size()) {
        host[index] = args[2].at(index);
        ++index;
    }
    host[index] = (char) 0;
    return k::io::NetworkAddress(host, port);
}

reportIoError(error: const k::io::IoError&) throws(Throwable) {
    k::io::stderrPrint.print("I/O error kind=");
    kind : k::io::IoErrorKind = error.kind();
    if (kind == k::io::IoErrorKind::System) k::io::stderrPrint.print("System");
    else if (kind == k::io::IoErrorKind::Cancelled) k::io::stderrPrint.print("Cancelled");
    else if (kind == k::io::IoErrorKind::Closed) k::io::stderrPrint.print("Closed");
    else if (kind == k::io::IoErrorKind::Timeout) k::io::stderrPrint.print("Timeout");
    else if (kind == k::io::IoErrorKind::EndOfStream) k::io::stderrPrint.print("EndOfStream");
    else if (kind == k::io::IoErrorKind::Unsupported) k::io::stderrPrint.print("Unsupported");
    else if (kind == k::io::IoErrorKind::ResourceExhausted) k::io::stderrPrint.print("ResourceExhausted");
    else if (kind == k::io::IoErrorKind::ExecutorRejected) k::io::stderrPrint.print("ExecutorRejected");
    else if (kind == k::io::IoErrorKind::InvalidArgument) k::io::stderrPrint.print("InvalidArgument");
    else if (kind == k::io::IoErrorKind::Interrupted) k::io::stderrPrint.print("Interrupted");
    k::io::stderrPrint.print(" errno=");
    k::io::stderrPrint.print(error.getErrno());
    k::io::stderrPrint.print(" bytes=");
    k::io::stderrPrint.println(error.bytesTransferred());
    k::io::stderrPrint.flush();
}

reportThrowable(error: Throwable&) throws(Throwable) {
    k::io::stderrPrint.print("Throwable code=");
    k::io::stderrPrint.print(error.getCode());
    ioError : k::io::IOException* = (k::io::IOException*) &error;
    if (ioError != null) {
        k::io::stderrPrint.print(" errno=");
        k::io::stderrPrint.print(ioError->getErrno());
    }
    interrupted : k::io::InterruptedIOException* = (k::io::InterruptedIOException*) &error;
    ended : k::io::EndOfStreamException* = (k::io::EndOfStreamException*) &error;
    if (interrupted != null) {
        k::io::stderrPrint.print(" bytes=");
        k::io::stderrPrint.print(interrupted->getBytesTransferred());
    } else if (ended != null) {
        k::io::stderrPrint.print(" bytes=");
        k::io::stderrPrint.print(ended->getBytesTransferred());
    }
    k::io::stderrPrint.println();
    k::io::stderrPrint.flush();
    cause : Throwable* = error.getCause();
    if (cause != null) {
        k::io::stderrPrint.print("Caused by: ");
        reportThrowable(*cause);
    }
}

failureStatus(error: Throwable&) : int {
    try {
        reportThrowable(error);
    } catch (diagnosticFailure: Throwable&) {
        // A failed diagnostic stream must still leave a nonzero exit status.
        return 1;
    }
    return 1;
}

emitBytes(buffer: k::io::ByteBuffer+, scratch: byte[], count: int) throws(Throwable) {
    if (count < 0 || count > scratch.size) throw IllegalArgumentException();
    buffer->flip();
    copied : unsigned int = buffer->get(scratch, 0u, (unsigned int) count);
    if (copied != (unsigned int) count) throw IllegalStateException();
    k::io::stdoutBytes.write(scratch, 0u, copied);
}

fillPing(buffer: k::io::ByteBuffer+) throws(Throwable) {
    buffer->clear();
    buffer->put((byte) 80);
    buffer->put((byte) 73);
    buffer->put((byte) 78);
    buffer->put((byte) 71);
    buffer->flip();
}

checkPing(buffer: k::io::ByteBuffer+, count: int) throws(Throwable) {
    if (count != 4 || buffer->position() != 4u ||
        buffer->get(0u) != (byte) 80 || buffer->get(1u) != (byte) 73 ||
        buffer->get(2u) != (byte) 78 || buffer->get(3u) != (byte) 71)
        throw IllegalArgumentException();
}

announcePort(port: int) throws(Throwable) {
    k::io::stdoutPrint.print("PORT=");
    k::io::stdoutPrint.println(port);
    k::io::stdoutPrint.flush();
}

announcePing() throws(Throwable) {
    k::io::stdoutPrint.println("PING");
    k::io::stdoutPrint.flush();
}
