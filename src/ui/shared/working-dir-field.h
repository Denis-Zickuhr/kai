#pragma once

#include <QString>
#include <QWidget>

#include "core/models.h"

class QComboBox;
class QLineEdit;
class QToolButton;

namespace kai::ui {

// Campo de DIRETÓRIO DE TRABALHO com os três estados do modelo (ver
// core::WorkingDirMode): herdar da pasta de cima, caminho próprio ou nenhum
// (corta a herança). O texto só vale — e só fica editável — em "próprio".
class WorkingDirField : public QWidget {
    Q_OBJECT
public:
    explicit WorkingDirField(QWidget *parent = nullptr);

    void setValue(core::WorkingDirMode mode, const QString &path);
    core::WorkingDirMode mode() const;
    // Vazio fora do modo "próprio": nunca devolve texto que o modelo ignoraria.
    QString path() const;

signals:
    void changed();

private:
    void syncEnabled();
    void browse();

    QComboBox *m_mode = nullptr;
    QLineEdit *m_path = nullptr;
    QToolButton *m_browse = nullptr;
};

} // namespace kai::ui
