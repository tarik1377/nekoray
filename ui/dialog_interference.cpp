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
#include "sys/WinShell.hpp"
#include "ui/mainwindow.h"

/**
 * «Что мешает подключению» — окно со списком и галочками.
 *
 * ЭТО ТРЕТИЙ ПУТЬ, а не замена «Починить сеть». Та кнопка сносит навсегда и
 * права на это имеет: она разбирает брошенный zapret, который человек ставил
 * полгода назад. Здесь — работающие чужие программы, которые нужны своему
 * хозяину, и трогать их можно только на время и только с возвратом.
 *
 * ПОЧЕМУ РАЗВЕДКА И ДЕЙСТВИЕ РАЗДЕЛЕНЫ. Список читается обычным пользователем
 * (Get-Service, Get-NetAdapter, Get-Process), поэтому окно открывается без
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
                case Meddler::Adapter: return QObject::tr("сетевой адаптер");
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
        return parseScan(runPlain(script, 30000));
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
    dlg.resize(620, 480);
    auto *box = new QVBoxLayout(&dlg);

    auto *head = new QLabel(
        tr("Эти программы работают с сетью так же, как мы, и могут мешать. Отметьте те, "
           "которые стоит <b>приостановить на время</b>.<br><br>"
           "Ничего не удаляется. Прежнее состояние записывается до остановки и возвращается, "
           "когда вы отключите VPN, закроете клиент — или при следующем запуске, если клиент "
           "закрылся неожиданно."),
        &dlg);
    head->setWordWrap(true);
    box->addWidget(head);

    auto *area = new QScrollArea(&dlg);
    area->setWidgetResizable(true);
    auto *inner = new QWidget(area);
    auto *list = new QVBoxLayout(inner);
    QList<QCheckBox *> boxes;

    for (const auto &m: found) {
        auto *cb = new QCheckBox(inner);
        QString text = QStringLiteral("<b>%1</b> — %2").arg(m.title.toHtmlEscaped(), kindWord(m.kind));
        if (!m.detail.isEmpty()) text += QStringLiteral("<br><span>%1</span>").arg(m.detail.toHtmlEscaped());
        if (!m.risk.isEmpty()) text += QStringLiteral("<br><b>%1</b>").arg(m.risk.toHtmlEscaped());
        if (!m.reversible) {
            // Правило 3: чего не умеем вернуть — то без галочки. Предложить
            // кнопку и не суметь откатить хуже, чем не предлагать вовсе.
            text += QStringLiteral("<br><i>%1</i>")
                        .arg(tr("вернуть автоматически не сможем — закройте сами, если решите"));
            cb->setEnabled(false);
        }
        cb->setText(text);
        cb->setChecked(false); // правило 1: ничего не трогаем без галочки
        list->addWidget(cb);
        boxes << cb;
    }
    list->addStretch();
    area->setWidget(inner);
    box->addWidget(area, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Приостановить отмеченные"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Ничего не делать"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    box->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QList<Meddling> chosen;
    for (int i = 0; i < found.size() && i < boxes.size(); ++i) {
        if (boxes[i]->isChecked() && found[i].reversible) chosen << found[i];
    }
    if (chosen.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = runElevated(pauseScript(chosen, QDir::toNativeSeparators(snapshotPath())), 120000);
    QApplication::restoreOverrideCursor();

    if (!ok || !interferencePaused()) {
        QMessageBox::warning(this, tr("Что мешает подключению"),
                             tr("Не получилось приостановить. Ничего не изменилось."));
        return;
    }
    show_log_impl(tr("Приостановлено на время работы: %1")
                      .arg([&] {
                          QStringList names;
                          for (const auto &m: chosen) names << m.title;
                          return names.join(QStringLiteral(", "));
                      }()));
    QMessageBox::information(this, tr("Что мешает подключению"),
                             tr("Приостановили. Вернём как было, когда вы отключите VPN или закроете клиент."));
#endif
}
