#include <QCoreApplication>
#include "core/utils/splitTunnelSession.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    auto check=[](bool ok){if(!ok)qFatal("Proxy snapshot invariant failed");};
    const QJsonObject initial{{"sitesEnabled",true},{"siteMode",1},
        {"sites",QJsonObject{{"1",QJsonObject{{"example.com",QJsonArray{"1.2.3.4"}}}}}},
        {"appsEnabled",true},{"appMode",1},{"apps",QJsonObject{{"1",QJsonArray{"C:/old.exe"}}}}};
    auto changed=initial;
    changed["apps"]=QJsonObject{{"1",QJsonArray{"C:/new.exe"}}}; changed["appMode"]=2;
    SplitTunnelSession proxy,vpn;
    proxy.begin(SplitTunnelSettings::forProxy(initial)); vpn.begin(initial);
    proxy.connected(SplitTunnelSettings::forProxy(initial)); vpn.connected(initial);
    proxy.save(SplitTunnelSettings::forProxy(changed)); vpn.save(changed);
    check(!proxy.pending()); check(vpn.pending());
    changed["sites"]=QJsonObject{{"1",QJsonObject{{"other.example",QJsonArray{}}}}};
    proxy.save(SplitTunnelSettings::forProxy(changed)); check(proxy.pending());
    // A failed attempt must not commit the saved settings as applied.
    proxy.begin(SplitTunnelSettings::forProxy(changed)); check(proxy.pending());
    proxy.connected(proxy.attempt()); check(!proxy.pending());
    return 0;
}
