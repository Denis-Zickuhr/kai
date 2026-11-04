#pragma once

#include <QDateTime>
#include <QList>
#include <QWidget>

#include <optional>

#include "utils/logger.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace kai::ui {

// Os logs do Kai como painel do layout: o histórico do arquivo (a cauda, lida fora da GUI) mais o que chega ao vivo.
// Filtra por nível mínimo e por texto, pausa/segue o fim, copia o que está à vista e limpa. Uma linha por entrada, cor por
// nível; leve (teto de entradas, sem realce por palavra).
class LogsPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogsPanel(QWidget *parent = nullptr);

    // Uma linha do arquivo de log ("2026-01-01T10:00:00 [WARN] [Tag] mensagem"); vazio se não for uma.
    static std::optional<utils::LogEntry> parseFileLine(const QString &line);
    // A entrada como texto de uma linha (o que a visão mostra e o que "copiar" leva).
    static QString formatEntry(const utils::LogEntry &entry);

    int entryCount() const { return int(m_entries.size()); }
    int visibleCount() const { return m_visible; }
    bool paused() const { return m_paused; }

    void setMinLevel(utils::LogLevel level);
    void setSearch(const QString &text);
    void setPaused(bool paused);
    void clear();
    // O texto das linhas à vista, uma por linha.
    QString visibleText() const;

    QPlainTextEdit *view() const { return m_view; }

    // Entrada vinda do Logger (ou de um teste); o histórico do arquivo entra por `addHistory`.
    void appendEntry(const utils::LogEntry &entry);
    void addHistory(const QList<utils::LogEntry> &history);

private:
    void applyMinLevel(utils::LogLevel level);
    bool accepts(const utils::LogEntry &entry) const;
    void appendToView(const utils::LogEntry &entry);
    void rebuildView();
    void refreshCount();
    void loadHistory();

    QPlainTextEdit *m_view = nullptr;
    QComboBox *m_levelFilter = nullptr;
    QLineEdit *m_searchField = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QLabel *m_countLabel = nullptr;

    QList<utils::LogEntry> m_entries;
    utils::LogLevel m_minLevel = utils::LogLevel::Debug;
    QString m_search;
    bool m_paused = false;
    int m_visible = 0;
    int m_held = 0; // entradas que chegaram com a visão pausada
    static constexpr int kMaxEntries = 5000;
};

} // namespace kai::ui
