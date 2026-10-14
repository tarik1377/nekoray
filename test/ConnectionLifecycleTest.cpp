#include "main/ConnectionLifecycle.hpp"
#include <QCoreApplication>
#include <QSemaphore>
#include <QThread>
#include <cstdio>
#include <atomic>

using Lifecycle = GreenRhythm::ConnectionLifecycle;
static int failures = 0;
static void check(const char *name, bool ok) {
    failures += !ok;
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Lifecycle life;
        life.beginStart(54);
        life.beginStop(false);
        std::atomic_bool shutdownReady{false};
        auto *waiter = QThread::create([&] { life.waitUntilIdle(); shutdownReady = true; });
        waiter->start();
        QThread::msleep(20);
        life.finishStop();
        QThread::msleep(20);
        check("shutdown waits for a pending start after stop finishes", !shutdownReady);
        life.finishStart();
        const bool completed = waiter->wait(1000);
        check("start completion releases shutdown without destroying callbacks early", completed && shutdownReady);
        if (completed) delete waiter;
    }
    {
        Lifecycle life;
        check("first stop owns the operation", life.beginStop(false));
        check("connect during stop waits instead of failing", life.beginStart(41) == Lifecycle::Start::WaitingForStop);
        check("a newer requested profile replaces the queued profile", life.beginStart(42) == Lifecycle::Start::WaitingForStop);
        check("two stop waiters join the same operation", !life.beginStop(true) && !life.beginStop(true));
        const auto done = life.finishStop();
        check("waiters wake only after actual stop completes", done.waiters == 2 && !life.isStopping());
        check("only the latest requested profile is resumed", done.pendingProfile == 42);
        check("new connect can acquire the completed stop", life.beginStart(done.pendingProfile) == Lifecycle::Start::Accepted);
        check("repeated click does not release another start", life.beginStart(43) == Lifecycle::Start::AlreadyStarting);
        life.finishStart();
    }
    {
        Lifecycle life;
        life.beginStart(51);
        const auto token = life.token();
        life.beginStop(true); // the start worker is switching from an old profile
        const auto stopped = life.finishStop();
        check("profile handoff wakes exactly one start worker", stopped.waiters == 1);
        check("switching profiles does not cancel its own start", life.isCurrent(token));
        check("start remains exclusive through the profile handoff", life.beginStart(52) == Lifecycle::Start::AlreadyStarting);
        life.cancel();
        check("a real disconnect rejects the late start result", !life.isCurrent(token));
        life.finishStart();
        check("cancelled attempt does not strand the gate", life.beginStart(53) == Lifecycle::Start::Accepted);
        life.finishStart();
    }
    {
        Lifecycle life;
        life.beginStop(false);
        life.beginStart(61);
        life.cancel();
        check("disconnect cancels a queued reconnect", life.finishStop().pendingProfile == -1);
    }
    {
        // Exercise the actual cross-thread handoff repeatedly. No mutex is
        // held by the GUI and unlocked by a different thread.
        Lifecycle life;
        QSemaphore sent, finished;
        auto *worker = QThread::create([&] {
            for (int i = 0; i < 1000; ++i) {
                sent.acquire();
                life.finishStop();
                finished.release();
            }
        });
        worker->start();
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            ok = life.beginStop(false) && ok;
            sent.release();
            finished.acquire();
            ok = !life.isStopping() && ok;
        }
        worker->wait(); delete worker;
        check("1000 worker completions release their operation safely", ok);
    }
    return failures ? 1 : 0;
}
