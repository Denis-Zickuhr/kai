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
    void setEditing(bool editing);
    bool editing() const { return m_editing; }
    // Com alterações não salvas o salvar fica ativo.
    void setDirty(bool dirty);
    void refreshStyle();

    QToolButton *editButton() const { return m_edit; }
    QToolButton *saveButton() const { return m_save; }
    QToolButton *discardButton() const { return m_discard; }
    QToolButton *toolsButton() const { return m_tools; }

signals:
    void editRequested();     // lápis com o modo desligado
    void exitRequested();     // lápis com o modo ligado
    void saveRequested();
    void discardRequested();
    void toolsRequested(const QPoint &globalPosition);
    void sizeChanged();

private:
    void updateButtons();

    QToolButton *m_edit = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_discard = nullptr;
    QToolButton *m_tools = nullptr;
    bool m_editable = false;
    bool m_editing = false;
    bool m_dirty = false;
};

} // namespace kai::ui
