#include "SiteRoutingDialog.hpp"
#include "main/SiteRouting.hpp"
#include "main/RobloxSitePreset.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace GreenRhythm {
SiteRoutingDialog::SiteRoutingDialog(const QString &saved, QWidget *parent) : QDialog(parent) {
    setObjectName("grSiteRouting"); setWindowTitle(tr("Умный выбор пути для сайтов"));
    const auto available = QGuiApplication::primaryScreen()->availableGeometry().size() - QSize(32,64);
    resize(qMax(320,qMin(720,available.width())),qMax(280,qMin(650,available.height())));
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget(scroll); scroll->setWidget(canvas); outer->addWidget(scroll,1);
    auto *layout = new QVBoxLayout(canvas);
    auto text = [&](const QString &value) { auto *label = new QLabel(value,canvas); label->setWordWrap(true); label->setTextFormat(Qt::PlainText); layout->addWidget(label); };
    text(tr("Вставьте весь список доменов в нужный режим. «Авто» сравнивает HTTPS напрямую и через VPN, предпочитает подтверждённый прямой путь и запоминает результат для сети и профиля. Списки работают и для сайтов в браузере."));
    enabled = new QCheckBox(tr("Включить выбор пути для этих сайтов"),canvas); enabled->setObjectName("siteRoutingEnabled"); layout->addWidget(enabled);
    auto *preset = new QPushButton(tr("Добавить Roblox и CDN в «Авто»"),canvas); preset->setObjectName("siteRobloxPreset"); layout->addWidget(preset);
    auto *tabs = new QTabWidget(canvas); tabs->setObjectName("siteRoutingTabs");
    auto editor = [&](const QString &title, const char *name) {
        auto *field = new QPlainTextEdit(tabs); field->setObjectName(name); field->setAccessibleName(title);
        field->setPlaceholderText(tr("domain:roblox.com\nfull:точный-домен-CDN")); field->setMinimumHeight(150);
        tabs->addTab(field,title); return field;
    };
    automatic = editor(tr("Авто"),"siteAutoList"); direct = editor(tr("Напрямую"),"siteDirectList"); proxy = editor(tr("Через VPN"),"siteProxyList");
    auto *bulk = new QHBoxLayout;
    auto *bulkMode = new QComboBox(canvas); bulkMode->setObjectName("siteBulkMode"); bulkMode->setAccessibleName(tr("Режим всего набора сайтов"));
    bulkMode->addItem(tr("Авто"),"auto"); bulkMode->addItem(tr("Напрямую"),"direct"); bulkMode->addItem(tr("Через VPN"),"proxy");
    auto *bulkButton = new QPushButton(tr("Выбрать для всего набора"),canvas); bulkButton->setObjectName("siteBulkApply");
    bulk->addWidget(bulkMode); bulk->addWidget(bulkButton); layout->addLayout(bulk);
    layout->addWidget(tabs);
    text(tr("domain: включает поддомены, full: — только точное имя. Общие CDN распознаются как точные имена. Для отдельного поддомена можно выбрать другой режим."));
    text(tr("Авто проверяет HTTPS на порту 443 без cookies и входа в аккаунт. Это проверка доступности, а не успешного входа. Если сайт ограничивает страну аккаунта, выберите «Через VPN». Работающие соединения не переносятся; QUIC/UDP остаётся по вашей схеме."));
    text(tr("Явные правила программ, существующие правила сайтов/IP и рабочие VPN имеют приоритет. Для браузера используйте «Помощь браузеру → По правилам сайтов» и системный прокси либо TUN. Расширение прокси браузера может выбирать другой путь."));
    text(tr("Сохраняется только этот набор с резервной копией. Применение — при следующем подключении GreenRhythm. Ваши остальные маршруты и текущее подключение сохраняются."));
    // Save feedback belongs beside the fixed footer. A pasted-list error must
    // not appear below the viewport and make a working Save button look inert.
    status = new QLabel(this); status->setObjectName("siteRoutingStatus"); status->setWordWrap(true); status->setTextFormat(Qt::PlainText); outer->addWidget(status);
    SiteRouting::State state; QString error;
    valid = SiteRouting::parse(saved,&state,&error); enabled->setChecked(state.enabled);
    for (const auto &entry : state.entries) (entry.mode == "auto" ? automatic : entry.mode == "direct" ? direct : proxy)->appendPlainText(entry.pattern);
    if (!valid) { status->setText(error); for (auto *field : {automatic,direct,proxy}) field->setReadOnly(true); enabled->setEnabled(false); preset->setEnabled(false); bulkButton->setEnabled(false); }
    connect(bulkButton,&QPushButton::clicked,this,[this,bulkMode,tabs] {
        SiteRouting::State all; QString error;
        if(!SiteRouting::fromLists(true,automatic->toPlainText()+'\n'+direct->toPlainText()+'\n'+proxy->toPlainText(),{},{},&all,&error)) {status->setText(error);return;}
        QStringList names;for(const auto &entry:all.entries)names<<entry.pattern;
        automatic->clear();direct->clear();proxy->clear();
        const auto mode=bulkMode->currentData().toString();auto *target=mode=="auto"?automatic:mode=="direct"?direct:proxy;
        target->setPlainText(names.join('\n'));tabs->setCurrentWidget(target);
        status->setText(tr("Режим всего набора изменён в черновике. Нажмите «Сохранить», чтобы применить его при следующем подключении."));
    });
    auto *decisions = new QLabel(canvas); decisions->setObjectName("siteRoutingDecisions"); decisions->setTextFormat(Qt::PlainText); decisions->setWordWrap(true); layout->addWidget(decisions);
    const auto refresh = [decisions] {
        decisions->setText(tr("Решения ядра появятся после включения авто, переподключения профиля и обращения к выбранным сайтам. Если сработало более раннее правило, авто для этого соединения не запускается."));
        QFile input(".site-auto/status.json");
        if (!input.open(QIODevice::ReadOnly) || input.size()>65536) return;
        const auto object=QJsonDocument::fromJson(input.read(65536)).object();
        const auto updated=QDateTime::fromString(object.value("updated").toString(),Qt::ISODateWithMs);
        const auto age=updated.secsTo(QDateTime::currentDateTimeUtc());
        if (object.value("schema")!=1 || !updated.isValid() || age<0 || age>900 || !object.value("entries").isObject()) return;
        const auto entries=object.value("entries").toObject(); if(entries.size()>128) return;
        QStringList lines{tr("Последние сохранённые решения ядра · %1 (могут относиться к предыдущему подключению)").arg(updated.toLocalTime().toString("HH:mm:ss"))};
        for(auto it=entries.begin();it!=entries.end() && lines.size()<9;++it) {
            if (!it.key().startsWith("tcp:")) continue;
            QString checked; if(!SiteRouting::pattern("full:"+it.key().mid(4),&checked)) continue;
            const auto value=it.value().toObject(); const auto path=value.value("path").toString();
            if(path!="direct" && path!="proxy") continue;
            lines<<tr("%1 — %2%3").arg(it.key().mid(4),path=="direct"?tr("напрямую"):tr("через VPN"),value.value("verified").toBool()?QString():tr(" · доступность не подтверждена"));
        }
        if(lines.size()>1) decisions->setText(lines.join('\n'));
    };
    refresh(); auto *timer=new QTimer(this);timer->setInterval(2000);connect(timer,&QTimer::timeout,this,refresh);timer->start();
    connect(preset,&QPushButton::clicked,this,[this,tabs] {
        automatic->appendPlainText(robloxSitePreset()); tabs->setCurrentIndex(0); enabled->setChecked(true);
        status->setText(tr("Набор Roblox добавлен в черновик. Повторы будут убраны при сохранении; существующий ручной выбор нужно оставить только в одном списке."));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Сохранить")); buttons->button(QDialogButtonBox::Save)->setObjectName("siteRoutingSave");
    buttons->button(QDialogButtonBox::Save)->setEnabled(valid); buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена")); outer->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&SiteRoutingDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
void SiteRoutingDialog::accept() {
    SiteRouting::State state; QString error;
    if (!valid || !SiteRouting::fromLists(enabled->isChecked(),automatic->toPlainText(),direct->toPlainText(),proxy->toPlainText(),&state,&error)) { status->setText(error); return; }
    if(state.enabled && state.entries.isEmpty()) { status->setText(tr("Добавьте домены или нажмите «Добавить Roblox и CDN».")); return; }
    if (!commit || !commit(SiteRouting::serialize(state),&error)) { status->setText(error.isEmpty() ? tr("Сохранение недоступно.") : error); return; }
    QDialog::accept();
}
}
