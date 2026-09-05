#include "ui/DialogPolish.hpp"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QWidget>

namespace GreenRhythm {

    namespace {
        void polishLayouts(QWidget *root) {
            // Корневая раскладка — поля диалога; вложенные — только промежутки.
            // Поля у вложенных давали бы лесенку отступов: 9 + 9 + 9 от края.
            if (auto *top = root->layout()) {
                top->setContentsMargins(20, 18, 20, 16);
            }
            for (auto *l: root->findChildren<QLayout *>()) {
                if (l == root->layout()) continue;
                l->setSpacing(10);
                if (auto *g = qobject_cast<QGridLayout *>(l)) {
                    g->setHorizontalSpacing(12);
                    g->setVerticalSpacing(10);
                } else if (auto *f = qobject_cast<QFormLayout *>(l)) {
                    f->setHorizontalSpacing(12);
                    f->setVerticalSpacing(10);
                    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                }
                // У коробки своё поле — внутри карточки текст не должен
                // прилипать к краю; у остальных вложенных полей нет.
                if (qobject_cast<QGroupBox *>(l->parentWidget()) != nullptr) {
                    l->setContentsMargins(14, 10, 14, 12);
                } else if (l->parentWidget() != root) {
                    l->setContentsMargins(0, 0, 0, 0);
                }
            }
        }

        void polishGroupBoxes(QWidget *root) {
            for (auto *g: root->findChildren<QGroupBox *>()) {
                // Заголовок — капителью, как подписи разделов в колонке главного
                // окна: «ОБЩИЕ», «ПОДПИСКА». Плоская коробка с заголовком в
                // разрыве рамки — это Windows 2000; карточка с подписью над ней
                // — то, как это выглядит сейчас. Саму карточку рисует тема.
                g->setTitle(g->title().toUpper());
                g->setFlat(false);
            }
        }

        void polishInputs(QWidget *root) {
            for (auto *e: root->findChildren<QLineEdit *>()) e->setMinimumHeight(32);
            for (auto *c: root->findChildren<QComboBox *>()) c->setMinimumHeight(32);
            for (auto *s: root->findChildren<QAbstractSpinBox *>()) s->setMinimumHeight(32);
            for (auto *b: root->findChildren<QPushButton *>()) {
                if (b->minimumHeight() < 32) b->setMinimumHeight(32);
            }
            for (auto *v: root->findChildren<QAbstractItemView *>()) {
                v->setFrameShape(QFrame::NoFrame);
            }
            if (auto *t = root->findChild<QTabWidget *>()) {
                t->setDocumentMode(true);
            }
        }

        void polishButtons(QWidget *root) {
            // Русские подписи стандартным кнопкам — здесь, а не только через
            // qtbase_ru.qm: перевод Qt в пакете может отсутствовать, а «OK» и
            // «Cancel» в русском окне видны всем и сразу. Главная — справа и
            // по умолчанию: Enter её нажимает, тема красит её акцентом.
            for (auto *box: root->findChildren<QDialogButtonBox *>()) {
                if (auto *ok = box->button(QDialogButtonBox::Ok)) {
                    ok->setText(QObject::tr("Готово"));
                    ok->setDefault(true);
                }
                if (auto *c = box->button(QDialogButtonBox::Cancel)) c->setText(QObject::tr("Отмена"));
                if (auto *s = box->button(QDialogButtonBox::Save)) s->setText(QObject::tr("Сохранить"));
                if (auto *a = box->button(QDialogButtonBox::Apply)) a->setText(QObject::tr("Применить"));
                if (auto *c = box->button(QDialogButtonBox::Close)) c->setText(QObject::tr("Закрыть"));
                if (auto *y = box->button(QDialogButtonBox::Yes)) y->setText(QObject::tr("Да"));
                if (auto *n = box->button(QDialogButtonBox::No)) n->setText(QObject::tr("Нет"));
                box->setCenterButtons(false);
            }
        }
    } // namespace

    void polishDialog(QWidget *root) {
        if (root == nullptr) return;
        polishLayouts(root);
        polishGroupBoxes(root);
        polishInputs(root);
        polishButtons(root);
        // Дать окну ВЫРАСТИ до нового содержимого — и только вырасти. Поля,
        // промежутки и высота полей ввода стали больше, а размер окна взят из
        // .ui; без этого строки в карточке ложились друг на друга. Сжимать
        // нельзя: у «Маршрутов» размер задан с запасом под списки намеренно.
        if (root->isWindow()) {
            const QSize want = root->sizeHint().expandedTo(root->minimumSizeHint());
            root->resize(root->size().expandedTo(want));
        }
    }

} // namespace GreenRhythm
