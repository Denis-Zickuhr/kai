#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "core/models.h"

class QLineEdit;
class QComboBox;
class QCheckBox;

namespace kai::ui {

// Diálogo mostrado em "Arquivo → Importar Projeto" logo depois de escolher a
// pasta no seletor nativo, ANTES de ler o kai.json (pedido do usuário: rodar
// o Kai sob WSL/WSLg, o seletor nativo de pasta às vezes devolve um path
// Windows/UNC — ex: \\wsl.localhost\Ubuntu\home\... — mesmo para um projeto
// cujos comandos rodam DENTRO do WSL via Alvo de Terminal; um PROJECT_PATH
// nesse formato quebra `cd`/working_dir que esperam POSIX).
//
// DOIS VALORES SEPARADOS, só UM aparece na tela (bug relatado: editar o
// path pra tirar o prefixo do WSL "\\wsl.localhost\Ubuntu\..." fazia o Kai
// não achar mais o kai.json — o mesmo texto editado era usado tanto pra LER
// o arquivo quanto pra virar PROJECT_PATH, então simplificar um quebrava o
// outro):
//   - directory(): o path REAL usado pra abrir o kai.json (QFile) — o valor
//     original devolvido pelo seletor nativo, NUNCA exibido/editado nesta
//     tela (pedido do usuário: a pasta já foi escolhida um passo antes, e
//     mostrar de novo aqui ao lado do campo abaixo era confuso/redundante).
//   - projectPath(): o ÚNICO campo de path visível — o valor GRAVADO como
//     PROJECT_PATH (folder.projectPath + env var), pré-preenchido com uma
//     versão AUTO-SIMPLIFICADA do path real (prefixo WSL removido, se
//     detectado — ver utils::toPosixPath) e livremente editável, SEM
//     nenhum efeito sobre a leitura do kai.json.
// O combo de formato (posix/windows/nativo) recalcula projectPath() a
// partir do directory() ATUAL quando trocado — um atalho pros 3 casos mais
// comuns, sem precisar digitar à mão.
//
// PASTA DE DESTINO (feedback do usuário: "importe um projeto na minha pasta
// de APIs, evita o trabalho de todas vez ter que ir lá e mudar a pasta") —
// reusa o MESMO seletor de pastas pesquisável já usado no editor de
// Comando/Coleção (busca embutida, hierarquia indentada), populado com as
// pastas que já existem no Kai. Default = raiz (comportamento antigo, cria
// a pasta do projeto direto na raiz); selecionar uma pasta existente importa
// o projeto JÁ como subpasta dela, sem precisar arrastar depois.
class ProjectImportOptionsDialog : public QDialog {
    Q_OBJECT

public:
    ProjectImportOptionsDialog(const QString &initialDirectory,
                                const QVector<core::Folder> &allFolders,
                                QWidget *parent = nullptr);

    // Path usado para LER o kai.json — plain text editável (pedido do
    // usuário: sem seletor nativo aqui, a pasta já foi escolhida um passo
    // antes, em ProjectSelector::promptForDirectory).
    QString directory() const;

    // Valor final gravado como PROJECT_PATH — já o texto PRONTO (nenhuma
    // conversão adicional é aplicada depois; o que está no campo é o que
    // será salvo, literalmente).
    QString projectPath() const;

    // Id da pasta do Kai escolhida como pai da pasta do projeto importado.
    // Vazio = raiz (comportamento antigo).
    QString parentFolderId() const;

    // IMPORTAÇÃO GENÉRICA (feedback do usuário): quando marcado, o Kai
    // também tenta reconhecer definições de execução em formatos que não
    // são kai.json — package.json, docker-compose.yml, requirements.txt/
    // pyproject.toml/manage.py, composer.json, Makefile (ver
    // ProjectDetectionStrategy) — mesmo em projetos SEM nenhum kai.json.
    bool detectGenericDefinitions() const;

private:
    void recomputeProjectPathFromFormat();

    QLineEdit *m_directoryField = nullptr;
    QLineEdit *m_projectPathField = nullptr;
    QComboBox *m_formatField = nullptr;
    QComboBox *m_parentFolderField = nullptr;
    QCheckBox *m_detectGenericField = nullptr;
};

} // namespace kai::ui
