#pragma once
#include <QWidget>
#include <QJsonObject>
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QPushButton;
namespace GreenRhythm {
    class SitesPage final : public QWidget {
        Q_OBJECT
    public:
        explicit SitesPage(QWidget *parent = nullptr);
        ~SitesPage() override { emit aboutToClose(); }
        void setLibrary(const QJsonObject &value);
        void setRuntime(const QJsonObject &value);
        void selectSite(const QString &id);
        void setBusy(bool busy, const QString &phase = {});
        void setFeedback(const QString &text);
        QString selectedID() const { return selected; }
    signals:
        void aboutToClose();
        void analyzeRequested(const QString &url);
        void cancelRequested();
        void modeRequested(const QString &id, const QString &mode);
        void rescanRequested(const QString &id);
        void removeRequested(const QString &id);
        void advancedRequested();
        void openRequested(const QString &url);

    private:
        void render();
        QJsonObject library, runtime;
        QString selected;
        bool busy = false;
        QLineEdit *address;
        QPushButton *analyze, *cancel, *rescan, *remove, *restore, *open;
        QLabel *feedback, *title, *description, *route;
        QListWidget *saved, *history;
        QComboBox *mode;
        QTableWidget *dependencies;
    };
} // namespace GreenRhythm
