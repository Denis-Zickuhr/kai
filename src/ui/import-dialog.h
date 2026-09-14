#pragma once

#include <QDialog>
#include <QString>

namespace kai::ui {

// ============================================================================
// TELA ÚNICA de importação — pedido do usuário (vendo o menu Arquivo
// bagunçado, com "Importar Projeto...", "Importar OpenAPI/Swagger..." e
// "Importar Configuração..." como 3 itens separados): "só dois botões...
// [export] e um jeito simplificado e mais fácil, porém completo, de
// importar, com apenas um form". Substitui os 3 pontos de entrada por UM:
// esta tela só decide a FONTE (uma pasta de projeto, ou um arquivo) — a
// escolha pasta/arquivo já resolve Projeto vs {OpenAPI, Configuração} sem
// perguntar nada (Projeto sempre aponta pra uma pasta; os outros dois são
// sempre um arquivo solto). Pra um ARQUIVO, o TIPO exato (OpenAPI/Swagger
// vs Configuração exportada do Kai) é detectado automaticamente pelo
// conteúdo — sem pedir pro usuário escolher. As telas de opções já
// existentes e testadas (ProjectImportOptionsDialog, ImportSelectionDialog)
// continuam rodando normalmente como o passo seguinte — esta tela só
// unifica a ENTRADA, não reimplementa a lógica de cada fluxo.
// ============================================================================
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind { Project, OpenApi, Config };

    explicit ImportDialog(QWidget *parent = nullptr);

    // Válidos só depois de exec() == QDialog::Accepted.
    Kind kind() const { return m_kind; }
    QString path() const { return m_path; }

    // Decide OpenApi vs Config pelo CONTEÚDO de um arquivo (chave "openapi"/
    // "swagger" no topo = OpenAPI; "kai_export"/"commands"/"folders"/
    // "collections"/"settings" = Configuração exportada do Kai). Aceita
    // JSON ou YAML (mesma detecção usada no resto do app — ver
    // core::looksLikeJson/yamlTextToJsonText). Retorna false se não
    // reconhecer nenhum dos dois formatos. Público + estático (em vez de
    // privado, operando num path) pra ser testável sem precisar de um
    // arquivo real no disco nem de um QDialog.
    static bool detectKindFromText(const QString &rawText, Kind *outKind);

private:
    void pickProjectFolder();
    void pickFile();

    Kind m_kind = Kind::Project;
    QString m_path;
};

} // namespace kai::ui
