#include "utils/design-tokens.h"

#include <qmath.h>

namespace kai::utils::tokens {
namespace {

QMap<QString, QString> g_vars;
Density g_density = Density::Comfortable;
Effects g_effects;

// Um tema pode ser claro OU escuro. As derivações precisam saber para qual
// lado "empurrar" a cor (escurecer no claro, clarear no escuro) — sem isso um
// token derivado sumiria no tema claro.
bool isDarkTheme()
{
    const QColor base(value(QStringLiteral("bg"), QStringLiteral("#282a36")));
    return base.isValid() ? base.lightnessF() < 0.5 : true;
}

// Mistura duas cores (t=0 -> a, t=1 -> b).
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t);
}

// Aproxima/afasta do "fundo" conforme o tema, para gerar camadas.
QColor shift(const QColor &c, qreal amount)
{
    return mix(c, isDarkTheme() ? QColor(Qt::white) : QColor(Qt::black), amount);
}

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

// Lê um token; se ausente, calcula a partir das cores base.
QString derived(const QString &key, const std::function<QString()> &fn)
{
    const QString explicitValue = g_vars.value(key);
    return explicitValue.isEmpty() ? fn() : explicitValue;
}

int intToken(const QString &key, int fallback)
{
    const QString raw = g_vars.value(key);
    if (raw.isEmpty()) {
        return fallback;
    }
    QString digits;
    for (const QChar &ch : raw) {
        if (ch.isDigit()) digits += ch;
        else if (!digits.isEmpty()) break;
    }
    bool ok = false;
    const int parsed = digits.toInt(&ok);
    return ok ? parsed : fallback;
}

} // namespace

void publishTheme(const QMap<QString, QString> &variables) { g_vars = variables; }

QString value(const QString &key, const QString &fallback)
{
    const QString v = g_vars.value(key);
    return v.isEmpty() ? fallback : v;
}

QColor color(const QString &key, const QColor &fallback)
{
    const QColor c(value(key));
    return c.isValid() ? c : fallback;
}

QString bg()     { return value(QStringLiteral("bg"), QStringLiteral("#282a36")); }
QString fg()     { return value(QStringLiteral("fg"), QStringLiteral("#f8f8f2")); }
QString altBg()  { return value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")); }
QString selBg()  { return value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")); }

QString accent()
{
    QString a = g_vars.value(QStringLiteral("accent_color"));
    if (a.isEmpty()) a = g_vars.value(QStringLiteral("accent"));
    return a.isEmpty() ? QStringLiteral("#bd93f9") : a;
}

QString buttonColor()
{
    // Cor dos botões primários. Variável de tema OPCIONAL "button_color":
    // quando ausente, cai no accent (retrocompatível — o roxo de antes).
    QString b = g_vars.value(QStringLiteral("button_color"));
    return b.isEmpty() ? accent() : b;
}

QString buttonFg()
{
    // Texto do botão primário. Variável OPCIONAL "button_fg"; quando ausente,
    // escolhe preto/branco automaticamente pelo brilho da cor do botão.
    QString f = g_vars.value(QStringLiteral("button_fg"));
    if (!f.isEmpty()) return f;
    return QColor(buttonColor()).lightnessF() > 0.6
        ? QStringLiteral("#101014") : QStringLiteral("#ffffff");
}

QString mutedFg()
{
    return derived(QStringLiteral("muted_fg"), [] {
        return hex(mix(QColor(fg()), QColor(bg()), 0.40));
    });
}

QString borderColor()
{
    return derived(QStringLiteral("border_color"), [] {
        return hex(mix(QColor(bg()), QColor(fg()), isDarkTheme() ? 0.14 : 0.18));
    });
}

QString hoverBg()
{
    return derived(QStringLiteral("hover_bg"), [] {
        return hex(shift(QColor(bg()), 0.10));
    });
}

QString treeStripeBg()
{
    // Listra de zebra SUTIL das árvores/listas (estilo CopyQ): mesmo desvio
    // mínimo já usado em app-stylesheet.cpp, exposto aqui como token para
    // ser reaproveitado por quem pinta a linha manualmente (ex:
    // DraggableTreeWidget, que assume o fundo da linha inteira para não
    // deixar vão entre colunas no hover — ver command-tree-widget.cpp).
    return hex(mix(QColor(bg()), QColor(fg()), isDarkTheme() ? 0.035 : 0.028));
}

QString surface()
{
    // 0.08 (era 0.05): bug visual real reportado com print ("antes pedi
    // pra que os campos tenham fundo, mas o fundo ficou na caixa atrás")
    // — em temas SEM um "surface" próprio definido (ex: Dracula, o tema
    // padrão de instalação nova — kai-dark tem "surface" explícito e por
    // isso nunca mostrou o problema), o valor derivado era claro demais
    // perto do bg(): o card mal se distinguia do fundo do diálogo, e os
    // campos (bg() puro) mal se distinguiam do card — tudo parecia "sem
    // contraste nenhum". 0.08 mantém a intenção original (derivar do
    // tema, sem cravar cor fixa) mas com um salto perceptível de verdade.
    return derived(QStringLiteral("surface"), [] {
        return hex(shift(QColor(bg()), isDarkTheme() ? 0.08 : 0.03));
    });
}

QString surface2()
{
    // 0.15 (era 0.10) — mesmo motivo do surface() acima, mantendo a
    // mesma distância relativa entre surface/surface2 (~quase o dobro).
    return derived(QStringLiteral("surface_2"), [] {
        return hex(shift(QColor(bg()), isDarkTheme() ? 0.15 : 0.06));
    });
}

// Terminal/código: no tema ESCURO afunda em relação ao fundo; no CLARO fica
// levemente acinzentado. Antes era #1e1e1e cravado, o que deixava o tema
// claro com um retângulo preto no meio da tela.
QString terminalBg()
{
    return derived(QStringLiteral("terminal_bg"), [] {
        const QColor base(bg());
        return hex(isDarkTheme() ? mix(base, QColor(Qt::black), 0.45)
                                 : mix(base, QColor(Qt::black), 0.04));
    });
}

QString terminalFg()
{
    return derived(QStringLiteral("terminal_fg"), [] { return fg(); });
}

QString codeBg()
{
    return derived(QStringLiteral("code_bg"), [] {
        const QColor base(bg());
        return hex(isDarkTheme() ? mix(base, QColor(Qt::black), 0.35)
                                 : mix(base, QColor(Qt::black), 0.03));
    });
}

QString codeFg()     { return derived(QStringLiteral("code_fg"), [] { return fg(); }); }
QString codeBorder() { return derived(QStringLiteral("code_border"), [] { return borderColor(); }); }

QString successFg()
{
    return derived(QStringLiteral("success_fg"), [] {
        return isDarkTheme() ? QStringLiteral("#50fa7b") : QStringLiteral("#137a3a");
    });
}

QString errorFg()
{
    return derived(QStringLiteral("error_fg"), [] {
        return isDarkTheme() ? QStringLiteral("#ff5555") : QStringLiteral("#c02626");
    });
}

QString warningFg()
{
    return derived(QStringLiteral("warning_fg"), [] {
        return isDarkTheme() ? QStringLiteral("#f1fa8c") : QStringLiteral("#8a6d00");
    });
}

QString infoFg()
{
    return derived(QStringLiteral("info_fg"), [] {
        return isDarkTheme() ? QStringLiteral("#8be9fd") : QStringLiteral("#0b6b86");
    });
}

QString dangerBg()
{
    return derived(QStringLiteral("danger_bg"), [] { return QStringLiteral("#e81123"); });
}

// --- Gradientes dinâmicos opcionais, em 3 bases (primary/secondary/
// tertiary) — ver comentário completo no .h ---
namespace {
bool g_gradientsEnabled = true;

// "primary" -> "gradient_primary_start"; "" (vazio, não usado pelas 3
// bases atuais, só por chamadas legadas em teste) -> "gradient_start".
QString gradientKey(const QString &slot, const QString &suffix)
{
    return slot.isEmpty()
        ? (QStringLiteral("gradient_") + suffix)
        : (QStringLiteral("gradient_") + slot + QStringLiteral("_") + suffix);
}
} // namespace

void setGradientsEnabled(bool enabled)
{
    g_gradientsEnabled = enabled;
}

bool gradientsEnabled()
{
    return g_gradientsEnabled;
}

bool hasGradient(const QString &slot)
{
    if (!g_gradientsEnabled) {
        return false;
    }
    return !g_vars.value(gradientKey(slot, QStringLiteral("start"))).isEmpty()
        && !g_vars.value(gradientKey(slot, QStringLiteral("end"))).isEmpty();
}

QString gradientStart(const QString &slot)
{
    return value(gradientKey(slot, QStringLiteral("start")), accent());
}

QString gradientEnd(const QString &slot)
{
    return value(gradientKey(slot, QStringLiteral("end")), surface2());
}

int gradientAngle(const QString &slot)
{
    return intToken(gradientKey(slot, QStringLiteral("angle")), 135);
}

QString gradientQss(const QString &property, const QString &slot)
{
    if (!hasGradient(slot)) {
        return QString();
    }
    // Converte o ângulo (0°=esquerda->direita, 90°=cima->baixo, sentido
    // horário, convenção CSS) num par de pontos x1/y1/x2/y2 normalizados
    // [0,1] que o QSS do Qt usa (não entende "deg" diretamente).
    const qreal rad = qDegreesToRadians(static_cast<qreal>(gradientAngle(slot)));
    const qreal dx = qSin(rad);
    const qreal dy = -qCos(rad);
    const qreal x1 = 0.5 - dx * 0.5, y1 = 0.5 - dy * 0.5;
    const qreal x2 = 0.5 + dx * 0.5, y2 = 0.5 + dy * 0.5;
    return QStringLiteral("%1: qlineargradient(x1:%2, y1:%3, x2:%4, y2:%5, "
                          "stop:0 %6, stop:1 %7);")
        .arg(property)
        .arg(x1, 0, 'f', 3).arg(y1, 0, 'f', 3).arg(x2, 0, 'f', 3).arg(y2, 0, 'f', 3)
        .arg(gradientStart(slot), gradientEnd(slot));
}

// --- Métricas: o estilo de canto escolhido no Settings escala os raios ---
int radiusSm()
{
    switch (g_effects.cornerStyle) {
    case 0: return 0;
    case 2: return 8;
    default: return intToken(QStringLiteral("radius_sm"), 5);
    }
}

int radiusMd()
{
    switch (g_effects.cornerStyle) {
    case 0: return 0;
    case 2: return 16;
    default: return intToken(QStringLiteral("radius_md"), 9);
    }
}

int radiusLg()
{
    switch (g_effects.cornerStyle) {
    case 0: return 0;
    case 2: return 24;
    default: return intToken(QStringLiteral("radius_lg"), 14);
    }
}

// Grade de 4px (8pt grid dividido): substitui os paddings avulsos
// (16,16,16,12 vs 20,20,20,16 vs 16,16,16,16 que coexistiam).
int space(int steps)
{
    const int unit = (g_density == Density::Compact) ? 3 : 4;
    return unit * (steps < 0 ? 0 : steps);
}

int iconButtonSize()
{
    return (g_density == Density::Compact) ? 26 : 30;
}

int controlHeight()
{
    return (g_density == Density::Compact) ? 28 : 32;
}

QString fontFamily()
{
    return value(QStringLiteral("font_family"), QStringLiteral("Ubuntu"));
}

QString monoFamily()
{
    return value(QStringLiteral("mono_family"),
                 QStringLiteral("'JetBrains Mono','Cascadia Code','Fira Code',"
                                "'Ubuntu Mono',Consolas,monospace"));
}

QStringList monoFamilies()
{
    QStringList families;
    for (QString family : monoFamily().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        family = family.trimmed();
        if (family.startsWith(QLatin1Char('\'')) && family.endsWith(QLatin1Char('\''))) {
            family = family.mid(1, family.size() - 2);
        }
        if (!family.isEmpty()) {
            families << family;
        }
    }
    return families;
}

QFont monoFont(int pointSize)
{
    QFont f;
    f.setFamilies(monoFamilies());
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSize(pointSize > 0 ? pointSize : fontSizePt());
    return f;
}

int fontSizePt()      { return intToken(QStringLiteral("font_size"), 12) - (g_density == Density::Compact ? 1 : 0); }
int fontSizeSmallPt() { return qMax(7, fontSizePt() - 2); }
int fontSizeTitlePt() { return fontSizePt() + 1; }
int titleWeight()     { return intToken(QStringLiteral("title_weight"), 600); }

Density density() { return g_density; }
void setDensity(Density d) { g_density = d; }

const Effects &effects() { return g_effects; }
void setEffects(const Effects &e) { g_effects = e; }

QString codeAreaQss()
{
    return QStringLiteral(
        "background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; font-family: %5;")
        .arg(codeBg(), codeFg(), codeBorder())
        .arg(radiusMd())
        .arg(monoFamily());
}

} // namespace kai::utils::tokens
