#include "engine/http-cookie-store.h"

#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QObject>

namespace kai::engine {

namespace {

// Pai do jar: o QNetworkAccessManager reparenta o jar que recebe, e o
// HttpRunner o devolve para cá logo depois de entregá-lo.
QObject *holder()
{
    static QObject instance;
    return &instance;
}

class InspectableJar : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> all() const { return allCookies(); }
    void reset() { setAllCookies({}); }
};

InspectableJar *sharedJar()
{
    static InspectableJar *jar = new InspectableJar(holder());
    return jar;
}

} // namespace

QNetworkCookieJar *HttpCookieStore::jar()
{
    return sharedJar();
}

QList<QNetworkCookie> HttpCookieStore::cookies()
{
    return sharedJar()->all();
}

void HttpCookieStore::clear()
{
    sharedJar()->reset();
}

void HttpCookieStore::attachTo(QNetworkAccessManager *manager)
{
    manager->setCookieJar(sharedJar());
    sharedJar()->setParent(holder());
}

} // namespace kai::engine
