#include "ProfileProbe.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QUuid>
#include <algorithm>

namespace GreenRhythm {
namespace {
QString kindName(WebCheckKind kind) {
    switch (kind) {
    case WebCheckKind::Http: return "http";
    case WebCheckKind::Dns: return "dns";
    case WebCheckKind::Tcp: return "tcp";
    case WebCheckKind::Tls: return "tls";
    case WebCheckKind::Timeout: return "timeout";
    case WebCheckKind::Cancelled: return "cancelled";
    case WebCheckKind::Network: return "network";
    case WebCheckKind::Unavailable: return "unavailable";
    }
    return "unknown";
}
}
ProfileProbe::ProfileProbe(QObject *parent) : QObject(parent) {
    deadline.setSingleShot(true); roundTimer.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this] { finish(ready ? "timeout" : "startup_timeout"); });
    connect(&roundTimer, &QTimer::timeout, this, &ProfileProbe::nextRound);
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        while (process.bytesAvailable()) {
            output += process.read(4096);
            if (running && !ready && output.contains(readyMarker)) {
                ready = true; output.clear(); deadline.start(18000);
                if (discord) {
                    transportDone = false; DiscordProbeOptions options; options.proxy = proxy;
                    transport.start(options);
                }
                nextRound();
            }
            // Read/discard all child output. No error strings or credentials
            // escape to the application log or copyable report.
            if (output.size() > readyMarker.size() + 4096) output = output.right(readyMarker.size());
        }
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { if (running) finish("startup_failed"); });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this] {
        if (running) finish(ready ? "core_exited" : "startup_failed");
    });
    connect(&transport, &DiscordTransport::completed, this, [this] { transportDone = true; maybeFinish(); });
}
ProfileProbe::~ProfileProbe() { running = false; reap(); }
void ProfileProbe::start(const HealthCandidate &value, const QString &executable,
                         const QList<ProfileProbeTarget> &checks, bool checkDiscord, int timeoutMs) {
    running = false; reap(); candidate = value; targets = checks; discord = checkDiscord;
    timeout = std::clamp(timeoutMs, 50, 5000); round = pending = 0; ready = false; transportDone = true;
    samples = {}; lastResults.clear(); output.clear(); running = true; state = "pending";
    started = QDateTime::currentDateTimeUtc(); finished = {};
    if (!candidate.unavailable.isEmpty() || candidate.outbound.isEmpty()) { finish("unsupported"); return; }
    if (!QFileInfo(executable).isExecutable()) { finish("core_missing"); return; }
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { finish("local_port"); return; }
    const auto port = reservation.serverPort();
    const auto user = QUuid::createUuid().toString(QUuid::Id128), password = QUuid::createUuid().toString(QUuid::Id128);
    proxy = QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", port, user, password);
    auto outbound = candidate.outbound; outbound["tag"] = "health-proxy";
    QJsonObject config{
        {"log", QJsonObject{{"disabled", true}}},
        {"inbounds", QJsonArray{QJsonObject{{"type", "mixed"}, {"listen", "127.0.0.1"}, {"listen_port", port},
            {"users", QJsonArray{QJsonObject{{"username", user}, {"password", password}}}}}}},
        {"outbounds", QJsonArray{outbound}},
        {"route", QJsonObject{{"final", "health-proxy"}, {"auto_detect_interface", true}}},
        {"dns", QJsonObject{{"servers", QJsonArray{
            QJsonObject{{"tag", "bootstrap"}, {"address", "local"}},
            QJsonObject{{"tag", "remote"}, {"address", "1.1.1.1"}, {"detour", "health-proxy"}}}},
            {"final", "remote"}, {"rules", QJsonArray{QJsonObject{{"domain", QJsonArray{outbound.value("server")}}, {"server", "bootstrap"}}}}}}};
    directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/greenrhythm-health-XXXXXX");
    if (!directory->isValid() || !QFile::setPermissions(directory->path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) { finish("private_storage"); return; }
    const auto path = directory->filePath("probe.json"); QFile file(path);
    auto bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.flush()) { bytes.fill('\0'); file.close(); finish("private_storage"); return; }
    bytes.fill('\0'); file.close(); readyMarker = "running " + path.toUtf8();
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : environment.keys()) if (QStringList{"http_proxy", "https_proxy", "all_proxy", "no_proxy"}.contains(key.toLower())) environment.remove(key);
    process.setProcessEnvironment(environment); process.setWorkingDirectory(directory->path());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setProgram(executable); process.setArguments({"run", "-c", path});
    reservation.close(); // A collision fails child startup; readiness is from this child, never an unrelated listener.
    deadline.start(6000); process.start();
}
void ProfileProbe::nextRound() {
    if (!running) return;
    ++round; pending = targets.size();
    if (targets.isEmpty()) { finish("unsupported"); return; }
    for (const auto &target : targets) {
        auto *probe = new BrowserNetworkProbe(this); requests.append(probe);
        WebCheckOptions options; options.profileRunning = true; options.checkSystemPath = false;
        options.greenRhythmProxy = proxy; options.timeoutMs = timeout;
        connect(probe, &BrowserNetworkProbe::pathFinished, this, [this, target](const WebCheckResult &result) {
            if (!running) return;
            lastResults[target.id] = result;
            samples.append(QJsonObject{{"target", target.id}, {"round", round}, {"ok", result.kind == WebCheckKind::Http && result.httpStatus == target.expected},
                {"kind", kindName(result.kind)}, {"http_status", result.httpStatus}, {"elapsed_ms", result.elapsedMs},
                {"observed_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
        });
        connect(probe, &BrowserNetworkProbe::completed, this, [this, probe] {
            requests.removeAll(probe); probe->deleteLater();
            if (running && --pending == 0) { if (round < 3) roundTimer.start(150); else maybeFinish(); }
        });
        probe->start(target.url, options);
    }
}
void ProfileProbe::maybeFinish() { if (running && round == 3 && pending == 0 && transportDone) finish("complete"); }
void ProfileProbe::reap() {
    deadline.stop(); roundTimer.stop(); transport.cancel();
    const auto old = requests; requests.clear();
    for (auto *probe : old) { probe->disconnect(this); probe->cancel(); probe->deleteLater(); }
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1500); }
    directory.reset(); output.fill('\0'); output.clear(); candidate.outbound = {};
}
void ProfileProbe::finish(const QString &value) {
    if (!running) return;
    running = false; state = value; reap(); finished = QDateTime::currentDateTimeUtc(); emit completed();
}
void ProfileProbe::cancel() { finish("cancelled"); }
QJsonObject ProfileProbe::report() const {
    QJsonObject result{{"state", state}, {"node_id", candidate.nodeId}, {"protocol", candidate.protocol},
        {"started_utc", started.toString(Qt::ISODateWithMs)}, {"finished_utc", finished.toString(Qt::ISODateWithMs)},
        {"unavailable_reason", candidate.unavailable}, {"samples", samples}, {"rules_applied", false}, {"voice_verified", false}};
    if (discord && ready) result["discord_transport"] = transport.report();
    return result;
}
QString ProfileProbe::details() const {
    QStringList lines;
    if (state == "pending") return tr("Изолированная проверка профиля выполняется…");
    if (state != "complete") {
        const auto reason = state == "unsupported" ? tr("Эта конфигурация требует цепочки, произвольного JSON или стороннего компонента и пока не поддерживается.")
            : state == "core_missing" ? tr("Файл установленного ядра не найден.")
            : state == "startup_failed" || state == "core_exited" ? tr("Отдельное ядро не запустилось или завершилось во время проверки.")
            : state == "startup_timeout" || state == "timeout" ? tr("Истёк срок ожидания отдельного ядра или его проб.")
            : state == "cancelled" ? tr("Проверка остановлена.") : tr("Не удалось подготовить отдельное ядро.");
        lines.append(reason + ' ' + tr("Причину сетевого сбоя по неполной проверке определить нельзя."));
    }
    for (const auto &target : targets) {
        int successes = 0, count = 0;
        for (const auto &sample : samples) if (sample.toObject().value("target") == target.id) { ++count; successes += int(sample.toObject().value("ok").toBool()); }
        lines.append(tr("%1: %2/%3 успешных HTTPS-запросов").arg(target.id).arg(successes).arg(count));
        if (lastResults.contains(target.id)) lines.append(webCheckResultText(lastResults.value(target.id)));
    }
    if (discord && ready) lines.append(transport.details());
    return lines.join('\n');
}
} // namespace GreenRhythm
