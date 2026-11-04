#pragma once

#include <QColor>
#include <QFont>
#include <functional>
#include <QMap>
#include <QString>
#include <QStringList>

namespace kai::utils {

// Ponto único de acesso aos tokens visuais. O MainWindow publica as variáveis do tema (publishTheme) e cada widget
// lê por token semântico, com fallback derivado das cores base quando o tema não define o token (temas antigos,
// schema 5, seguem funcionando).
namespace tokens {

void publishTheme(const QMap<QString, QString> &variables);

QString value(const QString &key, const QString &fallback = QString());

QColor color(const QString &key, const QColor &fallback = QColor());

QString bg();            // fundo da janela
QString fg();            // texto principal
QString altBg();         // fundo alternativo (barras, cabeçalhos)
QString selBg();         // seleção
QString accent();        // cor de destaque
QString mutedFg();
// Texto de aba NÃO selecionada: um degrau mais claro que mutedFg() para não
// perder contraste contra o fundo (ex: "Testes" ao lado de "Projetos").
QString tabInactiveFg();
QString buttonColor();   // botões primários (opcional "button_color", fallback accent)
QString buttonFg();      // texto do botão primário (opcional "button_fg", auto por brilho)
QString borderColor();   // borda sutil (derivado de bg/selBg)
QString hoverBg();       // hover de item/botão
QString treeStripeBg();  // listra de zebra sutil de árvores/listas

QString surface();       // painel sobre o fundo
QString surface2();      // painel sobre painel (diálogos, popovers)

QString terminalBg();
QString terminalFg();
QString codeBg();
QString codeFg();
QString codeBorder();

// Gradientes opcionais do tema, em 3 bases: "primary" (chrome do app: janela, diálogos, TopUtilityBar, árvore, superfícies
// da Saída e cards reutilizáveis), "secondary" (reservada; campos usam só fundo sólido) e "tertiary" (entalhes
// decorativos: logo e dica da Welcome Screen). Um tema declara uma base com "gradient_<base>_start"/"..._end" (+
// "..._angle" opcional, em graus, padrão 135); sem as duas variáveis daquela base, hasGradient(base) é false e o
// chamador cai num fundo sólido. `slot` é o nome da base.
bool hasGradient(const QString &slot = QString());
QString gradientStart(const QString &slot = QString());
QString gradientEnd(const QString &slot = QString());
int gradientAngle(const QString &slot = QString());
// QSS pronto (ex.: "background-color: qlineargradient(...);") usando `property` como propriedade CSS. Vazio quando o
// tema não define gradiente para este slot ou os gradientes estão desligados (setGradientsEnabled).
QString gradientQss(const QString &property = QStringLiteral("background-color"),
                    const QString &slot = QString());

// Liga/desliga todos os gradientes (Configurações -> Aparência, SettingsData::gradientsEnabled): desligado,
// hasGradient() é false em qualquer slot mesmo que o tema declare as variáveis.
void setGradientsEnabled(bool enabled);
bool gradientsEnabled();

QString successFg();
QString errorFg();
QString warningFg();
QString infoFg();
QString dangerBg();      // hover destrutivo (fechar janela, excluir)

int radiusSm();          // 4..6  (chips, badges)
int radiusMd();          // 8..10 (inputs, botões)
int radiusLg();          // 12..16 (painéis, diálogos)
// radiusMd limitado a um campo de altura `height`: o Qt desiste do canto arredondado (fica QUADRADO) quando o raio passa
// da metade da altura real, e o layout pode deixar o campo um pouco menor que o pedido — daí a folga.
int radiusMdForHeight(int height);
int space(int steps);    // grade de 4px: space(1)=4, space(2)=8, space(3)=12...
int iconButtonSize();
int controlHeight();     // altura padrão de input/botão

QString fontFamily();
// Lista CSS de fallback para QSS (ex.: "'JetBrains Mono', Consolas,monospace"). Nunca passar ao construtor de
// QFont: QFont(QString) trata a string inteira como um nome de família literal. Para um QFont use monoFamilies()
// + QFont::setFamilies().
QString monoFamily();
// Mesma lista de monoFamily() quebrada em nomes individuais (sem aspas), para QFont::setFamilies().
QStringList monoFamilies();
// QFont pronto com monoFamilies() + hint/fixedPitch (sem nenhuma fonte da lista instalada, o Qt ainda escolhe uma
// monoespaçada). `pointSize` <= 0 usa fontSizePt().
QFont monoFont(int pointSize = 0);
int fontSizePt();        // corpo
int fontSizeSmallPt();   // legendas/metadados
int fontSizeTitlePt();   // títulos de painel
int titleWeight();       // peso do título (600/700)

enum class Density { Compact, Comfortable };
Density density();
void setDensity(Density d);

struct Effects {
    bool shadows = false;      // QGraphicsDropShadowEffect em painéis/diálogos
    bool translucency = false; // fundo translúcido (base do "material líquido")
    bool blur = false;         // blur nativo: Mica/Acrylic no Win11, KWin no KDE
    bool animations = false;   // fade/slide em painéis e diálogos
    int  cornerStyle = 1;      // 0=reto, 1=suave, 2=arredondado (Material)
};
const Effects &effects();
void setEffects(const Effects &e);

// QSS pronto para áreas de código/terminal.
QString codeAreaQss();

} // namespace tokens
} // namespace kai::utils
