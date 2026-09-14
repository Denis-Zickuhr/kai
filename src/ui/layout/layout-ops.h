#pragma once

#include <QJsonArray>
#include <QStringList>

#include <functional>

#include "ui/layout/layout-model.h"

namespace kai::ui::layout {

// O que a ferramenta kai_layout_apply da IA pode fazer com um LayoutModel: uma lista de operações JSON aplicada de
// uma vez só (tudo ou nada) sobre uma cópia. Sem widgets: a janela só recebe o modelo final.
struct LayoutOpsContext {
    LayoutModel::Validator isKnown;
    // Painéis que o usuário pode fechar (a lista de comandos, por exemplo, não).
    std::function<bool(const QString &)> closable;
    // Onde um painel nasce quando nunca esteve em lugar nenhum.
    std::function<Home(const QString &)> home;
};

struct LayoutOpsResult {
    bool ok = false;
    QString error;
    LayoutModel model;
    // Uma linha por diferença visível (para o cartão de aprovação).
    QStringList changes;
};

LayoutOpsResult applyLayoutOps(const LayoutModel &start, const QJsonArray &ops, const LayoutOpsContext &context);

// Diferenças entre dois layouts em linhas curtas ("~ commands: closed → 0,0", "~ grid: 2x2 → 3x2").
QStringList describeLayoutChanges(const LayoutModel &before, const LayoutModel &after);

// Nomes dos presets embutidos, na ordem do menu.
QStringList layoutPresetNames();

} // namespace kai::ui::layout
