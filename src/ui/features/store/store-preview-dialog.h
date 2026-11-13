#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"
#include "core/store-model.h"
#include "core/store-preview.h"
#include "engine/store-installer.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QResizeEvent;
class QSplitter;

namespace kai::ui {

class FolderPickerWidget;

// Prévia ANTES de qualquer coisa tocar no disco do usuário (specs/14-store.md §5): o que será criado, o código de cada
// comando em visor somente leitura, binários exigidos (achados ou não), URLs que os comandos chamam, e as decisões
// (cron/auto_run por comando, padrão desligado). Hooks nunca entram. O diálogo só monta o pedido; quem importa é o app.
class StorePreviewDialog : public QDialog {
    Q_OBJECT

public:
    StorePreviewDialog(const engine::StoreStagedPackage &staged, const QString &providerName, const QString &trustLabel,
                       const QString &signatureLabel, const QVector<core::Folder> &folders, QWidget *parent = nullptr);

    // Decisões e destino escolhidos; o chamador completa provider/pacote/pastas de staging.
    void fillRequest(core::StoreInstallRequest &request) const;

protected:
    // Clicar em qualquer ponto da linha marca/desmarca o comando (não só na caixa).
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Janela estreita: a lista de comandos fica em cima do código em vez de ao lado.
    void resizeEvent(QResizeEvent *event) override;

private:
    const engine::StoreStagedPackage m_staged;
    QListWidget *m_commands = nullptr;
    QSplitter *m_split = nullptr;
    QPlainTextEdit *m_code = nullptr;
    QPushButton *m_installButton = nullptr;
    QLabel *m_selectionCount = nullptr;
    void updateSelection();
    FolderPickerWidget *m_destination = nullptr;
    struct Decision {
        QString name;
        QCheckBox *cron = nullptr;
        QCheckBox *autoRun = nullptr;
    };
    QVector<Decision> m_decisions;
};

} // namespace kai::ui
