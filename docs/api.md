# API

## Result types

`TraceResult` contains:

| Field | Description |
| --- | --- |
| `result` | `true` on success. |
| `status` | Machine-readable `TraceStatus`. |
| `message` | Human-readable status text. |

`TraceFlushResult` values are `Ok`, `Failed`, and `Retry`.

`Ok` removes flushed pending logs. `Failed` is terminal for the current `flushAndWait()` call but retains pending logs. `Retry` retains pending logs, schedules another attempt using `TraceConfig::retryIntervalMs`, and keeps `flushAndWait()` waiting until `Ok`, `Failed`, or timeout.

## Main methods

```cpp
TraceResult init(const TraceConfig &config = TraceConfig());
TraceResult end(uint32_t timeoutMs = 5000);

void onFlush(TraceFlushCallback callback);
void onLog(TraceLogCallback callback);
void setStream(Print *stream);
Print *getStream();

TraceResult attachTempo(Tempo &tempo, const TraceTempoConfig &config = TraceTempoConfig());
void detachTempo();

TraceResult debug(const char *tag, const char *message);
TraceResult info(const char *tag, const char *message);
TraceResult warn(const char *tag, const char *message);
TraceResult error(const char *tag, const char *message);
TraceResult fatal(const char *tag, const char *message);

TraceResult debug(const char *tag, const char *format, Args... args);
TraceResult info(const char *tag, const char *format, Args... args);
TraceResult warn(const char *tag, const char *format, Args... args);
TraceResult error(const char *tag, const char *format, Args... args);
TraceResult fatal(const char *tag, const char *format, Args... args);

TraceResult debug(const char *tag, const JsonDocument &doc);
TraceResult info(const char *tag, const JsonDocument &doc);
TraceResult warn(const char *tag, const JsonDocument &doc);
TraceResult error(const char *tag, const JsonDocument &doc);
TraceResult fatal(const char *tag, const JsonDocument &doc);

TraceResult debugf(const char *tag, const char *format, ...);
TraceResult infof(const char *tag, const char *format, ...);
TraceResult warnf(const char *tag, const char *format, ...);
TraceResult errorf(const char *tag, const char *format, ...);
TraceResult fatalf(const char *tag, const char *format, ...);

TraceResult flush();
TraceResult flushAndWait(uint32_t timeoutMs);
```

## Query methods

```cpp
TraceDiag getDiagnostics();
TraceLog getLastLog();
std::vector<TraceLog> getLogs();
std::vector<TraceLog> getLogs(TraceLevel level);
std::vector<TraceLog> getLastLogs(size_t count);
std::vector<TraceLog> getLogsByTag(const char *tag);
```

## TraceLog

`TraceLog` stores `sequence`, `level`, `tag`, `message`, `formatted`, `timeText`, `uptimeMs`, and `truncated`.

Without Tempo, `formatted` uses:

```txt
[L][TAG] - Message
```

With Tempo, `formatted` uses:

```txt
[L][TAG](time) - Message
```

`L` is the short level label: `D`, `I`, `W`, `E`, `F`, or `?`.

## Callbacks

`onLog()` is for realtime observation. `onFlush()` is for persistence. Both callbacks run from the internal Trace task and are `std::function` callbacks, so `std::bind` and lambdas with captures are supported.

Callbacks should avoid long blocking work and should not recursively log through the same Trace instance.

`onLog()` and stream output are delivered from a dedicated realtime queue controlled by `TraceConfig::maxRealtimeLogs`. They do not depend on `maxRecentLogs`.

## Stream output

`setStream()` writes formatted realtime logs to any Arduino `Print` stream.

```cpp
trace.setStream(&Serial);
trace.setStream(&Serial1);
trace.setStream(&client);
trace.setStream(nullptr);
```

Trace does not own the stream. Keep the stream alive until `Trace::end()` completes. Stream output runs from the internal Trace task and coexists with `onLog()` when both are configured.

Calling `setStream(nullptr)` or replacing the stream while Trace is active does not synchronize already snapshotted worker use. Use `Trace::end()` as the synchronization point before destroying an attached stream.

Stream output uses ANSI colors by default when `TraceConfig::enableColors` is `true`. `onLog()`, `onFlush()`, and query helpers receive plain `TraceLog::formatted` text without color codes.

## Tempo lifetime

Trace does not own attached `Tempo` instances. Keep the attached `Tempo` alive until `Trace::end()` completes.

Calling `detachTempo()` or attaching another `Tempo` while Trace is active does not synchronize already snapshotted worker use.
