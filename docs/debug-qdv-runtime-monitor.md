# TRAE-Debugger Session: qdv-runtime-monitor

**Session Date**: 2026-05-27
**Target**: QDetectVision.exe (build/bin)
**Method**: TRAE-debugger Scientific Debugging Protocol

## Issues Found

### Issue #1 (P0-Critical): Logger Mutex Deadlock
- **Symptom**: QDetectVision.exe started but NO log files were created. Process appeared responsive but hung during initialization.
- **Root Cause**: `Logger::instance()` locked `s_mutex` during singleton creation. The Logger constructor called `openLogFile()`, which attempted to lock the SAME `s_mutex` again → same-thread recursive lock on non-recursive QMutex → **DEADLOCK**.
- **Why it was missed before**: This bug was introduced by P1-4 "fix" for double-checked locking. The original code had the double-checked locking pattern which worked (by accident) because the null-check was outside the lock, preventing the deadlock path to be triggered on subsequent calls. The "fix" moved the lock to the outer level, which always triggers the deadlock on first instantiation.
- **Found at**: [Logger.h](./include/Core/Logger.h) - `instance()` and `openLogFile()` shared `s_mutex`

### Issue #2 (P2-Reliability): QTimer-based Buffer Flush Unreliable
- **Symptom**: Log entries sat in the buffer and were only flushed when the QTimer fired (every 100ms), or when the buffer reached 256 entries.
- **Root Cause**: The QTimer required the event loop to be running. At startup, log entries written before `app.exec()` could be lost if the timer didn't fire in time.
- **Fix**: Changed `log()` to flush the buffer immediately after appending, making log writes synchronous and reliable.

## Verification

### Unit Tests: 184/184 PASSED
```
Total: 184 | Passed: 184 | Failed: 0
```

### Runtime Behavior
- QDetectVision.exe starts successfully
- Log files are created and written immediately: `logs/20260527.log`
- Process is responsive (67MB working set, Responding=True)
- Program blocks on FirstRunSetup modal dialog (expected behavior on first run)

### Binary
- Size: 1,174,325 bytes
- MinGW 13.1.0 + Qt 6.11.1 + OpenCV 4.13.0
- All DLL dependencies deployed via windeployqt

## Changes Summary

### Logger.h - Core Fix
1. **Separated mutexes**: `s_instanceMutex` (singleton protection) vs `m_fileMutex` (file I/O)
2. **Synchronous flush**: `log()` now calls `flushBuffer()` immediately after buffer append
3. **Removed**: Diagnotic qDebug() traces

### Logger.cpp
- Changed `s_mutex` → `s_instanceMutex` to match header

### main.cpp
- No permanent changes (diagnostic traces were added and removed during debugging)