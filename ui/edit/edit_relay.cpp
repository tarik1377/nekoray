#include "edit_relay.h"
#include "ui_edit_relay.h"

#include "fmt/RelayBean.hpp"
#include "main/DeviceCredentials.hpp"
#include "ui/dialog_relay_activate.h"

#include <QRegularExpressionValidator>

EditRelay::EditRelay(QWidget *parent) : QWidget(parent), ui(new Ui::EditRelay) {
    ui->setupUi(this);

    // Состояние доступа показывается прямо в редакторе, а не только в отдельном
    // диалоге: человек, открывший профиль, потому что «не подключается»,
    // должен увидеть причину здесь, а не искать её в меню.
    connect(ui->activate, &QPushButton::clicked, this, [this] {
        DialogRelayActivate d(this);
        d.exec();
        repaintState();
    });

    repaintState();
}

EditRelay::~EditRelay() { delete ui; }

void EditRelay::repaintState() {
    // Простой текст явно: ниже сюда попадает сохранённый ответ сайта, а формат
    // по умолчанию — AutoText, и QLabel сам решит считать его разметкой, стоит
    // там появиться угловой скобке. Съедено при этом будет ровно то, ради чего
    // сообщение писали, — например, имя устройства, занявшего слот.
    ui->access->setTextFormat(Qt::PlainText);
    if (DeviceCredentials::IsProvisioned()) {
        ui->access->setText(tr("Доступ активен."));
        ui->activate->setText(tr("Изменить…"));
        return;
    }
    const auto said = DeviceCredentials::StateDetail();
    ui->access->setText(said.isEmpty() ? tr("Не активировано на этом устройстве.") : said);
    ui->activate->setText(tr("Активировать…"));
}

void EditRelay::onStart(std::shared_ptr<NekoGui::ProxyEntity> _ent) {
    this->ent = _ent;
    auto bean = this->ent->RelayBean();

    P_LOAD_BOOL(bypass_ru);
    P_LOAD_BOOL(udp_direct);
    P_LOAD_STRING(dns_servers);

    // Не P_LOAD_INT: он печатает ноль как «0», а ноль здесь значит «выбрать
    // свободный». Строка «0» в поле выглядит как настоящее значение, и человек
    // начинает думать, что это порт.
    ui->socks_port->setValidator(QRegExpValidator_Number);
    ui->socks_port->setText(bean->socks_port > 0 ? Int2String(bean->socks_port) : "");
}

bool EditRelay::onEnd() {
    auto bean = this->ent->RelayBean();

    P_SAVE_BOOL(bypass_ru);
    P_SAVE_BOOL(udp_direct);
    P_SAVE_STRING(dns_servers);
    bean->socks_port = ui->socks_port->text().trimmed().toInt();

    return true;
}
