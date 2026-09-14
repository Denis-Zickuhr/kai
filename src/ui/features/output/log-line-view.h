#pragma once

#include "ui/features/output/ansi-text-parser.h"

#include <QAbstractListModel>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QListView>
#include <QSet>
#include <QString>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVector>

namespace kai::ui {

// Saída formatada estilo Grafana/Loki (Command::formattedOutput): cada linha que for um objeto JSON vira um "card"
// colapsável (badge de nível, horário, mensagem; expande para os campos extras). O resto cai como texto cru com as
// mesmas cores ANSI da Saída normal (AnsiTextParser).
//
// Linha quebrada: alguns consoles (ex: cmd.exe via WSL) fazem wrap físico de linhas longas, inserindo '\n' no meio de
// um objeto JSON (até no meio de palavras/escapes). LogLineModel acumula linhas físicas até as chaves `{`/`}` (fora
// de string) voltarem a zero e só então faz o parse (appendLine/scanJsonChar).
//
// QListView + QAbstractListModel + delegate, e não QPlainTextEdit: cada linha tem altura própria (colapsada ou
// expandida) e o volume pode ser grande; a view só desenha o visível.
//
// Busca: não esconde linhas (o "jump" perderia o sentido); destaca as que casam e a atual, em granularidade de
// LINHA (cada linha é desenhada pelo delegate, não há um QTextDocument único).

struct LogLineEntry {
    QString raw;
    bool structured = false;
    // Grupo de log da aplicação: linhas cruas consecutivas (sem JSON entre elas) se fundem numa entrada
    // (LogLineModel::insertRawLine). `rawGroupTexts`/`rawGroupSegments` (um item por linha física, paralelos) só valem
    // com isRawGroup=true; `raw` guarda o texto juntado (busca/copiar).
    bool isRawGroup = false;
    QStringList rawGroupTexts;
    QVector<QVector<AnsiSegment>> rawGroupSegments;
    // Segmentos ANSI da última linha crua adicionada (só compatibilidade); o desenho usa rawGroupSegments.
    QVector<AnsiSegment> ansiSegments;
    // Nível normalizado em minúsculas (info/warn/error/debug/trace/""), para a cor do badge. `levelRaw` é o texto
    // original do campo ("WARNING", "4"...), exibido no badge.
    QString level;
    QString levelRaw;
    QString message;
    QString timestampText;
    // Campos do JSON não reconhecidos como nível/mensagem/horário; mostrados na grade ao expandir.
    QJsonObject extraFields;
    bool expanded = false;
    // Cache da largura que a linha pede (LogLineDelegate::rowContentWidth): refazer a medida de todas as linhas a cada
    // relayout da view custava O(linhas) por trecho de saída. Vale enquanto geração e estado de expansão não mudam.
    mutable int cachedContentWidth = -1;
    mutable int cachedWidthGeneration = -1;
    mutable bool cachedWidthExpanded = false;
};

class LogLineModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LogLineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Texto novo (pode ter várias linhas e uma linha parcial no fim, que fica em buffer até a próxima chamada).
    void appendText(const QString &text);
    void clearLog();

    // Busca (não filtra). Aceita termo livre (substring em qualquer campo) ou "campo:valor" estilo Grafana/Loki
    // (ex: "level:error"); vários tokens separados por espaço combinam em AND. `needle` vazio limpa o destaque.
    void setSearchTerm(const QString &needle);
    QString searchTerm() const { return m_searchTerm; }

    // Campos JSON já vistos (chaves de extraFields + "level"), em ordem alfabética, para o menu de filtro rápido.
    QStringList knownFieldNames() const;
    // Valores distintos de "level" já vistos (info/warn/error).
    QStringList knownLevelValues() const;

    // Filtro de nível (diferente da busca, que só destaca): multiselect que ESCONDE as linhas de níveis não marcados.
    // Vazio = sem filtro. Grupos de log cru (sem level) nunca são escondidos.
    void setLevelFilter(const QSet<QString> &levels);
    QSet<QString> levelFilter() const { return m_levelFilter; }
    int matchCount() const { return m_matchingRows.size(); }
    // Índice (1-based, para "N/M") da ocorrência atual, ou 0 sem busca/resultado.
    int currentMatchOrdinal() const;
    bool rowMatches(int row) const;
    bool isCurrentMatchRow(int row) const;
    // Anda para a próxima/anterior linha que casa (com wrap-around); devolve o índice da linha (-1 se nenhuma).
    int goToNextMatch();
    int goToPreviousMatch();

    void toggleExpanded(int row);
    const LogLineEntry &entryAt(int row) const;
    bool isEmpty() const { return m_all.isEmpty(); }

    static LogLineEntry parseLine(const QString &rawLine);

signals:
    // Emitido quando a linha atual da busca muda (a view rola até ela).
    void currentMatchRowChanged(int row);
    // Emitido ao adicionar uma linha JSON estruturada com nível de erro (error/fatal/crit, mesmo predicado de
    // colorForLevel). Quem escuta (OutputPanel) decide sobre "primeira vez" e notificação.
    void errorLineDetected();

private:
    void appendLine(const QString &line);
    // Profundidade de chaves (fora de string) para saber quando um objeto fragmentado por wrap fechou.
    void scanJsonChar(QChar c);
    void flushAccumulatedJson();
    void giveUpAccumulation();
    void insertEntry(const LogLineEntry &entry);
    // Funde `line` no grupo de log cru aberto no fim de m_all, ou abre um novo (LogLineEntry::isRawGroup).
    void insertRawLine(const QString &line);
    void recomputeMatches();
    bool passesLevelFilter(const LogLineEntry &entry) const;
    void rebuildVisibleRows();
    // Query completa (vários tokens; ver setSearchTerm) contra UMA entrada.
    bool entryMatchesTerm(const LogLineEntry &entry) const;
    // Um token. `field` vazio = busca livre; senão só no campo nomeado (level/message/time/chave de extraFields).
    bool entryMatchesToken(const LogLineEntry &entry, const QString &field, const QString &value) const;

    QVector<LogLineEntry> m_all;
    // Índices de m_all que passam o filtro de nível atual: é o que a view enxerga (rowCount/data/entryAt/toggleExpanded
    // operam em "linha visível"). Sem filtro, contém todos na mesma ordem.
    QVector<int> m_visibleRows;
    QSet<QString> m_levelFilter; // vazio = sem filtro
    QString m_pending; // linha parcial ainda não fechada por '\n'

    // Acumulação de objeto JSON fragmentado em várias linhas físicas (wrap do terminal).
    bool m_accumulatingJson = false;
    QString m_jsonAccumBuffer;
    QStringList m_jsonAccumRawLines;
    int m_jsonAccumDepth = 0;
    bool m_jsonAccumInString = false;
    bool m_jsonAccumEscaped = false;

    AnsiTextParser m_ansiParser; // só pra linhas cruas (ver ansiSegments)

    QString m_searchTerm;
    // Índices em espaço de linha visível (não m_all) que casam com a busca atual.
    QVector<int> m_matchingRows;
    int m_currentMatchPos = -1;  // índice DENTRO de m_matchingRows

    // Campos/valores de nível já vistos.
    QSet<QString> m_knownFields;
    QSet<QString> m_knownLevels;
};

// Desenha cada registro como um "card" no estilo do app: barra de nível à esquerda, chevron, horário, pílula do nível e
// a mensagem em fonte proporcional; expandido ganha moldura (cantos pelos tokens da preferência do usuário), a
// mensagem completa quebrada em linhas e a grade chave/valor dos campos. Grupos de texto cru seguem o mesmo desenho.
class LogLineDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LogLineDelegate(LogLineModel *model, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Expostos para a LogLineView acertar a área de clique do chevron (só ali alterna expandir/colapsar; o resto da
    // linha seleciona/copia). `rowHeight` é a altura do cabeçalho do card (colapsado = a linha inteira).
    int rowHeight() const;
    int chevronHitWidth() const;

private:
    // Fontes e medidas, calculadas das preferências atuais (fonte/tamanho/densidade): paint, sizeHint e a largura
    // de rolagem leem DAQUI, então nunca divergem.
    struct Metrics {
        QFont body;   // mensagem (proporcional)
        QFont mono;   // texto cru e linhas de grupo
        QFont small;  // horário e campos
        QFont badge;  // pílula do nível
        int bodyH = 0;
        int monoH = 0;
        int smallH = 0;
        int badgeH = 0;
        int mx = 0;        // folga do card até a borda da view
        int padX = 0;
        int padY = 0;
        int gap = 0;       // entre horário, pílula e mensagem
        int pillPadX = 0;
        int pillMinW = 0;  // largura mínima da pílula: os níveis comuns alinham a mensagem
        int headerH = 0;   // altura do cabeçalho do card (sem as folgas de cima/baixo)
        int rowH = 0;      // headerH + as folgas de cima/baixo entre cards
        int fieldRowH = 0;
        int lineH = 0;     // uma linha de um grupo de texto cru expandido
    };
    // Altura/geometria de um card para uma largura de linha (a mensagem quebra na largura disponível).
    struct Layout {
        int headerH = 0;
        int messageH = 0;
        int fieldsH = 0;
        int linesH = 0;
        int total = 0;
    };
    // Medidas das preferências atuais, reconstruídas só quando fonte/tamanho/densidade mudam (cada sizeHint/paint as lê).
    const Metrics &metrics() const;
    Metrics buildMetrics() const;
    Layout layoutFor(const LogLineEntry &entry, const Metrics &m, int rowWidth) const;
    // Linhas da grade de campos: o horário completo (quando o cabeçalho mostra só a hora) e os campos extras.
    static QVector<QPair<QString, QString>> fieldRows(const LogLineEntry &entry);
    QColor colorForLevel(const QString &level) const;
    QString badgeTextForLevel(const QString &levelRaw, const QString &level) const;
    int pillWidth(const Metrics &m, const QString &text) const;
    // Largura disponível ao texto do card (depois do chevron) numa linha de `rowWidth`.
    int contentWidth(const Metrics &m, int rowWidth) const;
    // Largura que a linha realmente PRECISA; sizeHint a usa para habilitar rolagem horizontal quando o conteúdo
    // excede o viewport.
    int rowContentWidth(const QModelIndex &index) const;
    int computeRowContentWidth(const LogLineEntry &entry, const Metrics &m) const;

    LogLineModel *m_model;
    mutable Metrics m_metrics;
    mutable QString m_metricsKey;
    mutable int m_metricsGeneration = 0;
};

class LogLineView : public QListView {
    Q_OBJECT
public:
    explicit LogLineView(QWidget *parent = nullptr);

    // Quem lê o modelo vê tudo o que já foi anexado: o texto ainda em espera é entregue antes.
    LogLineModel *logModel() const { flushPending(); return m_model; }
    // Texto novo. O primeiro trecho entra na hora; os seguintes, em rajada, são agrupados (~50 ms): a QListView refaz o
    // layout de TODAS as linhas a cada inserção, então um trecho por leitura do processo era O(linhas) cada vez.
    void appendText(const QString &text);
    // Entrega já o texto em espera (ver appendText).
    void flush() const { flushPending(); }
    void clearLog();
    void setFilterText(const QString &needle);
    bool isEmpty() const { flushPending(); return m_model->isEmpty(); }

    // Avança/retrocede a ocorrência atual e rola até ela.
    void goToNextMatch();
    void goToPreviousMatch();

signals:
    // Repassa LogLineModel::errorLineDetected (o dono não precisa conhecer logModel()).
    void errorLineDetected();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void copySelectionToClipboard();
    void jumpToRow(int row);
    void flushPending() const;

    LogLineModel *m_model;
    LogLineDelegate *m_delegate;
    mutable QString m_pending;
    mutable bool m_pendingAtBottom = true;
    QTimer *m_flushTimer = nullptr;
    QElapsedTimer m_sinceFlush;
};

} // namespace kai::ui
