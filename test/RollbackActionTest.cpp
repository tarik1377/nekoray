#include "ui/RollbackAction.hpp"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static int failures = 0;
static void check(const char *name, bool ok) { failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name); }
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QByteArray valid=R"({"available":true,"version":"v1.9.2.2","platform":"windows-x64","provenance":"explicit-verified-local","files":38})";
    if (app.arguments().contains("--rollback-info")) { std::fwrite(valid.constData(),1,valid.size(),stdout); return 0; }
    using namespace GreenRhythm::Rollback;
    QPushButton button;
    QLabel label;
    showStatus(&button,&label,parseStatus(valid));
    check("verified local version is identified without a signed-release claim",button.isEnabled()&&label.text().contains("v1.9.2.2")&&label.text().contains(QString::fromUtf8("локальная")));
    for(const auto &data:{QByteArray("{partial"),QByteArray(R"({"available":true,"version":"v1.9.2.2","platform":"linux-x64","provenance":"signed-release","files":38})"),QByteArray(R"({"available":true,"version":"v1.9.2.2","platform":"windows-x64","provenance":"unknown","files":38})")}){
        showStatus(&button,&label,parseStatus(data));check("invalid or unsupported status cannot enable rollback",!button.isEnabled());
    }
    showStatus(&button,&label,parseStatus(R"({"available":false,"error":"snapshot checksum mismatch"})"));
    check("verification failure is visible",!button.isEnabled()&&label.text().contains("checksum"));
    QTemporaryDir directory;
    int closes=0,starts=0;
    check("a missing helper never closes the working client",!launch(directory.path(),[&]{++closes;},[&](const QString &,const QStringList &,const QString &){++starts;return true;})&&closes==0&&starts==0);
#ifdef Q_OS_WIN
    QFile updater(directory.path()+"/updater.exe");updater.open(QIODevice::WriteOnly);updater.write("fixture helper bytes");updater.close();
    QString failedHelper;
    check("launch failure retains the working client",!launch(directory.path(),[&]{++closes;},[&](const QString &path,const QStringList &,const QString &){failedHelper=path;++starts;return false;})&&closes==0);
    check("failed helper preparation is cleaned up",!QFileInfo::exists(failedHelper));
    QString copied;
    bool arguments=false,bytes=false;
    const bool launched=launch(directory.path(),[&]{++closes;},[&](const QString &path,const QStringList &args,const QString &cwd){
        copied=path;arguments=args==QStringList{"--install-dir",directory.path(),"--rollback"}&&cwd==directory.path()&&QFileInfo(path).absolutePath()!=directory.path();
        QFile file(path);file.open(QIODevice::ReadOnly);bytes=file.readAll()=="fixture helper bytes";return true;
    });
    check("only a verified external helper can request closing the client",launched&&closes==1&&arguments&&bytes);
    QFile::remove(copied);
    QWidget parent;
    QFile::remove(directory.path()+"/updater.exe");
    QFile::copy(QCoreApplication::applicationFilePath(),directory.path()+"/updater.exe");
    refreshStatus(&parent,&button,&label,directory.path());
    QElapsedTimer elapsed;elapsed.start();
    while(!button.isEnabled()&&elapsed.elapsed()<4000){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
    check("the actual asynchronous helper status enables the saved version",button.isEnabled()&&button.property("rollbackVersion").toString()=="v1.9.2.2");
#endif
    return failures?1:0;
}
