#include "ui/features/settings/tabs/languages-tab.h"

#include "ui/shared/dialog-utils.h"
#include "utils/translation-manager.h"

#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
QLabel *makeCaption(QWidget *parent, const QString &key)
{
    auto *label = new QLabel(utils::tr(key), parent);
    label->setProperty("kaiRole", QStringLiteral("caption"));
    return label;
}

QLineEdit *makeField(QWidget *parent, const QString &tipKey, const QString &value, const QString &placeholder)
{
    auto *field = new QLineEdit(value, parent);
    field->setPlaceholderText(placeholder);
    field->setToolTip(utils::tr(tipKey));
    return field;
}
} // namespace

LanguagesTab::LanguagesTab(const core::InterpreterSettings &settings, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    layout->addWidget(layout_helpers::makeHintBanner(this, utils::tr(QStringLiteral("settings.languages.hint"))));

    auto *group = new QGroupBox(utils::tr(QStringLiteral("settings.group.languages")), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(14, 16, 14, 12);
    groupLayout->setSpacing(4);

    const core::InterpreterSettings defaults;
    m_python = makeField(group, QStringLiteral("settings.languages.python.tip"), settings.python, defaults.python);
    m_python->setObjectName(QStringLiteral("languagesPythonField"));
    groupLayout->addWidget(makeCaption(group, QStringLiteral("settings.languages.python")));
    groupLayout->addWidget(m_python);
    groupLayout->addSpacing(8);

    m_node = makeField(group, QStringLiteral("settings.languages.node.tip"), settings.node, defaults.node);
    m_node->setObjectName(QStringLiteral("languagesNodeField"));
    groupLayout->addWidget(makeCaption(group, QStringLiteral("settings.languages.node")));
    groupLayout->addWidget(m_node);
    groupLayout->addSpacing(8);

    m_php = makeField(group, QStringLiteral("settings.languages.php.tip"), settings.php, defaults.php);
    m_php->setObjectName(QStringLiteral("languagesPhpField"));
    groupLayout->addWidget(makeCaption(group, QStringLiteral("settings.languages.php")));
    groupLayout->addWidget(m_php);
    layout->addWidget(group);

    layout->addStretch(1);
}

core::InterpreterSettings LanguagesTab::settings() const
{
    core::InterpreterSettings out;
    // Vazio volta ao padrão da linguagem (mantém o settings.json sempre usável).
    if (!m_python->text().trimmed().isEmpty()) {
        out.python = m_python->text().trimmed();
    }
    if (!m_node->text().trimmed().isEmpty()) {
        out.node = m_node->text().trimmed();
    }
    if (!m_php->text().trimmed().isEmpty()) {
        out.php = m_php->text().trimmed();
    }
    return out;
}

} // namespace kai::ui
