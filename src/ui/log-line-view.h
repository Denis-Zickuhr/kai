#pragma once

#include "ui/ansi-text-parser.h"

#include <QAbstractListModel>
#include <QJsonObject>
#include <QListView>
#include <QSet>
#include <QString>
#include <QStyledItemDelegate>
#include <QVector>

namespace kai::ui {

// ============================================================================
// SAÍDA FORMATADA estilo Grafana/Loki (Command::formattedOutput)
// ----------------------------------------------------------------------------
// Cada linha do log é tratada como um possível registro JSON estruturado:
// se der pra fazer parse como objeto JSON, vira um "card" colapsável (badge
// de nível + timestamp + mensagem; expande pra ver os campos extras). Linha
// que NÃO é um objeto JSON válido cai pro texto cru, COM as mesmas cores
// ANSI que a Saída normal mostraria (ver AnsiTextParser) — não é mais texto
// liso de uma cor só.
//
// RECONSTRUÇÃO DE LINHA QUEBRADA (achado real, reportado com print): alguns
// terminais/consoles (ex: cmd.exe via WSL bridge) fazem WRAP FÍSICO de
// linhas longas na largura da coluna, inserindo '\n' de verdade NO MEIO de
// um objeto JSON de uma linha só (quebrando até palavras/escapes ao meio:
// "servic" + "e", "\/var\/log\" + "\/magazord..."). Sem reconstrução, cada
// pedaço vira uma linha crua fragmentada e feia. LogLineModel acumula
// linhas físicas enquanto as chaves `{`/`}` (fora de string) não fecham de
// volta a zero, e só então tenta o parse — ver appendLine/scanJsonChar.
//
// Baseado em QListView + QAbstractListModel + delegate custom (em vez de
// QPlainTextEdit, como o resto da Saída) porque cada linha precisa de altura
// e conteúdo próprios (colapsada vs. expandida) e volume pode ser grande —
// a view só desenha o que está visível, mesmo princípio de virtualização do
// próprio Grafana.
//
// BUSCA: NÃO esconde linhas (impedia o "jump" fazer sentido — pedido do
// usuário: "o jump não funcionou na view grafana"). Em vez disso, destaca
// as linhas que casam (highlight sutil) e a ATUAL (jump) num tom diferente
// — mesmo padrão Notepad-style de JsonViewerWidget/OutputPanel, adaptado
// pra granularidade de LINHA (não de substring) já que a view desenha cada
// linha via delegate, não é um único QTextDocument.
// ============================================================================

struct LogLineEntry {
    QString raw;
    bool structured = false;
    // GRUPO DE LOG DA APLICAÇÃO (pedido do usuário: "teria como agrupar as
    // linhas que não são json em grupo? e botar uma label"): linhas cruas
    // CONSECUTIVAS (sem JSON estruturado entre elas) se fundem numa única
    // entrada — LogLineModel::insertRawLine funde na última entrada aberta
    // em vez de criar uma nova. `rawGroupTexts`/`rawGroupSegments` (um item
    // por linha física do grupo, paralelos) só são usados quando
    // isRawGroup=true; `raw` fica com o texto JUNTADO (usado por
    // busca/copiar, que não precisam saber da divisão em linhas).
    bool isRawGroup = false;
    QStringList rawGroupTexts;
    QVector<QVector<AnsiSegment>> rawGroupSegments;
    // Segmentos ANSI coloridos da ÚLTIMA linha crua adicionada — mantido só
    // por compatibilidade de teste/uso pontual; a renderização de verdade
    // usa rawGroupSegments (ver LogLineDelegate::paint).
    QVector<AnsiSegment> ansiSegments;
    // Nível NORMALIZADO minúsculo (info/warn/error/debug/trace/""), usado
    // pra escolher a cor do badge. `levelRaw` é o texto original do campo
    // (pode vir "WARNING", "warn", "4"...) — o que é exibido no badge.
    QString level;
    QString levelRaw;
    QString message;
    QString timestampText;
    // Campos do objeto JSON que não foram reconhecidos como nível/mensagem/
    // horário — mostrados na grade ao expandir o card.
    QJsonObject extraFields;
    bool expanded = false;
};

class LogLineModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LogLineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Texto novo (pode ter várias linhas e uma linha parcial no fim — o
    // restante fica em buffer até a próxima chamada completar a linha,
    // mesma lógica de streaming que OutputPanel::appendChunkNow usa pros
    // timestamps).
    void appendText(const QString &text);
    void clearLog();

    // BUSCA (não filtra/esconde — ver comentário do topo). Aceita termo
    // livre (substring em qualquer campo) OU sintaxe "campo:valor" estilo
    // Grafana/Loki (ex: "level:error", "service:shopee") — vários tokens
    // separados por espaço combinam em E (AND). `needle` vazio limpa
    // qualquer destaque.
    void setSearchTerm(const QString &needle);
    QString searchTerm() const { return m_searchTerm; }

    // Campos JSON já vistos até agora (chaves de extraFields + "level"
    // quando presente), pra montar o menu de filtro rápido "estilo
    // Grafana" (pedido do usuário: "filtros por campo detectados na fly de
    // pesquisa"). Ordenado alfabeticamente.
    QStringList knownFieldNames() const;
    // Valores distintos de "level" já vistos (ex: info/warn/error) — nível
    // é o campo mais comum de filtrar, ganha um atalho dedicado.
    QStringList knownLevelValues() const;

    // FILTRO DE NÍVEL — DIFERENTE da busca (que só destaca): pedido do
    // usuário: "nos filtros de level, tem como fazer multiselect, e o que
    // não está selecionado some do render?" — um multiselect de valores de
    // level que de fato ESCONDE as linhas cujo nível não está marcado.
    // Conjunto vazio = sem filtro (mostra tudo). Grupos de log cru (sem
    // level) NUNCA são escondidos por este filtro — não têm nível pra
    // filtrar por, e escondê-los perderia contexto sem necessidade.
    void setLevelFilter(const QSet<QString> &levels);
    QSet<QString> levelFilter() const { return m_levelFilter; }
    int matchCount() const { return m_matchingRows.size(); }
    // Índice (1-based, pra exibir "N/M") da ocorrência atual, ou 0 se não
    // há busca ativa/nenhum resultado.
    int currentMatchOrdinal() const;
    bool rowMatches(int row) const;
    bool isCurrentMatchRow(int row) const;
    // Anda pra próxima/anterior linha que casa (com wrap-around) e retorna
    // o índice da linha (-1 se não há nenhuma), pra a view centralizar/rolar.
    int goToNextMatch();
    int goToPreviousMatch();

    void toggleExpanded(int row);
    const LogLineEntry &entryAt(int row) const;
    bool isEmpty() const { return m_all.isEmpty(); }

    // Exposto para testes.
    static LogLineEntry parseLine(const QString &rawLine);

signals:
    // Emitido quando a linha "atual" da busca muda (goToNext/Previous ou
    // uma busca nova) — a view usa isto pra rolar até ela.
    void currentMatchRowChanged(int row);

private:
    void appendLine(const QString &line);
    // Atualiza o estado de profundidade de chaves (fora de string) usado
    // pra saber quando um objeto JSON fragmentado por wrap físico do
    // terminal finalmente fechou — ver comentário no topo do arquivo.
    void scanJsonChar(QChar c);
    void flushAccumulatedJson();
    void giveUpAccumulation();
    void insertEntry(const LogLineEntry &entry);
    // Funde `line` no grupo de log cru ABERTO no fim de m_all, ou abre um
    // grupo novo se a última entrada não for um grupo cru (ou não houver
    // nenhuma ainda) — ver LogLineEntry::isRawGroup.
    void insertRawLine(const QString &line);
    void recomputeMatches();
    bool passesLevelFilter(const LogLineEntry &entry) const;
    void rebuildVisibleRows();
    // Query completa (pode ter vários tokens "campo:valor"/livres — ver
    // setSearchTerm) contra UMA entrada.
    bool entryMatchesTerm(const LogLineEntry &entry) const;
    // Um token só. `field` vazio = busca livre (qualquer campo); senão só
    // no campo nomeado (level/message/time/extraFields por nome de chave).
    bool entryMatchesToken(const LogLineEntry &entry, const QString &field, const QString &value) const;

    QVector<LogLineEntry> m_all;
    // Índices em m_all que passam o filtro de nível ATUAL — é isto que a
    // view enxerga (rowCount/data/entryAt/toggleExpanded operam em espaço
    // de "linha visível", não índice absoluto de m_all). Sem filtro ativo,
    // contém todo mundo na mesma ordem.
    QVector<int> m_visibleRows;
    QSet<QString> m_levelFilter; // vazio = sem filtro
    QString m_pending; // linha parcial ainda não fechada por '\n'

    // Acumulação de um objeto JSON fragmentado em várias linhas FÍSICAS
    // (wrap do terminal) — ver comentário do topo.
    bool m_accumulatingJson = false;
    QString m_jsonAccumBuffer;
    QStringList m_jsonAccumRawLines;
    int m_jsonAccumDepth = 0;
    bool m_jsonAccumInString = false;
    bool m_jsonAccumEscaped = false;

    AnsiTextParser m_ansiParser; // só pra linhas cruas (ver ansiSegments)

    QString m_searchTerm;
    // Índices EM ESPAÇO DE LINHA VISÍVEL (não m_all — ver m_visibleRows)
    // que casam com o termo de busca atual.
    QVector<int> m_matchingRows;
    int m_currentMatchPos = -1;  // índice DENTRO de m_matchingRows

    // Campos/valores de nível já vistos (ver knownFieldNames/knownLevelValues).
    QSet<QString> m_knownFields;
    QSet<QString> m_knownLevels;
};

class LogLineDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LogLineDelegate(LogLineModel *model, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Expostos pra LogLineView acertar a área de clique do chevron (só ali
    // alterna expandir/colapsar — o resto da linha é pra SELECIONAR/copiar,
    // ver LogLineView::mousePressEvent) sem duplicar a geometria aqui e lá.
    int rowHeight() const;
    int chevronHitWidth() const;

private:
    LogLineModel *m_model;
    QColor colorForLevel(const QString &level) const;
    QString badgeTextForLevel(const QString &levelRaw, const QString &level) const;
    int fieldRowHeight() const;
    // Uma "linha" dentro de um grupo de log cru expandido usa a mesma
    // altura de uma linha normal (fonte de corpo, não a pequena dos campos
    // extras — é log de verdade, não metadado).
    int rawGroupLineHeight() const { return rowHeight(); }
    // Altura do bloco de MENSAGEM COMPLETA (com quebra de linha) mostrado
    // ao expandir um card — pedido do usuário: "preciso que a msg tenha um
    // campo próprio ao expandir... não consigo ver tudo se for muito
    // grande" (a mensagem na linha colapsada é de uma linha só, cortada).
    // `availableWidth` já descontado de margens/indentação.
    int messageBlockHeight(const QString &message, int availableWidth) const;
    // Largura de verdade que a linha PRECISA (badge+timestamp+mensagem,
    // ou o maior campo/linha do grupo quando expandido) — pedido do
    // usuário: "quero log horizontal se houver necessidade de espaço",
    // em vez de cortar. Usada em sizeHint pra habilitar rolagem
    // horizontal na view quando o conteúdo é mais largo que o viewport.
    int rowContentWidth(const QModelIndex &index) const;
};

class LogLineView : public QListView {
    Q_OBJECT
public:
    explicit LogLineView(QWidget *parent = nullptr);

    LogLineModel *logModel() const { return m_model; }
    void appendText(const QString &text);
    void clearLog();
    void setFilterText(const QString &needle);
    bool isEmpty() const { return m_model->isEmpty(); }

    // Notepad-style: avança/retrocede a ocorrência atual e rola até ela.
    void goToNextMatch();
    void goToPreviousMatch();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void copySelectionToClipboard();
    void jumpToRow(int row);

    LogLineModel *m_model;
    LogLineDelegate *m_delegate;
};

} // namespace kai::ui
