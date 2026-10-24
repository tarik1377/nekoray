#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "main/TunBypass.hpp"
#include "ui/TunBypassDialog.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>
namespace Policy = GreenRhythm::TunBypass;
static int checks = 0, failures = 0;
static void check(bool pass, const char *message) { ++checks; if (!pass) ++failures; std::printf("%s %s\n", pass ? "PASS" : "FAIL", message); }
static QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
static void write(const QString &path, const QByteArray &data) { QFile f(path); f.open(QIODevice::WriteOnly); f.write(data); }
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStringList entries; QString error;
    const auto saved = QStringLiteral("10.20.0.0/16\n# keep note\n");
    check(Policy::parse(saved + "mks-racceess.rtk-it.ru\n212.164.137.29\n2001:db8::29\n", &entries, &error)
          && entries == QStringList{"10.20.0.0/16", "mks-racceess.rtk-it.ru", "212.164.137.29/32", "2001:db8::29/128"}, "gateway domain, IPv4, IPv6 and existing networks compile separately from DIRECT rules");
    check(Policy::parse("VPN.Example.RU.\nvpn.example.ru\nhttps://vpn.example.ru:443/connect?unused=1\n", &entries, &error)
          && entries == QStringList{"vpn.example.ru"}, "URL paste extracts host and deduplicates without saving router or interface indices");
    check(Policy::parse(QString::fromUtf8("шлюз.рф"), &entries, &error) && entries.first().startsWith("xn--"), "international gateway names use DNS ACE");
    for (const auto &bad : {"*.example.ru", "bad host", "10.0.0.1/99", "https://user:password@example.ru", "../route", "[bad", "a..b", "x:443"})
        check(!Policy::parse(bad, &entries, &error), "invalid routes and credential-bearing URLs are rejected");
    check(Policy::parse({}, &entries, &error) && entries.isEmpty(), "empty preference preserves the existing built-in policy");
    QTemporaryDir temp;
    const auto file = temp.filePath("settings.json"), route = temp.filePath("Default");
    const QJsonObject original{{"vpn_gateway_exclusions", saved}, {"vpn_route_exclude_extra", "172.20.0.0/16\n# legacy CIDRs stay byte-identical"}, {"custom_route_global", "do not change"}, {"future_setting", QJsonArray{1, 2}}};
    write(file, QJsonDocument(original).toJson()); write(route, "route bytes untouched\n"); const auto routeBytes = read(route);
    const auto changed = saved + "mks-racceess.rtk-it.ru\n";
    check(Policy::saveSetting(file, saved, changed, &error), "gateway preference persists atomically in existing settings key");
    auto after = QJsonDocument::fromJson(read(file)).object(); after.insert("vpn_gateway_exclusions", saved);
    check(after == original && read(route) == routeBytes, "unknown settings, route schemes and old exclusion content survive saving");
    const auto savedBytes = read(file);
    check(!Policy::saveSetting(file, saved, "other.example", &error) && read(file) == savedBytes, "stale editor cannot overwrite another gateway choice");
#ifdef _WIN32
    const auto native = file.toStdWString();
    const auto lock = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "Windows fixture locks only its temporary settings file");
    if (lock != INVALID_HANDLE_VALUE) {
        check(!Policy::saveSetting(file, changed, {}, &error) && read(file) == savedBytes, "real atomic replacement failure preserves settings bytes");
        CloseHandle(lock);
        check(Policy::saveSetting(file, changed, changed, &error), "save succeeds after releasing the temporary lock");
    }
#endif
    write(file, "damaged json");
    check(!Policy::saveSetting(file, {}, changed, &error) && read(file) == "damaged json", "unreadable unrelated settings are not replaced");
    GreenRhythm::TunBypassDialog done(saved);
    int commits = 0;
    done.setCommitter([&](const QString &, QString *) { ++commits; return true; });
    done.show(); app.processEvents();
    auto *button = done.findChild<QPushButton *>("tunBypassSave");
    check(button && button->isEnabled() && button->text() == QString::fromUtf8("Готово"), "unchanged dialog has a working Done button");
    check(done.rect().contains(QRect(button->mapTo(&done, QPoint{}), button->size())), "footer stays inside the available window");
    button->click(); check(done.result() == QDialog::Accepted && commits == 0, "Done closes without writing settings or reconnecting");
    GreenRhythm::TunBypassDialog edited(saved);
    auto *input = edited.findChild<QPlainTextEdit *>("tunBypassEntries"); input->setPlainText(changed);
    edited.setCommitter([&](const QString &, QString *e) { ++commits; *e = "fixture write denied"; return false; });
    edited.show(); app.processEvents(); edited.findChild<QPushButton *>("tunBypassSave")->click();
    check(edited.isVisible() && edited.findChild<QLabel *>("tunBypassStatus")->text() == "fixture write denied", "failed save stays open with the real error");
    QString accepted;
    edited.setCommitter([&](const QString &text, QString *) { accepted = text; return true; });
    edited.findChild<QPushButton *>("tunBypassSave")->click();
    check(edited.result() == QDialog::Accepted && accepted == changed && accepted.startsWith(saved), "retry saves the gateway without clearing existing networks");
    GreenRhythm::TunBypassDialog cancelled(saved);
    cancelled.findChild<QPlainTextEdit *>("tunBypassEntries")->setPlainText(changed);
    const auto priorCommits = commits;
    cancelled.setCommitter([&](const QString &, QString *) { ++commits; return true; }); cancelled.reject();
    check(cancelled.result() == QDialog::Rejected && commits == priorCommits && read(route) == routeBytes, "Cancel leaves both persistent settings and routing untouched");
    GreenRhythm::TunBypassDialog invalid(saved);
    invalid.findChild<QPlainTextEdit *>("tunBypassEntries")->setPlainText("*.invalid");
    invalid.setCommitter([&](const QString &, QString *) { ++commits; return true; }); invalid.show(); app.processEvents();
    invalid.findChild<QPushButton *>("tunBypassSave")->click();
    check(invalid.isVisible() && commits == priorCommits && !invalid.findChild<QLabel *>("tunBypassStatus")->text().isEmpty(), "invalid input is explained before invoking any persistence action");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
