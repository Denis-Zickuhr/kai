#include "ui/shared/working-dir-field.h"

#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

WorkingDirField::WorkingDirField(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(2));

    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("workingDirMode"));
    m_mode->addItem(utils::tr(QStringLiteral("working_dir.mode.inherit")), static_cast<int>(core::WorkingDirMode::Inherit));
    m_mode->addItem(utils::tr(QStringLiteral("working_dir.mode.custom")), static_cast<int>(core::WorkingDirMode::Custom));
    m_mode->addItem(utils::tr(QStringLiteral("working_dir.mode.none")), static_cast<int>(core::WorkingDirMode::None));
    layout->addWidget(m_mode);

    m_path = new QLineEdit(this);
    m_path->setObjectName(QStringLiteral("workingDirPath"));
    layout->addWidget(m_path, 1);

    m_browse = makeIconButton(this, QStringLiteral("folder-open"),
        utils::tr(QStringLiteral("working_dir.browse")), QColor(utils::tokens::accent()));
    m_browse->setObjectName(QStringLiteral("workingDirBrowse"));
    layout->addWidget(m_browse);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this]() {
        syncEnabled();
        emit changed();
    });
    connect(m_path, &QLineEdit::textChanged, this, &WorkingDirField::changed);
    connect(m_browse, &QToolButton::clicked, this, &WorkingDirField::browse);
    syncEnabled();
}

void WorkingDirField::setValue(core::WorkingDirMode mode, const QString &path)
{
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(static_cast<int>(mode))));
    m_path->setText(mode == core::WorkingDirMode::Custom ? path : QString());
    syncEnabled();
}

core::WorkingDirMode WorkingDirField::mode() const
{
    return static_cast<core::WorkingDirMode>(m_mode->currentData().toInt());
}

QString WorkingDirField::path() const
{
    return mode() == core::WorkingDirMode::Custom ? m_path->text().trimmed() : QString();
}

void WorkingDirField::syncEnabled()
{
    const core::WorkingDirMode current = mode();
    const bool custom = current == core::WorkingDirMode::Custom;
    m_path->setEnabled(custom);
    m_browse->setEnabled(custom);
    // Fora do modo "próprio" o campo vazio explica o que acontece naquele estado.
    switch (current) {
    case core::WorkingDirMode::Inherit:
        m_path->setPlaceholderText(utils::tr(QStringLiteral("working_dir.placeholder.inherit")));
        break;
    case core::WorkingDirMode::None:
        m_path->setPlaceholderText(utils::tr(QStringLiteral("working_dir.placeholder.none")));
        break;
    case core::WorkingDirMode::Custom:
        m_path->setPlaceholderText(utils::tr(QStringLiteral("working_dir.placeholder.custom")));
        break;
    }
}

void WorkingDirField::browse()
{
    const QString chosen = pickDirectory(
        this, utils::tr(QStringLiteral("working_dir.browse")), m_path->text());
    if (!chosen.isEmpty()) {
        m_path->setText(chosen);
    }
}

} // namespace kai::ui
