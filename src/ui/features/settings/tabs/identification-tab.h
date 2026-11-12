#pragma once

#include <QWidget>

#include "core/config-manager.h"

class QCheckBox;

namespace kai::ui {

// Aba "Identificação": liga/desliga os chips visuais que aparecem na árvore de comandos, entre o ícone e
// o nome do item (verbo HTTP, linguagem do script, nota, pasta-projeto, coleção HTTP).
class IdentificationTab : public QWidget {
    Q_OBJECT

public:
    explicit IdentificationTab(const core::IdentificationSettings &settings, QWidget *parent = nullptr);

    core::IdentificationSettings settings() const;

    QCheckBox *httpVerbField() const { return m_httpVerb; }
    QCheckBox *scriptLanguageField() const { return m_scriptLanguage; }
    QCheckBox *noteField() const { return m_note; }
    QCheckBox *folderProjectField() const { return m_folderProject; }
    QCheckBox *folderHttpCollectionField() const { return m_folderHttpCollection; }

private:
    QCheckBox *m_httpVerb = nullptr;
    QCheckBox *m_scriptLanguage = nullptr;
    QCheckBox *m_note = nullptr;
    QCheckBox *m_folderProject = nullptr;
    QCheckBox *m_folderHttpCollection = nullptr;
};

} // namespace kai::ui
