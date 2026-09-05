/**
 * Снимки всех диалогов из .ui — без сети, без ядра, без настроек.
 *
 * ЗАЧЕМ. Главное окно и панель уже снимаются превью, а диалоги — «Настройки»,
 * «Маршруты», редактор профиля и остальные — нет. Их можно было увидеть только
 * запустив клиент, поэтому они и остались нетронутыми: старый nekoray в новом
 * окне. Владелец сказал «всё старое переделать в новое»; переделывать вслепую
 * значит повторить то, из-за чего они и отстали.
 *
 * Поднимается только сгенерированная вёрстка (ui_*.h) на голом QDialog или
 * QWidget: та же тема, тот же шрифт, тот же перевод, что у человека. Логики
 * диалогов здесь нет — она тянет ядро и хранилище настроек. Значит, поля будут
 * пустыми, а вкладки — как в .ui, и это ровно то, что нужно для вёрстки.
 *
 * В выпуск не входит: EXCLUDE_FROM_ALL.
 *   ninja dialogs_preview && ./dialogs_preview <каталог-для-png>
 */

#include "ui_dialog_basic_settings.h"
#include "ui_dialog_edit_group.h"
#include "ui_dialog_edit_profile.h"
#include "ui_dialog_hotkey.h"
#include "ui_dialog_manage_groups.h"
#include "ui_dialog_manage_routes.h"
#include "ui_dialog_vpn_settings.h"
#include "ui_edit_chain.h"
#include "ui_edit_custom.h"
#include "ui_edit_naive.h"
#include "ui_edit_quic.h"
#include "ui_edit_relay.h"
#include "ui_edit_shadowsocks.h"
#include "ui_edit_socks_http.h"
#include "ui_edit_trojan_vless.h"
#include "ui_edit_vmess.h"

#include "main/AppFont.hpp"
#include "ui/DialogPolish.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QTabWidget>
#include <QTranslator>

#include <cstdio>

namespace {

    int shots = 0;
    int failed = 0;

    void save(QWidget &w, const QString &dir, const QString &name) {
        w.show();
        // Два прохода событий: у окна с прокруткой первая раскладка идёт до
        // появления полосы, вторая — после, и снимок с одного прохода показывал
        // содержимое под полосой.
        QApplication::processEvents();
        QApplication::processEvents();
        const QPixmap shot = w.grab();
        const QString path = QDir(dir).filePath(QStringLiteral("dlg-%1.png").arg(name));
        if (shot.save(path)) {
            std::printf("сохранено: %s (%dx%d)\n", qPrintable(path), shot.width(), shot.height());
            shots++;
        } else {
            std::printf("НЕ сохранено: %s\n", qPrintable(path));
            failed++;
        }
        w.hide();
    }

    template<class Ui>
    void dialog(const QString &dir, const QString &name, int tab = -1) {
        QDialog d;
        Ui ui;
        ui.setupUi(&d);
        GreenRhythm::polishDialog(&d);
        if (tab >= 0) {
            // У «Настроек» вкладки; снимаем каждую, иначе видна только первая.
            if (auto *t = d.findChild<QTabWidget *>()) t->setCurrentIndex(tab);
        }
        save(d, dir, tab >= 0 ? QStringLiteral("%1-%2").arg(name).arg(tab) : name);
    }

    template<class Ui>
    void widget(const QString &dir, const QString &name) {
        QWidget w;
        Ui ui;
        ui.setupUi(&w);
        GreenRhythm::polishDialog(&w);
        save(w, dir, name);
    }

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    GreenRhythm::applyAppFont(app);

    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");

    QTranslator ru;
    if (ru.load(QStringLiteral("ru_RU.qm"), QCoreApplication::applicationDirPath())) {
        QCoreApplication::installTranslator(&ru);
    } else {
        qWarning("перевод не загрузился — подписи будут английскими");
    }
    // И перевод самого Qt — иначе «OK» и «Cancel» на снимке останутся
    // английскими, каких у человека уже нет.
    QTranslator qt;
    if (qt.load(QStringLiteral("qtbase_ru.qm"), QCoreApplication::applicationDirPath())) {
        QCoreApplication::installTranslator(&qt);
    }

    // Тема — с диска, тем же файлом, что грузит ThemeManager.
    for (const auto &p: {QStringLiteral("../res/theme/feiyangqingyun/qss/modern.css"),
                         QStringLiteral("res/theme/feiyangqingyun/qss/modern.css")}) {
        QFile qss(p);
        if (qss.open(QIODevice::ReadOnly)) {
            app.setStyleSheet(QString::fromUtf8(qss.readAll()));
            break;
        }
    }

    // «Настройки» — по вкладке на снимок.
    {
        QDialog probe;
        Ui::DialogBasicSettings ui;
        ui.setupUi(&probe);
        const int tabs = probe.findChild<QTabWidget *>() != nullptr ? probe.findChild<QTabWidget *>()->count() : 1;
        for (int i = 0; i < tabs; ++i) dialog<Ui::DialogBasicSettings>(dir, QStringLiteral("settings"), i);
    }
    dialog<Ui::DialogManageRoutes>(dir, QStringLiteral("routes"));
    dialog<Ui::DialogVPNSettings>(dir, QStringLiteral("vpn"));
    dialog<Ui::DialogHotkey>(dir, QStringLiteral("hotkey"));
    dialog<Ui::DialogManageGroups>(dir, QStringLiteral("groups"));
    dialog<Ui::DialogEditGroup>(dir, QStringLiteral("edit-group"));
    dialog<Ui::DialogEditProfile>(dir, QStringLiteral("edit-profile"));

    widget<Ui::EditTrojanVLESS>(dir, QStringLiteral("edit-vless"));
    widget<Ui::EditVMess>(dir, QStringLiteral("edit-vmess"));
    widget<Ui::EditShadowSocks>(dir, QStringLiteral("edit-ss"));
    widget<Ui::EditSocksHttp>(dir, QStringLiteral("edit-socks"));
    widget<Ui::EditQUIC>(dir, QStringLiteral("edit-quic"));
    widget<Ui::EditNaive>(dir, QStringLiteral("edit-naive"));
    widget<Ui::EditRelay>(dir, QStringLiteral("edit-relay"));
    widget<Ui::EditChain>(dir, QStringLiteral("edit-chain"));
    widget<Ui::EditCustom>(dir, QStringLiteral("edit-custom"));

    std::printf("снимков: %d, не удалось: %d\n", shots, failed);
    return failed == 0 ? 0 : 1;
}
