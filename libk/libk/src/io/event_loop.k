/*
 * K Language standard library — EventLoop (Phase 6 foundation)
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

namespace io {

public class EventLoop {
    _lock      : ReentrantLock!;
    _queue     : Vector<Runnable*>!;
    _running   : bool;
    _stopping  : bool;

public:
    EventLoop() {
        _lock = null;
        _queue = null;
        _running = false;
        _stopping = false;
        _lock = new ReentrantLock();
        _queue = new Vector<Runnable*>();
    }

    ~EventLoop() {
        stop();
        if (_queue != null) {
            delete _queue;
            _queue = null;
        }
        if (_lock != null) {
            delete _lock;
            _lock = null;
        }
    }

    const isRunning() : bool {
        lock : ReentrantLock* = _lock;
        lock->lock();
        try {
            return _running;
        } finally {
            lock->unlock();
        }
    }

    submit(task: Runnable*) : void {
        if (task == null) {
            return;
        }
        lock : ReentrantLock* = _lock;
        queue : Vector<Runnable*>* = _queue;
        lock->lock();
        try {
            queue->emplaceBack(task);
        } finally {
            lock->unlock();
        }
    }

    scheduleAfter(delay: const Duration&, task: Runnable*) : void {
        if (task == null) {
            return;
        }
        Thread::sleep(delay);
        submit(task);
    }

    stop() : void {
        lock : ReentrantLock* = _lock;
        lock->lock();
        try {
            _stopping = true;
        } finally {
            lock->unlock();
        }
    }

    run() : void {
        lock : ReentrantLock* = _lock;
        queue : Vector<Runnable*>* = _queue;

        lock->lock();
        _running = true;
        _stopping = false;
        lock->unlock();

        while (true) {
            task : Runnable* = null;
            shouldStop : bool = false;

            lock->lock();
            try {
                if (!queue->isEmpty()) {
                    task = queue->first();
                    queue->removeAt(0u);
                } else if (_stopping) {
                    shouldStop = true;
                }
            } finally {
                lock->unlock();
            }

            if (shouldStop) {
                break;
            }

            if (task != null) {
                task->run();
            } else {
                Thread::sleep(Duration::ofMillis(1L));
            }
        }

        lock->lock();
        try {
            _running = false;
            while (!queue->isEmpty()) {
                queue->removeAt(0u);
            }
        } finally {
            lock->unlock();
        }
    }
}

} // namespace io
