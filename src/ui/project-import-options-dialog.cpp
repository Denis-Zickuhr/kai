#include "ui/project-import-options-dialog.h"

#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/path-format.h"

#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
// Profundidade de uma pasta na hierarquia via parent_id, pra indentação
// visual no combo — mesma lógica de CommandEditorDialog::depthOf/
// FolderEditorDialog::depthOf (não compartilhada num header comum; réplica
// pequena o bastante pra não justificar extrair um utilitário só por isso).
int depthOf(const QVector<core::Folder> &allFolders, const QString &folderId)
{
    int depth = 0;
    QString currentId = folderId;
    for (int guard = 0; guard < 64; ++guard) {
        const core::Folder *current = nullptr;
        for (const core::Folder &f : allFolders) {
            if (f.id == currentId) {
                current = &f;
                break;
            }
        }
        if (!current || !current->parentId.has_value()) {
            break;
        }
        currentId = current->parentId.value();
        ++depth;
    }
    return depth;
}
} // namespace

ProjectImportOptionsDialog::ProjectImportOptionsDialog(const QString &initialDirectory,
                                                        const QVector<core::Folder> &allFolders,
                                                        QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("project_import_options.title")));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    outer->setSpacing(tk::space(3));

    outer->addWidget(layout_helpers::makeHintBanner(this,
        utils::tr(QStringLiteral("project_import_options.hint"))));

    // Path REAL usado pra abrir o kai.json — NÃO aparece na tela (pedido do
    // usuário: a pasta já foi escolhida um passo antes, em
    // ProjectSelector::promptForDirectory, então mostrar/editar de novo
    // aqui era redundante e confuso ao lado do campo de PROJECT_PATH
    // abaixo). Fica só internamente, sempre o valor original devolvido pelo
    // seletor nativo — hide() explícito pra nunca virar uma janela órfã
    // flutuante (widget sem layout, ver ActionGroupContainer::setGroups).
    m_directoryField = new QLineEdit(initialDirectory, this);
    m_directoryField->hide();

    // PASTA DE DESTINO no Kai (feedback do usuário: "importe um projeto na
    // minha pasta de APIs, evita o trabalho de todas vez ter que ir lá e
    // mudar a pasta") — mesmo seletor pesquisável já usado no editor de
    // Comando/Coleção, populado com as pastas que já existem. Default =
    // raiz (índice 0, comportamento antigo).
    m_parentFolderField = new QComboBox(this);
    m_parentFolderField->addItem(utils::tr(QStringLiteral("folder.parent.none")), QString());
    for (const core::Folder &folder : allFolders) {
        const int depth = depthOf(allFolders, folder.id);
        const QString indent = QString(QStringLiteral("    ")).repeated(depth);
        m_parentFolderField->addItem(indent + folder.name, folder.id);
    }
    makeSearchableCombo(m_parentFolderField);
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("project_import_options.parent_folder_label")), m_parentFolderField));

    // Valor GRAVADO como PROJECT_PATH — campo SEPARADO do de cima (bug
    // relatado: editar o path pra tirar o prefixo do WSL fazia o Kai não
    // achar mais o kai.json, porque os dois usavam o MESMO texto). Pré-
    // preenchido com uma versão simplificada (prefixo WSL removido, se
    // detectado) e livremente editável dali pra frente — editar aqui NUNCA
    // afeta a leitura do kai.json.
    const QString autoProjectPath = utils::toPosixPath(initialDirectory);
    m_projectPathField = new QLineEdit(autoProjectPath, this);
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("project_import_options.directory_label")), m_projectPathField));

    // Mesmas 3 opções/textos do formato de path do seletor de arquivo de
    // parâmetros (core::Parameter::filePathFormat) — mesmo conceito, mesma
    // conversão (utils::convertFilePathFormat). Aqui vira um atalho: ao
    // trocar, recalcula o campo de PROJECT_PATH acima a partir do path real
    // (não precisa digitar à mão pros 3 casos mais comuns) — o usuário
    // ainda pode editar livremente depois.
    m_formatField = new QComboBox(this);
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.native")), QStringLiteral("native"));
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.posix")), QStringLiteral("posix"));
    m_formatField->addItem(utils::tr(QStringLiteral("params.path_format.windows")), QStringLiteral("windows"));
    connect(m_formatField, &QComboBox::currentIndexChanged, this, [this](int) { recomputeProjectPathFromFormat(); });
    outer->addWidget(layout_helpers::wrapWithLabel(this,
        utils::tr(QStringLiteral("params.path_format.label")), m_formatField));

    // IMPORTAÇÃO GENÉRICA (feedback do usuário): flag opcional que tenta
    // reconhecer definições de execução em formatos que não são kai.json —
    // funciona mesmo em projetos sem NENHUM kai.json (ver
    // ProjectSelector::importFromDirectory/ProjectDetectionStrategy).
    m_detectGenericField = new QCheckBox(
        utils::tr(QStringLiteral("project_import_options.detect_generic")), this);
    m_detectGenericField->setProperty("kaiRole", QStringLiteral("switch"));
    m_detectGenericField->setToolTip(utils::tr(QStringLiteral("project_import_options.detect_generic.tip")));
    outer->addWidget(m_detectGenericField);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    if (QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok)) {
        ok->setText(utils::tr(QStringLiteral("project_import_options.confirm")));
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttonBox);

    adjustSize();
    setMinimumWidth(460);
    resize(qMax(460, width()), height());
    centerOnParent(this);
}

void ProjectImportOptionsDialog::recomputeProjectPathFromFormat()
{
    const QString format = m_formatField->currentData().toString();
    m_projectPathField->setText(utils::convertFilePathFormat(m_directoryField->text().trimmed(), format));
}

QString ProjectImportOptionsDialog::directory() const
{
    return m_directoryField->text().trimmed();
}

QString ProjectImportOptionsDialog::projectPath() const
{
    return m_projectPathField->text().trimmed();
}

bool ProjectImportOptionsDialog::detectGenericDefinitions() const
{
    return m_detectGenericField->isChecked();
}

QString ProjectImportOptionsDialog::parentFolderId() const
{
    return m_parentFolderField->currentData().toString();
}

} // namespace kai::ui
