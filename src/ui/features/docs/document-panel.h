#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

#include <functional>

#include "core/note-files.h"
#include "ui/features/docs/doc-note.h"

class QLabel;
class QPushButton;
class QStackedLayout;

namespace kai::ui {

class DocViewer;
class DocSessionStore;

// O painel Documento: o leitor de README e notas da pasta, num painel próprio do quadro. É o único lugar onde a
// documentação aparece (a Saída não a hospeda mais): o painel segue a seleção da árvore, e a Saída segue mostrando a
// execução. Sem documento, mostra um estado vazio curto.
class DocumentPanel : public QWidget {
    Q_OBJECT

public:
    explicit DocumentPanel(QWidget *parent = nullptr);

    // `file` = documento escolhido; `showFolderReadme` abre o README do diretório (ou o primeiro documento que achar); numa
    // pasta que existe mas não tem documento, mostra o atalho "criar README.md"; se o diretório não existe, mostra a tela
    // "caminho não encontrado" (corrigir sozinho / à mão). Qualquer chamada nova cancela a anterior; `clearDocument` volta
    // ao estado vazio. `notes`: as notas da pasta (seção "Notas" do explorador); sem documento, a primeira nota abre.
    void showDocument(const QString &file, const QString &label, const QVector<DocNote> &notes = {}, bool notesEnabled = false);
    void showFolderReadme(const QString &directory, const QString &label, const QVector<DocNote> &notes = {},
                          bool notesEnabled = false);
    // Abre a nota `noteId` (entre `notes`) com `directory` como raiz do explorador (pode ser vazio).
    void showNote(const QString &noteId, const QVector<DocNote> &notes, const QString &directory, const QString &label,
                  bool startEditing);
    // Atualiza só a lista de notas do leitor aberto (depois de criar/renomear/excluir).
    void updateNotes(const QVector<DocNote> &notes, bool notesEnabled);
    void clearDocument();

    // Quem grava as notas editadas no leitor (o app).
    void setNoteSaver(std::function<bool(const QString &, const QString &)> saver);
    // Os nomes do autocompletar do editor de documentos (ver DocViewer::setNameProvider); vale também para o leitor criado depois.
    void setNameProvider(std::function<QStringList(const QString &)> provider);
    // A memória do leitor (posições, rascunhos, recentes): o mesmo store para todo o app. `scope` = a pasta do Kai a que o
    // documento mostrado pertence (chamar antes de showFolderReadme/showNote/showDocument).
    void setSessionStore(DocSessionStore *store);
    void setScope(const QString &scope);
    // Monta o leitor antes de ser preciso (a primeira abertura custava dezenas de ms de montagem de widgets).
    void prepareViewer();
    // Guarda no store onde o usuário está (fim do app, troca de tela).
    void captureState();
    void refreshStyle();

    // Há um documento (ou tela do leitor) na vista, e não o estado vazio.
    bool documentMode() const { return m_docMode; }
    // O usuário está editando: nada o tira dali sem ele pedir.
    bool editing() const;
    bool focusSearch();
    void focusContent();
    // nullptr até o primeiro documento aparecer neste painel.
    DocViewer *docViewer() const { return m_viewer; }

signals:
    // Documentação da pasta: nada a mostrar (caminho inexistente) e clique num link `kai:<ação>/<alvo>`.
    void documentUnavailable(const QString &directory);
    // Os botões da tela "caminho não encontrado" (sem README e com o diretório inexistente).
    void documentDirectoryFixAutomatically(const QString &directory, const QString &suggestion);
    void documentDirectoryFixManually(const QString &directory);
    void documentKaiLinkActivated(const QString &action, const QString &target);
    // O "+" da seção de notas do leitor.
    void documentNewNoteRequested();
    void documentNoteDeleteRequested(const QString &noteId);
    void documentNoteFileActionRequested(const QString &noteId, core::NoteFileAction action, const QString &content);
    // O botão das pastas com documentação (na barra do leitor, ou no estado vazio): o app abre o menu na posição global dada.
    void documentFoldersRequested(const QPoint &globalPosition);

private:
    DocViewer *ensureViewer();
    void enterDocMode();
    void updatePage();
    void applyEmptyStyle();
    // Começa um pedido novo SEM derrubar o que está na tela: trocar de pasta não pode passar pelo estado vazio enquanto o
    // arquivo novo é lido. Com edição em andamento, o caminho completo (clearDocument) é o que pergunta sobre alterações.
    void beginRequest();

    QStackedLayout *m_stack = nullptr;
    QWidget *m_emptyPage = nullptr;
    QLabel *m_emptyIcon = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QPushButton *m_emptyBrowse = nullptr;
    DocViewer *m_viewer = nullptr;
    DocSessionStore *m_store = nullptr;
    QString m_scope;
    std::function<bool(const QString &, const QString &)> m_noteSaver;
    std::function<QStringList(const QString &)> m_nameProvider;
    bool m_docMode = false;
    int m_request = 0;
};

} // namespace kai::ui
