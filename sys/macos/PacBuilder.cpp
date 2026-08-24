#include "PacBuilder.hpp"

namespace NekoGui_sys {

    namespace {

/** Тело файла автонастройки — от списков не зависит и потому вынесено. */
static const char *const kPacBody = R"JS(
function tail(h, s) {
  if (h === s) return true;
  return h.length > s.length && h.charAt(h.length - s.length - 1) === "." &&
         h.substring(h.length - s.length) === s;
}
function any(h, list) {
  for (var i = 0; i < list.length; i++) if (tail(h, list[i])) return true;
  return false;
}
function maskOf(bits) {
  var m = [];
  for (var i = 0; i < 4; i++) {
    var n = bits >= 8 ? 8 : (bits > 0 ? bits : 0);
    m.push(256 - Math.pow(2, 8 - n));
    bits -= n;
  }
  return m.join(".");
}
function net(ip, rule) {
  var p = rule.split("/");
  return isInNet(ip, p[0], maskOf(p.length > 1 ? parseInt(p[1], 10) : 32));
}
function anyNet(ip, list) {
  if (!ip) return false;
  for (var i = 0; i < list.length; i++) if (net(ip, list[i])) return true;
  return false;
}

function FindProxyForURL(url, host) {
  var h = host.toLowerCase();

  // 1. МЕСТНОЕ — ВСЕГДА НАПРЯМУЮ, и это не обходится ничем ниже.
  //    Ради этого пункта режим построен на файле, а не на «один адрес на всё»:
  //    домашний NAS, принтер и рабочая сеть обязаны остаться доступны, что бы
  //    ни было написано в остальных правилах.
  if (h === "localhost" || h === "127.0.0.1" || h === "::1") return "DIRECT";
  if (h.indexOf(".") < 0) return "DIRECT";
  if (tail(h, "local") || tail(h, "lan") || tail(h, "home.arpa") ||
      tail(h, "internal")) return "DIRECT";

  var ip = null;
  if (/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/.test(h)) ip = h;
  if (ip) {
    if (isInNet(ip, "10.0.0.0", "255.0.0.0")) return "DIRECT";
    if (isInNet(ip, "172.16.0.0", "255.240.0.0")) return "DIRECT";
    if (isInNet(ip, "192.168.0.0", "255.255.0.0")) return "DIRECT";
    if (isInNet(ip, "169.254.0.0", "255.255.0.0")) return "DIRECT";
    // CGNAT: по нему живёт Tailscale, и без этой строки домашняя сеть человека
    // уезжает в канал ровно в момент включения прокси.
    if (isInNet(ip, "100.64.0.0", "255.192.0.0")) return "DIRECT";
  }

  // 2. Названо человеком явно как «через канал» — раньше, чем «мимо».
  if (any(h, P) || anyNet(ip, PI)) return VIA;

  // 3. Блокировка. В PAC нет «отказать», поэтому единственная честная
  //    подстановка — тупик: соединение не состоится, и это видно сразу.
  if (any(h, B)) return "PROXY 0.0.0.0:0";

  // 4. Мимо канала.
  if (any(h, D) || anyNet(ip, DI)) return "DIRECT";

  // 5. Всё остальное.
  return VIA;
}
)JS";

        /** Строки правил, как их вводит человек: по одной, решётка — заметка. */
        QStringList lines(const QString &text) {
            QStringList out;
            for (const auto &raw: text.split('\n')) {
                const auto s = raw.trimmed();
                if (s.isEmpty() || s.startsWith('#')) continue;
                out << s;
            }
            return out;
        }

        /**
         * Невыразимо ли правило в PAC.
         *
         * geosite/geoip — списки на десятки тысяч записей, живут в ядре.
         * regexp — в PAC нет функции для него, а самодельная проверка вела бы
         * себя иначе, чем настоящая, то есть отправляла бы часть трафика не
         * туда МОЛЧА. Лучше честно пропустить и сказать.
         */
        bool inexpressible(const QString &rule) {
            static const QStringList heads = {"geosite:", "geoip:", "regexp:", "ext:"};
            for (const auto &h: heads) {
                if (rule.startsWith(h, Qt::CaseInsensitive)) return true;
            }
            return false;
        }

        /** Строка в кавычках для JavaScript. Кавычки и слэши экранируются. */
        QString js(const QString &s) {
            QString out = s;
            out.replace('\\', "\\\\").replace('"', "\\\"");
            return "\"" + out + "\"";
        }

        /** Массив строк для JavaScript, либо пустой. */
        QString jsArray(const QStringList &items) {
            QStringList quoted;
            for (const auto &i: items) quoted << js(i);
            return "[" + quoted.join(",") + "]";
        }

        /** Разложить правила по доменам; невыразимое уходит в skipped. */
        QStringList collectDomains(const QString &text, QStringList *skipped) {
            QStringList out;
            for (const auto &rule: lines(text)) {
                if (inexpressible(rule)) {
                    if (skipped != nullptr) *skipped << rule;
                    continue;
                }
                // Приставки «domain:» и «full:» — форма записи ядра. Здесь обе
                // сводятся к сравнению по хвосту: разница между ними в PAC не
                // выражается, и притворяться, что выражается, хуже.
                QString r = rule;
                for (const auto &head: {QStringLiteral("domain:"), QStringLiteral("full:")}) {
                    if (r.startsWith(head, Qt::CaseInsensitive)) r = r.mid(head.size());
                }
                r = r.trimmed().toLower();
                if (!r.isEmpty()) out << r;
            }
            return out;
        }

        /**
         * Правила по адресам. Выразимы только сети IPv4 с маской.
         *
         * IPv6 в PAC не выражается вовсе: isInNet работает лишь с IPv4, а
         * заменителя нет. Такие правила пропускаются НАЗВАННЫМИ, а не тихо —
         * иначе поддержка будет искать причину «почему этот адрес пошёл не
         * туда» вслепую.
         */
        QStringList collectIps(const QString &text, QStringList *skipped) {
            QStringList out;
            for (const auto &rule: lines(text)) {
                if (inexpressible(rule) || rule.contains(':')) {
                    if (skipped != nullptr) *skipped << rule;
                    continue;
                }
                out << rule;
            }
            return out;
        }

    } // namespace

    QString BuildPac(const PacInput &in, PacNotes *notes) {
        QStringList skipped;

        const auto proxyDomains = collectDomains(in.proxyDomain, &skipped);
        const auto directDomains = collectDomains(in.directDomain, &skipped);
        const auto blockDomains = collectDomains(in.blockDomain, &skipped);
        const auto proxyIps = collectIps(in.proxyIp, &skipped);
        const auto directIps = collectIps(in.directIp, &skipped);

        if (notes != nullptr) notes->skipped = skipped;

        QString pac;
        pac += "// Файл автонастройки прокси. Собран приложением; правки не сохраняются.\n";
        pac += "var P = " + jsArray(proxyDomains) + ";\n";
        pac += "var D = " + jsArray(directDomains) + ";\n";
        pac += "var B = " + jsArray(blockDomains) + ";\n";
        pac += "var PI = " + jsArray(proxyIps) + ";\n";
        pac += "var DI = " + jsArray(directIps) + ";\n";
        pac += QStringLiteral("var VIA = \"SOCKS5 127.0.0.1:%1; SOCKS 127.0.0.1:%1; DIRECT\";\n")
                   .arg(in.socksPort);
        pac += kPacBody;
        return pac;
    }

} // namespace NekoGui_sys
