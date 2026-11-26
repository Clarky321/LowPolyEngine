# ADR 0030: Log listeners

- **Status:** Accepted (2026-11-26, M5.7). The plan asked M5.7 to decide the log listener's API, with an ADR if it is more than a callback (`docs/milestones/m5-minimal-editor.md`, M5.7, "Decisions to make"). It is a callback with a registration and thread rules, so it has this ADR; the decisions were taken under `docs/DEVELOPMENT_WORKFLOW.md` §4.
- **Date:** 2026-11-26
- **Related:** `engine/core/Log.hpp` (the engine's log since M1), ADR 0027 (Raven in the engine, the editor), ADR 0029 (the editor's document), M5 plan (M5.7: the console)

## Context

The engine's log (`engine/core/Log.hpp`) writes each line to stderr and nowhere else. The editor's console (M5.7) must show the same lines: the platform's, the renderer's, the world generator's, the project's and the editor's own, from the first line of the run.

- Lines can be written on any thread. The engine's own code logs on the main thread today; Raven may report from its threads, and tests log from several.
- The console runs on the editor's main thread, between Raven's frames.
- `engine/core` depends on nothing but the standard library. It must not learn of the editor, Raven or a UI.

## Decision

1. **The API** (`engine/core/Log.hpp`):
   - `LogRecord { level, category, message }`. The strings are views, valid during the call only.
   - `LogListener`: `std::function<void(const LogRecord&)>`.
   - `addLogListener(listener)` returns a `LogSubscription`, move-only. Its `reset()` or its destructor removes the listener; an empty function adds nothing.
   - `logListenerCount()`, for checks.
2. **What a listener receives:** every line written from its addition on, at the levels `isLogLevelEnabled` lets through (debug lines in debug builds only), after the line went to stderr. Stderr is unchanged.
3. **Threads:**
   - A listener runs on the thread that writes the line, while the log holds its lock.
   - One line at a time reaches all listeners, in the order they were added. Every listener sees the lines in one order, the order of stderr.
   - A listener must be quick and must not wait for another thread that logs. The editor's listener appends the line to a list under a lock of its own.
4. **Removal:**
   - When `reset()` returns, the listener is not called again: a call in progress on another thread is waited for, so what the listener refers to can be destroyed right after.
   - From inside a listener (on the logging thread), a removal takes effect at once, also for the rest of the current line. The entry is erased once the line has reached every listener.
   - A removed listener is destroyed after the log's lock is released, since what it holds may log when it goes.
5. **Inside a listener:**
   - A line it logs goes to stderr only, not to the listeners: no recursion and no second lock on the same thread (a thread-local flag).
   - A listener added from inside a listener receives the lines after the current one.
   - An exception from a listener is caught and dropped. `writeLog` stays `noexcept`, and the other listeners still get the line.
6. **Lifetime:** the log's state is a function-local static. Subscriptions are held by objects with ordinary lifetimes, never by other statics, so none outlives it.
7. **The editor's listener** (M5.7):
   - `editor::LogInbox` appends each line under its own lock.
   - `editor::ConsoleLog` takes the lines once per frame on the main thread.
   - The editor shell listens from its construction, before the engine starts, until its `stop()`.

## Alternatives

- **One global callback pointer:** one listener only, and no rule for removing it while another thread logs.
- **Calling listeners outside the lock:** a removal could then not promise that no call comes after it without reference counting each call, and lines from different threads could reach two listeners in different orders.
- **spdlog's sinks:** spdlog is an approved candidate (`docs/TECH_STACK.md`), but the engine's log is its own. Adding the library for one listener goes against keeping the dependencies few (AGENTS.md §3).

## Consequences

- `engine/core` gains no dependency. The console is one listener, and tests can listen too.
- While listeners exist, a line costs their calls under the lock. The editor's call copies the line into a list.
- **Not covered:**
  - filtering by category or level at the source (the console filters what it received);
  - delivery on another thread or later (a queue the log fills);
  - listeners in Lua;
  - a file sink.
