#pragma once

#include <QList>
#include <QNetworkCookie>

class QNetworkAccessManager;
class QNetworkCookieJar;

namespace kai::engine {

// Os cookies das requisições HTTP vivem enquanto o Kai está aberto e são
// compartilhados entre os comandos (login numa requisição, uso na seguinte).
// Nada vai para o disco. Cada requisição pode optar por não usá-los
// (HttpOptions::useCookies).
class HttpCookieStore {
public:
    // Faz o gerenciador usar o jar compartilhado sem ficar dono dele (o
    // QNetworkAccessManager reparenta o jar que recebe e o apagaria junto).
    static void attachTo(QNetworkAccessManager *manager);
    static QNetworkCookieJar *jar();
    static QList<QNetworkCookie> cookies();
    static void clear();
};

} // namespace kai::engine
