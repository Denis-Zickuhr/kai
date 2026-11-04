#pragma once

#include <QWidget>

class QToolButton;

namespace kai::ui {

// Os botões de edição do leitor, ao lado da lupa (mesma moldura flutuante da busca): o lápis entra e sai do modo de edição;
// editando, aparecem salvar, descartar e o menu de ferramentas (formatar, converter...). Só a interface: quem salva é o
// DocViewer.
class DocEditBar : public QWidget {
    Q_OBJECT

public:
    explicit DocEditBar(QWidget *parent = nullptr);

    // O arquivo aberto pode ser editado? (binário e erro de leitura não).
    void setEditable(bool editable);
    bool editable() const { return m_editable; }
    // Numa nota: botão com as ações de arquivo (salvar como, abrir cópia, copiar). Aparece com ela aberta, editando ou não.
    void setNoteMode(bool noteMode);
    // O documento tem dobras (JSON, YAML, XML): aparecem os botões de recolher e expandir tudo, lendo ou editando.
    void setFoldable(bool foldable);
    void setEditing(bool editing);
    bool editing() const { return m_editing; }
    // Com alterações não salvas o salvar fica ativo.
    void setDirty(bool dirty);
    void refreshStyle();

    QToolButton *editButton() const { return m_edit; }
    QToolButton *saveButton() const { return m_save; }
    QToolButton *discardButton() const { return m_discard; }
    QToolButton *toolsButton() const { return m_tools; }
    QToolButton *noteActionsButton() const { return m_noteActions; }
    QToolButton *collapseAllButton() const { return m_collapseAll; }
    QToolButton *expandAllButton() const { return m_expandAll; }

signals:
    void editRequested();     // lápis com o modo desligado
    void exitRequested();     // lápis com o modo ligado
    void saveRequested();
    void discardRequested();
    void toolsRequested(const QPoint &globalPosition);
    void noteActionsRequested(const QPoint &globalPosition);
    void collapseAllRequested();
    void expandAllRequested();
    void sizeChanged();

private:
    void updateButtons();

    QToolButton *m_edit = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_discard = nullptr;
    QToolButton *m_tools = nullptr;
    QToolButton *m_noteActions = nullptr;
    QToolButton *m_collapseAll = nullptr;
    QToolButton *m_expandAll = nullptr;
    bool m_foldable = false;
    bool m_noteMode = false;
    bool m_editable = false;
    bool m_editing = false;
    bool m_dirty = false;
};

} // namespace kai::ui
