#pragma once

#ifndef NKR_NO_GRPC

#include "go/grpc_server/gen/libcore.pb.h"
#include <QString>
#include <functional>
#include <memory>
#include "main/StartupPolicy.hpp"

namespace QtGrpc {
    class Http2GrpcChannelPrivate;
}

namespace NekoGui_rpc {
    class Client {
    public:
        explicit Client(std::function<void(const QString &)> onError, const QString &target, const QString &token);

        void Exit();

        bool KeepAlive();

        // QString returns is error string

        // Wintun alone may wait 15 seconds for its interface, in addition to
        // device creation and configuration. The outer deadline must allow it
        // to complete or return its own error before the GUI replaces the core.
        QString Start(bool *rpcOK, const libcore::LoadConfigReq &request, int timeoutMs = GreenRhythm::StartupPolicy::profileStartMs);

        QString Stop(bool *rpcOK, int timeoutMs = 5000);

        long long QueryStats(const std::string &tag, const std::string &direct);

        std::string ListConnections();

        libcore::TestResp Test(bool *rpcOK, const libcore::TestReq &request);

        libcore::UpdateResp Update(bool *rpcOK, const libcore::UpdateReq &request);

    private:
        std::function<std::unique_ptr<QtGrpc::Http2GrpcChannelPrivate>()> make_grpc_channel;
        std::unique_ptr<QtGrpc::Http2GrpcChannelPrivate> default_grpc_channel;
        std::function<void(const QString &)> onError;
    };

    inline Client *defaultClient;
} // namespace NekoGui_rpc
#endif
