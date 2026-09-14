#pragma once

#include "core/models.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace kai::ui {

class FolderPickerWidget;
class IconPickerWidget;

// Propriedades de uma nota: nome, ícone, tipo (markdown, texto, JSON, YAML, XML), a pasta onde fica e se é só deste Kai
// ("local") ou sincroniza com o projeto. O texto da nota se edita no leitor, não aqui.
class NotePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    // `note` já preenchida (nova ou existente); `folders` alimenta o seletor de pasta.
    NotePropertiesDialog(const core::Note &note, const QVector<core::Folder> &folders, bool isNew, QWidget *parent = nullptr);

    core::Note buildNote() const;

    QLineEdit *nameField() const { return m_name; }
    QComboBox *typeField() const { return m_type; }
    QCheckBox *localField() const { return m_local; }
    FolderPickerWidget *folderField() const { return m_folder; }

private:
    void accept() override;

    core::Note m_note;
    QLineEdit *m_name = nullptr;
    IconPickerWidget *m_icon = nullptr;
    QComboBox *m_type = nullptr;
    FolderPickerWidget *m_folder = nullptr;
    QCheckBox *m_local = nullptr;
};

} // namespace kai::ui
