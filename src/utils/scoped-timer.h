#pragma once

#include "utils/logger.h"

#include <QElapsedTimer>
#include <QString>

#include <utility>

namespace kai::utils {

// Registra "<label>: início" ao nascer e "<label>: concluído em N ms" ao sair do escopo (Logger::info).
class ScopedTimer {
public:
    ScopedTimer(QString tag, QString label)
        : m_tag(std::move(tag)), m_label(std::move(label))
    {
        Logger::info(m_tag, QStringLiteral("%1: início").arg(m_label));
        m_clock.start();
    }

    ~ScopedTimer()
    {
        Logger::info(m_tag, QStringLiteral("%1: concluído em %2 ms").arg(m_label).arg(m_clock.elapsed()));
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

private:
    QString m_tag;
    QString m_label;
    QElapsedTimer m_clock;
};

} // namespace kai::utils
