#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace kai::ui {

// O explorador de arquivos do leitor de documentos: uma árvore com os arquivos da pasta, um campo de pesquisa por nome,
// o filtro de tipos (por extensão) e o botão de arquivo novo. Só cuida da interface e do que mostrar; abrir e criar
// arquivos é do DocViewer, que escuta os sinais.
//
// Por padrão mostra só documentos: .md (e variantes), .txt e arquivos sem extensão. O menu do filtro lista as extensões
// detectadas na pasta (com a quantidade) para ligar outras.
class DocFileSidebar : public QWidget {
    Q_OBJECT

public:
    // Uma nota da pasta, na seção "Notas" abaixo dos arquivos.
    struct NoteItem {
        QString id;
        QString title;
        QString icon;
        bool local = true;
    };

    explicit DocFileSidebar(QWidget *parent = nullptr);

    // Todos os arquivos da pasta `root` (caminhos absolutos). Reconstrói a árvore com o filtro e a pesquisa atuais.
    void setFiles(const QString &root, const QStringList &files);
    // Marca o arquivo aberto e abre as pastas que levam até ele.
    void setCurrentFile(const QString &file);
    void clear();
    void refreshStyle();

    // A seção "Notas" (abaixo dos arquivos): só aparece quando `enabled` (o leitor mostra notas desta pasta), mesmo vazia,
    // para o botão de nota nova.
    void setNotes(const QVector<NoteItem> &notes, bool enabled);
    int noteCount() const { return int(m_noteItems.size()); }

    // Quantos arquivos a árvore mostra agora, sem contar `except` (para decidir se vale abrir o painel).
    int visibleFileCount(const QString &except = QString()) const;
    // A pasta onde um arquivo novo nasce: a selecionada na árvore (ou a do arquivo selecionado), senão a raiz.
    QString targetDirectory() const;

    // Extensões (minúsculas, sem o ponto; "" = sem extensão) detectadas na pasta, com a contagem.
    QStringList detectedExtensions() const;
    int extensionCount(const QString &extension) const { return m_counts.value(extension); }
    QSet<QString> enabledExtensions() const { return m_enabled; }
    void setExtensionEnabled(const QString &extension, bool enabled);
    void resetExtensions();
    void enableAllExtensions();
    void disableAllExtensions();
    bool isDefaultFilter() const;
    static QSet<QString> defaultExtensions();

    // Para os testes.
    QTreeWidget *tree() const { return m_tree; }
    QTreeWidget *notesTree() const { return m_notesTree; }
    QWidget *notesSection() const { return m_notesSection; }
    QToolButton *newNoteButton() const { return m_newNote; }
    QLineEdit *filterField() const { return m_filter; }
    QToolButton *typesButton() const { return m_types; }
    QToolButton *newFileButton() const { return m_new; }
    QLabel *emptyLabel() const { return m_empty; }
    QString root() const { return m_root; }

signals:
    // Clique num arquivo da árvore.
    void fileActivated(const QString &path);
    // Clique numa nota / no "+" da seção de notas.
    void noteActivated(const QString &noteId);
    void newNoteRequested();
    // "Excluir" no menu de contexto (ou a tecla Delete) sobre uma nota da seção de notas.
    void deleteNoteRequested(const QString &noteId);
    // O botão de arquivo novo, com a pasta de destino.
    void newFileRequested(const QString &directory);
    // "Excluir" no menu de contexto (ou a tecla Delete) sobre um arquivo da árvore.
    void deleteFileRequested(const QString &path);

private:
    void rebuild();
    void showTypesMenu();
    void updateTypesButton();
    bool passesFilter(const QString &path, const QStringList &terms) const;

    QLineEdit *m_filter = nullptr;
    QToolButton *m_types = nullptr;
    QToolButton *m_new = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_empty = nullptr;
    QWidget *m_notesSection = nullptr;
    QTreeWidget *m_notesTree = nullptr;
    QToolButton *m_newNote = nullptr;
    QVector<NoteItem> m_noteItems;
    QTimer *m_debounce = nullptr;

    QString m_root;
    QString m_current;
    QStringList m_files;
    QHash<QString, int> m_counts;
    QSet<QString> m_enabled;
};

} // namespace kai::ui
