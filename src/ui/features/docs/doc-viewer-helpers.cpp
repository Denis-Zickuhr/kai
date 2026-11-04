#include "ui/features/docs/doc-viewer-internal.h"

#include "ui/features/docs/doc-viewer.h"
#include "ui/features/docs/doc-code-editor.h"
#include "ui/features/docs/doc-decorations.h"
#include "ui/features/docs/doc-edit-bar.h"
#include "ui/features/docs/doc-editor-pane.h"
#include "ui/features/docs/doc-folding.h"
#include "ui/features/docs/doc-note-refs.h"
#include "ui/features/docs/doc-file-sidebar.h"
#include "ui/features/docs/doc-tools-menu.h"
#include "ui/features/docs/doc-search-bar.h"
#include "ui/features/docs/doc-session-store.h"
#include "ui/features/docs/doc-nav-bar.h"
#include "ui/features/docs/mermaid-flowchart.h"
#include "ui/shared/loading-overlay.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"
#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QImageReader>
#include <QTimer>
#include <QToolTip>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QHeaderView>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QStackedWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTextFrame>
#include <QThread>
#include <QCursor>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace kai::ui::docview {

namespace {
constexpr int kMaxScanDocuments = 800;
constexpr int kMaxScanOthers = 3000;
constexpr int kMaxScanVisited = 30000;
constexpr int kMaxScanDepth = 5;
// A coluna de leitura nunca é mais estreita que isto (ou que a janela, se ela for menor) e cresce com a janela, ocupando
// esta fração dela: sem um teto fixo, telas largas aproveitam tabelas e código.
constexpr int kContentBaseWidth = 940;
constexpr double kContentFillFraction = 0.82;
}

int contentColumnWidth(int viewport)
{
    return std::max(std::min(viewport, kContentBaseWidth), int(viewport * kContentFillFraction));
}

// O primeiro caminho equivalente de `dir` (ver utils::equivalentPathCandidates) que existe de verdade; roda em thread de
// trabalho (o disco pode ser um UNC do WSL).
QString existingEquivalentDirectory(const QString &dir)
{
#if defined(Q_OS_WIN)
    constexpr bool kWindowsHost = true;
#else
    constexpr bool kWindowsHost = false;
#endif
    for (const QString &candidate : utils::equivalentPathCandidates(dir, utils::installedWslDistros(), kWindowsHost)) {
        if (QFileInfo(candidate).isDir()) {
            return candidate;
        }
    }
    return QString();
}

QStringList readmeCandidates()
{
    return {QStringLiteral("README.md"), QStringLiteral("readme.md"), QStringLiteral("Readme.md"),
            QStringLiteral("README.markdown"), QStringLiteral("readme.markdown"), QStringLiteral("README.txt"),
            QStringLiteral("README")};
}

bool isMarkdownFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("md") || suffix == QLatin1String("markdown") || suffix == QLatin1String("mdown");
}

int longestLine(const QString &text)
{
    int longest = 0;
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0) {
            end = text.size();
        }
        longest = std::max(longest, int(end - start));
        start = end + 1;
    }
    return longest;
}

// Só para mostrar: quebra as linhas maiores que `chunk` em pedaços, para o layout de texto não engasgar.
QString wrapLongLines(const QString &text, int chunk)
{
    QString out;
    out.reserve(text.size() + text.size() / chunk + 8);
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        const bool last = end < 0;
        if (last) {
            end = text.size();
        }
        for (qsizetype at = start; at < end; at += chunk) {
            out.append(QStringView(text).mid(at, std::min<qsizetype>(chunk, end - at)));
            if (at + chunk < end) {
                out.append(QLatin1Char('\n'));
            }
        }
        if (last) {
            break;
        }
        out.append(QLatin1Char('\n'));
        start = end + 1;
    }
    return out;
}

// Roda em thread de trabalho: lê o arquivo (com teto de tamanho).
QByteArray readCapped(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    return file.read(kMaxDocumentBytes);
}

// Varre `root` atrás de arquivos (documentos e outros tipos têm tetos separados, para uma pasta cheia de código não
// esconder os documentos). Ordenado sem diferenciar maiúsculas. Roda em thread de trabalho.
QStringList scanFolderFiles(const QString &root)
{
    static const QSet<QString> skipped = {QStringLiteral("node_modules"), QStringLiteral("vendor"), QStringLiteral("dist"),
                                          QStringLiteral("build"), QStringLiteral("target"), QStringLiteral("__pycache__")};
    QStringList files;
    int documents = 0;
    int others = 0;
    int visited = 0;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    // QDirIterator não poda por nome: filtra pelo caminho relativo ao percorrer.
    while (it.hasNext() && visited < kMaxScanVisited && (documents < kMaxScanDocuments || others < kMaxScanOthers)) {
        const QString path = it.next();
        ++visited;
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(root).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'));
        bool skip = parts.size() > kMaxScanDepth;
        for (int i = 0; i < parts.size() && !skip; ++i) {
            const bool isLast = i + 1 == parts.size();
            skip = parts.at(i).startsWith(QLatin1Char('.')) || ((!isLast || info.isDir()) && skipped.contains(parts.at(i)));
        }
        if (skip || info.isDir()) {
            continue;
        }
        const bool isDocument = isDocumentFile(path);
        if (isDocument ? documents >= kMaxScanDocuments : others >= kMaxScanOthers) {
            continue;
        }
        ++(isDocument ? documents : others);
        files.append(QDir::cleanPath(path));
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

// O primeiro documento da varredura: o mais raso (arquivos da raiz antes dos de subpastas), em ordem alfabética.
QString firstDocument(const QString &root, const QStringList &files)
{
    QString best;
    int bestDepth = 0;
    for (const QString &path : files) {
        if (!isDocumentFile(path)) {
            continue;
        }
        const int depth = int(QDir(root).relativeFilePath(path).count(QLatin1Char('/')));
        if (best.isEmpty() || depth < bestDepth) {
            best = path;
            bestDepth = depth;
        }
    }
    return best;
}

} // namespace kai::ui::docview
