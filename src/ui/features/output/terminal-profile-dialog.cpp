#include "ui/features/output/terminal-profile-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = utils::tokens;
using core::DockerHost;
using core::TemplateKind;
using core::TemplateSpec;

namespace {

#if defined(Q_OS_WIN)
constexpr bool kWindowsHost = true;
#else
constexpr bool kWindowsHost = false;
#endif

QCheckBox *makeSwitch(const QString &text, QWidget *parent)
{
    auto *box = new QCheckBox(text, parent);
    box->setProperty("kaiRole", QStringLiteral("switch"));
    return box;
}

QString shellComboText(core::ShellFlavor s)
{
    switch (s) {
    case core::ShellFlavor::Posix:      return utils::tr(QStringLiteral("terminal_targets.shell.posix"));
    case core::ShellFlavor::PowerShell: return utils::tr(QStringLiteral("terminal_targets.shell.powershell"));
    case core::ShellFlavor::Cmd:        return utils::tr(QStringLiteral("terminal_targets.shell.cmd"));
    case core::ShellFlavor::Auto:       break;
    }
    return utils::tr(QStringLiteral("terminal_targets.shell.auto"));
}

} // namespace

TerminalProfileDialog::TerminalProfileDialog(const core::TerminalProfile &profile,
                                             std::optional<core::TemplateKind> presetKind, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("terminals.edit_row")));
    setSizeGripEnabled(true);
    buildUi(profile);

    m_loading = true;
    if (presetKind.has_value() && profile.commandTemplate.isEmpty()) {
        writeSpec(core::defaultSpec(*presetKind));
        m_shell->setCurrentIndex(m_shell->findData(int(core::defaultShellFor(*presetKind))));
        m_tty->setChecked(core::defaultPtyFor(*presetKind));
        showMode(false);
    } else if (const auto spec = core::parseTemplate(profile.commandTemplate, kWindowsHost);
               spec.has_value() && !profile.commandTemplate.isEmpty()) {
        writeSpec(*spec);
        showMode(false);
    } else {
        writeSpec(core::defaultSpec(TemplateKind::Wsl));
        showMode(true);
    }
    m_loading = false;
    updateVisibility();
    setMinimumWidth(560);
    // Cabe inteiro em telas comuns; em telas baixas o corpo rola e os botões continuam à vista.
    const QScreen *screen = QGuiApplication::primaryScreen();
    resize(560, screen ? qMin(570, int(screen->availableGeometry().height() * 0.9)) : 570);
    centerOnParent(this);
}

void TerminalProfileDialog::buildUi(const core::TerminalProfile &profile)
{
    // O corpo rola (telas baixas): os botões ficam fora da rolagem, sempre à vista.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    outer->setSpacing(tk::space(3));
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // O QSS global pinta todo QWidget (inclusive o viewport da rolagem) com o fundo escuro: aqui fica transparente.
    scroll->setObjectName(QStringLiteral("profileScroll"));
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#profileScroll, QScrollArea#profileScroll > QWidget#qt_scrollarea_viewport { background: transparent; }"));
    auto *body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("profileBody"));
    body->setStyleSheet(QStringLiteral("QWidget#profileBody { background: transparent; }"));
    scroll->setWidget(body);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(tk::space(3));
    outer->addWidget(scroll, 1);

    auto makeGrid = [](QWidget *card) {
        auto *grid = new QGridLayout(card);
        grid->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
        grid->setHorizontalSpacing(tk::space(4));
        grid->setVerticalSpacing(tk::space(3));
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        return grid;
    };

    // Um card só (formulário compacto): identificação, o tipo/modo e o comportamento.
    auto *card = layout_helpers::makeSurfaceCard(body);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    cardLayout->setSpacing(tk::space(3));
    auto *identity = new QWidget(card);
    identity->setObjectName(QStringLiteral("profileIdentity"));
    identity->setStyleSheet(QStringLiteral("QWidget#profileIdentity { background: transparent; }"));
    auto *identityGrid = makeGrid(identity);
    identityGrid->setContentsMargins(0, 0, 0, 0);
    identityGrid->setColumnStretch(0, 2);
    m_name = new QLineEdit(profile.name, identity);
    m_name->setPlaceholderText(utils::tr(QStringLiteral("terminal_targets.field.name.placeholder")));
    identityGrid->addWidget(layout_helpers::wrapWithLabel(identity, utils::tr(QStringLiteral("field.label.name")), m_name), 0, 0);
    m_icon = new IconPickerWidget(identity);
    m_icon->setSelectedIconName(profile.icon);
    identityGrid->addWidget(layout_helpers::wrapWithLabel(identity, utils::tr(QStringLiteral("command.field.icon")), m_icon), 0, 1);
    cardLayout->addWidget(identity);

    // --- O formulário do tipo (modo simples) ou o texto cru (modo avançado) ---
    m_advanced = makeSwitch(utils::tr(QStringLiteral("terminal_profile.advanced")), this);
    connect(m_advanced, &QCheckBox::clicked, this, [this](bool on) { setAdvanced(on); });

    m_pages = new QStackedWidget(card);
    m_pages->setObjectName(QStringLiteral("profilePages"));
    m_pages->setStyleSheet(QStringLiteral("QStackedWidget#profilePages { background: transparent; }"));
    cardLayout->addWidget(m_pages);

    auto *simple = new QWidget(m_pages);
    simple->setObjectName(QStringLiteral("profileSimplePage"));
    simple->setStyleSheet(QStringLiteral("QWidget#profileSimplePage { background: transparent; }"));
    m_simpleGrid = new QGridLayout(simple);
    m_simpleGrid->setContentsMargins(0, 0, 0, 0);
    m_simpleGrid->setHorizontalSpacing(tk::space(4));
    m_simpleGrid->setVerticalSpacing(tk::space(3));
    m_simpleGrid->setColumnStretch(0, 1);
    m_simpleGrid->setColumnStretch(1, 1);

    auto addCell = [this, simple](QWidget *widget, int span) {
        m_cells.append({widget, span});
        widget->setParent(simple);
        return widget;
    };

    m_kind = new QComboBox(simple);
    m_kind->addItem(utils::tr(QStringLiteral("terminal_targets.preset.wsl")), int(TemplateKind::Wsl));
    m_kind->addItem(utils::tr(QStringLiteral("terminal_profile.kind.docker")), int(TemplateKind::Docker));
    m_kind->addItem(utils::tr(QStringLiteral("terminal_targets.preset.powershell")), int(TemplateKind::PowerShell));
    m_kind->addItem(utils::tr(QStringLiteral("terminal_targets.preset.local_shell")), int(TemplateKind::LocalShell));
    addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.type")), m_kind), 1);

    m_dockerHost = new QComboBox(simple);
    m_dockerHost->addItem(utils::tr(QStringLiteral("terminal_profile.host.local")), int(DockerHost::Local));
    m_dockerHost->addItem(utils::tr(QStringLiteral("terminal_profile.host.wsl")), int(DockerHost::Wsl));
    m_dockerHost->addItem(utils::tr(QStringLiteral("terminal_profile.host.ssh")), int(DockerHost::Ssh));
    m_dockerHostRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.docker_host")), m_dockerHost), 1);

    m_distro = new QLineEdit(simple);
    m_distro->setPlaceholderText(utils::tr(QStringLiteral("terminal_profile.distro.placeholder")));
    m_distroRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.distro")), m_distro), 1);

    m_sshTarget = new QLineEdit(simple);
    m_sshTarget->setPlaceholderText(QStringLiteral("user@host"));
    m_sshRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.ssh_target")), m_sshTarget), 1);

    m_psExe = new QComboBox(simple);
    m_psExe->addItems({QStringLiteral("powershell.exe"), QStringLiteral("pwsh.exe")});
    m_psRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.ps.exe")), m_psExe), 1);

    {
        auto *host = new QWidget(simple);
        host->setObjectName(QStringLiteral("profileContainerHost"));
        host->setStyleSheet(QStringLiteral("QWidget#profileContainerHost { background: transparent; }"));
        auto *vbox = new QVBoxLayout(host);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(tk::space(1));
        auto *row = new QHBoxLayout();
        row->setSpacing(tk::space(2));
        m_container = new QComboBox(host);
        m_container->setEditable(true);
        m_container->setInsertPolicy(QComboBox::NoInsert);
        m_container->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("terminal_profile.container.placeholder")));
        row->addWidget(m_container, 1);
        m_refresh = makeIconButton(host, QStringLiteral("refresh-cw"), utils::tr(QStringLiteral("terminal_profile.containers.refresh")),
                                   QColor(tk::accent()));
        connect(m_refresh, &QToolButton::clicked, this, [this]() { refreshContainers(); });
        row->addWidget(m_refresh);
        vbox->addLayout(row);
        m_containerStatus = new QLabel(host);
        m_containerStatus->setProperty("kaiRole", QStringLiteral("caption"));
        m_containerStatus->setVisible(false);
        vbox->addWidget(m_containerStatus);
        m_containerRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.container")), host), 2);
    }

    m_workingDir = new QLineEdit(simple);
    m_workingDir->setPlaceholderText(utils::tr(QStringLiteral("terminal_profile.working_dir.placeholder")));
    m_workingDirRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.working_dir")), m_workingDir), 1);

    m_interpreter = new QComboBox(simple);
    m_interpreter->setEditable(true);
    m_interpreter->addItems({QStringLiteral("bash"), QStringLiteral("sh"), QStringLiteral("zsh"), QStringLiteral("ash"),
                             QStringLiteral("fish")});
    m_interpreterRow = addCell(layout_helpers::wrapWithLabel(simple, utils::tr(QStringLiteral("terminal_profile.interpreter")), m_interpreter), 1);

    {
        auto *host = new QWidget(simple);
        host->setObjectName(QStringLiteral("profileFlagsHost"));
        host->setStyleSheet(QStringLiteral("QWidget#profileFlagsHost, QWidget#profileFlagsHost QCheckBox { background: transparent; }"));
        auto *row = new QHBoxLayout(host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(tk::space(4));
        m_login = makeSwitch(utils::tr(QStringLiteral("terminal_profile.flag.login")), host);
        m_interactive = makeSwitch(utils::tr(QStringLiteral("terminal_profile.flag.interactive")), host);
        m_dockerTty = makeSwitch(utils::tr(QStringLiteral("terminal_profile.flag.tty")), host);
        row->addWidget(m_login);
        row->addWidget(m_interactive);
        row->addWidget(m_dockerTty);
        row->addStretch();
        m_flagsRow = addCell(host, 2);
        m_dockerTtyRow = m_dockerTty;
    }
    {
        auto *host = new QWidget(simple);
        host->setObjectName(QStringLiteral("profilePsFlagsHost"));
        host->setStyleSheet(QStringLiteral("QWidget#profilePsFlagsHost, QWidget#profilePsFlagsHost QCheckBox { background: transparent; }"));
        auto *row = new QHBoxLayout(host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(tk::space(4));
        m_noProfile = makeSwitch(utils::tr(QStringLiteral("terminal_profile.ps.no_profile")), host);
        m_bypass = makeSwitch(utils::tr(QStringLiteral("terminal_profile.ps.bypass")), host);
        row->addWidget(m_noProfile);
        row->addWidget(m_bypass);
        row->addStretch();
        m_psFlagsRow = addCell(host, 2);
    }

    m_pages->addWidget(simple);

    m_template = new QPlainTextEdit(profile.commandTemplate, m_pages);
    m_template->setPlaceholderText(QStringLiteral("wsl -d Ubuntu -- bash -lc '{{command}}'"));
    m_template->setMinimumHeight(tk::space(20));
    m_template->setToolTip(utils::tr(QStringLiteral("terminal_targets.field.template.tip")));
    m_pages->addWidget(layout_helpers::wrapWithLabel(m_pages, utils::tr(QStringLiteral("terminal_targets.field.template")), m_template));

    // --- Comportamento (vale nos dois modos) ---
    auto *behavior = new QWidget(card);
    behavior->setObjectName(QStringLiteral("profileBehavior"));
    behavior->setStyleSheet(QStringLiteral("QWidget#profileBehavior { background: transparent; }"));
    auto *behaviorGrid = makeGrid(behavior);
    behaviorGrid->setContentsMargins(0, 0, 0, 0);
    m_shell = new QComboBox(behavior);
    for (const core::ShellFlavor flavor : {core::ShellFlavor::Auto, core::ShellFlavor::Posix, core::ShellFlavor::PowerShell,
                                           core::ShellFlavor::Cmd}) {
        m_shell->addItem(shellComboText(flavor), int(flavor));
    }
    m_shell->setCurrentIndex(m_shell->findData(int(profile.shell)));
    m_shell->setToolTip(utils::tr(QStringLiteral("terminal_targets.field.shell.tip")));
    behaviorGrid->addWidget(layout_helpers::wrapWithLabel(behavior, utils::tr(QStringLiteral("terminal_targets.field.shell")), m_shell), 0, 0);
    auto *switches = new QWidget(behavior);
    switches->setObjectName(QStringLiteral("profileSwitches"));
    switches->setStyleSheet(QStringLiteral("QWidget#profileSwitches, QWidget#profileSwitches QCheckBox { background: transparent; }"));
    auto *switchesLayout = new QVBoxLayout(switches);
    switchesLayout->setContentsMargins(0, 0, 0, 0);
    switchesLayout->setSpacing(tk::space(2));
    m_tty = makeSwitch(utils::tr(QStringLiteral("terminal_targets.field.tty_full")), switches);
    m_tty->setChecked(profile.usePty);
    m_tty->setToolTip(utils::tr(QStringLiteral("terminal_targets.field.tty.tip")));
    m_default = makeSwitch(utils::tr(QStringLiteral("terminal_targets.field.is_default")), switches);
    m_default->setChecked(profile.isDefault);
    m_default->setToolTip(utils::tr(QStringLiteral("terminal_targets.field.is_default.tip")));
    switchesLayout->addWidget(m_tty);
    switchesLayout->addWidget(m_default);
    behaviorGrid->addWidget(switches, 0, 1, Qt::AlignBottom);
    cardLayout->addWidget(behavior);
    bodyLayout->addWidget(card);
    bodyLayout->addStretch();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // O modo avançado fica na linha dos botões, à esquerda.
    auto *footer = new QHBoxLayout();
    footer->addWidget(m_advanced);
    footer->addStretch();
    footer->addWidget(box);
    outer->addLayout(footer);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this]() { kindChanged(); });
    connect(m_dockerHost, &QComboBox::currentIndexChanged, this, [this]() { updateVisibility(); });
}

TemplateSpec TerminalProfileDialog::readSpec() const
{
    TemplateSpec spec = core::defaultSpec(TemplateKind(m_kind->currentData().toInt()));
    spec.dockerHost = DockerHost(m_dockerHost->currentData().toInt());
    spec.distro = m_distro->text().trimmed();
    spec.sshTarget = m_sshTarget->text().trimmed();
    spec.container = m_container->currentText().trimmed();
    spec.workingDir = m_workingDir->text().trimmed();
    spec.shell = m_interpreter->currentText().trimmed();
    spec.login = m_login->isChecked();
    spec.interactive = m_interactive->isChecked();
    spec.dockerTty = m_dockerTty->isChecked();
    spec.powerShellExe = m_psExe->currentText();
    spec.noProfile = m_noProfile->isChecked();
    spec.bypassPolicy = m_bypass->isChecked();
    return spec;
}

void TerminalProfileDialog::writeSpec(const TemplateSpec &spec)
{
    const bool wasLoading = m_loading;
    m_loading = true;
    m_kind->setCurrentIndex(m_kind->findData(int(spec.kind)));
    m_dockerHost->setCurrentIndex(m_dockerHost->findData(int(spec.dockerHost)));
    m_distro->setText(spec.distro);
    m_sshTarget->setText(spec.sshTarget);
    m_container->setCurrentText(spec.container);
    m_workingDir->setText(spec.workingDir);
    m_interpreter->setCurrentText(spec.shell);
    m_login->setChecked(spec.login);
    m_interactive->setChecked(spec.interactive);
    m_dockerTty->setChecked(spec.dockerTty);
    m_psExe->setCurrentText(spec.powerShellExe);
    m_noProfile->setChecked(spec.noProfile);
    m_bypass->setChecked(spec.bypassPolicy);
    m_loading = wasLoading;
    updateVisibility();
}

void TerminalProfileDialog::kindChanged()
{
    if (m_loading) {
        return;
    }
    // Trocar o tipo recomeça dos valores dele (o que não vale para o tipo novo não fica escondido guardado).
    const TemplateKind kind = TemplateKind(m_kind->currentData().toInt());
    TemplateSpec spec = core::defaultSpec(kind);
    spec.container = m_container->currentText().trimmed();
    writeSpec(spec);
    m_shell->setCurrentIndex(m_shell->findData(int(core::defaultShellFor(kind))));
    m_tty->setChecked(core::defaultPtyFor(kind));
}

void TerminalProfileDialog::updateVisibility()
{
    const TemplateKind kind = TemplateKind(m_kind->currentData().toInt());
    const DockerHost host = DockerHost(m_dockerHost->currentData().toInt());
    const bool wsl = kind == TemplateKind::Wsl;
    const bool docker = kind == TemplateKind::Docker;
    const bool ps = kind == TemplateKind::PowerShell;
    const bool local = kind == TemplateKind::LocalShell;
    m_dockerHostRow->setVisible(docker);
    m_distroRow->setVisible(wsl || (docker && host == DockerHost::Wsl));
    m_sshRow->setVisible(docker && host == DockerHost::Ssh);
    m_containerRow->setVisible(docker);
    m_workingDirRow->setVisible(wsl || docker);
    m_interpreterRow->setVisible(wsl || docker || local);
    m_flagsRow->setVisible(wsl || docker || local);
    m_dockerTtyRow->setVisible(docker);
    m_psRow->setVisible(ps);
    m_psFlagsRow->setVisible(ps);
    m_interpreter->lineEdit()->setPlaceholderText(local ? utils::tr(QStringLiteral("terminal_profile.interpreter.default")) : QString());
    relayoutSimpleForm();
}

// Os campos visíveis se acomodam em duas colunas, sem buracos onde os outros tipos têm campos escondidos.
void TerminalProfileDialog::relayoutSimpleForm()
{
    for (const Cell &cell : std::as_const(m_cells)) {
        m_simpleGrid->removeWidget(cell.widget);
    }
    int row = 0;
    int column = 0;
    for (const Cell &cell : std::as_const(m_cells)) {
        if (cell.widget->isHidden()) {
            continue;
        }
        if (cell.span == 2 && column != 0) {
            ++row;
            column = 0;
        }
        m_simpleGrid->addWidget(cell.widget, row, column, 1, cell.span);
        column += cell.span;
        if (column >= 2) {
            ++row;
            column = 0;
        }
    }
}

void TerminalProfileDialog::showMode(bool advanced)
{
    m_pages->setCurrentIndex(advanced ? 1 : 0);
    m_advanced->setChecked(advanced);
}

void TerminalProfileDialog::setAdvanced(bool advanced)
{
    if (advanced == advancedMode()) {
        showMode(advanced);
        return;
    }
    if (advanced) {
        // O texto cru começa no que o formulário gera.
        m_template->setPlainText(core::buildTemplate(readSpec(), kWindowsHost));
    } else {
        const auto spec = core::parseTemplate(m_template->toPlainText(), kWindowsHost);
        if (spec.has_value()) {
            writeSpec(*spec);
        } else if (!m_template->toPlainText().trimmed().isEmpty()
                   && !confirmYesNo(this, utils::tr(QStringLiteral("terminals.edit_row")),
                                    utils::tr(QStringLiteral("terminal_profile.advanced_discard")))) {
            showMode(true);
            return;
        }
    }
    showMode(advanced);
}

bool TerminalProfileDialog::advancedMode() const
{
    return m_pages->currentIndex() == 1;
}

QString TerminalProfileDialog::currentTemplate() const
{
    return advancedMode() ? m_template->toPlainText() : core::buildTemplate(readSpec(), kWindowsHost);
}

core::TerminalProfile TerminalProfileDialog::profile() const
{
    core::TerminalProfile profile;
    profile.name = m_name->text().trimmed();
    profile.icon = m_icon->selectedIconName();
    profile.commandTemplate = currentTemplate();
    profile.shell = core::ShellFlavor(m_shell->currentData().toInt());
    profile.usePty = m_tty->isChecked();
    profile.isDefault = m_default->isChecked();
    return profile;
}

void TerminalProfileDialog::accept()
{
    // Sem container (ou destino SSH) o template sai com um buraco: o formulário não deixa confirmar assim.
    if (!advancedMode() && TemplateKind(m_kind->currentData().toInt()) == TemplateKind::Docker) {
        const TemplateSpec spec = readSpec();
        QWidget *missing = spec.container.isEmpty() ? static_cast<QWidget *>(m_container)
            : (spec.dockerHost == DockerHost::Ssh && spec.sshTarget.isEmpty() ? static_cast<QWidget *>(m_sshTarget) : nullptr);
        if (missing) {
            QMessageBox::information(this, utils::tr(QStringLiteral("terminals.edit_row")),
                                     utils::tr(QStringLiteral("terminal_profile.missing_field")));
            missing->setFocus();
            return;
        }
    }
    QDialog::accept();
}

// ------------------------------------------------------------------------------------------------------- containers

bool TerminalProfileDialog::containerListBusy() const
{
    return m_listProcess != nullptr;
}

void TerminalProfileDialog::refreshContainers()
{
    if (m_listProcess) {
        return;
    }
    const core::ContainerListCommand command =
        m_listOverride.value_or(core::containerListCommand(readSpec(), kWindowsHost));
    m_refresh->setEnabled(false);
    m_containerStatus->setVisible(false);
    m_listProcess = new QProcess(this);
    m_listProcess->setProgram(command.program);
    m_listProcess->setArguments(command.arguments);
    connect(m_listProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        finishContainerList(status == QProcess::NormalExit ? exitCode : -1, m_listProcess->readAllStandardOutput());
    });
    connect(m_listProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finishContainerList(-1, QByteArray());
        }
    });
    // Um host fora do ar (SSH, WSL parado) não pode deixar o botão preso.
    QTimer::singleShot(15000, m_listProcess, [this]() {
        if (m_listProcess && m_listProcess->state() != QProcess::NotRunning) {
            m_listProcess->kill();
        }
    });
    m_listProcess->start();
}

void TerminalProfileDialog::finishContainerList(int exitCode, const QByteArray &out)
{
    if (!m_listProcess) {
        return; // finished e errorOccurred para o mesmo processo
    }
    m_listProcess->deleteLater();
    m_listProcess = nullptr;
    m_refresh->setEnabled(true);
    if (exitCode != 0) {
        m_containerStatus->setText(utils::tr(QStringLiteral("terminal_profile.containers.error")));
        m_containerStatus->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::errorFg()));
        m_containerStatus->setVisible(true);
        return;
    }
    const QString current = m_container->currentText();
    m_container->clear();
    int count = 0;
    for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.trimmed().split(QLatin1Char('\t'));
        if (parts.first().isEmpty()) {
            continue;
        }
        m_container->addItem(parts.first());
        if (parts.size() > 1) {
            m_container->setItemData(m_container->count() - 1, parts.at(1), Qt::ToolTipRole);
        }
        ++count;
    }
    m_container->setCurrentText(current); // o que já estava escolhido/digitado continua
    m_containerStatus->setText(utils::tr(QStringLiteral("terminal_profile.containers.found")).arg(count));
    m_containerStatus->setStyleSheet(QString());
    m_containerStatus->setVisible(true);
}

} // namespace kai::ui
