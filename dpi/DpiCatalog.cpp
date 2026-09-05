#include "dpi/DpiCatalog.hpp"

namespace GreenRhythm::Dpi {

    const QList<Strategy> &catalog() {
        static const QList<Strategy> list{
            Strategy{
                QStringLiteral("general"),
                QStringLiteral("Обычная"),
                QStringLiteral("Режет приветствие TLS на части и подкладывает перед первой частью кусок "
                               "чужого приветствия. Без подделок пакетов, без системных настроек."),
                QStringLiteral("Flowseal zapret-discord-youtube 1.10.1, general.bat, профиль Google "
                               "(--filter-tcp=443 --hostlist=list-google.txt), дословно"),
                {QStringLiteral("--dpi-desync=multisplit"),
                 QStringLiteral("--dpi-desync-split-seqovl=681"),
                 QStringLiteral("--dpi-desync-split-pos=1"),
                 QStringLiteral("--dpi-desync-split-seqovl-pattern={BIN}tls_clienthello_www_google_com.bin")},
                {QStringLiteral("--dpi-desync=fake"),
                 QStringLiteral("--dpi-desync-repeats=6"),
                 QStringLiteral("--dpi-desync-fake-quic={BIN}quic_initial_www_google_com.bin")},
                {QStringLiteral("tls_clienthello_www_google_com.bin"),
                 QStringLiteral("quic_initial_www_google_com.bin")},
                false,
            },
            Strategy{
                QStringLiteral("fake-tls-auto"),
                QStringLiteral("Подделка и перестановка"),
                QStringLiteral("Шлёт впереди поддельное приветствие с испорченным номером, а настоящее — "
                               "частями в перепутанном порядке. Заметнее для фильтра, но пробивает больше."),
                QStringLiteral("Flowseal zapret-discord-youtube 1.10.1, general (FAKE TLS AUTO).bat, "
                               "TCP-профиль; подделка — встроенная в winws, а не файл"),
                {QStringLiteral("--dpi-desync=fake,multidisorder"),
                 QStringLiteral("--dpi-desync-split-pos=1,midsld"),
                 QStringLiteral("--dpi-desync-repeats=11"),
                 QStringLiteral("--dpi-desync-fooling=badseq"),
                 QStringLiteral("--dpi-desync-fake-tls-mod=rnd,dupsid,sni=www.google.com")},
                {QStringLiteral("--dpi-desync=fake"),
                 QStringLiteral("--dpi-desync-repeats=6"),
                 QStringLiteral("--dpi-desync-fake-quic={BIN}quic_initial_www_google_com.bin")},
                {QStringLiteral("quic_initial_www_google_com.bin")},
                false,
            },
            Strategy{
                QStringLiteral("alt11-badseq"),
                QStringLiteral("Усиленная, без меток времени"),
                QStringLiteral("Связка из ALT11: подделка плюс нарезка с наложением, восемь повторов. "
                               "Вместо меток времени TCP — испорченный номер, чтобы не трогать "
                               "системную настройку. Сообществом в таком виде не проверена."),
                QStringLiteral("Flowseal 1.10.1, general (ALT11).bat, TCP-профиль, с заменой "
                               "--dpi-desync-fooling=ts на badseq и заготовок Flowseal на "
                               "заготовку из релиза bol-van"),
                {QStringLiteral("--dpi-desync=fake,multisplit"),
                 QStringLiteral("--dpi-desync-split-seqovl=681"),
                 QStringLiteral("--dpi-desync-split-pos=1"),
                 QStringLiteral("--dpi-desync-split-seqovl-pattern={BIN}tls_clienthello_www_google_com.bin"),
                 QStringLiteral("--dpi-desync-repeats=8"),
                 QStringLiteral("--dpi-desync-fooling=badseq"),
                 QStringLiteral("--dpi-desync-fake-tls={BIN}tls_clienthello_www_google_com.bin")},
                {QStringLiteral("--dpi-desync=fake"),
                 QStringLiteral("--dpi-desync-repeats=8"),
                 QStringLiteral("--dpi-desync-fake-quic={BIN}quic_initial_www_google_com.bin")},
                {QStringLiteral("tls_clienthello_www_google_com.bin"),
                 QStringLiteral("quic_initial_www_google_com.bin")},
                true,
            },
        };
        return list;
    }

    const Strategy *findStrategy(const QString &id) {
        for (const auto &s: catalog()) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }

    QStringList expandArgs(const QStringList &args, const QString &binDir) {
        // winws собран под cygwin и в путях ждёт прямые слэши; обратные он тоже
        // переваривает, но смешивать два вида в одной строке незачем.
        QString dir = binDir;
        dir.replace(QChar('\\'), QChar('/'));
        if (!dir.endsWith(QChar('/'))) dir += QChar('/');
        QStringList out;
        for (auto a: args) {
            a.replace(QStringLiteral("{BIN}"), dir);
            out << a;
        }
        return out;
    }

} // namespace GreenRhythm::Dpi
