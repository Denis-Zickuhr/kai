#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>
#include <optional>

#include "core/config-manager.h"
#include "core/terminal-template.h"

class QCheckBox;
class QGridLayout;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QStackedWidget;
class QToolButton;

namespace kai::ui {

class IconPickerWidget;

// Formulário de um Perfil de Execução. Modo simples: o tipo (WSL, Docker, PowerShell, shell local) e só os campos que ele
// precisa; o template sai pronto (core::buildTemplate). Modo avançado: o texto cru do template. Um perfil existente abre
// no simples quando o formulário o reproduz exatamente (core::parseTemplate), senão no avançado.
class TerminalProfileDialog : public QDialog {
    Q_OBJECT

public:
    // `presetKind`: abre um perfil novo já no modo simples com esse tipo.
    TerminalProfileDialog(const core::TerminalProfile &profile, std::optional<core::TemplateKind> presetKind,
                          QWidget *parent = nullptr);

    core::TerminalProfile profile() const;
    bool advancedMode() const;
    // O usuário liga/desliga o modo avançado (os testes usam).
    void requestAdvanced(bool advanced) { setAdvanced(advanced); }
    QComboBox *kindCombo() const { return m_kind; }
    QPlainTextEdit *templateEdit() const { return m_template; }
    // O template que o formulário gera agora (modo simples) ou o texto cru (modo avançado).
    QString currentTemplate() const;

    // Containers (docker ps) do local escolhido: assíncrono; os testes substituem o comando.
    void refreshContainers();
    void setContainerListOverride(const core::ContainerListCommand &command) { m_listOverride = command; }
    QComboBox *containerCombo() const { return m_container; }
    // O último "refresh" terminou (com ou sem erro).
    bool containerListBusy() const;

    void accept() override;

private:
    void buildUi(const core::TerminalProfile &profile);
    core::TemplateSpec readSpec() const;
    void writeSpec(const core::TemplateSpec &spec);
    void setAdvanced(bool advanced); // a troca feita pelo usuário (converte o conteúdo entre os modos)
    void showMode(bool advanced);    // só mostra a página
    void kindChanged();
    void updateVisibility();
    void relayoutSimpleForm();
    void finishContainerList(int exitCode, const QByteArray &out);

    QLineEdit *m_name = nullptr;
    IconPickerWidget *m_icon = nullptr;
    QCheckBox *m_advanced = nullptr;
    QStackedWidget *m_pages = nullptr;
    QComboBox *m_kind = nullptr;
    QComboBox *m_dockerHost = nullptr;
    QLineEdit *m_distro = nullptr;
    QLineEdit *m_sshTarget = nullptr;
    QComboBox *m_container = nullptr;
    QToolButton *m_refresh = nullptr;
    QLabel *m_containerStatus = nullptr;
    QLineEdit *m_workingDir = nullptr;
    QComboBox *m_interpreter = nullptr;
    QCheckBox *m_login = nullptr;
    QCheckBox *m_interactive = nullptr;
    QCheckBox *m_dockerTty = nullptr;
    QComboBox *m_psExe = nullptr;
    QCheckBox *m_noProfile = nullptr;
    QCheckBox *m_bypass = nullptr;
    QPlainTextEdit *m_template = nullptr;
    QComboBox *m_shell = nullptr;
    QCheckBox *m_tty = nullptr;
    QCheckBox *m_default = nullptr;
    // Cada campo vem dentro de um wrapper (rótulo + campo): são eles que aparecem e somem.
    QWidget *m_dockerHostRow = nullptr;
    QWidget *m_distroRow = nullptr;
    QWidget *m_sshRow = nullptr;
    QWidget *m_containerRow = nullptr;
    QWidget *m_workingDirRow = nullptr;
    QWidget *m_interpreterRow = nullptr;
    QWidget *m_flagsRow = nullptr;
    QWidget *m_dockerTtyRow = nullptr;
    QWidget *m_psRow = nullptr;
    QWidget *m_psFlagsRow = nullptr;

    struct Cell {
        QWidget *widget;
        int span;
    };
    QGridLayout *m_simpleGrid = nullptr;
    QVector<Cell> m_cells; // na ordem em que aparecem

    QProcess *m_listProcess = nullptr;
    std::optional<core::ContainerListCommand> m_listOverride;
    bool m_loading = false; // preenchendo os campos: nada de recalcular o modelo
};

} // namespace kai::ui
