

#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

// Nome de arquivo aceito pelo "arquivo novo": um nome simples (sem pastas), sem caracteres que o sistema recusa.
static bool isValidNewFileName(const QString &name)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") && !name.contains(forbidden)
        && !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' '));
}

void DocViewer::promptNewFile(const QString &directory)
{
    if (directory.isEmpty()) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, utils::tr(QStringLiteral("doc.files.new_title")),
        utils::tr(QStringLiteral("doc.files.new_prompt")).arg(QDir::toNativeSeparators(directory)), QLineEdit::Normal,
        QStringLiteral("new-file.md"), &accepted);
    if (accepted) {
        createFile(directory, name);
    }
}

// Cria o arquivo numa thread de trabalho (nunca bloqueia a interface): nome simples, sem sobrescrever. Um .md nasce com
// um título; os demais, vazios. Ao terminar, abre o arquivo e atualiza o explorador.
void DocViewer::createFile(const QString &directory, const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (directory.isEmpty() || !isValidNewFileName(name)) {
        emit fileCreateFailed(utils::tr(QStringLiteral("doc.files.new_invalid")));
        return;
    }
    const QString path = QDir::cleanPath(directory + QLatin1Char('/') + name);
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, directory, path, name]() {
        QString error;
        if (QFileInfo::exists(path)) {
            error = utils::tr(QStringLiteral("doc.files.new_exists")).arg(name);
        } else if (!QDir().mkpath(directory)) {
            error = utils::tr(QStringLiteral("doc.files.new_failed")).arg(name, QStringLiteral("mkdir"));
        } else {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                error = utils::tr(QStringLiteral("doc.files.new_failed")).arg(name, file.errorString());
            } else if (fileExtension(path) == QLatin1String("md") || fileExtension(path) == QLatin1String("markdown")) {
                file.write(QStringLiteral("# %1\n\n").arg(QFileInfo(name).completeBaseName()).toUtf8());
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, error]() {
            if (!guard) {
                return;
            }
            if (!error.isEmpty()) {
                emit guard->fileCreateFailed(error);
                return;
            }
            guard->rescan();
            guard->navigateTo(path, QString());
            emit guard->fileCreated(path);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::requestDeleteFile(const QString &path)
{
    if (path.isEmpty() || !m_deleteConfirmer || !m_deleteConfirmer(path)) {
        return;
    }
    QPointer<DocViewer> guard(this);
    const bool useTrash = m_moveToTrash;
    QThread *thread = QThread::create([guard, path, useTrash]() {
        QString error;
        // Lixeira primeiro; onde não há (contêiner, WSL sem lixeira), apaga de vez — o diálogo já avisou.
        if (!useTrash || !QFile::moveToTrash(path)) {
            QFile file(path);
            if (!file.remove()) {
                error = utils::tr(QStringLiteral("doc.files.delete_failed")).arg(QFileInfo(path).fileName(), file.errorString());
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, error]() {
            if (!guard) {
                return;
            }
            if (!error.isEmpty()) {
                emit guard->fileDeleteFailed(error);
                QMessageBox::warning(guard, utils::tr(QStringLiteral("doc.files.delete_title")), error);
                return;
            }
            guard->finishDelete(path);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// O arquivo já saiu do disco: tira-o do histórico e, se era o aberto, mostra o README (ou um aviso).
void DocViewer::finishDelete(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    m_knownDocs.remove(cleaned);
    for (int i = int(m_history.size()) - 1; i >= 0; --i) {
        if (m_history.at(i).file == cleaned) {
            m_history.removeAt(i);
            if (i <= m_historyIndex) {
                --m_historyIndex;
            }
        }
    }
    const bool wasCurrent = m_currentFile == cleaned;
    if (m_rootDoc == cleaned) {
        m_rootDoc.clear();
    }
    rescan();
    if (wasCurrent) {
        if (m_editing) {
            leaveEditMode(false);
        }
        setEditable(false);
        if (!m_rootDoc.isEmpty()) {
            navigateTo(m_rootDoc, QString());
        } else {
            m_currentFile.clear();
            m_rawText.clear();
            showMessage(utils::tr(QStringLiteral("doc.files.deleted")).arg(QFileInfo(cleaned).fileName()));
            updateCrumbs();
        }
    }
    updateNavigationState();
    emit fileDeleted(cleaned);
}

void DocViewer::rescan()
{
    m_scannedRoot.clear();
    startScan();
}

void DocViewer::finishScan(int token, const QStringList &files)
{
    if (token != m_scanToken) {
        return;
    }
    // Só os documentos servem de destino para links relativos e breadcrumbs; a árvore recebe todos os tipos e filtra.
    m_knownDocs.clear();
    for (const QString &path : files) {
        if (isDocumentFile(path)) {
            m_knownDocs.insert(path);
        }
    }
    // O README da raiz também é "conhecido" (pode estar fora da pasta varrida).
    if (!m_rootDoc.isEmpty()) {
        m_knownDocs.insert(m_rootDoc);
    }
    m_sidebar->setFiles(m_rootDir, files);
    m_sidebar->setCurrentFile(m_currentFile);
    updateCrumbs();
}

void DocViewer::syncTreeSelection()
{
    m_sidebar->setCurrentFile(m_currentFile);
}

} // namespace kai::ui
