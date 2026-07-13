#pragma once

#include "fmt/AbstractBean.hpp"
#include "fmt/V2RayStreamSettings.hpp"

namespace NekoGui_fmt {
    class TrojanVLESSBean : public AbstractBean {
    public:
        static constexpr int proxy_Trojan = 0;
        static constexpr int proxy_VLESS = 1;
        int proxy_type = proxy_Trojan;

        QString password = "";
        QString flow = "";
        bool forceExternal = false; // dial this profile through the xray core instead of sing-box

        std::shared_ptr<V2rayStreamSettings> stream = std::make_shared<V2rayStreamSettings>();

        explicit TrojanVLESSBean(int _proxy_type) : AbstractBean(0) {
            proxy_type = _proxy_type;
            _add(new configItem("pass", &password, itemType::string));
            _add(new configItem("flow", &flow, itemType::string));
            _add(new configItem("fe", &forceExternal, itemType::boolean));
            _add(new configItem("stream", dynamic_cast<JsonStore *>(stream.get()), itemType::jsonStore));
        };

        QString DisplayType() override { return proxy_type == proxy_VLESS ? "VLESS" : "Trojan"; };
        QString DisplayCoreType() override { return forceExternal ? "Xray" : software_core_name; };

        CoreObjOutboundBuildResult BuildCoreObjSingBox() override;

        int NeedExternal(bool isFirstProfile) override;
        ExternalBuildResult BuildExternal(int mapping_port, int socks_port, int external_stat) override;

        bool TryParseLink(const QString &link);

        QString ToShareLink() override;
    };
} // namespace NekoGui_fmt