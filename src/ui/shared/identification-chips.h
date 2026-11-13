#pragma once

#include <QColor>
#include <QSet>
#include <QString>
#include <QVector>

#include "core/models.h"

namespace kai::ui {

// Chip de identificação (Configurações → Identificação): texto curto + cor, ou só o ícone de marca (linguagem do script).
struct IdentificationChip {
    QString text;
    QColor color;
    QString icon;
    bool isValid() const { return !text.isEmpty() || !icon.isEmpty(); }
};

// Cor do chip de verbo HTTP: verde (leitura), azul (criação), âmbar (atualização), vermelho (remoção).
QColor httpVerbChipColor(const QString &verb);
// Cor (do logo) e ícone do chip de linguagem do script: cores de marca, não tokens, para o logo ser reconhecível.
QColor scriptLangChipColor(core::CommandLanguage language);
QString scriptLangChipIcon(core::CommandLanguage language);

// Chip de um comando conforme as preferências ligadas; inválido quando nenhuma se aplica.
IdentificationChip commandChip(const core::Command &command);
IdentificationChip noteChip();
// Pasta: projeto, ou coleção HTTP (`httpCollection`: tem comando HTTP em algum nível abaixo).
IdentificationChip folderChip(const core::Folder &folder, bool httpCollection);
// Ids das pastas que contêm comando HTTP, direta ou indiretamente; vazio com a preferência desligada.
QSet<QString> httpCollectionFolderIds(const QVector<core::Command> &commands, const QVector<core::Folder> &folders);

} // namespace kai::ui
