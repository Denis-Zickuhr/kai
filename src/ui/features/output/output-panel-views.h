#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <functional>

class QLabel;
class QTabBar;
class QTableWidget;
class QToolButton;
class QWidget;

namespace kai::ui {

class CodeOutputView;
class OutputMetricsHeader;

namespace outputview {

// Faixa de overflow do cabeçalho: ele cresce enquanto há overflow (a linha de abas mantém a altura normal). Gap =
// distância até a linha de abas; Inset = respiro até a linha que separa do corpo da Saída.
constexpr int kHeaderOverflowInset = 4;
constexpr int kHeaderOverflowGap = 2;

struct RequestViewWidgets {
    QWidget *page = nullptr;
    OutputMetricsHeader *metricsHeader = nullptr;
    QWidget *urlBar = nullptr;
    QLabel *methodBadge = nullptr;
    QLabel *urlLabel = nullptr;
    QWidget *bodyHeader = nullptr;   // título + tamanho + formatar + copiar (some quando não há corpo)
    QLabel *bodyMeta = nullptr;
    QToolButton *formatButton = nullptr;
    QLabel *infoLabel = nullptr;
    QWidget *errorBanner = nullptr;  // aparece quando não houve resposta HTTP
    QLabel *errorLabel = nullptr;
    QLabel *headersCount = nullptr;
    QTableWidget *headers = nullptr;
    CodeOutputView *body = nullptr;
};

QString requestUrlBarQss();
QString keyValueTableQss();
QTableWidget *makeKeyValueTable(QWidget *parent, const QString &keyHeader, const QString &valueHeader);
void fillKeyValueTable(QTableWidget *table, const QList<QPair<QString, QString>> &rows);
void updateHttpVerbPill(QLabel *badge, const QString &method);
RequestViewWidgets makeRequestView(QWidget *parent);
QString ucFirst(const QString &text);
void applyTabBarStyle(QTabBar *tabBar, int barHeight);

} // namespace outputview
} // namespace kai::ui
