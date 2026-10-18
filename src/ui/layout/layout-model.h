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

// As quatro zonas fixas da janela (esquerda | centro / baixo | direita), como a barra lateral, o editor e o painel do
// VSCode. Cada zona é um grupo de abas; uma zona sem painéis some.
enum class Zone { Left, Center, Right, Bottom };

inline constexpr Zone kAllZones[] = {Zone::Left, Zone::Center, Zone::Right, Zone::Bottom};

QString zoneName(Zone zone);
std::optional<Zone> zoneFromName(const QString &name);

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
    // Fração do eixo da janela (largura para esquerda/direita; altura da coluna central para baixo). 0 = padrão.
    double size = 0.0;
    // Recolhida numa faixa fina de ícones na borda (estilo JetBrains).
    bool collapsed = false;

    bool operator==(const ZoneState &other) const = default;
};

struct FloatingView {
    QString view;
    QRect geometry;
    bool alwaysOnTop = false;

    bool operator==(const FloatingView &other) const = default;
};

// O estado do layout, sem nenhum widget: onde cada painel está, qual aba é a ativa, os tamanhos, os flutuantes e os
// fechados. Toda a regra de validação/reparo vive aqui (testável sem janela): um layout.json corrompido, com ids
// desconhecidos ou duplicados, nunca chega à interface sem antes ser consertado.
class LayoutModel {
public:
    // Diz se um id de painel existe (a lista fixa mais os `kip:<id>` de comandos que ainda existem, etc.).
    using Validator = std::function<bool(const QString &)>;

    static constexpr int kVersion = 1;
    static constexpr double kDefaultBottomSize = 0.5;
    static constexpr double kDefaultSideSize = 1.0 / 3.0;
    static constexpr double kMinSize = 0.1;
    static constexpr double kMaxSideSize = 0.85;
    static constexpr double kMaxBottomSize = 0.85;

    // Classic = o arranjo de antes do layout flexível (lista no centro, Saída embaixo).
    enum class Preset { Compact, Runner, Http, Reading, Classic };

    // Comandos à esquerda e Saída à direita, metade da janela cada uma, com as abas à mostra (modo reorganizável).
    static LayoutModel defaultLayout();
    static LayoutModel preset(Preset preset);
    static std::optional<Preset> presetFromName(const QString &name);
    static QString presetName(Preset preset);
    // `output_position` (bottom|left|right) + `output_splitter_sizes` das configurações antigas.
    static LayoutModel fromLegacy(const QString &outputPosition, const QList<int> &outputSplitterSizes);
    // Lê e CONSERTA; `problems` recebe uma linha por coisa consertada (para o log).
    static LayoutModel fromJson(const QJsonObject &root, const Validator &isKnown, QStringList *problems = nullptr);
    QJsonObject toJson() const;

    // --- consulta
    const ZoneState &zone(Zone zone) const { return m_zones[int(zone)]; }
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
    bool zoneOccupied(Zone zone) const { return !m_zones[int(zone)].views.isEmpty(); }
    // Tamanho efetivo (o padrão quando nada foi definido).
    double effectiveSize(Zone zone) const;
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
    bool moveWithinZone(const QString &view, int index);
    // Fecha todos menos `keep` na zona dele.
    bool closeOthersInZone(const QString &keep);

    // Conserta o que estiver inconsistente. Chamado por fromJson; público para os testes e para o host.
    void repair(const Validator &isKnown, QStringList *problems = nullptr);

    bool operator==(const LayoutModel &other) const = default;

private:
    void removeEverywhere(const QString &view);
    void fixActive(ZoneState &zone);
    void rememberClosed(const QString &view);

    ZoneState m_zones[4];
    QList<FloatingView> m_floating;
    QStringList m_closed;
    QHash<QString, Zone> m_last;
    QString m_maximized;
    bool m_tabsAlways = true;
};

} // namespace kai::ui::layout
