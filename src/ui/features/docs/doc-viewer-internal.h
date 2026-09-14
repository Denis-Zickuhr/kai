#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace kai::ui::docview {

inline constexpr qint64 kMaxDocumentBytes = 2 * 1024 * 1024;
// Linhas gigantes (JSON minificado de megabytes) travam o layout de texto do Qt por minutos: acima destes limites o
// arquivo é mostrado com as linhas quebradas e não é editável aqui.
inline constexpr int kMaxLineForRichRender = 20000;
inline constexpr int kDisplayLineChunk = 4000;
inline constexpr qint64 kMaxEditableBytes = 1024 * 1024;
inline constexpr int kMinZoom = 60;
inline constexpr int kMaxZoom = 250;
inline constexpr int kZoomStep = 10;

int contentColumnWidth(int viewport);
QString existingEquivalentDirectory(const QString &dir);
QStringList readmeCandidates();
bool isMarkdownFile(const QString &path);
int longestLine(const QString &text);
QString wrapLongLines(const QString &text, int chunk);
QByteArray readCapped(const QString &path, QString *error);
QStringList scanFolderFiles(const QString &root);
QString firstDocument(const QString &root, const QStringList &files);

} // namespace kai::ui::docview
