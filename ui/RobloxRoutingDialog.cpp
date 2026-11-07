#include "RobloxRoutingDialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QDateTime>
#include <QFile>
#include <QTimer>

namespace GreenRhythm {
RobloxRoutingDialog::RobloxRoutingDialog(const ProgramRouting::State &saved, bool tunnel, QWidget *parent)
    : QDialog(parent), original(saved) {
    setObjectName("grRobloxRouting"); setWindowTitle(tr("Roblox и Roblox Studio"));
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget(scroll); scroll->setWidget(canvas); outer->addWidget(scroll,1);
    auto *layout = new QVBoxLayout(canvas); layout->setContentsMargins(16,16,16,16); layout->setSpacing(12);
    const auto *screen=QGuiApplication::primaryScreen();
    const auto available=(screen?screen->availableGeometry().size():QSize(800,600))-QSize(32,64);
    resize(qMax(320,qMin(640,available.width())),qMax(280,qMin(520,available.height())));
    auto text = [&](const QString &value) { auto *label=new QLabel(value,this); label->setWordWrap(true); layout->addWidget(label); };
    text(tr("Выберите маршрут отдельно для игры и редактора. «Авто» проверяет доступность сервисов напрямую и через VPN и запоминает результат для этой сети и профиля. Можно также выбрать одно направление для всей программы."));
    auto add = [&](const QString &title, const QString &name, const char *objectName) {
        layout->addWidget(new QLabel(title,this));
        auto *row = new QHBoxLayout;
        auto *choice = new QComboBox(this); choice->setObjectName(objectName); choice->setAccessibleName(tr("Подключение: %1").arg(title));
        choice->addItem(tr("Авто: выбирать рабочий путь"),"adaptive");
        choice->addItem(tr("TCP через VPN, UDP напрямую"),"smart");
        choice->addItem(tr("Напрямую"),"bypass"); choice->addItem(tr("Через VPN"),"proxy");
        choice->addItem(tr("По правилам сайтов"),"auto");
        if (saved.whitelist) {
            choice->setItemText(4,tr("Общие правила белого списка"));
            static_cast<QStandardItemModel *>(choice->model())->item(4)->setEnabled(false);
        }
        const auto mode=ProgramRouting::overrideMode(saved,name);
        choice->setCurrentIndex(choice->findData(mode));
        if(mode=="unknown") choice->setEnabled(false);
        row->addWidget(choice,1); auto *inspect=new QPushButton(tr("Проверить"),this);
        inspect->setObjectName(QString::fromLatin1(objectName)+"Inspect"); row->addWidget(inspect); layout->addLayout(row);
        connect(inspect,&QPushButton::clicked,this,[this,name] { if(inspector) inspector(name); });
        return choice;
    };
    player=add(tr("Roblox — игра"),"RobloxPlayerBeta.exe","robloxPlayerRoute");
    studio=add(tr("Roblox Studio — редактор"),"RobloxStudioBeta.exe","robloxStudioRoute");
    if(saved.whitelist) text(tr("В белом списке доступны «Авто» и явные направления. Для выбора по сайтам нужен обычный режим TUN; это окно его не переключает."));
    text(tr("В авто HTTPS проверяется отдельными запросами без входа в аккаунт. Игровой UDP оценивается по ответам самого адреса: после двух попыток без ответа другой путь пробуется для нового потока. Работающие соединения не переносятся. Локальные подключения Studio остаются в локальной сети."));
    text(tr("Результаты авто сохраняются локально на 10 минут отдельно для сети и VPN-профиля. При восстановлении прямого HTTPS нужны две успешные проверки. «По правилам сайтов» снимает выбор всей программы и возвращает вашу схему сайтов/IP. Принудительное направление относится ко всему TCP и UDP программы."));
    text(tunnel ? tr("Сохранённый выбор применится при следующем подключении GreenRhythm. Текущее соединение продолжит работать.")
                : tr("Выбор программы работает во встроенном TUN Windows. Один системный HTTP-прокси не направляет игровой UDP через VPN."));
    auto *availability=new QLabel(this);availability->setObjectName("robloxAvailabilityStatus");
    availability->setWordWrap(true);availability->setTextFormat(Qt::PlainText);layout->addWidget(availability);
    const bool savedAuto=ProgramRouting::overrideMode(saved,"RobloxPlayerBeta.exe")=="adaptive"
        || ProgramRouting::overrideMode(saved,"RobloxStudioBeta.exe")=="adaptive";
    const auto refresh=[this,availability,savedAuto] {
        availability->setText(tr("Результаты появятся после включения авто и новых подключений Roblox. Проверка здесь не меняет маршрут."));
        if(!savedAuto) return;
        QFile input(".roblox-auto/status.json");
        if(!input.open(QIODevice::ReadOnly) || input.size()>65536) return;
        const auto root=QJsonDocument::fromJson(input.read(65536)).object();
        const auto updated=QDateTime::fromString(root.value("updated").toString(),Qt::ISODateWithMs);
        if(root.value("schema")!=1 || !updated.isValid() || updated.secsTo(QDateTime::currentDateTimeUtc())>900) return;
        const auto entries=root.value("entries").toObject();if(entries.size()>128) return;
        QStringList lines;lines<<tr("Последние результаты авто · %1").arg(updated.toLocalTime().toString("HH:mm:ss"));
        for(auto it=entries.begin();it!=entries.end() && lines.size()<9;++it) {
            const auto value=it.value().toObject();const auto route=value.value("path").toString();
            if(route!="direct" && route!="proxy") continue;
            const auto key=it.key().left(100);
            lines<<tr("%1: %2%3").arg(key,route=="direct"?tr("напрямую"):tr("через VPN"),
                value.value("verified").toBool()?QString():tr(" · ещё не подтверждён"));
        }
        availability->setText(lines.join('\n'));
    };
    refresh();auto *timer=new QTimer(this);timer->setInterval(2000);
    connect(timer,&QTimer::timeout,this,refresh);timer->start();
    status=new QLabel(this); status->setObjectName("robloxRouteStatus"); status->setWordWrap(true); layout->addWidget(status);
    QJsonObject apps; QString error;
    QStringList split;
    if(!GameAppRules::parse(saved.applications,&apps,&error) || !RobloxRouting::parse(saved.robloxSplit,&split,&error) || !RobloxRouting::parse(saved.robloxAdaptive,nullptr,&error)) status->setText(error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this); outer->addWidget(buttons);
    save=buttons->button(QDialogButtonBox::Save); save->setObjectName("robloxRouteSave"); save->setText(tr("Готово"));
    save->setEnabled(error.isEmpty()); buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    for(auto *choice:{player,studio}) connect(choice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this] { save->setText(tr("Сохранить")); status->clear(); });
    connect(buttons,&QDialogButtonBox::accepted,this,&RobloxRoutingDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
void RobloxRoutingDialog::accept() {
    auto candidate=original; QString error;
    for(const auto &entry : {qMakePair(QStringLiteral("RobloxPlayerBeta.exe"),player),qMakePair(QStringLiteral("RobloxStudioBeta.exe"),studio)}) {
        const auto mode=entry.second->currentData().toString();
        if(mode==ProgramRouting::overrideMode(original,entry.first)) continue;
        ProgramRouting::State next;
        if(!ProgramRouting::choose(candidate,entry.first,mode,&next,&error)) {status->setText(error);return;}
        candidate=next;
    }
    if(!(candidate==original) && commit && !commit(candidate,&error)) {status->setText(error);return;}
    QDialog::accept();
}
}
