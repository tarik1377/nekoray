#pragma once

#include <atomic>

#include "main/NekoGui.hpp"
#include "ProxyEntity.hpp"

namespace NekoGui {
    class Group : public JsonStore {
    public:
        int id = -1;
        bool archive = false;
        bool skip_auto_update = false;
        QString name = "";
        QString url = "";
        QString info = "";
        qint64 sub_last_update = 0;
        int front_proxy_id = -1;

        // transient, NOT persisted (not registered as configItem): outcome of the
        // last subscription update. 0 unknown, 1 ok, 2 empty list / request failed.
        // atomic — written on the updater worker, read on the UI thread.
        std::atomic<int> last_update_outcome{0};

        // list ui
        bool manually_column_width = false;
        QList<int> column_width;
        QList<int> order;

        Group();

        // 按 id 顺序
        [[nodiscard]] QList<std::shared_ptr<ProxyEntity>> Profiles() const;

        // 按 显示 顺序
        [[nodiscard]] QList<std::shared_ptr<ProxyEntity>> ProfilesWithOrder() const;
    };
} // namespace NekoGui
