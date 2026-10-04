# Proactive Network I/O (`k::io`)

TCP/IPv4, UDP/IPv4 and AF_UNIX stream/datagram channels use the Linux io_uring
proactor described in [io-async.md](io-async.md). Datagram transfers preserve
packet boundaries and return structured receive results; stream fully/EOF
contracts do not apply to datagrams.

## Addresses and creation

`NetworkAddress(host: const char[], port: int)` represents an IPv4 endpoint.
`loopback(port)` and `any(port)` return owning addresses. Ports must be in
`0..65535`; binding port zero selects an ephemeral port, observable through
`ServerSocket::localPort()`. Numeric IPv4 addresses are prepared immediately.
Hostname connects, including `"localhost"`, use a bounded native resolver pool
independent of callback executors. There is no caller-thread DNS resolution.
Immediate binds accept numeric IPv4 or `"*"`, not hostnames.

Address construction validates the supplied endpoint immediately. An out-of-range
port or invalid endpoint raises the canonical I/O exception before any operation
is requested; an out-of-range port raises `IOException` with exception code `200`
and platform errno `EINVAL`. This is distinct from operation preparation failure.

`NetworkAddress` is a copyable immutable value sharing native address storage.
The `ofName(host, port)` convenience factory preserves unresolved hostnames;
datagram `connect` and `sendTo` resolve them asynchronously on the same bounded
pool, using UDP hints and one deadline across lookup and transport. `family()`
returns `4` for IPv4, `1` for Unix, and `0` for an empty address;
`isResolved()` distinguishes numeric/path endpoints from hostname inputs.
`host()` returns an independently owned, zero-terminated `char[]!` copy.
For a default empty address, this array contains exactly one zero character.
`NetworkAddress::unix(path)` returns a Unix address value, with `path` either
`const file::Path&` or `const char[]`. Pathname support is provided; abstract
namespace and other address families are not supported. An unnamed Unix
sender is represented by a valid Unix source with empty text and cannot be
used as a destination. An empty value or an IPv4 destination with port zero
or wildcard address fails explicitly, not as a successful no-op.
The default constructor creates an empty value without throwing. Passing that
value to `SocketChannel::connect` returns an operation whose result reports
`IoErrorKind::InvalidArgument` / `EINVAL`; it does not throw during construction.

AF_UNIX stream endpoints use `const file::Path&`. The caller owns the socket pathname:
bind must not unlink an existing pathname, and close does not unlink it.
Fixtures and applications must remove only paths they created themselves.
`localPort()` is IPv4-only and fails explicitly for a Unix listener.

Socket creation performs checked nonblocking and close-on-exec setup. Bind and
listen are immediate setup methods; their errors raise the canonical I/O
exceptions. Connect preparation errors are delivered through its operation
result, including invalid arguments, allocation failure and runtime rejection.
Inherited post-fork runtime use is unsupported.

`DatagramSocket::open()` binds IPv4 port zero immediately.
`DatagramSocket::bind(address)` and `bind(path)` return owners of copyable
socket values, with checked NONBLOCK/CLOEXEC setup. `boundAddress()` returns
a shared address value snapshot; `localPort()` exposes the actual assigned
IPv4 port. Both raise canonical exceptions with the actual setup errno.
Unix datagram bind must not pre-unlink a pathname: an existing file/socket
fails with `EADDRINUSE` and remains intact. Only a successful bind owns the
created pathname. Successful close removes that path, or it is removed when
the last owning native handle is released after in-flight operations drain.
Cleanup checks the recorded socket identity and must not remove a replacement
file. Releasing copies, remote/source addresses, or a failed bind must not
unlink another socket's pathname. Descriptor adoption does not claim a path.

## Three operation forms

`SocketChannel` and `ServerSocket` are copyable values retaining a shared native
handle. All copies observe the same closed state; closing one closes them all.
Connect and accept Futures store `SocketChannel`, not `SocketChannel!`.
Repeated `get()`/`share()` is safe and returns additional shared descriptor
values, not independent connections.

| Operation | Callback result | Future form | Sync form |
|-----------|-----------------|-------------|-----------|
| `SocketChannel::connect(endpoint, completion, executor)` | `IoResult<SocketChannel>` | `connectAsync(endpoint)` | `connectSync(endpoint)` |
| `socket.read(buffer, completion, executor)` | `IoResult<int>` | `readAsync(buffer)` | `readSync(buffer)` |
| `socket.write(buffer, completion, executor)` | `IoResult<int>` | `writeAsync(buffer)` | `writeSync(buffer)` |
| `socket.readFully(buffer, completion, executor)` | `IoResult<int>` | `readFullyAsync(buffer)` | `readFullySync(buffer)` |
| `socket.writeFully(buffer, completion, executor)` | `IoResult<int>` | `writeFullyAsync(buffer)` | `writeFullySync(buffer)` |
| `server.accept(completion, executor)` | `IoResult<SocketChannel>` | `acceptAsync()` | `acceptSync()` |
| `channel.close(completion, executor)` | `IoResult<IoDone>` | `closeAsync()` | `closeSync()` |

Callback forms return `IoOperation!` immediately. Omitting `executor` selects
the system executor. Connect, accept and transfers also have raw-nanosecond and
`const Duration&` overloads: the timeout precedes `completion`, with the
executor last. Future forms accept an optional selected executor after the
timeout, or directly after the endpoint/buffer when no timeout is needed.
`closeAsync(executor)` also accepts a selected lease. Close is non-cancellable
and has no I/O deadline.

Positive I/O timeouts establish one monotonic deadline; nonpositive values
(including `IO_NO_TIMEOUT`) impose no deadline. A hostname connect's deadline
includes resolver queue wait, lookup and kernel connect; it must not restart at
the stage transition. `Future.get(timeout)` is only an observation timeout.
Future publication does not depend on a callback executor. Sync or Future
waits from an EventLoop task are therefore safe from publication deadlock,
although they block other tasks on that loop. Sync waits on the native
completion thread are forbidden.

## Transfer and lifetime rules

A shared stream handle admits at most one read and one write concurrently.
Another submission in the same direction fails with
`IoErrorKind::ResourceExhausted` / `EBUSY`, even through another K copy.
Future overloads report this as an `ExecutionException` with an `IOException`
cause: `getErrno()` reports `EBUSY`, while `getCode()` remains the exception code
`200`. The rejected request releases its buffer lease before publishing failure.
A fully parent reserves its direction through every short child and both
target/timeout CQEs; it must not release and reacquire the direction between
children. Read and write directions are independent.

Primitive read returns zero on orderly EOF. Fully read before filling the
requested buffer fails with `EndOfStream` and the cumulative transferred count.
A nonempty send making zero progress fails with `System` / `EIO`.
Fully success returns the requested byte count. Transfers exceeding the
signed-int result range fail before submission. Stream children do not use
file offsets or advance a file cursor. Sends suppress SIGPIPE.

One continuous native buffer lease protects a fully operation. Byte positions
advance by actual completed bytes, including suppressed transfers. Registered
storage may still use ordinary RECV/SEND: registration is a buffer property,
not a claim that this stream operation used a fixed-buffer opcode.

Cancellation before a callback starts suppresses it and rolls back an
undelivered accepted or connected descriptor. Pending descriptor adoption is
claimed before observable Future publication; failed publication destroys the
adopted value without a second native close. Cancellation during DNS waits for
the accepted job to discard its result and must never launch connect afterward.
The resolver rejects new jobs at shutdown and drains accepted jobs. Shutdown
also suppresses resource callbacks already queued on a live selected executor,
rolls back undelivered descriptors and drops their carriers exactly once without
waiting for that executor to run. Address storage remains owned until kernel
quiescence.

Close rejects new work, cancels all current directions/accepts and drains their
kernel storage before descriptor close. Sync interruption/cancellation drains
before returning control: no-byte cancellation raises `CancellationException`;
partial cancellation/interruption raises `InterruptedIOException` with the
actual count. Timeout, EOF, closed-channel and system failures retain their
canonical exception taxonomy and native errno. Resolver failures expose
`EAGAIN`, `ENOMEM`, `ENOENT`, `EAFNOSUPPORT`, `EHOSTUNREACH`, or the actual
`EAI_SYSTEM` errno, rather than success or an errno-zero placeholder.

## Datagram operations and packet results

| Operation | Callback result | Future form | Sync form |
|-----------|-----------------|-------------|-----------|
| `datagram.connect(remote, completion, executor)` | `IoResult<IoDone>` | `connectAsync(remote)` | `connectSync(remote)` |
| `datagram.send(buffer, completion, executor)` | `IoResult<int>` | `sendAsync(buffer)` | `sendSync(buffer)` |
| `datagram.sendTo(buffer, remote, completion, executor)` | `IoResult<int>` | `sendToAsync(buffer, remote)` | `sendToSync(buffer, remote)` |
| `datagram.receive(buffer, completion, executor)` | `IoResult<DatagramReceive>` | `receiveAsync(buffer)` | `receiveSync(buffer)` |
| `datagram.close(completion, executor)` | `IoResult<IoDone>` | `closeAsync()` | `closeSync()` |

Each transport method has selected/default executor forms and raw-nanosecond
or `const Duration&` I/O deadlines, with the same parameter order as stream
methods. `connect` and `sendTo` accept `NetworkAddress` or `file::Path` in
every form. Close has no deadline and is non-cancellable. Existing char-array
Unix convenience calls and parameterless blocking aliases delegate to Sync;
blocking `receive` now returns `DatagramReceive`, not an integer.

Connected `send` uses the socket's peer; unconnected `sendTo` supplies its
own persistent destination and does not connect the socket. `sendTo` on a
connected socket supplies an explicit destination. Native SENDMSG/RECVMSG
are the only transport path; no blocking send/receive/connect/poll engine
runs on a caller, control worker, or callback executor. Buffer registration
does not imply use of fixed-buffer packet operations.

One operation submits one packet, including a zero-length send. There is
no datagram fully loop, and receive zero is a valid empty packet, never EOF.
A zero-capacity receive still consumes one packet and can report truncation.
The result is a copyable value, safe for repeated Future `get()` and `share()`:

| Accessor | Meaning |
|----------|---------|
| `bytesTransferred(): int` | Number of bytes actually copied into the buffer. |
| `source(): NetworkAddress` | Independently retained source address value. |
| `isTruncated(): bool` | Kernel `MSG_TRUNC` or a packet length exceeding capacity. |
| `datagramLength(): long` | Full packet length from Linux `MSG_TRUNC` input; `-1` if unavailable. |

The buffer cursor, `IoOperation::bytesTransferred()` and all error counts
advance by `min(nonnegative kernel result, remaining buffer capacity)`, not
by the full truncated packet length. The unused part of a packet is discarded,
not returned by a later receive. Kernel-provided source names are validated
using their actual returned family/length. A connected receive without a name
uses the retained peer; a missing unconnected source fails explicitly.
Invalid source metadata preserves irreversible copied bytes in the operation
error; it does not manufacture a successful null address.

Socket copies share one receive gate and one send/connect gate. DNS sendTo
and connect retain the send gate through queue wait, resolution and all kernel
CQEs. A buffer cannot be leased by two operations even in opposite directions.
Same-direction overlap fails with `ResourceExhausted` / `EBUSY`. All request
iovecs, msghdrs and sockaddr storage remain owned through both target and linked
timeout CQEs. Callback metadata remains alive after native quiescence until
executor delivery or task drop. Source address copies outlive both operation
and socket. Metadata does not represent an adopted descriptor.

Close cancels/drains receive, send and accepted resolver work before closing
one shared descriptor. Cancellation suppresses queued callbacks, while actual
copied/sent counts remain observable after native drain. Simple Future
cancellation can publish before target drain; `waitQuiescent` or the Sync
bridge is required before accessing a still-leased buffer. Sync methods use
`getIoSync` and the same partial-byte cancellation/interruption rules as other
channels. Preparation, resolver, unsupported kernel, post-fork, close and
capacity errors must be explicit; there is no synthetic success fallback.

## Example

```k
main() : int throws(Throwable) {
    deadline : Duration = Duration::ofSeconds(5L);
    local : k::io::NetworkAddress("127.0.0.1", 0);
    server : k::io::ServerSocket! = k::io::ServerSocket::bind(local);
    remote : k::io::NetworkAddress("localhost", server->localPort());
    accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline);
    client : k::io::SocketChannel = k::io::SocketChannel::connectSync(remote, deadline);
    peer : k::io::SocketChannel = accepted->get(deadline);
    buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
    buffer->put((byte) 79); buffer->put((byte) 75); buffer->flip();
    client.writeFullySync(buffer, deadline);
    buffer->clear();
    peer.readFullySync(buffer, deadline);
    client.closeSync(); peer.closeSync(); server->closeSync();
    delete buffer; delete accepted; delete server;
    return 0;
}
```

`Socket` and owner-returning legacy `connect`/`accept`, blocking `read`/`write`,
`writeFully(buffer, timeoutNanos)` and parameterless `close()` are transitional
aliases over these Sync methods. They do not retain a separate blocking stream
engine. New code should use the explicit callback/Future/Sync names.
`InputStream`/`OutputStream` and synchronous file streams remain separate and
unchanged. [EventLoop](executors.md#9-eventloop) is a serial executor, not a selector.
The [Application exit barrier](application.md#exit-lifecycle) drains network
requests, resolver work and callback capture cleanup before fields are destroyed,
on normal return and unwinding. Kernel drain alone does not complete user delivery.

See [`samples/stream_channel_loopback.k`](../../../samples/stream_channel_loopback.k)
and [`samples/datagram_loopback.k`](../../../samples/datagram_loopback.k) for
complete Future/Sync examples with ephemeral endpoints.
