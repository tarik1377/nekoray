/**
 * Исполнить готовый файл автонастройки прокси и сверить решения.
 *
 * ЗАЧЕМ ОТДЕЛЬНО ОТ pac_test. Тот проверяет ТЕКСТ: что попало в списки, что
 * выброшено, в каком порядке стоят проверки. На вопрос «а этот файл вообще
 * разбирается и что он отвечает» разбор текста не отвечает никак — а именно
 * здесь и живёт настоящая цена ошибки. Неверный PAC не отказывает: система
 * молча начинает решать иначе, чем мы думаем, и человек видит «интернет
 * работает», пока половина запросов идёт не туда.
 *
 * Движок здесь настоящий (node), а недостающие функции — isInNet, dnsResolve и
 * прочие — подставляются так же, как их даёт macOS. Сеть не трогается: имена
 * разрешаются по заранее заданной таблице, иначе набор зависел бы от того, что
 * сегодня отвечает DNS.
 *
 * Запуск:
 *     ninja pac_test && ./pac_test          # он пишет pac-sample.pac
 *     node support/Test-Pac.js pac-sample.pac
 */

const fs = require("fs");
const vm = require("vm");

const file = process.argv[2];
if (!file) {
  console.error("укажите файл: node support/Test-Pac.js <файл.pac>");
  process.exit(2);
}

/* ---------------- Окружение, которое даёт система ---------------- */

/** Разбор «a.b.c.d» в число. Возвращает null, если это не адрес IPv4. */
function toLong(ip) {
  const m = /^(\d+)\.(\d+)\.(\d+)\.(\d+)$/.exec(String(ip));
  if (!m) return null;
  let n = 0;
  for (let i = 1; i <= 4; i++) {
    const part = Number(m[i]);
    if (part > 255) return null;
    n = n * 256 + part;
  }
  return n;
}

/**
 * Имена, которые «разрешаются». Таблица, а не сеть: набор обязан давать один и
 * тот же ответ на машине без интернета и через год.
 */
const HOSTS = {
  "nas.local": "192.168.1.50",
  "printer.lan": "192.168.1.60",
  "example.com": "93.184.216.34",
  "bank.ru": "5.255.255.70",
};

const sandbox = {
  isInNet(host, pattern, mask) {
    const h = toLong(host);
    const p = toLong(pattern);
    const m = toLong(mask);
    if (h === null || p === null || m === null) return false;
    // Побитовое И на 32 битах: в JS оно даёт знаковое число, поэтому >>> 0.
    return ((h & m) >>> 0) === ((p & m) >>> 0);
  },
  dnsResolve(host) {
    return HOSTS[host] || null;
  },
  shExpMatch() {
    return false;
  },
  myIpAddress() {
    return "192.168.1.10";
  },
  console,
};

vm.createContext(sandbox);
try {
  vm.runInContext(fs.readFileSync(file, "utf8"), sandbox, { filename: file });
} catch (e) {
  console.error("файл не разбирается:", e.message);
  process.exit(1);
}

if (typeof sandbox.FindProxyForURL !== "function") {
  console.error("в файле нет FindProxyForURL");
  process.exit(1);
}

/* ---------------- Чего мы от него ждём ---------------- */

const VIA = "SOCKS5 127.0.0.1:2080; SOCKS 127.0.0.1:2080; DIRECT";

const cases = [
  // Пункт первый и главный: местное всегда мимо канала. Ради него весь режим и
  // построен на файле, а не на «один адрес на всё».
  ["http://localhost/", "localhost", "DIRECT", "localhost напрямую"],
  ["http://127.0.0.1/", "127.0.0.1", "DIRECT", "петля напрямую"],
  ["http://nas.local/", "nas.local", "DIRECT", "домашний NAS по имени .local"],
  ["http://printer.lan/", "printer.lan", "DIRECT", "принтер по имени .lan"],
  ["http://router.home.arpa/", "router.home.arpa", "DIRECT", "роутер по .home.arpa"],
  ["http://nas/", "nas", "DIRECT", "имя без точки — своё"],
  ["http://192.168.1.50/", "192.168.1.50", "DIRECT", "домашняя сеть по адресу"],
  ["http://10.1.2.3/", "10.1.2.3", "DIRECT", "сеть 10/8"],
  ["http://172.16.5.5/", "172.16.5.5", "DIRECT", "сеть 172.16/12"],
  ["http://169.254.1.1/", "169.254.1.1", "DIRECT", "link-local"],
  // Tailscale живёт здесь. Без этой строки домашняя сеть человека уезжает в
  // канал ровно в момент включения прокси.
  ["http://100.100.1.1/", "100.100.1.1", "DIRECT", "CGNAT — сеть Tailscale"],

  // Граница 172.16/12: 172.32 в неё НЕ входит, и ошибка в маске видна только
  // здесь. Проверка на то, что маска посчитана, а не списана на глаз.
  ["http://172.32.0.1/", "172.32.0.1", VIA, "172.32 — уже не частная сеть"],

  // Названное человеком.
  ["http://example.com/", "example.com", VIA, "домен через канал"],
  ["http://a.example.com/", "a.example.com", VIA, "и его поддомен"],
  ["http://bank.ru/", "bank.ru", "DIRECT", "домен мимо канала"],
  ["http://ads.example.net/", "ads.example.net", "PROXY 0.0.0.0:0", "блокировка — в тупик"],
  ["http://203.0.113.7/", "203.0.113.7", VIA, "сеть через канал"],
  ["http://198.51.100.7/", "198.51.100.7", "DIRECT", "сеть мимо канала"],

  // Хвост сравнивается по границе точки, а не по подстроке: notexample.com
  // заканчивается на «example.com», но им не является.
  ["http://notexample.com/", "notexample.com", VIA, "похожее имя — по общему правилу"],

  // Всё прочее.
  ["https://ya.ru/", "ya.ru", VIA, "незнакомое имя — в канал"],
];

let ok = 0;
let bad = 0;
for (const [url, host, want, what] of cases) {
  let got;
  try {
    got = sandbox.FindProxyForURL(url, host);
  } catch (e) {
    got = "исключение: " + e.message;
  }
  if (got === want) {
    ok++;
    console.log("  ок    " + what);
  } else {
    bad++;
    console.log("  ПЛОХО " + what + " — вышло: " + got + ", ждали: " + want);
  }
}

console.log("");
console.log(`проверок: ${ok + bad}, провалов: ${bad}`);
process.exit(bad > 0 ? 1 : 0);
