// Заголовки Qt — ДО mainwindow.h. Он объявляет поля типов QLabel и
// QPushButton, но сам их не включает: остальные единицы трансляции приходят к
// нему через mainwindow_common.hpp, где включено всё. Без этого порядка
// компилятор спотыкается на чужой строке с полем, а не на нашем include.
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "main/Interference.hpp"
#include "main/NekoGui.hpp"
#include "sys/WinShell.hpp"
#include "ui/mainwindow.h"

/**
 * «Что мешает подключению» — окно, которое сначала объясняет, а потом
 * предлагает.
 *
 * ГЛАВНОЕ ОТЛИЧИЕ ОТ ПЕРВОЙ ВЕРСИИ. Та показывала все чужие туннели одним
 * списком с галочкой «приостановить». На машине владельца это были три рабочих
 * OpenVPN, которые с нашим туннелем уживаются сами (см. Interference.hpp), и
 * предложение читалось как «сломайте себе работу». Теперь строки разложены по
 * исходам, и у тех, кто уживается, галочки нет вовсе.
 *
 * ЭТО ТРЕТИЙ ПУТЬ, а не замена «Починить сеть». Та кнопка сносит навсегда и
 * права на это имеет: она разбирает брошенный zapret, который человек ставил
 * полгода назад. Здесь — работающие чужие программы, которые нужны своему
 * хозяину.
 *
 * ПОЧЕМУ РАЗВЕДКА И ДЕЙСТВИЕ РАЗДЕЛЕНЫ. Список читается обычным пользователем
 * (Get-Service, Get-NetAdapter, Get-NetRoute), поэтому окно открывается без
 * единого запроса прав. UAC спрашивается ровно один раз и только если человек
 * поставил галочку и нажал. Посмотреть, что мешает, не должно быть действием.
 */
namespace GreenRhythm {

    namespace {
        /** Обычный проход: без повышения, только чтение. */
        QString runPlain(const QString &script, int msec) {
            QProcess p;
            p.start(PowerShellPath(), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                       QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                       QStringLiteral("-EncodedCommand"), PowerShellEncode(script)});
            if (!p.waitForFinished(msec)) {
                p.kill();
                return {};
            }
            return QString::fromUtf8(p.readAllStandardOutput());
        }

        /**
         * Проход с повышением.
         *
         * Тело уезжает через -EncodedCommand, а не файлом. Файл, который вот-вот
         * выполнится с правами администратора, всё время между записью и
         * запуском лежал бы доступным на запись обычному пользователю — ту же
         * дыру уже закрывали в «Починить сеть», и повторять её здесь незачем.
         */
        bool runElevated(const QString &script, int msec) {
            const QString launcher =
                QStringLiteral("Start-Process -FilePath '%1' -Verb RunAs -WindowStyle Hidden -Wait "
                               "-ArgumentList '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',"
                               "'-EncodedCommand','%2'")
                    .arg(QDir::toNativeSeparators(PowerShellPath()), PowerShellEncode(script));
            QProcess p;
            p.start(PowerShellPath(), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                       QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                       QStringLiteral("-Command"), launcher});
            if (!p.waitForFinished(msec)) {
                p.kill();
                return false;
            }
            return p.exitCode() == 0;
        }

        QString kindWord(Meddler k) {
            switch (k) {
                case Meddler::Adapter: return QObject::tr("туннель");
                case Meddler::Driver: return QObject::tr("драйвер");
                case Meddler::Process: return QObject::tr("программа");
                case Meddler::Filter: return QObject::tr("сетевой фильтр");
                case Meddler::Service: break;
            }
            return QObject::tr("служба");
        }
    } // namespace

    QList<Meddling> scanInterference() {
#ifndef Q_OS_WIN
        return {};
#else
        QString script = scanScript();
        script.prepend(QStringLiteral("$env:GR_OWN_DIR='%1'\n")
                           .arg(QString(QDir::toNativeSeparators(QApplication::applicationDirPath()
                                                                 + QStringLiteral("/dpi")))
                                    .replace(QChar('\''), QStringLiteral("''"))));
        auto found = parseScan(runPlain(script, 30000));
        classify(found, tunnelExcludes());
        return found;
#endif
    }

    bool resumeInterference(QString *said) {
#ifndef Q_OS_WIN
        Q_UNUSED(said)
        return true;
#else
        const auto path = snapshotPath();
        QFile f(path);
        if (!f.exists()) return true;
        if (said != nullptr && f.open(QIODevice::ReadOnly)) {
            *said = snapshotSummary(QString::fromUtf8(f.readAll())).join(QStringLiteral("; "));
            f.close();
        }
        return runElevated(resumeScript(QDir::toNativeSeparators(path)), 120000);
#endif
    }

    bool interferencePaused() { return QFile::exists(snapshotPath()); }

} // namespace GreenRhythm

void MainWindow::on_menu_interference_triggered() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, tr("Что мешает подключению"),
                             tr("Эта проверка есть только под Windows."));
#else
    using namespace GreenRhythm;

    // Уже что-то приостановлено — предлагаем ровно одно: вернуть.
    if (interferencePaused()) {
        QString said;
        QFile f(snapshotPath());
        if (f.open(QIODevice::ReadOnly)) {
            said = snapshotSummary(QString::fromUtf8(f.readAll())).join(QChar('\n'));
            f.close();
        }
        if (QMessageBox::question(this, tr("Что мешает подключению"),
                                  tr("Сейчас приостановлено:\n\n%1\n\nВернуть как было?").arg(said))
            == QMessageBox::Yes) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool ok = resumeInterference(nullptr);
            QApplication::restoreOverrideCursor();
            QMessageBox::information(this, tr("Что мешает подключению"),
                                     ok ? tr("Вернули как было.")
                                        : tr("Не получилось вернуть. Службы можно включить вручную: "
                                             "«Панель управления» → «Администрирование» → «Службы»."));
        }
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto found = scanInterference();
    QApplication::restoreOverrideCursor();

    if (found.isEmpty()) {
        QMessageBox::information(this, tr("Что мешает подключению"),
                                 tr("Ничего мешающего не нашли: чужих VPN, обходов и туннельных "
                                    "адаптеров на машине нет."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Что мешает подключению"));
    dlg.resize(680, 560);
    auto *box = new QVBoxLayout(&dlg);

    auto *head = new QLabel(&dlg);
    head->setWordWrap(true);
    box->addWidget(head);

    auto *area = new QScrollArea(&dlg);
    area->setWidgetResizable(true);
    auto *inner = new QWidget(area);
    auto *list = new QVBoxLayout(inner);

    // Разложено по ИСХОДАМ, а не по видам. Человеку важно не «служба это или
    // адаптер», а «мне что-то делать или нет».
    struct Section {
        Cure cure;
        QString title;
        QString hint;
        bool checkable;
        bool checkedByDefault;
    };
    const QList<Section> sections{
        {Cure::Pause, tr("Мешают: забирают весь трафик"),
         tr("Два маршрута по умолчанию не уживаются — кто-то должен уступить. "
            "Приостановка обратима: вернём, когда отключите VPN или закроете клиент."),
         true, false},
        {Cure::Separate, tr("Можно развести по адресам"),
         tr("Останавливать не нужно. Их подсети допишутся в «мимо туннеля», и оба туннеля "
            "будут работать одновременно."),
         true, true},
        {Cure::Manual, tr("Только вашими руками"),
         tr("Сюда мы не лезем: корпоративные клиенты и запущенное вручную. "
            "Вернуть автоматически не сможем, поэтому и останавливать не предлагаем."),
         false, false},
        {Cure::Nothing, tr("Уживаются — делать ничего не надо"),
         tr("Нашли, посмотрели маршруты, убедились, что нашему туннелю они не мешают."), false, false},
    };

    QList<QCheckBox *> boxes;
    QList<Meddling> rows;
    int actionable = 0;

    for (const auto &sec: sections) {
        QList<Meddling> mine;
        for (const auto &m: found) {
            if (m.cure == sec.cure) mine << m;
        }
        if (mine.isEmpty()) continue;

        auto *caption = new QLabel(QStringLiteral("<h3>%1</h3><p>%2</p>").arg(sec.title, sec.hint), inner);
        caption->setWordWrap(true);
        list->addWidget(caption);

        for (const auto &m: mine) {
            QString text = QStringLiteral("<b>%1</b> — %2").arg(m.title.toHtmlEscaped(), kindWord(m.kind));
            if (!m.detail.isEmpty()) text += QStringLiteral("<br>%1").arg(m.detail.toHtmlEscaped());
            if (!m.advice.isEmpty()) text += QStringLiteral("<br>%1").arg(m.advice.toHtmlEscaped());
            if (!m.risk.isEmpty()) text += QStringLiteral("<br><b>%1</b>").arg(m.risk.toHtmlEscaped());

            if (sec.checkable) {
                auto *cb = new QCheckBox(text, inner);
                cb->setChecked(sec.checkedByDefault);
                list->addWidget(cb);
                boxes << cb;
                rows << m;
                actionable++;
            } else {
                auto *lbl = new QLabel(QStringLiteral("• ") + text, inner);
                lbl->setWordWrap(true);
                lbl->setContentsMargins(18, 0, 0, 6);
                list->addWidget(lbl);
            }
        }
    }
    list->addStretch();
    area->setWidget(inner);
    box->addWidget(area, 1);

    head->setText(actionable > 0
                      ? tr("Посмотрели маршруты каждого чужого туннеля и разложили по тому, "
                           "<b>что с ними делать</b>. Ничего не удаляется и ничего не делается без галочки.")
                      : tr("Чужие VPN на машине есть, но <b>ни один из них нам не мешает</b>: "
                           "каждый несёт только свои подсети, а наш туннель их не трогает. "
                           "Делать ничего не надо."));

    auto *buttons = new QDialogButtonBox(
        actionable > 0 ? (QDialogButtonBox::Ok | QDialogButtonBox::Cancel) : QDialogButtonBox::Close, &dlg);
    if (actionable > 0) {
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Применить отмеченное"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("Ничего не делать"));
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    box->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted || actionable == 0) return;

    QList<Meddling> toPause;
    QStringList toSeparate;
    QStringList separateNames;
    for (int i = 0; i < rows.size() && i < boxes.size(); ++i) {
        if (!boxes[i]->isChecked()) continue;
        if (rows[i].cure == Cure::Separate) {
            toSeparate << rows[i].overlap;
            separateNames << rows[i].title;
        } else if (rows[i].cure == Cure::Pause && rows[i].reversible) {
            toPause << rows[i];
        }
    }

    // РАЗВЕДЕНИЕ ПО АДРЕСАМ — ПЕРВЫМ, потому что оно ничего не ломает и часто
    // снимает нужду в остановке вовсе.
    if (!toSeparate.isEmpty()) {
        auto lines = NekoGui::dataStore->vpn_route_exclude_extra.split(QChar('\n'));
        for (const auto &p: toSeparate) {
            if (!lines.contains(p)) lines << p;
        }
        lines.removeAll(QString());
        NekoGui::dataStore->vpn_route_exclude_extra = lines.join(QChar('\n'));
        show_log_impl(tr("Мимо туннеля добавлены адреса чужих туннелей: %1 (для %2)")
                          .arg(toSeparate.join(QStringLiteral(", ")), separateNames.join(QStringLiteral(", "))));
        // Исключения читаются при старте ядра, поэтому без перезапуска ничего
        // не изменится — RouteChanged об этом и спросит.
        MW_dialog_message("", "UpdateDataStore,RouteChanged");
    }

    if (!toPause.isEmpty()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = runElevated(pauseScript(toPause, QDir::toNativeSeparators(snapshotPath())), 120000);
        QApplication::restoreOverrideCursor();
        if (!ok || !interferencePaused()) {
            QMessageBox::warning(this, tr("Что мешает подключению"),
                                 tr("Не получилось приостановить. Ничего не изменилось."));
            return;
        }
        QStringList names;
        for (const auto &m: toPause) names << m.title;
        show_log_impl(tr("Приостановлено на время работы: %1").arg(names.join(QStringLiteral(", "))));
        QMessageBox::information(
            this, tr("Что мешает подключению"),
            tr("Приостановили. Вернём как было, когда вы отключите VPN или закроете клиент."));
    } else if (!toSeparate.isEmpty()) {
        QMessageBox::information(this, tr("Что мешает подключению"),
                                 tr("Развели по адресам. Ничего не выключено — оба туннеля будут "
                                    "работать одновременно."));
    }
#endif
}
