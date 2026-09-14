#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace kai::core::detail {

QJsonObject stripIdsFromExport(QJsonObject root, const QString &exportRootFolderId,
                               const QMap<QString, QString> &allCommandNames = {});
QJsonObject resolveIdFreeImport(QJsonObject root);
bool looksIdFree(const QJsonObject &root);

} // namespace kai::core::detail
