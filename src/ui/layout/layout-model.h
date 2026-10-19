#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace kai::ui::layout {

// O quadro é uma grade de colunas × linhas (1 a 5 de cada). Cada célula é um grupo de abas; uma célula sem painéis não
// aparece e as vizinhas ocupam o lugar dela (ver Expansion). `Zone` é o endereço de uma célula (o nome vem de quando eram
// cinco zonas fixas).
inline constexpr int kMaxGridLines = 5;

struct Zone {
    int row = 0;
    int col = 0;

    bool operator==(const Zone &other) const = default;
};

inline size_t qHash(const Zone &zone, size_t seed = 0) noexcept { return qHashMulti(seed, zone.row, zone.col); }

// Como as células vazias são absorvidas. Vertical: cada coluna é uma pilha de cima para baixo só das células preenchidas
// (a que sobra ocupa a coluna toda). Horizontal: o mesmo com as linhas, da esquerda para a direita.
enum class Expansion { Vertical, Horizontal };

// Onde um painel nasce quando nunca esteve em lugar nenhum: na célula da lista de comandos ou na da Saída.
enum class Home { Primary, Secondary };

// A borda da janela onde uma célula recolhida vira uma faixa de ícones.
enum class Edge { Left, Right, Top, Bottom };
inline bool isVerticalEdge(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }

// "linha,coluna" (zero-based): vai para o layout.json.
QString zoneName(Zone zone);
std::optional<Zone> zoneFromName(const QString &name);
QString expansionName(Expansion expansion);
Expansion expansionFromName(const QString &name);

// Ids estáveis dos painéis (vão para o layout.json — nunca renomear).
namespace view_id {
inline const QString kCommands = QStringLiteral("commands");
inline const QString kOutput = QStringLiteral("output");
inline const QString kDocument = QStringLiteral("document");
inline const QString kAiChat = QStringLiteral("ai_chat");
inline const QString kProcesses = QStringLiteral("processes");
inline const QString kHistory = QStringLiteral("history");
inline const QString kNotifications = QStringLiteral("notifications");
inline const QString kVariables = QStringLiteral("variables");
// Um comando KIP fixado como painel: "kip:<commandId>".
inline const QString kKipPrefix = QStringLiteral("kip:");
// Uma execução arrastada para fora da Saída: "run:<commandId>".
inline const QString kRunPrefix = QStringLiteral("run:");
} // namespace view_id

struct ZoneState {
    QStringList views;
    QString active;
    // Fração da pilha da célula (altura da coluna no modo vertical, largura da linha no horizontal). 0 = parte igual.
    double size = 0.0;
    // Recolhida numa faixa fina de ícones na borda (estilo JetBrains). Só as células da borda recolhem.
    bool collapsed = false;
    // Modo dividir: em vez de abas, a célula mostra TODOS os painéis dela ao mesmo tempo, cada um numa caixa com o próprio
    // cabeçalho (lado a lado nas células largas, empilhados nas estreitas).
    bool split = false;
    // Numa célula recolhida, o painel que está aberto ao lado do conteúdo (vazio = a faixa está fechada). Volta aberto ao
    // reiniciar o Kai.
    QString open;

    bool operator==(const ZoneState &other) const = default;
};

struct FloatingView {
    QString view;
    QRect geometry;
    bool alwaysOnTop = false;

    bool operator==(const FloatingView &other) const = default;
};

// Uma pilha do plano: uma coluna (modo vertical) ou linha (horizontal) com as células que aparecem, na ordem.
struct GridStack {
    int line = 0;
    QList<Zone> cells;
    // A fração de cada célula na pilha (soma 1).
    QList<double> weights;
};

// O que aparece e com que proporção: o DockHost só traduz isto em divisores. Pilhas sem célula visível não entram.
struct GridPlan {
    Expansion expansion = Expansion::Vertical;
    QList<GridStack> stacks;
    // A fração de cada pilha no eixo principal (soma 1), na mesma ordem de `stacks`.
    QList<double> lineWeights;

    bool empty() const { return stacks.isEmpty(); }
};

// O estado do layout, sem nenhum widget: onde cada painel está, qual aba é a ativa, os tamanhos, os flutuantes e os
// fechados. Toda a regra de validação/reparo vive aqui (testável sem janela): um layout.json corrompido, com ids
// desconhecidos ou duplicados, nunca chega à interface sem antes ser consertado.
class LayoutModel {
public:
    // Diz se um id de painel existe (a lista fixa mais os `kip:<id>` de comandos que ainda existem, etc.).
    using Validator = std::function<bool(const QString &)>;

    // 3: o quadro virou uma grade (arquivos de versões anteriores, de cinco zonas fixas, não são convertidos).
    static constexpr int kVersion = 3;
    static constexpr int kDefaultCols = 2;
    static constexpr int kDefaultRows = 2;
    // Uma célula/pilha nunca é menor que isto (fração): o usuário redimensiona à vontade.
    static constexpr double kMinSize = 0.02;

    // Classic = o arranjo de antes do layout flexível (lista em cima, Saída embaixo).
    enum class Preset { Compact, Runner, Http, Reading, Classic, Triple, Quad };

    // Comandos à esquerda e Saída à direita, metade da janela cada uma, com as abas à mostra (modo reorganizável).
    static LayoutModel defaultLayout();
    static LayoutModel preset(Preset preset);
    static std::optional<Preset> presetFromName(const QString &name);
    static QString presetName(Preset preset);
    // `output_position` (bottom|left|right) + `output_splitter_sizes` das configurações antigas.
    static LayoutModel fromLegacy(const QString &outputPosition, const QList<int> &outputSplitterSizes);
    // Lê e CONSERTA; `problems` recebe uma linha por coisa consertada (para o log). Arquivo de versão anterior à grade:
    // assume o padrão (quem chama é quem guarda a cópia do arquivo).
    static LayoutModel fromJson(const QJsonObject &root, const Validator &isKnown, QStringList *problems = nullptr);
    // O arquivo é de antes da grade (cinco zonas)? O LayoutStore guarda uma cópia antes de seguir com o padrão.
    static bool isPreGrid(const QJsonObject &root);
    QJsonObject toJson() const;

    // --- a grade
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    Expansion expansion() const { return m_expansion; }
    bool inGrid(Zone zone) const { return zone.row >= 0 && zone.col >= 0 && zone.row < m_rows && zone.col < m_cols; }
    // As células da grade, linha a linha.
    QList<Zone> cells() const;
    // Onde a lista de comandos nasce / onde a Saída (e o resto) nasce.
    Zone primaryZone() const { return Zone{0, 0}; }
    Zone secondaryZone() const;
    Zone homeZone(Home home) const { return home == Home::Primary ? primaryZone() : secondaryZone(); }
    // A borda onde a célula recolhe, ou nada se ela não está na borda (a do meio não recolhe).
    std::optional<Edge> railEdge(Zone zone) const;
    bool collapsible(Zone zone) const { return railEdge(zone).has_value(); }
    // Muda o tamanho da grade e/ou o modo. Os painéis das células que saem passam (como abas) para a célula mais próxima
    // que sobra: nada se perde. Mudar o modo zera os tamanhos (o eixo das proporções troca).
    bool setGrid(int cols, int rows, Expansion expansion);
    // O que aparece. `visible` decide por célula (o padrão: tem painel e não está recolhida).
    GridPlan plan(const std::function<bool(Zone)> &visible = {}) const;
    double lineSize(int line) const;
    bool setLineSize(int line, double size);

    // --- consulta
    const ZoneState &zone(Zone zone) const { return m_zones[slotOf(zone)]; }
    const QList<FloatingView> &floating() const { return m_floating; }
    const QString &maximized() const { return m_maximized; }
    const QStringList &closedViews() const { return m_closed; }
    std::optional<Zone> zoneOf(const QString &view) const;
    bool isFloating(const QString &view) const;
    // Visível = numa zona ou flutuando.
    bool isPlaced(const QString &view) const;
    QStringList placedViews() const;
    int placedCount() const;
    // A última zona onde o painel esteve (ao reabrir, volta para lá).
    Zone lastZoneOf(const QString &view, Zone fallback) const;
    // A zona tem painéis (e portanto aparece na janela)?
    bool zoneOccupied(Zone zone) const { return !m_zones[slotOf(zone)].views.isEmpty(); }
    // A faixa de abas aparece em TODA zona (o modo reorganizável, o padrão); desligada, só aparece com mais de um painel.
    bool tabsAlways() const { return m_tabsAlways; }
    bool setTabsAlways(bool on);

    // --- mudança (cada uma devolve se algo mudou)
    bool placeView(const QString &view, Zone zone, int index = -1);
    bool closeView(const QString &view);
    // Reabre na última zona (ou em `fallback` se nunca esteve em nenhuma).
    bool openView(const QString &view, Zone fallback);
    // Esquece o painel por completo (lugar, fechado, última zona): para painéis de uma sessão só (`run:<id>`).
    bool forgetView(const QString &view);
    bool floatView(const QString &view, const QRect &geometry, bool alwaysOnTop = false);
    bool setFloatingGeometry(const QString &view, const QRect &geometry);
    bool setFloatingOnTop(const QString &view, bool onTop);
    bool setActive(const QString &view);
    bool setMaximized(const QString &view);
    bool setZoneSize(Zone zone, double size);
    bool setZoneCollapsed(Zone zone, bool collapsed);
    // Marca (ou limpa, com vazio) o painel aberto numa zona recolhida.
    bool setZoneOpen(Zone zone, const QString &view);
    // Liga/desliga o modo dividir da zona (só vale com ao menos um painel nela).
    bool setZoneSplit(Zone zone, bool split);
    bool moveWithinZone(const QString &view, int index);
    // Fecha todos menos `keep` na zona dele.
    bool closeOthersInZone(const QString &keep);

    // Conserta o que estiver inconsistente. Chamado por fromJson; público para os testes e para o host.
    void repair(const Validator &isKnown, QStringList *problems = nullptr);

    bool operator==(const LayoutModel &other) const = default;

private:
    // Um endereço fora de 5×5 cai na célula mais próxima em vez de estourar o vetor.
    static int slotOf(Zone zone)
    {
        const auto limit = [](int value) { return value < 0 ? 0 : (value >= kMaxGridLines ? kMaxGridLines - 1 : value); };
        return limit(zone.row) * kMaxGridLines + limit(zone.col);
    }
    static constexpr int kSlots = kMaxGridLines * kMaxGridLines;
    static Zone zoneAt(int slot) { return Zone{slot / kMaxGridLines, slot % kMaxGridLines}; }

    void removeEverywhere(const QString &view);
    void fixActive(ZoneState &zone);
    void rememberClosed(const QString &view);
    // Painéis de células fora da grade passam para a célula mais próxima dentro dela.
    void relocateOutsideGrid();
    void clampSizes(QStringList *notes);

    ZoneState m_zones[kSlots];
    double m_lineSizes[kMaxGridLines] = {0, 0, 0, 0, 0};
    int m_cols = kDefaultCols;
    int m_rows = kDefaultRows;
    Expansion m_expansion = Expansion::Vertical;
    QList<FloatingView> m_floating;
    QStringList m_closed;
    QHash<QString, Zone> m_last;
    QString m_maximized;
    bool m_tabsAlways = true;
};

} // namespace kai::ui::layout
