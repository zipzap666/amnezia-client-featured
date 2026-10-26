#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    std::string line;
    while(std::getline(std::cin,line)) {
        const auto object=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        if(object["version"].toInt()!=1) return 3;
        if(object["type"].toString()=="stop") return 0;
        if(object["native"].toString().contains("#crash")) return 2;
        if(object["native"].toString().contains("#no-ready")) continue;
        const QJsonObject response{{"version",1},{"session",object["session"]},{"type","ready"}};
        std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
    }
    return 0;
}
