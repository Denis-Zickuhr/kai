#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>

class QWidget;

namespace kai::ui {

// Filtro global: para toda janela de nível superior que é um QDialog, registra no log o início da abertura (Show) e o
// tempo até o primeiro desenho. Não inclui a construção do diálogo (essa fica a cargo de utils::ScopedTimer).
class OpenTimeFilter : public QObject {
    Q_OBJECT

public:
    explicit OpenTimeFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static QString describe(const QWidget *widget);

    QHash<const QObject *, QElapsedTimer> m_pending;
};

} // namespace kai::ui
