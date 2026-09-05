#include "dialog_relay_activate.h"
#include "ui/DialogPolish.hpp"
#include "ui_dialog_relay_activate.h"

#include "main/DeviceCredentials.hpp"
#include "main/NekoGui_Utils.hpp"
#include "main/RelayActivation.hpp"
#include "main/RelayComponent.hpp"

#include <QDesktopServices>
#include <QFontDatabase>
#include <QMessageBox>
#include <QUrl>

#include <QGraphicsOpacityEffect>

namespace {
    /** Приглушить, не трогая цвет: он принадлежит теме, а тем несколько. */
    void dim(QWidget *w, qreal amount) {
        auto *fade = new QGraphicsOpacityEffect(w);
        fade->setOpacity(amount);
        w->setGraphicsEffect(fade);
    }

    /**
     * Состояние — своими словами и с действием, а не кодом ответа.
     *
     * «402» человеку не говорит ничего; «подписка закончилась» плюс кнопка
     * «Продлить» отвечают и на «что случилось», и на «что делать». Это тот же
     * приём, что на Android, и заведён он там ровно потому, что поддержка
     * тонула в скриншотах с числами.
     */
    QString stateLine() {
        const auto detail = DeviceCredentials::StateDetail();
        switch (DeviceCredentials::CurrentState()) {
            case DeviceCredentials::Active:
                return QObject::tr("Подключено к вашей подписке");
            case DeviceCredentials::Expired:
                return detail.isEmpty() ? QObject::tr("Подписка закончилась") : detail;
            case DeviceCredentials::Limit:
                return detail.isEmpty() ? QObject::tr("Достигнут лимит устройств по тарифу") : detail;
            case DeviceCredentials::Closed:
                return detail.isEmpty() ? QObject::tr("Пока не открыто для вашего аккаунта") : detail;
            case DeviceCredentials::SignedOut:
            case DeviceCredentials::Unknown:
            default:
                return QObject::tr("Не активировано на этом устройстве");
        }
    }
} // namespace

DialogRelayActivate::DialogRelayActivate(QWidget *parent) : QDialog(parent), ui(new Ui::DialogRelayActivate) {
    ui->setupUi(this);
    GreenRhythm::polishDialog(this);
    dressUp();

    // Код на сайте — восемь знаков в верхнем регистре. Приводим на лету, чтобы
    // человек, вставивший его строчными, не получил «код не подошёл» от того,
    // с чем мы справились бы сами.
    connect(ui->code, &QLineEdit::textChanged, this, [this](const QString &t) {
        const auto tidy = t.trimmed().toUpper();
        if (tidy != t) {
            const int at = ui->code->cursorPosition();
            ui->code->setText(tidy);
            ui->code->setCursorPosition(at);
        }
    });
    connect(ui->code, &QLineEdit::returnPressed, ui->activate, &QPushButton::click);

    connect(ui->getCode, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(RelayActivation::ProfileUrl()));
    });

    connect(ui->activate, &QPushButton::clicked, this, [this] {
        if (busy) return;
        const auto code = ui->code->text().trimmed();

        /*
         * ОБНОВЛЕНИЕ БЕЗ КОДА — не удобство, а единственная дорога, по которой
         * до человека доезжают исправления в компоненте.
         *
         * Здесь стояло дополнительное условие «и компонента нет». Оно закрывало
         * ровно то, ради чего эта ветка нужна: у кого компонент УЖЕ стоит,
         * условие не выполнялось, пустой код упирался в «введите код», а код
         * одноразовый и давно потрачен. Сама Install обновление умеет — она
         * сверяет сумму и перекачивает при расхождении, — но позвать её было
         * неоткуда. То есть установленный компонент застревал навсегда на той
         * версии, с которой приехал, и следующая версия движка не досталась бы
         * никому, кроме тех, кто заново заводит устройство.
         *
         * Это второй раз в этом же окне, когда проверка «уже есть» отрезала
         * путь к тому, что она проверяет. Первый был со спрятанным пунктом меню.
         *
         * Стоит это дёшево: при совпадении суммы Install делает один короткий
         * запрос и возвращает alreadyCurrent, ничего не качая.
         */
        const bool refresh = code.isEmpty() && DeviceCredentials::IsProvisioned();

        if (code.isEmpty() && !refresh) {
            showResult(tr("Введите код из личного кабинета."), true);
            showAction(tr("Взять код"), RelayActivation::ProfileUrl());
            ui->code->setFocus();
            return;
        }

        setBusy(true);
        showResult(refresh ? tr("Проверяем…") : tr("Проверяем код…"), false);
        showAction({}, {});

        // В отдельном потоке: оба запроса блокирующие, а замерший на десять
        // секунд диалог человек считает зависшим и закрывает — посреди обмена
        // одноразового кода, который после этого уже потрачен.
        //
        // Задание заводится ЗДЕСЬ, до старта потока, и живёт в куче: поток не
        // должен держать указателя на диалог, который стоит на стеке
        // вызывающего и закрывается кнопкой прямо во время обмена. Подробно —
        // в шапке RelayActivateJob.
        auto *job = new RelayActivateJob();

        // Получателем указан диалог. Это и есть защита: связь с разрушенным
        // получателем Qt рвёт в его деструкторе, под мьютексом, и отбрасывает
        // уже поставленное событие. Проверять указатель самим бесполезно —
        // объект может умереть между проверкой и обращением.
        connect(job, &RelayActivateJob::done, this, [this, job] {
            const auto out = job->out;
            setBusy(false);
            repaintState();
            if (out.ok) {
                // Профиль заводится здесь же: активация без него ничего не
                // даёт — человек увидел бы «готово» и не нашёл, что нажать.
                const int id = RelayActivation::EnsureProfile();
                if (id >= 0) profileAdded = true;
                ui->code->clear();
                showAction({}, {});

                if (id < 0) {
                    showResult(tr("Готово, но профиль завести не удалось — добавьте его вручную."), true);
                    return;
                }
                // Компонент — часть готовности, а не отдельная новость. Без
                // него профиль в списке есть, а подключиться нечем, и человек
                // узнает об этом только нажав «Подключиться». Поэтому исход
                // закачки говорится ЗДЕСЬ, вместе с остальным.
                if (job->comp.ok) {
                    // «Уже последняя» — отдельная фраза. Общее «готово» на
                    // проверке обновлений читается как «ничего не произошло», и
                    // человек жмёт ещё раз, думая, что не сработало.
                    showResult(job->comp.alreadyCurrent
                                   ? tr("Всё на месте, установлена последняя версия.")
                                   : tr("Готово. Резервное подключение добавлено в список."),
                               false);
                    return;
                }
                showResult(tr("Подписка подтверждена, профиль добавлен.") + "\n" +
                               job->comp.detail + "\n" +
                               tr("Нажмите «Загрузить компонент», чтобы повторить."),
                           true);
                return;
            }
            showResult(out.detail, true);
            showAction(out.actionText, out.actionUrl);
        });

        // Порядок подписок значим: обе очереди складываются в одну и разбираются
        // по порядку, поэтому обработчик выше отработает раньше самоудаления. И
        // это единственная подписка, которая обязана сработать ВСЕГДА, — иначе
        // закрытый диалог оставлял бы задание в памяти навсегда.
        connect(job, &RelayActivateJob::done, job, &QObject::deleteLater);

        // Счётчик закачки. Подписка на диалог — по той же причине, что и done:
        // закрытое окно перестаёт получать, а не получает мусор.
        connect(job, &RelayActivateJob::progress, this, [this](qint64 got, qint64 total) {
            if (total <= 0) return;
            showResult(tr("Загрузка компонента: %1%").arg(got * 100 / total), false);
        });

        runOnNewThread([job, code, refresh] {
            // При обновлении обмен кода пропускается: код одноразовый и уже
            // потрачен, а реквизиты на диске. Реквизиты всё равно перезапрашиваются —
            // подписка могла кончиться с прошлого раза, и узнать это лучше
            // здесь, чем при попытке подключиться.
            job->out = refresh ? RelayActivation::Provision() : RelayActivation::Redeem(code);
            if (job->out.ok && !refresh) job->out = RelayActivation::Provision();
            // Компонент — только после выданных реквизитов: сайт отдаёт его по
            // той же подписке, и без токена запрос обязан получить отказ.
            if (job->out.ok) {
                job->comp = RelayComponent::Install([job](qint64 got, qint64 total) {
                    emit job->progress(got, total);
                });
            }
            // Сигнал из чужого потока — Qt сам поставит вызов в очередь того
            // потока, где задание заведено. Если диалога уже нет, здесь просто
            // никого не окажется на другом конце.
            emit job->done();
        });
    });

    connect(ui->forget, &QPushButton::clicked, this, [this] {
        if (busy) return;
        // Переспрашиваем: реквизиты после этого придётся получать новым кодом,
        // а обратной кнопки нет.
        QMessageBox ask(QMessageBox::Question, tr("Отключить резервное подключение"),
                        tr("Ключи этого устройства будут забыты. Чтобы включить снова, "
                           "понадобится новый код из личного кабинета.\n\nОтключить?"),
                        QMessageBox::NoButton, this);
        auto *go = ask.addButton(tr("Отключить"), QMessageBox::AcceptRole);
        ask.addButton(tr("Отмена"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() != go) return;

        const bool gone = RelayActivation::Forget();
        repaintState();
        // Если стереть не вышло, сказать об этом обязательно: человек ушёл бы с
        // уверенностью, что ключей на устройстве больше нет, и проверить это
        // ему нечем.
        showResult(gone ? tr("Отключено. Ключи этого устройства забыты.")
                        : tr("Не удалось стереть ключи — они остались на этом устройстве. "
                             "Попробуйте ещё раз."),
                   !gone);
        // Кнопку НЕ гасим: repaintState выше уже поставила «Взять код», и это
        // ровно следующий шаг для того, кто только что отключился и захочет
        // включить обратно. Прежде она здесь безусловно стиралась.
    });

    connect(ui->close, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->action, &QPushButton::clicked, this, [this] {
        if (!actionUrl.isEmpty()) QDesktopServices::openUrl(QUrl(actionUrl));
    });

    // Строка результата и кнопка действия при открытии пусты. Без этого вызова
    // кнопка оставалась ВИДИМОЙ и БЕЗ ТЕКСТА — пустая рамка рядом с пустой
    // строкой: в вёрстке она не скрыта, а showAction её прячет только при
    // пустом тексте, и до первого нажатия он туда не попадал ни разу.
    showResult({}, false);
    showAction({}, {});

    repaintState();
    ui->code->setFocus();
}

DialogRelayActivate::~DialogRelayActivate() { delete ui; }

/**
 * Иерархия — размером и начертанием, а не цветом.
 *
 * У экрана три уровня: СОСТОЯНИЕ (что сейчас), КОД (что сделать) и всё
 * остальное. В первой версии они весили одинаково, экран читался как анкета, и
 * глазу не за что было зацепиться.
 *
 * Размеры считаются ОТ ШРИФТА ПРИЛОЖЕНИЯ, а не задаются числом: человек мог
 * увеличить системный шрифт, и жёсткие пункты сломали бы ему всю разметку.
 * Цвета не трогаются вовсе — их задаёт тема, а тем в приложении несколько.
 * Приглушённое берётся из палитры (mid), она есть у любой темы.
 */
void DialogRelayActivate::dressUp() {
    const auto base = font();

    QFont big = base;
    big.setPointSize(base.pointSize() + 3);
    big.setBold(true);
    ui->state->setFont(big);

    QFont small = base;
    small.setPointSize(qMax(base.pointSize() - 1, 7));
    ui->hint->setFont(small);
    ui->footnote->setFont(small);
    // ПРИГЛУШЕНИЕ — ПРОЗРАЧНОСТЬЮ, А НЕ ЦВЕТОМ. Первая версия ставила
    // color: palette(mid), и на тёмной теме это оказался почти чёрный: и
    // подсказка, и сноска стали нечитаемы. Ровно та же ошибка, что была со
    // ссылкой «Продлить», — палитра у каждой темы своя, и роль mid значит в
    // них разное. Прозрачность отсчитывается от того цвета, который тема уже
    // назначила тексту, поэтому работает и на тёмной, и на светлой.
    dim(ui->hint, 0.72);
    dim(ui->footnote, 0.62);

    // Поле кода — герой экрана: восемь знаков, которые человек переписывает
    // с сайта. Моноширинный с разрядкой, потому что читают его посимвольно и
    // сверяют глазами; в обычном шрифте O и 0 в такой задаче не различить.
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(base.pointSize() + 9);
    mono.setBold(true);
    mono.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    ui->code->setFont(mono);

    // Кнопка «получить код» — не действие экрана, а подсказка, куда идти.
    // Плоской её делать нечем (тема рисует все QPushButton одинаково), поэтому
    // хотя бы не даём ей соперничать размером с «Активировать».
    QFont link = small;
    ui->getCode->setFont(link);
}

void DialogRelayActivate::repaintState() {
    // Явно простой текст: строка собирается в том числе из сохранённого ответа
    // сайта (StateDetail), а формат по умолчанию — AutoText, то есть QLabel сам
    // решает считать её разметкой, стоит там появиться угловой скобке.
    ui->state->setTextFormat(Qt::PlainText);
    ui->state->setText(stateLine());

    /*
     * ДЕЙСТВИЕ ВОССТАНАВЛИВАЕТСЯ ПО СОХРАНЁННОМУ СОСТОЯНИЮ.
     *
     * Кнопка появлялась только как ответ на свежий запрос. Человек, у которого
     * подписка кончилась вчера, открывал окно, читал крупными буквами
     * «Подписка закончилась» — и не находил, чем это лечится: кнопки
     * «Продлить» не было, потому что никакого запроса он ещё не делал.
     *
     * Состояние мы храним, значит и действие к нему обязаны показать сразу.
     */
    switch (DeviceCredentials::CurrentState()) {
        case DeviceCredentials::Expired:
            showAction(tr("Продлить"), RelayActivation::ProfileUrl());
            break;
        case DeviceCredentials::SignedOut:
        case DeviceCredentials::Unknown:
            showAction(tr("Взять код"), RelayActivation::ProfileUrl());
            break;
        case DeviceCredentials::Limit:
            // Отключать чужое устройство отсюда нечем — это делается в
            // кабинете, туда и ведём.
            showAction(tr("Управлять устройствами"), RelayActivation::ProfileUrl());
            break;
        default:
            showAction({}, {});
            break;
    }

    const bool on = DeviceCredentials::IsProvisioned();
    ui->forget->setEnabled(on);
    // Ввод кода не прячется у активированного: перенести устройство на другой
    // аккаунт — обычное дело, и заставлять ради этого сначала «отключить»
    // значит требовать двух шагов там, где хватает одного.

    // Пока компонента нет, кнопка ГОВОРИТ о нём. Иначе единственная надпись —
    // «Активировать заново», и человек, у которого сорвалась закачка, читает её
    // как предложение начать всё сначала: искать новый код в кабинете ради
    // файла, который ему уже положен.
    // Три состояния, а не два. Третье — «есть всё, но могло выйти новое»: без
    // него у активированного оставалась одна надпись «Активировать заново»,
    // которая читается как «начать сначала», то есть искать новый код ради
    // проверки обновления. Код для этого не нужен и не должен быть нужен.
    const bool needsComponent = on && !RelayComponent::IsInstalled();
    ui->activate->setText(needsComponent  ? tr("Загрузить компонент")
                          : on            ? tr("Проверить обновления")
                                          : tr("Активировать"));
}

void DialogRelayActivate::setBusy(bool on) {
    busy = on;
    ui->activate->setEnabled(!on);
    ui->forget->setEnabled(!on && DeviceCredentials::IsProvisioned());
    ui->code->setEnabled(!on);
}

/**
 * Действие показывается КНОПКОЙ, а не ссылкой в тексте.
 *
 * Ссылка была первой попыткой, и на снимке вёрстки сразу стало видно, во что
 * она превращается: серый текст на тёмном фоне, неотличимый от выключенного, —
 * то есть человек с кончившейся подпиской не увидел бы, что продлить можно
 * отсюда. Цвет ссылки задаёт палитра, а тема у приложения своя и вдобавок
 * бывает светлой; полагаться на неё в единственном действии экрана нельзя.
 * Кнопку тема оформляет сама, и выглядит она действием, потому что действие и
 * есть.
 */
void DialogRelayActivate::showAction(const QString &text, const QString &url) {
    actionUrl = url;
    ui->action->setText(text);
    ui->action->setVisible(!text.isEmpty() && !url.isEmpty());
}

/**
 * ТЕКСТ САЙТА НЕ ПОПАДАЕТ В РАЗБОР HTML — начертание задаётся шрифтом.
 *
 * Здесь стояло setTextFormat(Qt::RichText) и <b>%1</b> с подстановкой строки,
 * пришедшей от сайта побайтно (поле error/message/detail). Начертание так
 * получалось, но вместе с ним QLabel начинал читать содержимое как разметку.
 *
 * Ежедневный вред — не подмена, а ПОТЕРЯ ПОЛОВИНЫ ЗАКОННОГО СООБЩЕНИЯ. Ответ
 * «слот занят <ноутбук на работе>, освободите его» человек читает как «слот
 * занят , освободите его»: угловые скобки съедены как несуществующий тег.
 * Именно ту часть, ради которой сообщение и писали, — какое устройство занимает
 * место, — он не увидит никогда, и ни одна строка в журнале об этом не скажет.
 *
 * Экранирование починило бы это тоже, но лишним оказался сам переход на
 * разметку: замысел, записанный ниже, — выделять отказ НАЧЕРТАНИЕМ, а
 * начертание даёт шрифт. В PlainText вопрос экранирования просто не возникает.
 */
void DialogRelayActivate::showResult(const QString &text, bool bad) {
    ui->result->setTextFormat(Qt::PlainText);
    // ОТКАЗ ВЫДЕЛЯЕТСЯ НАЧЕРТАНИЕМ, А НЕ ЦВЕТОМ, и это не вкусовщина.
    //
    // Своего цвета для ошибки тема проекта не определяет (modern.css знает
    // текст #e4e6eb, приглушённый #9aa0a8 и зелёный акцент #3fb950), а жёсткий
    // #RRGGBB подошёл бы только к одной из тем: приложение умеет и светлую.
    // Первый вариант брал palette(link-visited) — на тёмной теме получился
    // серый, неотличимый от выключенного, и ссылка «Продлить» рядом читалась
    // как недоступная. Полужирный работает в обеих темах и ничего не обещает
    // про палитру.
    QFont f = ui->result->font();
    f.setBold(bad);
    ui->result->setFont(f);
    ui->result->setText(text);
}
