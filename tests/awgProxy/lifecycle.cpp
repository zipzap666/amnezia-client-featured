#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include "core/proxy/proxyConnection.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir directory;
    ProxyConnection proxy(nullptr,QCoreApplication::applicationDirPath()+"/proxyTestHelper.exe",directory.filePath("recovery.json"));
    Vpn::ConnectionState state=Vpn::Disconnected;
    QObject::connect(&proxy,&ProxyConnection::stateChanged,&app,[&](Vpn::ConnectionState value){state=value;});
    const QJsonObject settings{{"systemProxy",false},{"httpPort",8080},{"socksPort",1080}};
    auto config=[](const QString &marker){return QJsonObject{{"protocol","awg"},
        {"awg_config_data",QJsonObject{{"config","[Interface]\nAddress=10.0.0.2/32\n"+marker+"\n[Peer]\n"}}}};};
    auto waitFor=[&](Vpn::ConnectionState wanted){
        if(state==wanted)return;
        QEventLoop loop; QTimer timeout;timeout.setSingleShot(true);timeout.start(4000);
        QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        auto connection=QObject::connect(&proxy,&ProxyConnection::stateChanged,&loop,[&](Vpn::ConnectionState value){if(value==wanted)loop.quit();});
        loop.exec();QObject::disconnect(connection);if(state!=wanted)qFatal("Proxy lifecycle timed out");
    };
    auto check=[](bool value){if(!value)qFatal("Proxy lifecycle invariant failed");};
    proxy.start(config(""),settings);waitFor(Vpn::Connected);check(proxy.running());check(!proxy.recoveryPending());
    check(proxy.stop());check(state==Vpn::Disconnected);check(!proxy.running());
    proxy.start(config("#crash"),settings);waitFor(Vpn::Error);check(!proxy.running());check(!proxy.error().isEmpty());
    check(proxy.stop());check(proxy.error().isEmpty());
    proxy.start(config("#no-ready"),settings);
    QEventLoop loop;QTimer::singleShot(100,&loop,&QEventLoop::quit);loop.exec();
    check(proxy.stop());check(state==Vpn::Disconnected);check(!proxy.running());
    proxy.start(config(""),settings);waitFor(Vpn::Connected);check(proxy.stop());
    // A corrupt recovery journal blocks readiness, preserving recovery evidence.
    QFile journal(directory.filePath("recovery.json"));check(journal.open(QIODevice::WriteOnly));journal.write("invalid");journal.close();
    proxy.start(config(""),settings);waitFor(Vpn::Error);check(proxy.recoveryPending());check(!proxy.running());
    check(journal.remove());check(proxy.stop());
    return 0;
}
