#pragma once

#include "ui/main-window.h"

#include <memory>

#include <QDateTime>
#include <QMap>

#include "core/environment-manager.h"
#include "core/models.h"
#include "engine/execution-pipeline.h"

namespace kai::ui {

// Uma execução de ação de pasta em andamento: tudo que o pipeline e o ambiente
// referenciam por ponteiro precisa viver tanto quanto ela.
struct MainWindow::ActionRun {
    engine::ExecutionPipeline *pipeline = nullptr; // filho do MainWindow
    std::unique_ptr<core::EnvironmentManager> env;
    QMap<QString, core::Command> allCommands;      // pra resolver hooks
    core::Command command;                         // o comando virtual (id = ação+pasta)
    QDateTime startedAt;
};

} // namespace kai::ui
