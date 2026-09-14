#pragma once

#include "ui/features/docs/doc-links.h"
#include "ui/features/docs/doc-note.h"
#include "ui/features/docs/doc-style.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QTextCursor>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QSplitter;
class QTextDocument;
class QStackedWidget;
class QTextBrowser;
class QTreeWidget;

namespace kai::ui {

class DocBrowser;
class DocEditBar;
class DocEditorPane;
class DocFileSidebar;
class DocNavBar;
class DocSearchBar;

// Leitor de documentos do Kai: mostra um arquivo .md/.txt com o visual do app (títulos, tabelas, código, citações,
// links, diagramas Mermaid de fluxograma) e deixa NAVEGAR entre os documentos ligados por links relativos. Em cima, uma
// barra com voltar/avançar, breadcrumbs do caminho do arquivo, o botão da árvore e o zoom; ao lado, a árvore dos
// documentos da pasta. Links `kai:` viram o sinal kaiLinkActivated (quem decide o que fazer é o app).
//
// Todo acesso a disco (ler o documento, procurar README, listar os documentos) roda em thread de trabalho.
class DocViewer : public QWidget {
    Q_OBJECT

public:
    explicit DocViewer(QWidget *parent = nullptr);

    // Abre `file` (documento escolhido). `label` = nome da pasta do Kai (1º breadcrumb). Zera o histórico.
    void openFile(const QString &file, const QString &label);
    // Abre o README de `dir` (README.md, readme.md, ...); sem README, o primeiro documento que achar na pasta. Pasta sem
    // nenhum documento: emite folderEmpty(); pasta inexistente: unavailable(). Nenhum dos dois mexe na tela.
    void openFolderReadme(const QString &dir, const QString &label);
    // Estado vazio da pasta: no centro, o atalho para criar o README.md.
    void showCreateReadme(const QString &dir, const QString &label);
    // Esvazia o leitor (o app trocou de seleção).
    void clear();

    QString currentFile() const { return m_currentFile; }
    QString rootDirectory() const { return m_rootDir; }
    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex + 1 < m_history.size(); }
    void goBack();
    void goForward();
    int zoomPercent() const { return m_zoom; }
    void setZoomPercent(int percent);
    // Mostra/esconde a árvore de documentos.
    void setTreeVisible(bool visible);
    bool treeVisible() const;

    // Reaplica tema/cantos (troca de tema em tempo de execução).
    void refreshStyle();

    // --- Notas ---------------------------------------------------------------------------------------------------
    // As notas da pasta mostrada: aparecem na seção "Notas" do explorador. `enabled` liga a seção mesmo sem nenhuma (para o
    // botão de nota nova).
    void setNotes(const QVector<DocNote> &notes, bool enabled = true);
    // Abre uma nota (já listada em setNotes) como documento; `note:<id>` no histórico. Com alterações, pergunta antes.
    void openNote(const QString &noteId);
    // Abre a nota com a pasta `dir` (pode ser vazia) como raiz do explorador e `label` como 1º breadcrumb.
    void openNoteInFolder(const QString &noteId, const QString &dir, const QString &label, bool startEditing);
    bool isNoteView() const { return !m_noteId.isEmpty(); }
    QString currentNoteId() const { return m_noteId; }
    // Quem grava a nota (o app): recebe id e conteúdo e diz se deu certo.
    void setNoteSaver(std::function<bool(const QString &, const QString &)> saver) { m_noteSaver = std::move(saver); }
    static QString noteKey(const QString &noteId) { return QStringLiteral("note:") + noteId; }

    // --- Edição (o lápis ao lado da lupa) ---------------------------------------------------------------------------
    // O que fazer com alterações não salvas ao sair do arquivo (ou descartar).
    enum class UnsavedDecision { Save, Discard, Cancel };
    enum class UnsavedContext { Leaving, Discarding };
    using UnsavedHandler = std::function<UnsavedDecision(UnsavedContext)>;
    // Troca a pergunta padrão (um diálogo) por outra; os testes usam para responder sem diálogo.
    void setUnsavedChangesHandler(UnsavedHandler handler) { m_unsavedHandler = std::move(handler); }

    bool editing() const { return m_editing; }
    bool isDirty() const;
    bool canEdit() const;
    // Entra no modo de edição do arquivo aberto (só arquivos de texto lidos por inteiro).
    void enterEditMode();
    // Salva em segundo plano; `done(ok)` ao terminar. Só vale editando.
    void saveCurrent(std::function<void(bool)> done = {});
    // Descarta as alterações (pergunta antes se houver) e volta à leitura.
    void discardChanges();
    // O lápis com a edição ligada: sai dela, perguntando antes se há alterações.
    void requestLeaveEdit();
    DocEditorPane *editorPane() const { return m_pane; }
    DocEditBar *editBar() const { return m_editBar; }
    // Roda uma ferramenta do menu (ver DocTool) no texto do editor.
    void runTool(const QString &toolId);

    // Busca no texto mostrado (igual à da Saída): abre a barra e põe o foco no campo. Sempre devolve true.
    bool focusSearch();
    DocSearchBar *searchBar() const { return m_search; }
    int searchMatchCount() const { return int(m_matches.size()); }
    int currentSearchMatch() const { return m_matchIndex; }

    // Abre `file` (e rola até `anchor`) dentro do leitor, mantendo o histórico. Com alterações não salvas, pergunta antes.
    void navigateTo(const QString &file, const QString &anchor);

    // Confirmação de "excluir arquivo" (true = excluir). A padrão é um diálogo; os testes trocam para não abrir um.
    using DeleteConfirmer = std::function<bool(const QString &path)>;
    void setDeleteConfirmer(DeleteConfirmer confirmer) { m_deleteConfirmer = std::move(confirmer); }
    // Os testes desligam a lixeira para não encher a do usuário.
    void setMoveToTrash(bool enabled) { m_moveToTrash = enabled; }
    // Pede a exclusão de `path` (pergunta antes). Vai para a lixeira quando o sistema tem; senão apaga de vez.
    // Assíncrono: o resultado vem em fileDeleted / fileDeleteFailed.
    void requestDeleteFile(const QString &path);

    // Cria um arquivo novo em `directory` (nome simples, sem sobrescrever) e o abre. Assíncrono: o resultado vem em
    // fileCreated / fileCreateFailed.
    void createFile(const QString &directory, const QString &name);

    // Para os testes e para quem precisa do conteúdo.
    QTextBrowser *browser() const;
    QTreeWidget *tree() const;
    DocFileSidebar *sidebar() const { return m_sidebar; }
    DocNavBar *navBar() const { return m_nav; }
    QVector<DocCrumb> breadcrumbs() const { return m_crumbs; }

signals:
    // Um documento foi mostrado (ou o erro de abri-lo, quando o arquivo foi pedido explicitamente).
    void loaded(const QString &file);
    // Nada a mostrar: a pasta não existe (ou o arquivo achado não abriu) (openFolderReadme).
    void unavailable(const QString &directory);
    // A pasta existe mas não tem nenhum documento (openFolderReadme).
    void folderEmpty(const QString &directory);
    // Clique num link `kai:<ação>/<alvo>`.
    void kaiLinkActivated(const QString &action, const QString &target);
    // Arquivo novo criado (e já em abertura) ou o motivo de não ter sido possível.
    void fileCreated(const QString &path);
    void fileCreateFailed(const QString &message);
    void fileDeleted(const QString &path);
    void fileDeleteFailed(const QString &message);
    void newNoteRequested();
    // Excluir uma nota (quem decide e confirma é o app, dono das notas).
    void noteDeleteRequested(const QString &noteId);
    void editingChanged(bool editing);
    void fileSaved(const QString &path);
    void saveFailed(const QString &message);

private:
    struct HistoryEntry {
        QString file;
        QString anchor;
    };
    struct LoadResult {
        bool found = false;
        QString file;
        QByteArray bytes;
        QString error;
        bool truncated = false; // maior que o teto de leitura: lido só em parte (não pode ser editado)
    };

    void startLoad(const QString &file, const QString &anchor, bool recordHistory, bool automatic);
    void finishLoad(int token, const LoadResult &result, const QString &anchor, bool recordHistory, bool automatic);
    void render(const QString &anchor);
    void showMessage(const QString &text);
    void leaveEmptyState();
    void handleLink(const QUrl &url);
    void scrollToAnchor(const QString &anchor);
    void promptNewFile(const QString &directory);
    void showNote(const QString &noteId, const QString &anchor, bool recordHistory);
    void refreshNotesSection();
    void leaveEditMode(bool rerender);
    void guardedLeave(std::function<void()> proceed);
    void finishSave(bool ok, const QString &error, const QString &savedText, const std::function<void(bool)> &done);
    void showToolsMenu(const QPoint &globalPosition);
    QTextDocument *activeDocument() const;
    void setEditable(bool editable);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void replaceCurrentMatch();
    void replaceAllMatches();
    void runSearch(bool jump);
    void goToMatch(int index);
    void stepMatch(int delta);
    void paintMatches();
    void positionSearchBar();
    void startScan();
    void rescan();
    void finishDelete(const QString &path);
    void finishScan(int token, const QStringList &files);
    void updateCrumbs();
    void updateNavigationState();
    void updateContentWidth();
    void syncTreeSelection();
    DocTheme currentTheme() const;

    DocNavBar *m_nav = nullptr;
    DocSearchBar *m_search = nullptr;
    QWidget *m_content = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_emptyPage = nullptr;
    QPushButton *m_createReadme = nullptr;
    QLabel *m_emptyText = nullptr;
    DocEditorPane *m_pane = nullptr;
    DocEditBar *m_editBar = nullptr;
    bool m_editing = false;
    bool m_editable = false;
    bool m_saving = false;
    QString m_lineEnding = QStringLiteral("\n");
    QVector<DocNote> m_notes;
    bool m_notesEnabled = false;
    QString m_noteId; // não vazio = o documento mostrado é esta nota
    std::function<bool(const QString &, const QString &)> m_noteSaver;
    UnsavedHandler m_unsavedHandler;
    DeleteConfirmer m_deleteConfirmer;
    bool m_moveToTrash = true;
    QList<QTextCursor> m_matches;
    int m_matchIndex = -1;
    QSplitter *m_splitter = nullptr;
    DocFileSidebar *m_sidebar = nullptr;
    DocBrowser *m_browser = nullptr;

    QString m_label;
    QString m_rootDir;
    QString m_rootDoc;
    QString m_currentFile;
    QString m_rawText;
    bool m_markdown = true;
    QVector<HistoryEntry> m_history;
    int m_historyIndex = -1;
    QSet<QString> m_knownDocs;
    QVector<DocCrumb> m_crumbs;
    QHash<QString, int> m_anchorPositions;
    QHash<QString, QImage> m_mermaidImages;
    QString m_scannedRoot;
    int m_zoom = 100;
    int m_loadToken = 0;
    int m_scanToken = 0;
    bool m_automaticProbe = false;

    friend class DocBrowser;
};

} // namespace kai::ui
