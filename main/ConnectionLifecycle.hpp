#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QtGlobal>

namespace GreenRhythm {

// Each short critical section is acquired and released on the same thread.
// RPC work runs outside this lock; a completed operation is handed back to the
// GUI before a queued request can begin.
class ConnectionLifecycle {
public:
    enum class Start { Accepted, AlreadyStarting, WaitingForStop };
    struct Stopped { int waiters = 0; int pendingProfile = -1; };

    Start beginStart(int profile) {
        QMutexLocker lock(&mutex);
        if (starting) return Start::AlreadyStarting;
        if (stopping) {
            pendingProfile = profile;
            return Start::WaitingForStop;
        }
        starting = true;
        return Start::Accepted;
    }
    int finishStart() {
        QMutexLocker lock(&mutex);
        starting = false;
        idle.wakeAll();
        return takePending();
    }
    bool beginStop(bool wait) {
        QMutexLocker lock(&mutex);
        if (wait) ++stopWaiters;
        if (stopping) return false;
        stopping = true;
        return true;
    }
    Stopped finishStop() {
        QMutexLocker lock(&mutex);
        stopping = false;
        idle.wakeAll();
        const Stopped result{stopWaiters, takePending()};
        stopWaiters = 0;
        return result;
    }
    bool isStopping() const {
        QMutexLocker lock(&mutex);
        return stopping;
    }
    quint64 token() const {
        QMutexLocker lock(&mutex);
        return generation;
    }
    bool isCurrent(quint64 token) const {
        QMutexLocker lock(&mutex);
        return token == generation;
    }
    void cancel() {
        QMutexLocker lock(&mutex);
        ++generation;
        pendingProfile = -1;
    }
    void waitUntilIdle() {
        QMutexLocker lock(&mutex);
        while (starting || stopping) idle.wait(&mutex);
    }

private:
    int takePending() {
        if (starting || stopping) return -1;
        const int profile = pendingProfile;
        pendingProfile = -1;
        return profile;
    }
    mutable QMutex mutex;
    QWaitCondition idle;
    bool starting = false;
    bool stopping = false;
    int pendingProfile = -1;
    int stopWaiters = 0;
    quint64 generation = 0;
};
}
