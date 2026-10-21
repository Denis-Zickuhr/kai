#pragma once

#include <QMenu>
#include <QString>
#include <QStringList>

namespace kai::ui {

// Menu de contexto da bandeja. Só desenha e emite sinais: quem o possui
// (MainWindow) entrega o estado atual em rebuild() a cada abertura e liga os
// sinais aos slots que já existem. Toda ação aqui é pedida pelo usuário.
class TrayMenu : public QMenu {
    Q_OBJECT
public:
    struct State {
        int runningCount = 0;
        bool notificationsEnabled = false;
        QStringList environmentIds;
        QStringList environmentNames;
        QString activeEnvironmentId;
        QStringList themes;
        QString activeTheme;
    };

    explicit TrayMenu(QWidget *parent = nullptr);

    void rebuild(const State &state);

    // QSS do menu a partir dos tokens do tema e do estilo de cantos atual.
    static QString menuStyleSheet();

signals:
    void toggleRequested();
    void quickRunRequested();
    void stopAllRequested();
    void notificationsHistoryRequested();
    void notificationsToggled(bool enabled);
    void environmentChosen(const QString &environmentId);
    void themeChosen(const QString &themeName);
    void settingsRequested();
    void logsRequested();
    void importRequested();
    void quitRequested();
};

} // namespace kai::ui
