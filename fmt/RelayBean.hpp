#pragma once

#include "fmt/AbstractBean.hpp"

namespace NekoGui_fmt {
    /**
     * Имя файла компонента рядом с программой — оно же идентификатор ядра.
     *
     * ExtraCore::Get(id) ищет id в настройках, а не найдя — берёт файл с этим
     * именем из каталога программы. Имя нейтральное намеренно: оно видно в
     * диспетчере задач и в списке файлов, и «relay» или «s3» рассказали бы про
     * устройство канала тому, кто просто смотрит на папку.
     */
    inline const char *const kRelayCoreId = "grlink";

    /**
     * Резервное подключение.
     *
     * ПРОФИЛЬ — ВКЛЮЧАТЕЛЬ, А НЕ НОСИТЕЛЬ ДОСТУПА. Здесь нет и не должно
     * появиться ни одного выданного поля: ни адреса, ни ключей, ни тега
     * устройства. Всё это живёт в запечатанном device.dat
     * (main/DeviceCredentials.hpp), а profiles/*.json пишутся открытым текстом
     * и открываются пунктом меню «Открыть папку конфигурации» — человек,
     * приславший в поддержку «папку с настройками», прислал бы и ключи.
     *
     * Отсюда же следует, что профиль не переносится между устройствами и не
     * представим ссылкой: резервом его делают реквизиты, выданные ЭТОМУ
     * устройству, а не содержимое строки.
     */
    class RelayBean : public AbstractBean {
    public:
        /**
         * Российские адреса — напрямую, мимо канала.
         *
         * По умолчанию включено: канал платный по трафику, а сайты, которые и
         * так открываются, гонять через него — значит тратить деньги человека
         * на то, что ему не нужно.
         */
        bool bypass_ru = true;

        /**
         * Чем разрешать имена. Пусто — движок возьмёт публичные сам.
         *
         * Поле оставлено потому, что у части людей провайдерский DNS отвечает
         * подменой, и это единственный способ им помочь, не заводя настройку
         * на каждый случай.
         */
        QString dns_servers = "";

        /**
         * Порт локального SOCKS движка. 0 — выдать свободный.
         *
         * Закрепляют его редко и по делу: когда рядом стоит программа, которой
         * порт прописан руками. Обычному человеку трогать нечего.
         */
        int socks_port = 0;

        RelayBean() : AbstractBean(0) {
            _add(new configItem("bypass_ru", &bypass_ru, itemType::boolean));
            _add(new configItem("dns_servers", &dns_servers, itemType::string));
            _add(new configItem("socks_port", &socks_port, itemType::integer));
        };

        // «Резерв», а не имя бинаря и не название транспорта: в списке профилей
        // эта колонка видна всем, кто смотрит на экран через плечо.
        QString DisplayCoreType() override { return QObject::tr("Резерв"); };

        QString DisplayType() override { return QObject::tr("Резерв"); };

        // Адреса у резерва нет. Пустая колонка честнее, чем 127.0.0.1: последнее
        // человек прочитает как «подключение к своему компьютеру».
        [[nodiscard]] QString DisplayAddress() override { return {}; };

        [[nodiscard]] QString DisplayName() override;

        int NeedExternal(bool isFirstProfile) override;

        ExternalBuildResult BuildExternal(int mapping_port, int socks_port, int external_stat) override;

        // Ссылкой не представим — см. шапку. Пустая строка прячет «Поделиться»
        // и QR там, где делиться нечем.
        QString ToShareLink() override { return {}; };
    };
} // namespace NekoGui_fmt
