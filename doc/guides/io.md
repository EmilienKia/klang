# File and network I/O: callbacks, Futures and Sync

K's proactive channels expose three ways to observe the **same native I/O
operation**. This guide explains their trade-offs and walks through nine
standalone examples in [`samples/io`](../../samples/io/). It complements the
[filesystem guide](file-system.md), not a replacement for the API references:

- [Proactor, buffers, results and operations](../spec/stdlib/io-async.md)
- [File paths and channels](../spec/stdlib/io-file.md)
- [TCP/Unix streams and UDP/Unix datagrams](../spec/stdlib/io-network.md)
- [Executors and leases](../spec/stdlib/executors.md)
- [Future and Promise](../spec/stdlib/futures.md)
- [Application exit lifecycle](../spec/stdlib/application.md#exit-lifecycle)

## 1. Two separate I/O families

**Channels** (`FileChannel`, `SocketChannel`, `ServerSocket`, `DatagramSocket`)
submit work to the process-wide proactor. Their callback, `Async` and `Sync`
forms share handle ownership, deadlines, cancellation and native buffers.
`FileChannel` lives in `k::io::file`; the network channels and `ByteBuffer`
live in `k::io`.

**Generic streams** (`InputStream<T>`, `OutputStream<T>`, `PrintStream`,
`FileInputStream`, `FileOutputStream`) are the separate synchronous stream
framework. Choose streams for existing stream consumers, printing, codecs and
text processing; choose channels for proactive transfers and operation tracking.
There is no Selector or asynchronous file-stream adapter in the current API.

These examples perform file and TCP **transport** through channels. Their
diagnostics and raw stdout writes use existing synchronous streams:
`stderrPrint`, `stdoutPrint` and `stdoutBytes`. Printing from a callback does
not make stdout asynchronous; a slow output consumer can block that callback
and therefore its serial EventLoop.

### Platform requirements

The current backend requires **Linux and liburing 2.5 or newer** at build time.
Runtime initialization also probes the kernel's required io_uring data-plane
operations. A kernel, container or security policy that prevents io_uring use
can make these programs fail even when they compiled successfully.

File reads/writes and socket transport do not fall back to blocking POSIX I/O.
Optional metadata/control operations (`open`, `statx`, `fsync`, `ftruncate`) may
use a bounded internal worker pool when their kernel opcodes are unavailable.
This is not a blocking transport fallback, nor work submitted to a user executor.
Sparse buffer registration has a different fallback, described below.

## 2. Choosing an observation form

| Form | Submission and result | Caller behavior | Best fit |
|------|-----------------------|-----------------|----------|
| Callback / proactor | `read(buffer, deadline, completion, lease)` returns `IoOperation!`; completion receives `IoResult<int>` | Submit, return, later deliver on the chosen executor | State machines, serial event-driven applications |
| Future | `readAsync(buffer, deadline)` returns `Future<int>!` | Submit immediately; `get()` is a blocking join | Composing submissions with separately retained result owners |
| Sync | `readSync(buffer, deadline)` returns `int` | Interruptibly wait for native completion | Linear code without explicit completion handling |

Open/connect/accept return **copyable channel values**, not owners, through
`IoResult<T>` or `Future<T>`. Copies retain one native handle and share its
closed state. `ServerSocket::bind(address)` is an immediate setup operation
returning `ServerSocket!`; it has no callback/Future/Sync trio.

Examples of actual calls in the runnable programs:

```k
// File read: implicit file position, at most one bufferful.
reading = channel.readAsync(buffer, deadline);
count : int = reading->get(joinTimeout);

// TCP exchange: exactly the buffer's four remaining bytes.
written : int = channel.writeFullySync(buffer, deadline);
buffer->clear();
count : int = channel.readFullySync(buffer, deadline);
checkPing(buffer, count);
```

The Future samples retain their `Future<T>!` owners in Application fields.
Calling `get()` after each submission deliberately demonstrates a **blocking
join**, not concurrency or an event-driven main thread. To overlap independent
operations, submit them first, retain every Future, and join later; still obey
buffer leases and per-channel ordering.

### Callback results and typed diagnostics

The public types are:

```k
public template<typename T>
alias IoResult : Expected<T, IoError>;
public template<typename T>
alias IoCompletion : !(IoResult<T>);
```

Test `hasResult()` / `hasError()` before using `getResult()` / `getError()`.
Close uses `IoDone`, rather than `void`. An `IoError` has `kind()`,
`getErrno()` and `bytesTransferred()`. The examples' `reportIoError` reports all
three to stderr. A callback can receive a failed operation even when submission
returned an operation observer normally; returning that observer is not proof
of a successful transfer.

This read step is from `file_read_proactor.k`:

```k
fileRead(state: FileReadState*) throws(Throwable) {
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
                fileClose(state);
                return;
            }
            count : int = result.getResult();
            if (count == 0) {
                k::io::stdoutBytes.flush();
                fileClose(state);
                return;
            }
            emitBytes(state->buffer, state->scratch, count);
            state->buffer->clear();
            fileRead(state);
        } catch (error: Throwable&) {
            fileFail(state, error);
        }
    };
    state->operation = state->channel.read(state->buffer, state->deadline, completion, *state->lease);
}
```

The lambda has **no `throws` clause**. Checked exceptions from byte output,
buffer helpers or submitting the next step must be caught **inside** the
callback. `fileFail` preserves a nonzero exit status, diagnoses the Throwable
and starts proactive close; it does not silently abandon the chain.

### Future and Sync failures

`Future.get()` can throw:

- `ExecutionException`, whose `getCause()` identifies the I/O failure;
- `CancellationException` when the Future is cancelled;
- `ThreadInterruptionException` when the waiting thread is interrupted;
- `TimeoutException` for an observer wait timeout with `get(Duration)`;
- `IllegalStateException` for a forbidden wait, such as on the native I/O
  completion thread or an inherited post-fork Future.

A failed channel operation can instead produce `ExecutionException` with a
`TimeoutException`, `IOException`, `FileNotFoundException`,
`ClosedChannelException`, `EndOfStreamException` or other typed cause.
The shared diagnostic helper reports the outer code and recursively reports
its cause; for I/O exceptions it also reports errno, and for
`EndOfStreamException` / `InterruptedIOException` the partial byte count.
Some typed exceptions have no platform errno; do not infer one from their
Throwable code.

Sync bridges map the native `IoError` directly to the corresponding checked
exception. Ordinary `IOException` carries `getErrno()`; a missing file is not
an empty file. Diagnostics go to stderr, so successful binary file output is
not polluted with messages. If stderr itself fails, the program still returns
nonzero.

## 3. I/O deadlines are not Future wait timeouts

The `Duration` passed to a channel method bounds that **I/O operation**.
The duration passed to `Future.get(Duration)` bounds only the **observer's
wait**. A timed-out observer does not cancel the operation, release its buffer
lease, or close a newly opened/accepted handle.

The samples use a 30-second I/O deadline and a 35-second Future join timeout:

```k
deadline : Duration = Duration::ofSeconds(30L);
joinTimeout : Duration = Duration::ofSeconds(35L);
connecting = k::io::SocketChannel::connectAsync(address, deadline);
channel = connecting->get(joinTimeout);
```

The margin helps observe the operation's own timeout after cancellation/drain.
These are per-operation durations, not one overall 30-second program budget.
Native timeout and interruption wait for real kernel drain before publishing
the terminal outcome; drain/close may take additional time. Close has no timed
overload in these channel APIs. The Future close join is bounded, but its timeout
does not replace eventual native cleanup.

Hostname connect includes bounded resolver work and kernel connect under the
same I/O deadline. The default numeric loopback host avoids DNS entirely.
A server waiting for a client cannot wait forever on accept: its accept
operation has the same 30-second deadline. A peer that stalls mid-frame is
bounded by the read/write deadline.

## 4. Buffers, position, EOF and framing

`ByteBuffer` has `position`, `limit` and `capacity`, with
`0 <= position <= limit <= capacity`. Allocation starts in fill mode
(`position = 0`, `limit = capacity`). Native completion advances the position
by the actual transferred count **before** releasing its lease and publishing
the result.

- `flip()` sets `limit = position`, then `position = 0`: drain the filled range.
- `clear()` sets `position = 0`, `limit = capacity`: refill the same storage.
- `get(index)` is absolute and leaves position unchanged.
- `get(array, offset, length)` copies bytes relatively and advances position.

The shared raw output helper flips the completed buffer, copies exactly the
returned count into a `byte[4096]`, then calls:

```k
k::io::stdoutBytes.write(scratch, 0u, copied);
```

No String conversion, encoding, newline insertion or NUL termination occurs.
Empty files produce zero stdout bytes and succeed. Files larger than 4096 bytes
require multiple reads; a short positive read is **not EOF**. Ordinary
`FileChannel.read` returns `0` at EOF, which ends the loop. These programs open
with `OPEN_READ` only and never write, create or truncate the input file.

Implicit file reads advance the channel's shared file position. Implicit
transfers and truncate use a native FIFO; channel copies share that cursor.
Positional forms such as `readSync(buffer, offset, deadline)` do not change
the implicit cursor. Do not reposition a channel while implicit requests
are queued.

### Partial transfers and fully operations

Primitive read/write methods may transfer less than the remaining buffer.
`readFully` / `writeFully` compose native child operations until that range is
complete or a terminal condition occurs. Their deadlines and reported counts
cover the complete parent operation, not independently restarted child budgets.
File fully parents reserve the implicit FIFO; socket fully parents reserve
one entire direction. Opposite-direction socket transfers can coexist, but
competing same-direction operations are not interchangeable.

Fully reads reaching EOF early fail with `EndOfStream` and a cumulative partial
count, rather than reporting successful completion of a short frame. Sync
raises `EndOfStreamException`; a Future publishes a typed cause through
`ExecutionException`. Cancellation/interruption may also have moved bytes:
inspect the count and cursor before deciding how to recover.

**TCP is a byte stream**, not a message transport. One send need not correspond
to one receive. The six TCP samples agree on a fixed frame of exactly four
ASCII bytes, `PING`, without a newline. The client writes fully, reads fully,
checks all four bytes, closes, then prints `PING` plus a newline to stdout.
The server reads fully, validates, flips the same buffer, echoes exactly those
four bytes, closes the peer and listener, and exits. It accepts one connection,
not a persistent service. Invalid, truncated or stalled frames fail rather
than being printed as successful exchanges.

### Heap, direct and mapped buffers

`ByteBuffer::allocate` uses native heap storage; `allocateDirect` uses aligned
native storage. Neither means “registered” or promises a fixed-buffer opcode.
The examples use direct buffers for reuse and explicit cursor management.

`MappedByteBuffer` provides file/anonymous mapped storage and `ReadOnly`,
`ReadWrite`, `Private` mapping modes. A read into a read-only mapping fails.
Mappings attempt sparse registration automatically; unsupported fixed opcodes,
registration-table exhaustion or memory-lock limits leave an otherwise valid
mapping usable with ordinary kernel operations. `isRegistered()` observes
registration, not the opcode selected by a particular transfer.
Only file READ/WRITE can use fixed-buffer opcodes here; network operations
remain ordinary kernel RECV/SEND or RECVMSG/SENDMSG.

Submission retains native handle and buffer leases, **not borrowed K wrapper
addresses**. Destroying a wrapper does not invalidate native backing storage
already retained by an operation. That does not allow callback code to use a
destroyed wrapper. Do not access leased ranges or mutate cursor/content,
compact, force or remap until native drain; prohibited mutation raises
`BufferInUseException`.

## 5. Executors, operation tracking and safe lifetime

The default callback endpoint comes from `ExecutorLease::system()`. A custom
executor supplies a strong native endpoint through `acquireLease()`. The lease
retains that endpoint, not a borrowed address of the K executor wrapper.
Stopping/shutting down an executor is terminal and can reject delivery even
while leases still exist.

The callback programs select an EventLoop lease explicitly for **every**
open/connect/accept/transfer/close completion. Native I/O is not performed by
the EventLoop: its sole role here is FIFO serial user delivery. Main pumps:

```k
while (!state.done) state.loop->runOnce();
```

Untimed `runOnce()` sleeps while idle; this is not a busy-poll loop. The pending
I/O deadline produces the next completion if a peer is absent. Because all user
callbacks run serially on main's loop thread, the sample state does not need a
cross-thread polling flag. A final close callback sets `done`, and `runOnce()`
finishes callback execution and task cleanup before main observes that flag.

The public operation observer offers `stage()`, `isIoDone()`,
`isCallbackRunning()`, `isDone()`, `bytesTransferred()`, `error()`, `share()`
and `cancel()`. Destroying an `IoOperation!` only detaches that observer; it
does **not** cancel the native request. Replacing the state field from inside
its own completion is safe: the running callback task retains native ownership.

Cancellation is cooperative. It can cancel pending native I/O or request
interruption of a running callback; callbacks must consult the execution
context and cooperate, rather than expecting arbitrary code to be preempted.
Native quiescence (`waitQuiescent`) releases kernel leases, but does **not**
mean that a queued user callback has run or a running one has finished.
Never treat a timed-out/nonzero drain wait as permission to reuse a buffer.
Close cancels/drains target requests before descriptor close; its completion
is also not a substitute for unrelated queued user-callback cleanup.

Future publication is independent of user callback delivery. A Future join or
Sync bridge does not need an EventLoop to publish its result. Blocking from the
native completion thread is forbidden; blocking inside a user EventLoop task
can stall unrelated tasks even when the Future publication itself needs no loop.
Sync interruption/cancellation requests cancellation and drains the kernel
before propagating. No transferred bytes yields
`ThreadInterruptionException` / `CancellationException`; partial bytes yield
`InterruptedIOException` with the actual count.

### Application-owned callback state

Each callback program declares an explicit `Application : public k::Application`
containing its state, loop, lease, buffers, handles and observer. Free functions
take a state pointer, and fresh lambdas capture that pointer by value. No
callback borrows a potentially destroyed main-local buffer or channel.

The generated Application exit barrier completes process I/O shutdown and
callback cleanup **before destroying Application fields**, on both normal
return and unwinding. It does not rescue ordinary main stack locals that
already unwound. Keep borrowed callback resources in surviving state, and
explicitly close on success/failure. If allocation, submission, output or
delivery fails, preserve the error status and let the barrier protect the
retained state during final cleanup. Do not call process-wide
`IoRuntime::shutdown()` inside an I/O callback or its cleanup.

## 6. Build the samples

Run from the repository root:

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DKLANG_BUILD_SAMPLES=ON -DKLANG_BUILD_IO_SAMPLES=ON
cmake --build cmake-build-debug --target klang-io-samples
```

`KLANG_BUILD_SAMPLES` defaults to `OFF`: normal project builds do not compile
examples. When enabled, `klang-samples` joins the default build and includes
the four existing root samples. `KLANG_BUILD_IO_SAMPLES` defaults to `ON`
but has no effect while the master option is off. Set it to `OFF` with the
master enabled to compile only the root examples.

Each executable uses its named source plus `common.k`, both declaring
`module io_samples`; there is no separately built sample library. Do not
compile all nine main programs into one unit. Source, compiler, `libk.kdi`
and `libk.so` changes trigger the necessary rebuilds; an unchanged build does
not recompile every sample unconditionally.

| Purpose | Callback target / output | Future target / output | Sync target / output |
|---------|--------------------------|------------------------|----------------------|
| File reader | `sample-io-file-read-proactor` / `file_read_proactor` | `sample-io-file-read-future` / `file_read_future` | `sample-io-file-read-sync` / `file_read_sync` |
| TCP client | `sample-io-tcp-client-proactor` / `tcp_client_proactor` | `sample-io-tcp-client-future` / `tcp_client_future` | `sample-io-tcp-client-sync` / `tcp_client_sync` |
| TCP server | `sample-io-tcp-server-proactor` / `tcp_server_proactor` | `sample-io-tcp-server-future` / `tcp_server_future` | `sample-io-tcp-server-sync` / `tcp_server_sync` |

All nine outputs are in `cmake-build-debug/samples/io/`. For example, to build
only one:

```sh
cmake --build cmake-build-debug --target sample-io-file-read-sync
```

Alternatively build `klangc` and `libk_stdlib`, then compile any mode manually:

```sh
cmake --build cmake-build-debug --target klangc libk_stdlib
mkdir -p cmake-build-debug/samples/io
for mode in proactor future sync; do
  for program in file_read tcp_client tcp_server; do
    ./cmake-build-debug/klang/klangc \
      "samples/io/${program}_${mode}.k" samples/io/common.k \
      -I cmake-build-debug/libk/libk -L cmake-build-debug/libk/libk \
      -o "cmake-build-debug/samples/io/${program}_${mode}"
  done
done
```

The explicit `-I` and `-L` select the built stdlib metadata and binary; the
compiler links the executable with the library search paths/rpath. Compilation
alone is not evidence that the host kernel can run proactive I/O.

The optional root sample targets are `sample-fibo`,
`sample-file-channel-fully`, `sample-stream-channel-loopback` and
`sample-datagram-loopback`, with outputs under `cmake-build-debug/samples/`.
The legacy `file_channel_fully.k` writes a fixed pathname; the aggregate target
**compiles only**, and must not be used to execute that sample automatically.
Use the read-only, caller-path file examples below instead.

## 7. Run all three file modes

Every file program takes **exactly one nonempty filepath**:

```sh
io=cmake-build-debug/samples/io
input=README.md
"$io/file_read_proactor" "$input" > cmake-build-debug/file-proactor.out
"$io/file_read_future"   "$input" > cmake-build-debug/file-future.out
"$io/file_read_sync"     "$input" > cmake-build-debug/file-sync.out
cmp "$input" cmake-build-debug/file-proactor.out
cmp "$input" cmake-build-debug/file-future.out
cmp "$input" cmake-build-debug/file-sync.out
```

Success means exit status zero and byte-for-byte identical output, with no
success message on stderr. Diagnostics remain on stderr; never use `2>&1`
when comparing the raw file bytes.

For empty, binary and multi-chunk files, create **caller-owned** fixtures:

```sh
mkdir -p cmake-build-debug/io-demo
: > cmake-build-debug/io-demo/empty.bin
printf 'before\000after\n' > cmake-build-debug/io-demo/binary.bin
dd if=/dev/zero of=cmake-build-debug/io-demo/multi.bin bs=4096 count=3
for mode in proactor future sync; do
  for name in empty binary multi; do
    input="cmake-build-debug/io-demo/$name.bin"
    output="cmake-build-debug/io-demo/$name-$mode.out"
    "$io/file_read_$mode" "$input" > "$output" || exit 1
    cmp "$input" "$output" || exit 1
  done
done
```

Outputs are build-subtree artifacts, not hardcoded inputs in K sources. A
nonexistent input must return nonzero with a diagnostic, not an empty success.
Usage errors also return nonzero. K's `main(args: const String[])` includes
`argv[0]`: `args[0]` is the executable name, `args[1]` the first user argument.

## 8. Run TCP pairs without choosing a fixed port

Servers take `[port]`, default `0`, bind only `127.0.0.1`, and print/flush one
machine-readable line **before accept**:

```text
PORT=<actual-port>
```

Clients take `port [host]`, default host `127.0.0.1`. Ports must be decimal digits
only: client `1..65535`, server `0..65535`. Empty strings, signs, spaces, nondecimal
text and overflow are invalid and print usage with a nonzero status.

For an interactive two-terminal run, in terminal A:

```sh
cmake-build-debug/samples/io/tcp_server_proactor
```

Copy the actual number from its `PORT=` line. In terminal B, immediately run:

```sh
port=THE_NUMBER_PRINTED_BY_THE_SERVER
cmake-build-debug/samples/io/tcp_client_proactor "$port"
```

Successful client output is exactly `PING` followed by a newline; server output
remains the `PORT=` line. Both close and exit zero after the single exchange.
Substitute `future` or `sync` for both suffixes to run those modes, or pair
different modes: the wire protocol is identical.

This POSIX-shell recipe discovers each printed ephemeral port without polling.
It covers all nine client/server combinations and keeps logs under the build:

```sh
io=cmake-build-debug/samples/io
mkdir -p cmake-build-debug/io-demo
for server_mode in proactor future sync; do
  for client_mode in proactor future sync; do
    tag="$server_mode-$client_mode"
    ready="cmake-build-debug/io-demo/$tag.ready"
    err="cmake-build-debug/io-demo/$tag.server.err"
    rm -f "$ready"
    mkfifo "$ready"
    "$io/tcp_server_$server_mode" 0 > "$ready" 2> "$err" &
    server_pid=$!
    IFS= read -r announcement < "$ready"
    rm -f "$ready"
    case "$announcement" in
      PORT=*) port=${announcement#PORT=} ;;
      *) wait "$server_pid"; cat "$err" >&2; exit 1 ;;
    esac
    client_status=0
    "$io/tcp_client_$client_mode" "$port" 127.0.0.1 \
      > "cmake-build-debug/io-demo/$tag.client.out" \
      2> "cmake-build-debug/io-demo/$tag.client.err" || client_status=$?
    server_status=0
    wait "$server_pid" || server_status=$?
    test "$client_status" -eq 0 && test "$server_status" -eq 0 || exit 1
    printf 'PING\n' > cmake-build-debug/io-demo/expected-ping.out
    cmp cmake-build-debug/io-demo/expected-ping.out \
      "cmake-build-debug/io-demo/$tag.client.out" || exit 1
  done
done
```

The FIFO wait blocks until the server flushes its announcement or closes stdout;
it does not spin or guess a startup delay. Start the client promptly within the
accept deadline. Missing peers, early EOF, wrong bytes, refused connections or
unavailable io_uring produce nonzero statuses and stderr diagnostics. Binding
an explicitly supplied port can also fail if another process owns it; leave the
server port at zero for independent runs.

## 9. Beyond these examples: Unix sockets and datagrams

TCP/IPv4 and Unix **stream** channels use the same `SocketChannel` and
`ServerSocket` forms. Pass a `k::io::file::Path` instead of `NetworkAddress`
for Unix endpoints; listener paths require creator-managed cleanup and are not
unlinked automatically on listener close.

UDP/IPv4 and Unix **datagram** sockets use `DatagramSocket` with
callback/Future/Sync `sendTo` and `receive`. Datagram boundaries are
preserved: there are no “fully” loops, and truncation/source-address metadata
are part of the receive result. Successful Unix datagram binds own only the
pathname they created and remove it on close/final handle release, never a
remote destination's pathname.

See the [network API](../spec/stdlib/io-network.md) for exact signatures and
[`stream_channel_loopback.k`](../../samples/stream_channel_loopback.k) /
[`datagram_loopback.k`](../../samples/datagram_loopback.k) for compact
in-process loopback examples. Keep stream framing separate from packet
semantics when adapting the PING programs to a larger application.
