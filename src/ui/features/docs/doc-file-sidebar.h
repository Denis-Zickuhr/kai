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
class QTreeWidgetItem;

namespace kai::ui {

// O explorador de arquivos do leitor de documentos: duas seções recolhíveis, "Documentos" (uma árvore com os arquivos da
// pasta, um campo de pesquisa por nome e o filtro de tipos por extensão) e "Notas". Cada uma tem o seu "+" no cabeçalho.
// Uma seção sem conteúdo nasce recolhida e a outra fica com o espaço; com as duas cheias, dividem a altura. Só cuida da
// interface e do que mostrar; abrir e criar arquivos é do DocViewer, que escuta os sinais.
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
        QString type = QStringLiteral("markdown"); // markdown, text, json, yaml ou xml: aparece ao lado do título
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

    // Os documentos com alterações não salvas (rascunhos): chaves = caminho do arquivo ou `note:<id>`. Aparecem em itálico e
    // na cor de aviso, com um balão explicando.
    void setDraftKeys(const QSet<QString> &keys);

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

    // As seções: aberta/recolhida. Sem escolha do usuário, abre só a que tem conteúdo; clicar no cabeçalho (ou estas
    // funções) fixa a escolha até a pasta mudar.
    bool docsExpanded() const { return m_docsExpanded; }
    bool notesExpanded() const { return m_notesExpanded; }
    void setDocsExpanded(bool expanded);
    void setNotesExpanded(bool expanded);

    // Para os testes.
    QWidget *docsSection() const { return m_docsSection; }
    QWidget *docsHeader() const { return m_docsHeader; }
    QWidget *notesHeader() const { return m_notesHeader; }
    QTreeWidget *tree() const { return m_tree; }
    QTreeWidget *notesTree() const { return m_notesTree; }
    QWidget *notesSection() const { return m_notesSection; }
    QLabel *notesEmptyLabel() const { return m_notesEmpty; }
    // O texto do tipo (MD, JSON...) da nota da linha `row`.
    QString noteTypeTag(int row) const;
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

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    void rebuild();
    void applyDraftMarks();
    void showTypesMenu();
    void updateTypesButton();
    void applySectionStates();
    void updateSectionLimits();
    void setSectionExpanded(bool notes, bool expanded);
    bool passesFilter(const QString &path, const QStringList &terms) const;

    QLineEdit *m_filter = nullptr;
    QToolButton *m_types = nullptr;
    QToolButton *m_new = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_empty = nullptr;
    QWidget *m_docsSection = nullptr;
    QWidget *m_docsHeader = nullptr;
    QWidget *m_docsBody = nullptr;
    QLabel *m_docsChevron = nullptr;
    QLabel *m_docsTitle = nullptr;
    QLabel *m_docsCount = nullptr;
    QWidget *m_notesSection = nullptr;
    QWidget *m_notesHeader = nullptr;
    QWidget *m_notesBody = nullptr;
    QLabel *m_notesChevron = nullptr;
    QLabel *m_notesTitle = nullptr;
    QLabel *m_notesCount = nullptr;
    QTreeWidget *m_notesTree = nullptr;
    QLabel *m_notesEmpty = nullptr;
    QToolButton *m_newNote = nullptr;
    int m_shownCount = 0;
    bool m_docsExpanded = false;
    bool m_notesExpanded = false;
    bool m_docsChosen = false;  // o usuário abriu/fechou à mão
    bool m_notesChosen = false;
    QVector<NoteItem> m_noteItems;
    QTimer *m_debounce = nullptr;

    QString m_root;
    QString m_current;
    QStringList m_files;
    QHash<QString, int> m_counts;
    QSet<QString> m_enabled;
    QSet<QString> m_drafts;
};

} // namespace kai::ui
