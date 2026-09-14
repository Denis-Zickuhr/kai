#include "ui/features/output/terminal-profiles-editor-widget.h"

#include "ui/shared/table-utils.h"
#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QColor>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QStyle>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPoint>
#include <QResizeEvent>
#include <QSizePolicy>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

// Formato MIME próprio pro drag-reorder entre cards (ver DragHandleLabel /
// ProfileCardFrame). Carrega só o índice (linha) de origem, como texto.
const char *kProfileRowMime = "application/x-kai-profile-row";

// Label que elide (…) o próprio texto conforme a largura disponível, em vez
// de forçar a linha inteira a crescer. Bug reportado: um nome de perfil
// longo (ex: "Acessar container (docker no WSL)") empurrava os botões de
// ação (editar/excluir/"Definir Padrão") pra fora da área visível do card,
// em vez de truncar — QLabel comum sempre reporta a largura do texto SEM
// quebra como preferida, então o layout crescia pra caber, e isso se
// propagava pro card, pra lista e pro diálogo de Configurações inteiro.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &text, bool bold, QWidget *parent = nullptr)
        : QLabel(parent), m_fullText(text)
    {
        if (bold) {
            QFont f = font();
            f.setBold(true);
            setFont(f);
        }
        setToolTip(text);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        const QFontMetrics fm(font());
        setText(fm.elidedText(m_fullText, Qt::ElideRight, width()));
    }

    QString m_fullText;
};

// Rótulo curto do sabor para a coluna.
// Nomes de sabor de shell: termos técnicos (não prosa), mantidos idênticos
// em en/pt no pacote i18n — passam por utils::tr por consistência de política,
// mas o valor pt é igual ao en de propósito, já que shellFromComboText()
// compara o texto do combo contra estes MESMOS literais para round-trip.
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
core::ShellFlavor shellFromComboText(const QString &t)
{
    if (t == QLatin1String("POSIX"))      return core::ShellFlavor::Posix;
    if (t == QLatin1String("PowerShell")) return core::ShellFlavor::PowerShell;
    if (t == QLatin1String("Cmd"))        return core::ShellFlavor::Cmd;
    return core::ShellFlavor::Auto;
}

// Alça de arrastar (⋮⋮) — pedido do usuário, "tenta drag-reorder, corta se
// ficar desproporcional". Ficou compacto o bastante para não arriscar o
// resto do widget: só ESTA label inicia o QDrag (o resto do card continua
// só clicável para editar/definir padrão/excluir, sem o gesto de arrastar
// disputando o clique). O card ONDE cai o drop (ProfileCardFrame) decide a
// reordenação de verdade.
class DragHandleLabel : public QLabel {
public:
    explicit DragHandleLabel(QWidget *parent) : QLabel(QStringLiteral("⋮⋮"), parent)
    {
        setCursor(Qt::OpenHandCursor);
        setProperty("kaiRole", QStringLiteral("caption"));
        setToolTip(utils::tr(QStringLiteral("terminals.drag_hint")));
    }

    int row = -1;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && row >= 0
            && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            auto *drag = new QDrag(this);
            auto *mime = new QMimeData();
            mime->setData(QLatin1String(kProfileRowMime), QByteArray::number(row));
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
        }
        QLabel::mouseMoveEvent(event);
    }

private:
    QPoint m_pressPos;
};

// Card de UM perfil (mockup: borda de accent quando é o Padrão, ícone
// pool/estrela + nome + badge, preview do comando em caixa monoespaçada,
// ações inline). Aceita DROP do DragHandleLabel de outro card para
// reordenar (ver TerminalProfilesEditorWidget::moveRow).
class ProfileCardFrame : public QFrame {
public:
    explicit ProfileCardFrame(QWidget *parent) : QFrame(parent)
    {
        setObjectName(QStringLiteral("profileCard"));
        setAcceptDrops(true);
    }

    int row = -1;
    std::function<void(int from, int to)> onReordered;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(QLatin1String(kProfileRowMime))) {
            const int sourceRow = event->mimeData()->data(QLatin1String(kProfileRowMime)).toInt();
            if (sourceRow != row) {
                event->acceptProposedAction();
                setProperty("kaiDrop", true);
                style()->unpolish(this);
                style()->polish(this);
            }
        }
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        setProperty("kaiDrop", false);
        style()->unpolish(this);
        style()->polish(this);
    }

    void dropEvent(QDropEvent *event) override
    {
        setProperty("kaiDrop", false);
        style()->unpolish(this);
        style()->polish(this);
        if (!event->mimeData()->hasFormat(QLatin1String(kProfileRowMime))) {
            return;
        }
        const int sourceRow = event->mimeData()->data(QLatin1String(kProfileRowMime)).toInt();
        if (sourceRow == row) {
            return;
        }
        event->acceptProposedAction();
        if (onReordered) {
            onReordered(sourceRow, row);
        }
    }
};

} // namespace

TerminalProfilesEditorWidget::TerminalProfilesEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void TerminalProfilesEditorWidget::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    // Cabeçalho: título + subtítulo à esquerda, "+ Novo Perfil" à direita
    // (mockup enviado pelo usuário).
    auto *header = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto *titleLabel = new QLabel(utils::tr(QStringLiteral("settings.group.terminals")), this);
    titleLabel->setProperty("kaiRole", QStringLiteral("title"));
    titleBox->addWidget(titleLabel);
    auto *subtitleLabel = new QLabel(utils::tr(QStringLiteral("terminals.page.subtitle")), this);
    subtitleLabel->setProperty("kaiRole", QStringLiteral("caption"));
    subtitleLabel->setWordWrap(true);
    // Ignored: mesmo motivo do previewLabel/nameLabel dos cards abaixo —
    // sem isso o subtítulo empurrava o cabeçalho inteiro (e o botão "+
    // Adicionar" à direita) pra fora da largura visível.
    subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleBox->addWidget(subtitleLabel);
    header->addLayout(titleBox, 1);

    // O botão "+" abre um MENU DE PRESETS (WSL, Docker, PowerShell, Shell local,
    // em branco) — o usuário não precisa decorar a sintaxe do template.
    m_addButton = new QToolButton(this);
    m_addButton->setText(utils::tr(QStringLiteral("terminals.add")));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addButton->setProperty("kaiRole", QStringLiteral("primary"));
    // Cor do tema (accent). O QSS global só pinta kaiRole="primary" em
    // QPushButton; este é um QToolButton (tem menu de presets), então a
    // regra não pegava e o botão saía neutro. Estilo explícito de accent
    // aqui, no padrão do botão primário (texto/ícone contrastando com o
    // accent). O ícone usa a mesma cor de contraste do texto.
    {
        const QColor accent(utils::tokens::accent());
        const QString onAccent = accent.lightnessF() > 0.6
            ? QStringLiteral("#101014") : QStringLiteral("#ffffff");
        m_addButton->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(onAccent), 14));
        m_addButton->setStyleSheet(QStringLiteral(
            "QToolButton { background-color: %1; color: %2; border: none;"
            " border-radius: %3px; padding: %4px %5px; font-weight: 600; }"
            "QToolButton:hover { background-color: %6; }"
            "QToolButton::menu-indicator { image: none; width: 0; }")
            .arg(utils::tokens::accent(), onAccent).arg(utils::tokens::radiusMd())
            .arg(utils::tokens::space(1)).arg(utils::tokens::space(3))
            .arg(QColor(utils::tokens::accent()).lighter(115).name()));
    }
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    auto *presetMenu = new QMenu(m_addButton);
    struct Preset { QString key; QString tpl; bool pty; core::ShellFlavor shell; };
    const QVector<Preset> presets = {
        {QStringLiteral("terminal_targets.preset.wsl"),
         QStringLiteral("wsl.exe -- bash -lic 'eval \"$(echo \"$1\" | base64 -d)\"' kai {{command_b64}}"),
         true, core::ShellFlavor::Posix},
        {QStringLiteral("terminal_targets.preset.docker_wsl"),
         QStringLiteral("wsl.exe -- bash -lic 'docker exec -i -w /var/www/html CONTAINER bash -lc \"$(echo $1 | base64 -d)\"' kai {{command_b64}}"),
         true, core::ShellFlavor::Posix},
        {QStringLiteral("terminal_targets.preset.powershell"),
         QStringLiteral("cmd.exe /d /s /c powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
                        "^\"Invoke-Expression([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{{command_b64}}')))^\""),
         false, core::ShellFlavor::PowerShell},
        {QStringLiteral("terminal_targets.preset.local_shell"), QStringLiteral("{{command}}"), true, core::ShellFlavor::Posix},
    };
    for (const Preset &p : presets) {
        const QString name = utils::tr(p.key);
        const QString tpl = p.tpl;
        const bool pty = p.pty;
        const core::ShellFlavor shell = p.shell;
        connect(presetMenu->addAction(name), &QAction::triggered, this, [this, name, tpl, pty, shell]() {
            core::TerminalProfile t;
            t.name = name; t.commandTemplate = tpl; t.usePty = pty; t.shell = shell;
            m_targets.append(t);
            rebuildList();
        });
    }
    presetMenu->addSeparator();
    connect(presetMenu->addAction(utils::tr(QStringLiteral("terminals.add_blank"))),
            &QAction::triggered, this, [this]() {
        m_targets.append(core::TerminalProfile{});
        const int row = m_targets.size() - 1;
        if (editRowViaForm(row)) {
            rebuildList();
        } else {
            m_targets.remove(row);
        }
    });
    m_addButton->setMenu(presetMenu);
    header->addWidget(m_addButton, 0, Qt::AlignTop);
    outer->addLayout(header);

    // Lista de cards, rolável (a página do SettingsDialog já embrulha em
    // QScrollArea, mas este widget também é usado standalone/testado —
    // manter a lista com seu próprio layout vertical simples é suficiente,
    // igual à tabela de antes).
    auto *listContainer = new QWidget(this);
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(10);
    outer->addWidget(listContainer, 1);
}

QWidget *TerminalProfilesEditorWidget::buildCard(int row)
{
    const core::TerminalProfile &t = m_targets.at(row);

    auto *card = new ProfileCardFrame(this);
    card->row = row;
    card->setProperty("kaiState", t.isDefault ? QStringLiteral("default") : QString());
    card->onReordered = [this](int from, int to) { moveRow(from, to); };

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto *handle = new DragHandleLabel(card);
    handle->row = row;
    topRow->addWidget(handle);

    auto *iconLabel = new QLabel(card);
    const QIcon profileIcon = !t.icon.isEmpty()
        ? IconPickerWidget::iconForName(t.icon)
        : (t.isDefault ? LucideIcons::icon(QStringLiteral("star-filled"), QColor(tk::warningFg()), 18)
                       : LucideIcons::icon(QStringLiteral("terminal"), QColor(tk::mutedFg()), 18));
    iconLabel->setPixmap(profileIcon.pixmap(18, 18));
    topRow->addWidget(iconLabel);

    auto *nameLabel = new ElidedLabel(t.name, /*bold=*/true, card);
    // Stretch=1: com SizePolicy::Ignored, a label não tem mais um
    // sizeHint que "ancore" espaço nenhum pro layout — sem um fator de
    // stretch explícito ela colapsava pra largura 0 (nome sumia por
    // completo). O stretch garante que ela reclama a fatia disponível
    // (competindo com o addStretch() lá embaixo, que empurra os botões
    // pra direita) antes de elidir o que não couber.
    topRow->addWidget(nameLabel, 1);

    if (t.isDefault) {
        auto *badge = new QLabel(utils::tr(QStringLiteral("terminal_targets.field.is_default")).toUpper(), card);
        badge->setObjectName(QStringLiteral("profileDefaultBadge"));
        badge->setStyleSheet(QStringLiteral(
            "QLabel#profileDefaultBadge { background-color: %1; color: %2;"
            " border-radius: %3px; padding: 1px 8px; font-weight: 700; font-size: %4pt; }")
            .arg(tk::warningFg(), tk::bg()).arg(tk::radiusSm()).arg(tk::fontSizeSmallPt()));
        topRow->addWidget(badge);
    }

    topRow->addStretch();

    if (!t.isDefault) {
        auto *setDefaultButton = new QPushButton(utils::tr(QStringLiteral("terminals.set_default")), card);
        connect(setDefaultButton, &QPushButton::clicked, this, [this, row]() { setDefaultRow(row); });
        topRow->addWidget(setDefaultButton);
    }

    topRow->addWidget(makeRowActionsCell(card,
        [this, row]() { if (editRowViaForm(row)) rebuildList(); },
        [this, row]() { removeRow(row); }));

    cardLayout->addLayout(topRow);

    // Preview do comando: caixa monoespaçada (mesmo QSS de áreas de
    // código/terminal do tema — ver tk::codeAreaQss()).
    auto *previewLabel = new QLabel(t.commandTemplate, card);
    previewLabel->setWordWrap(true);
    // Ignored (não Preferred): mesmo com wordWrap ligado, QLabel::sizeHint()
    // relata a largura da linha SEM quebra como preferida, o que forçava o
    // card (e o diálogo de Configurações inteiro) a crescer pra caber
    // comandos longos de um só fôlego (ex: "docker exec -it app_backend
    // /bin/sh") — bug reportado ("perfis ficando tão largo que não cabe").
    // Ignored diz ao layout "não me use pra decidir a largura", deixando o
    // texto quebrar de verdade dentro do espaço que sobrar.
    previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    previewLabel->setContentsMargins(10, 6, 10, 6);
    previewLabel->setStyleSheet(tk::codeAreaQss());
    cardLayout->addWidget(previewLabel);

    return card;
}

void TerminalProfilesEditorWidget::rebuildList()
{
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    for (int row = 0; row < m_targets.size(); ++row) {
        m_listLayout->addWidget(buildCard(row));
    }
    m_listLayout->addStretch();
}

void TerminalProfilesEditorWidget::setTargets(const QVector<core::TerminalProfile> &targets)
{
    m_targets = targets;
    rebuildList();
}

QVector<core::TerminalProfile> TerminalProfilesEditorWidget::targets() const
{
    QVector<core::TerminalProfile> result;
    for (const core::TerminalProfile &t : m_targets) {
        if (!t.name.trimmed().isEmpty()) {
            result.append(t);
        }
    }
    return result;
}

void TerminalProfilesEditorWidget::removeRow(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return;
    }
    m_targets.remove(row);
    rebuildList();
}

void TerminalProfilesEditorWidget::setDefaultRow(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return;
    }
    for (int i = 0; i < m_targets.size(); ++i) {
        m_targets[i].isDefault = (i == row);
    }
    rebuildList();
}

void TerminalProfilesEditorWidget::moveRow(int from, int to)
{
    if (from < 0 || from >= m_targets.size() || to < 0 || to >= m_targets.size() || from == to) {
        return;
    }
    m_targets.move(from, to);
    rebuildList();
}

bool TerminalProfilesEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return false;
    }
    const core::TerminalProfile &t = m_targets.at(row);

    QVector<RowEditDialog::FieldSpec> fields;
    fields.append({QStringLiteral("name"), utils::tr(QStringLiteral("field.label.name")),
                   RowEditDialog::FieldType::Text, t.name,
                   {}, utils::tr(QStringLiteral("terminal_targets.field.name.placeholder")), false, {}});
    fields.append({QStringLiteral("icon"), utils::tr(QStringLiteral("command.field.icon")),
                   RowEditDialog::FieldType::Icon, t.icon,
                   {}, {}, false, {}});
    fields.append({QStringLiteral("template"), utils::tr(QStringLiteral("terminal_targets.field.template")),
                   RowEditDialog::FieldType::MultiLine, t.commandTemplate,
                   {}, QStringLiteral("wsl -d Ubuntu -- bash -lc '{{command}}'"), false,
                   utils::tr(QStringLiteral("terminal_targets.field.template.tip"))});
    fields.append({QStringLiteral("shell"), utils::tr(QStringLiteral("terminal_targets.field.shell")),
                   RowEditDialog::FieldType::Combo, shellComboText(t.shell),
                   {utils::tr(QStringLiteral("terminal_targets.shell.auto")),
                    utils::tr(QStringLiteral("terminal_targets.shell.posix")),
                    utils::tr(QStringLiteral("terminal_targets.shell.powershell")),
                    utils::tr(QStringLiteral("terminal_targets.shell.cmd"))},
                   {}, false,
                   utils::tr(QStringLiteral("terminal_targets.field.shell.tip"))});
    fields.append({QStringLiteral("tty"), utils::tr(QStringLiteral("terminal_targets.field.tty_full")),
                   RowEditDialog::FieldType::Bool,
                   t.usePty ? QStringLiteral("true") : QStringLiteral("false"),
                   {}, {}, false,
                   utils::tr(QStringLiteral("terminal_targets.field.tty.tip"))});
    fields.append({QStringLiteral("default"), utils::tr(QStringLiteral("terminal_targets.field.is_default")),
                   RowEditDialog::FieldType::Bool,
                   t.isDefault ? QStringLiteral("true") : QStringLiteral("false"),
                   {}, {}, false,
                   utils::tr(QStringLiteral("terminal_targets.field.is_default.tip"))});

    RowEditDialog dialog(utils::tr(QStringLiteral("terminals.edit_row")), fields, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    core::TerminalProfile updated;
    updated.name = dialog.value(QStringLiteral("name")).trimmed();
    updated.icon = dialog.value(QStringLiteral("icon"));
    updated.commandTemplate = dialog.value(QStringLiteral("template"));
    updated.shell = shellFromComboText(dialog.value(QStringLiteral("shell")));
    updated.usePty = dialog.value(QStringLiteral("tty")) == QStringLiteral("true");
    updated.isDefault = dialog.value(QStringLiteral("default")) == QStringLiteral("true");
    m_targets[row] = updated;
    // Exclusividade do "Padrão": se este virou padrão, desmarca os outros.
    if (updated.isDefault) {
        for (int i = 0; i < m_targets.size(); ++i) {
            if (i != row) m_targets[i].isDefault = false;
        }
    }
    return true;
}

} // namespace kai::ui
