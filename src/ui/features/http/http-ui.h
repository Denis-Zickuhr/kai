#pragma once

#include <QString>
#include <QWidget>

namespace kai::ui {

// O QSS global pinta TODO QWidget/QLabel/QCheckBox com o fundo escuro do tema; um contêiner meu sobre o fundo de um
// card viraria uma faixa preta. Isto o deixa transparente (e os rótulos/caixas dentro dele), só ele — qualificado
// pelo objectName para não vazar para o resto da tela.
inline void makeTransparentHost(QWidget *host, const QString &name)
{
    host->setObjectName(name);
    host->setStyleSheet(QStringLiteral(
        "QWidget#%1, QWidget#%1 QLabel, QWidget#%1 QCheckBox { background: transparent; }").arg(name));
}

} // namespace kai::ui
