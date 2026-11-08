#include "SitesPage.hpp"
#include "main/SiteServices.hpp"
#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace GreenRhythm {
    namespace {
        QString modeText(const QString &mode) { return mode == "direct" ? QObject::tr("Напрямую") : mode == "proxy" ? QObject::tr("Через VPN")
                                                                                                                    : QObject::tr("Авто"); }
        QString sampleText(const QJsonObject &sample) {
            if (sample.isEmpty() || sample.value("requests").toInt() == 0) return QObject::tr("Не наблюдалось");
            if (sample.value("ok").toInt() > 0 && sample.value("failed").toInt() == 0) return QObject::tr("Ответ получен");
            return QObject::tr("Не подтверждено (%1)").arg(sample.value("status").toInt());
        }
        QString timeText(const QString &text) {
            auto time = QDateTime::fromString(text, Qt::ISODateWithMs);
            if (!time.isValid()) time = QDateTime::fromString(text.left(19) + "Z", Qt::ISODate);
            return time.isValid() ? time.toLocalTime().toString("dd.MM HH:mm:ss") : QString();
        }
    } // namespace
    SitesPage::SitesPage(QWidget *parent) : QWidget(parent) {
        setObjectName("grSitesPage");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(10);
        auto *heading = new QLabel(tr("Сайты"), this);
        auto font = heading->font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * 1.4);
        heading->setFont(font);
        layout->addWidget(heading);
        auto *intro = new QLabel(tr("Вставьте URL. GreenRhythm загрузит публичную страницу в отдельной сессии, найдёт увиденные зависимости и сравнит прямой путь с VPN. Ваш браузер и аккаунты не используются."), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *input = new QHBoxLayout;
        address = new QLineEdit(this);
        address->setObjectName("siteUrlInput");
        address->setAccessibleName(tr("URL сайта"));
        address->setPlaceholderText("https://www.roblox.com/");
        input->addWidget(address, 1);
        analyze = new QPushButton(tr("Анализировать и сохранить"), this);
        analyze->setObjectName("siteAnalyze");
        input->addWidget(analyze);
        layout->addLayout(input);
        auto *statusRow = new QHBoxLayout;
        feedback = new QLabel(tr("Сначала добавьте сайт. Сохранённые карточки появятся и в левой колонке."), this);
        feedback->setObjectName("siteFeedback");
        feedback->setWordWrap(true);
        feedback->setTextFormat(Qt::PlainText);
        statusRow->addWidget(feedback, 1);
        cancel = new QPushButton(tr("Отменить"), this);
        cancel->setObjectName("siteCancelScan");
        cancel->hide();
        statusRow->addWidget(cancel);
        layout->addLayout(statusRow);
        auto *split = new QSplitter(this);
        split->setObjectName("siteSplit");
        saved = new QListWidget(split);
        saved->setObjectName("siteSavedList");
        saved->setMinimumWidth(100);
        saved->setMaximumWidth(240);
        saved->setAccessibleName(tr("Сохранённые сайты"));
        auto *detail = new QWidget(split);
        auto *body = new QVBoxLayout(detail);
        body->setContentsMargins(10, 0, 0, 0);
        title = new QLabel(tr("Выберите сайт"), detail);
        title->setObjectName("siteTitle");
        title->setWordWrap(true);
        title->setTextFormat(Qt::PlainText);
        body->addWidget(title);
        description = new QLabel(detail);
        description->setObjectName("siteSummary");
        description->setWordWrap(true);
        description->setTextFormat(Qt::PlainText);
        body->addWidget(description);
        auto *controls = new QHBoxLayout;
        mode = new QComboBox(detail);
        mode->setObjectName("siteMode");
        mode->setAccessibleName(tr("Режим сайта и его зависимостей"));
        mode->addItem(tr("Авто"), "auto");
        mode->addItem(tr("Напрямую"), "direct");
        mode->addItem(tr("Через VPN"), "proxy");
        controls->addWidget(mode, 1);
        open = new QPushButton(tr("Открыть"), detail);
        open->setObjectName("siteOpen");
        controls->addWidget(open);
        body->addLayout(controls);
        auto *actions = new QHBoxLayout;
        rescan = new QPushButton(tr("Перепроверить"), detail);
        rescan->setObjectName("siteRescan");
        remove = new QPushButton(tr("Удалить"), detail);
        remove->setObjectName("siteRemove");
        actions->addWidget(rescan);
        actions->addWidget(remove);
        actions->addStretch();
        body->addLayout(actions);
        route = new QLabel(detail);
        route->setObjectName("siteLiveRoute");
        route->setWordWrap(true);
        route->setTextFormat(Qt::PlainText);
        body->addWidget(route);
        auto *tabs = new QTabWidget(detail);
        tabs->setObjectName("siteDetailsTabs");
        dependencies = new QTableWidget(0, 4, tabs);
        dependencies->setObjectName("siteDependencies");
        dependencies->setHorizontalHeaderLabels({tr("Адрес"), tr("Напрямую"), tr("VPN"), tr("Выбор набора")});
        dependencies->setEditTriggers(QAbstractItemView::NoEditTriggers);
        dependencies->setSelectionBehavior(QAbstractItemView::SelectRows);
        dependencies->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        dependencies->horizontalHeader()->setStretchLastSection(true);
        dependencies->setMinimumWidth(0);
        tabs->addTab(dependencies, tr("Зависимости"));
        auto *historyPage = new QWidget(tabs);
        auto *historyLayout = new QVBoxLayout(historyPage);
        history = new QListWidget(historyPage);
        history->setObjectName("siteHistory");
        history->setWordWrap(true);
        historyLayout->addWidget(history);
        restore = new QPushButton(tr("Выбрать режим из истории"), historyPage);
        restore->setObjectName("siteRestoreMode");
        historyLayout->addWidget(restore);
        tabs->addTab(historyPage, tr("История"));
        body->addWidget(tabs, 1);
        split->setStretchFactor(1, 1);
        layout->addWidget(split, 1);
        auto *note = new QLabel(tr("Авто перепроверяет доступность для новых соединений и помнит результат отдельно для сети и профиля. Найдены запросы наблюдаемой публичной загрузки; запросы после входа, действий и отложенных событий могут отличаться. Общие зависимости разных карточек используют VPN при конфликте режимов. Явные правила программ, сайтов/IP и рабочие VPN сохраняют приоритет."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *advanced = new QPushButton(tr("Расширенные списки доменов…"), this);
        advanced->setObjectName("siteAdvancedLists");
        layout->addWidget(advanced, 0, Qt::AlignLeft);
        connect(analyze, &QPushButton::clicked, this, [this] { emit analyzeRequested(address->text()); });
        connect(address, &QLineEdit::returnPressed, analyze, &QPushButton::click);
        connect(cancel, &QPushButton::clicked, this, &SitesPage::cancelRequested);
        connect(saved, &QListWidget::currentRowChanged, this, [this](int row) {selected=row>=0?saved->item(row)->data(Qt::UserRole).toString():QString();render(); });
        connect(mode, QOverload<int>::of(&QComboBox::activated), this, [this](int) {if(!selected.isEmpty())emit modeRequested(selected,mode->currentData().toString()); });
        connect(rescan, &QPushButton::clicked, this, [this] {if(!selected.isEmpty())emit rescanRequested(selected); });
        connect(remove, &QPushButton::clicked, this, [this] {if(!selected.isEmpty())emit removeRequested(selected); });
        connect(restore, &QPushButton::clicked, this, [this] {if(history->currentItem()&&!selected.isEmpty())emit modeRequested(selected,history->currentItem()->data(Qt::UserRole).toString()); });
        connect(open, &QPushButton::clicked, this, [this] {for(const auto &value:library.value("sites").toArray())if(value.toObject().value("id")==selected)emit openRequested(value.toObject().value("url").toString()); });
        connect(advanced, &QPushButton::clicked, this, &SitesPage::advancedRequested);
        setLibrary(SiteServices::empty());
    }
    void SitesPage::setFeedback(const QString &text) { feedback->setText(text.left(1000)); }
    void SitesPage::setBusy(bool value, const QString &phase) {
        busy = value;
        analyze->setEnabled(!value);
        cancel->setVisible(value);
        rescan->setEnabled(!value && !selected.isEmpty());
        remove->setEnabled(!value && !selected.isEmpty());
        if (value) setFeedback(phase);
    }
    void SitesPage::setLibrary(const QJsonObject &value) {
        library = value;
        QSignalBlocker block(saved);
        saved->clear();
        int row = -1;
        for (const auto &entry: library.value("sites").toArray()) {
            const auto site = entry.toObject();
            auto *item = new QListWidgetItem(site.value("title").toString() + "\n" + modeText(site.value("mode").toString()), saved);
            item->setData(Qt::UserRole, site.value("id"));
            item->setToolTip(site.value("url").toString());
            if (site.value("id") == selected) row = saved->count() - 1;
        }
        if (row < 0 && saved->count() > 0) row = 0;
        saved->setCurrentRow(row);
        selected = row >= 0 ? saved->item(row)->data(Qt::UserRole).toString() : QString();
        render();
    }
    void SitesPage::setRuntime(const QJsonObject &value) {
        runtime = value;
        render();
    }
    void SitesPage::selectSite(const QString &id) {
        for (int row = 0; row < saved->count(); ++row)
            if (saved->item(row)->data(Qt::UserRole) == id) {
                saved->setCurrentRow(row);
                selected = id;
                render();
                return;
            }
    }
    void SitesPage::render() {
        QJsonObject site;
        for (const auto &value: library.value("sites").toArray())
            if (value.toObject().value("id") == selected) site = value.toObject();
        const bool exists = !site.isEmpty();
        for (auto *button: {rescan, remove}) button->setEnabled(exists && !busy);
        mode->setEnabled(exists);
        open->setEnabled(exists);
        restore->setEnabled(exists);
        history->clear();
        dependencies->setRowCount(0);
        if (!exists) {
            title->setText(tr("Выберите сайт или добавьте URL сверху"));
            description->clear();
            route->clear();
            return;
        }
        title->setText(site.value("title").toString());
        description->setText(site.value("url").toString() + tr(" · %1 адресов").arg(site.value("endpoints").toArray().size()));
        {
            QSignalBlocker blocker(mode);
            mode->setCurrentIndex(mode->findData(site.value("mode").toString()));
        }
        if (!site.value("enabled").toBool())
            route->setText(tr("Загрузка пока не подтверждена; карточка сохранена без изменения маршрутов. Режим можно выбрать вручную."));
        else if (runtime.isEmpty() || runtime.value("pending").toBool())
            route->setText(tr("Сохранено, но пока не применено. Требуется подключённый профиль с полной новой сборкой."));
        else
            route->setText(tr("%1 · применяется к новым соединениям; открытые соединения сохраняют путь.").arg(modeText(site.value("mode").toString())));
        const auto recommendation = site.value("last_scan").toObject().value("recommendation").toString();
        if (!recommendation.isEmpty() && recommendation != "unknown") description->setText(description->text() + tr(" · по последнему анализу: %1").arg(recommendation == "mixed" ? tr("разные пути для зависимостей") : modeText(recommendation)));
        QMap<QString, QJsonObject> observations;
        for (const auto &value: site.value("last_scan").toObject().value("endpoints").toArray()) observations[value.toObject().value("origin").toString()] = value.toObject();
        const auto live = runtime.value("entries").toObject();
        const auto effective = runtime.value("effective").toObject();
        for (const auto &value: site.value("endpoints").toArray()) {
            const auto origin = value.toString();
            const auto sample = observations.value(origin);
            const auto entry = live.value("tcp:" + origin).toObject();
            const auto resolved = effective.value(origin).toObject();
            const auto actualMode = resolved.value("mode").toString(site.value("mode").toString());
            QString chosen;
            if (runtime.isEmpty() || runtime.value("pending").toBool() || !site.value("enabled").toBool())
                chosen = tr("Не применено");
            else if (actualMode != "auto")
                chosen = modeText(actualMode) + (actualMode != site.value("mode").toString() ? tr(" · общая зависимость") : QString());
            else if (!entry.isEmpty())
                chosen = modeText(entry.value("path").toString()) + (entry.value("verified").toBool() ? QString() : tr(" · не подтверждено"));
            else
                chosen = tr("Ожидает обращения");
            const int row = dependencies->rowCount();
            dependencies->insertRow(row);
            QStringList values{origin, sampleText(sample.value("direct").toObject()), sampleText(sample.value("proxy").toObject()), chosen};
            for (int col = 0; col < 4; ++col) dependencies->setItem(row, col, new QTableWidgetItem(values.at(col)));
        }
        QList<QJsonObject> events;
        for (const auto &value: site.value("history").toArray()) events.append(value.toObject());
        for (const auto &value: runtime.value("history").toArray())
            if (value.toObject().value("site_id") == selected) events.append(value.toObject());
        std::sort(events.begin(), events.end(), [](const QJsonObject &a, const QJsonObject &b) { return a.value("at").toString() > b.value("at").toString(); });
        for (const auto &event: events) {
            QString summary = event.value("summary").toString();
            if (summary.isEmpty()) summary = event.value("origin").toString() + " · " + modeText(event.value("from").toString()) + " → " + modeText(event.value("mode").toString());
            auto *item = new QListWidgetItem(timeText(event.value("at").toString()) + " · " + summary, history);
            item->setData(Qt::UserRole, event.value("mode"));
            if (history->count() >= 50) break;
        }
    }
} // namespace GreenRhythm
