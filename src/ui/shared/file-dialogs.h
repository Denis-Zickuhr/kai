#pragma once

#include <QString>

class QWidget;

namespace kai::ui {

// Seletores nativos de arquivo/pasta que abrem no último diretório usado e lembram o escolhido
// (utils::LastDirectory). `hint` é o caminho que o campo já tem: se existir, o diálogo abre nele.
// Devolvem vazio quando o usuário cancela.
QString pickOpenFile(QWidget *parent, const QString &title, const QString &filter = QString(),
                     const QString &hint = QString());
QString pickSaveFile(QWidget *parent, const QString &title, const QString &suggestedName,
                     const QString &filter = QString());
QString pickDirectory(QWidget *parent, const QString &title, const QString &hint = QString());

} // namespace kai::ui
