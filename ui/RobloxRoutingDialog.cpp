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
    text(tr("Выберите маршрут отдельно для игры и редактора. «По правилам сайтов» убирает принудительный маршрут всей программы: дальше действуют ваши правила сайтов и IP-адресов."));
    auto add = [&](const QString &title, const QString &name, const char *objectName) {
        layout->addWidget(new QLabel(title,this));
        auto *row = new QHBoxLayout;
        auto *choice = new QComboBox(this); choice->setObjectName(objectName); choice->setAccessibleName(tr("Подключение: %1").arg(title));
        choice->addItem(tr("По правилам сайтов"),"auto"); choice->addItem(tr("Напрямую"),"bypass"); choice->addItem(tr("Через VPN"),"proxy");
        if (saved.whitelist) {
            choice->setItemText(0,tr("Общие правила белого списка"));
            static_cast<QStandardItemModel *>(choice->model())->item(0)->setEnabled(false);
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
    if(saved.whitelist) text(tr("В белом списке доступен явный выбор «Напрямую» и «Через VPN». Для выбора по сайтам нужен обычный режим TUN; это окно его не переключает."));
    text(tr("Принудительный выбор действует на TCP и UDP выбранной программы. Автоматический выбор следует вашей схеме и не проверяет блокировки провайдера. Локальные подключения Studio остаются в локальной сети."));
    text(tunnel ? tr("Сохранённый выбор применится при следующем подключении GreenRhythm. Текущее соединение продолжит работать.")
                : tr("Выбор программы работает во встроенном TUN Windows. Один системный HTTP-прокси не направляет игровой UDP через VPN."));
    status=new QLabel(this); status->setObjectName("robloxRouteStatus"); status->setWordWrap(true); layout->addWidget(status);
    QJsonObject apps; QString error;
    if(!GameAppRules::parse(saved.applications,&apps,&error)) status->setText(error);
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
