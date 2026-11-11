#include "rpc/SiteToolsRpc.hpp"
#include "go/grpc_server/gen/libcore.pb.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <cstdio>
#include <memory>

class OwnedPeer : public QTcpServer {
public:
    bool respond = true;
    QString action;
    QJsonObject payload;
    static QByteArray frame(int type, int flags, int stream, const QByteArray &body) {
        QByteArray data(9, '\0');
        data[0] = char(body.size() >> 16);
        data[1] = char(body.size() >> 8);
        data[2] = char(body.size());
        data[3] = char(type);
        data[4] = char(flags);
        qToBigEndian<quint32>(quint32(stream), reinterpret_cast<uchar *>(data.data() + 5));
        return data + body;
    }
    OwnedPeer() {
        connect(this, &QTcpServer::newConnection, this, [this] {auto *socket=nextPendingConnection();auto input=std::make_shared<QByteArray>();auto preface=std::make_shared<bool>(false);
  connect(socket,&QTcpSocket::readyRead,socket,[this,socket,input,preface]{*input+=socket->readAll();if(!*preface){if(input->size()<24)return;input->remove(0,24);*preface=true;socket->write(frame(4,0,0,{}));}
   while(input->size()>=9){auto byte=[&](int i){return uchar(input->at(i));};const int size=(byte(0)<<16)|(byte(1)<<8)|byte(2),type=byte(3),flags=byte(4),stream=int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(input->constData()+5)));if(input->size()<9+size)return;const auto body=input->mid(9,size);input->remove(0,9+size);if(type==4&&!(flags&1))socket->write(frame(4,1,0,{}));if(type!=0||!respond)continue;
    libcore::SiteToolsReq request;if(body.size()<5||!request.ParseFromArray(body.constData()+5,body.size()-5))continue;action=QString::fromStdString(request.action());payload=QJsonDocument::fromJson(QByteArray::fromStdString(request.payload_json())).object();libcore::SiteToolsResp response;response.set_result_json(R"({"scope":"owned","applied":true})");std::string bytes;response.SerializeToString(&bytes);QByteArray message(5,'\0');qToBigEndian<quint32>(quint32(bytes.size()),reinterpret_cast<uchar *>(message.data()+1));message+=QByteArray::fromStdString(bytes);
    QByteArray headers("\x88",1);headers+=QByteArray("\0\x0c",2)+"content-type"+QByteArray("\x10",1)+"application/grpc";socket->write(frame(1,4,stream,headers));socket->write(frame(0,1,stream,message));
   }});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater); });
        listen(QHostAddress::LocalHost, 0);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failed = 0;
    auto check = [&](bool ok, const char *label) {std::printf("%s %s\n",ok?"PASS":"FAIL",label);failed+=!ok; };
    // A global proxy must never intercept the authenticated loopback RPC.
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
    OwnedPeer peer;
    GreenRhythm::SiteToolsRpc transport;
    bool done = false;
    QString error;
    QJsonObject result;
    int heartbeats = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start(10);
    auto wait = [&] {QElapsedTimer timer;timer.start();while(!done&&timer.elapsed()<6000)app.processEvents(QEventLoop::AllEvents,20);return timer.elapsed(); };
    transport.request(peer.serverPort(), "owned-test-token", "apply", {{"library", "owned library"}}, [&](const QJsonObject &value, const QString &failure) {done=true;result=value;error=failure; });
    wait();
    check(done && error.isEmpty() && result.value("applied").toBool() && peer.action == "apply" && peer.payload.value("library") == "owned library", "actual h2c protobuf request and reply bypass system proxy");
    peer.respond = false;
    done = false;
    error.clear();
    const int before = heartbeats;
    transport.request(peer.serverPort(), "owned-test-token", "status", {}, [&](const QJsonObject &, const QString &failure) {done=true;error=failure; });
    const auto elapsed = wait();
    check(done && !error.isEmpty() && elapsed < 5800 && heartbeats - before > 20, "hung core request times out while UI event loop remains responsive");
    return failed ? 1 : 0;
}
