# -*- coding: utf-8 -*-
"""
Значок приложения: один рисунок, два файла.

ЗАЧЕМ СКРИПТ. Значок легко положить в дерево двоичным файлом и забыть, но тогда
поменять в нём что-либо может только тот, у кого остался исходник. Здесь рисунок
описан кодом: цвета, пропорции и форма лепестка видны и правятся.

ПРОПОРЦИИ ВЗЯТЫ У APPLE и применяются к обоим файлам. Полотно 1024, скруглённый
квадрат занимает 80% и имеет радиус 22.4% своей стороны. Поля вокруг — не
потерянное место: macOS сама добавляет тень и рассчитывает на этот запас, а без
него значок выпирает из дока крупнее соседних.

Windows своих полей не добавляет, и раньше здесь стоял отдельный вариант почти
без отступа. От него отказались сознательно: два рисунка в одном продукте
однажды разъезжаются, а разница в паре процентов площади того не стоит.

Запуск (нужен Pillow):
    python tools/make_icons.py
"""

import io
import math
import struct
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CANVAS = 1024
SHAPE = int(CANVAS * 0.805)          # доля полотна под фигуру
RADIUS = int(SHAPE * 0.2237)         # радиус скругления от стороны фигуры
MARGIN = (CANVAS - SHAPE) // 2
SS = 4                               # сглаживание рисованием в увеличенном виде

TOP = (0x4d, 0xcd, 0x60)
BOTTOM = (0x23, 0x8b, 0x39)
CENTER = (0x2c, 0x9c, 0x46)
PETALS = 5

ICO_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def petal(length, width, steps=90):
    """Контур лепестка: расширяется от черешка и сходится в остриё.

    Считается кривой, а не берётся готовым овалом: у овала оба конца тупые, а
    лист узнаётся именно по острию.
    """
    pts = []
    for direction in (1, -1):
        rng = range(steps + 1) if direction == 1 else range(steps, -1, -1)
        for i in rng:
            t = i / steps
            w = width * math.sin(math.pi * t) ** 1.35 * (1.0 - 0.45 * t)
            pts.append((direction * w, -length * t))
    return pts


def draw() -> Image.Image:
    big = CANVAS * SS
    out = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    pen = ImageDraw.Draw(out)

    side = SHAPE * SS
    gradient = Image.new("RGBA", (1, side))
    gp = gradient.load()
    for y in range(side):
        t = y / (side - 1)
        gp[0, y] = tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,)
    gradient = gradient.resize((side, side))

    mask = Image.new("L", (side, side), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, side - 1, side - 1], RADIUS * SS, fill=255)
    shape = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    shape.paste(gradient, (0, 0), mask)
    out.paste(shape, (MARGIN * SS, MARGIN * SS), shape)

    cx = cy = big // 2
    base = petal(int(side * 0.30), int(side * 0.105))
    for k in range(PETALS):
        ang = math.radians(-90 + k * (360 / PETALS)) + math.pi / 2
        ca, sa = math.cos(ang), math.sin(ang)
        pen.polygon([(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in base],
                    fill=(255, 255, 255, 242))
    r = int(side * 0.032)
    pen.ellipse([cx - r, cy - r, cx + r, cy + r], fill=CENTER + (255,))

    return out.resize((CANVAS, CANVAS), Image.LANCZOS)


def _dib_frame(im: Image.Image) -> bytes:
    """Кадр значка в формате DIB.

    ЗАЧЕМ НЕ PNG. Pillow пишет все кадры .ico картинками PNG, и файл при этом
    получается меньше. Но разбирают такие кадры не все: старый способ загрузки
    значков в Windows и GDI+ (через него значок берут установщики и часть
    системных окон) на мелких размерах отдают вместо рисунка шум. Проверено
    здесь же: полоска 16-64 из PNG-кадров рисовалась мусором, из DIB — верно.
    Крупный кадр 256 остаётся PNG: там PNG понимают все, а выигрыш в размере
    заметный.
    """
    w, h = im.size
    rgba = im.convert("RGBA")

    # Высота в заголовке удвоена: следом за цветом идёт маска прозрачности.
    # Строки идут снизу вверх — отсюда переворот.
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    xor = rgba.transpose(Image.FLIP_TOP_BOTTOM).tobytes("raw", "BGRA")

    # Маска при 32 битах не используется, но обязана присутствовать и иметь
    # верный размер: строка выравнивается по четыре байта.
    stride = ((w + 31) // 32) * 4
    return header + xor + b"\x00" * (stride * h)


def write_ico(icon: Image.Image, path: str) -> None:
    frames = []
    for w, h in ICO_SIZES:
        im = icon.resize((w, h), Image.LANCZOS)
        if w >= 256:
            buf = io.BytesIO()
            im.save(buf, format="PNG")
            frames.append((w, h, buf.getvalue()))
        else:
            frames.append((w, h, _dib_frame(im)))

    offset = 6 + 16 * len(frames)
    entries = b""
    body = b""
    for w, h, blob in frames:
        # Ширина и высота — один байт; 256 записывается нулём, так условлено.
        entries += struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        body += blob

    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(frames)) + entries + body)


def main():
    icon = draw()

    png = os.path.join(ROOT, "res", "public", "greenrhythm.png")
    icon.save(png)

    # .ico собирается из того же изображения. Мелкие размеры уменьшаются здесь,
    # и важно, чтобы исходник был крупным: из 256 точек шестнадцать выходят
    # кашей, а из 1024 — читаемым знаком.
    ico = os.path.join(ROOT, "res", "greenrhythm.ico")
    write_ico(icon, ico)

    print("png:", png, icon.size)
    print("ico:", ico, "размеров:", len(ICO_SIZES))


if __name__ == "__main__":
    main()
