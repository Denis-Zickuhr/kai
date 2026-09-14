#pragma once

#include <QDialog>
#include <QVector>

#include "utils/logger.h"

class QPlainTextEdit;
class QComboBox;
class QToolButton;

namespace kai::ui {

// Janela real de logs da aplicação (substitui a antiga sessão de
// "Debug", que apenas expandia/colapsava o Terminal Drawer sem exibir
// nenhuma informação útil de diagnóstico). Se inscreve em
// utils::LoggerSink::logEmitted e acumula um histórico rolante de
// entradas, com filtro por nível mínimo (Debug/Info/Warning/Error) e botão
// de limpar/copiar. Não é modal: pode ficar aberta enquanto o usuário
// continua usando o Kai normalmente.
class LogViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit LogViewerDialog(QWidget *parent = nullptr);

private slots:
    void handleLogEmitted(const utils::LogEntry &entry);
    void handleClearClicked();
    void handleLevelFilterChanged(int index);

private:
    void setupUi();
    // Popula a janela com o conteúdo já existente no arquivo de log.
    void loadExistingLogFile();
    void appendEntry(const utils::LogEntry &entry);
    void rebuildView();
    static QString formatEntry(const utils::LogEntry &entry);
    static QString levelLabel(utils::LogLevel level);

    QPlainTextEdit *m_logView = nullptr;
    QComboBox *m_levelFilter = nullptr;
    QToolButton *m_clearButton = nullptr;

    QVector<utils::LogEntry> m_entries;
    utils::LogLevel m_minLevel = utils::LogLevel::Debug;
    static constexpr int kMaxEntries = 2000;
};

} // namespace kai::ui
