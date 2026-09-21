#pragma once

#include <QColor>
#include <QFont>
#include <functional>
#include <QMap>
#include <QString>
#include <QStringList>

namespace kai::utils {

// ============================================================================
// DESIGN SYSTEM DO KAI
// ----------------------------------------------------------------------------
// Ponto ÚNICO de acesso aos tokens visuais. Antes cada widget carregava suas
// próprias cores no código (~45 setStyleSheet com #1e1e1e, #50fa7b, #ff5555,
// #44475a fixos), o que (a) fazia o tema CLARO não funcionar de verdade —
// terminal e editores ficavam escuros à força — e (b) impedia qualquer
// repaginação central.
//
// Agora: o MainWindow publica as variáveis do tema aqui (publishTheme) e
// QUALQUER widget lê por token semântico, com FALLBACK DERIVADO das cores
// base quando o tema não define o token. Assim temas antigos (schema 5)
// continuam funcionando sem precisar declarar os tokens novos.
// ============================================================================
namespace tokens {

// --- Publicação (chamado pelo MainWindow ao aplicar/recarregar o tema) ---
void publishTheme(const QMap<QString, QString> &variables);

// --- Leitura crua (string) com default ---
QString value(const QString &key, const QString &fallback = QString());

// --- Cores semânticas (derivadas quando ausentes no tema) ---
QColor color(const QString &key, const QColor &fallback = QColor());

QString bg();            // fundo da janela
QString fg();            // texto principal
QString altBg();         // fundo alternativo (barras, cabeçalhos)
QString selBg();         // seleção
QString accent();        // cor de destaque
QString mutedFg();       // texto secundário (derivado de fg)
QString buttonColor();   // botões primários (opcional "button_color", fallback accent)
QString buttonFg();      // texto do botão primário (opcional "button_fg", auto por brilho)
QString borderColor();   // borda sutil (derivado de bg/selBg)
QString hoverBg();       // hover de item/botão
QString treeStripeBg();  // listra de zebra sutil de árvores/listas

// Superfícies em camadas (elevação por cor, estilo Material 3)
QString surface();       // painel sobre o fundo
QString surface2();      // painel sobre painel (diálogos, popovers)

// Áreas de código/terminal: seguem o tema (antes eram #1e1e1e fixo)
QString terminalBg();
QString terminalFg();
QString codeBg();
QString codeFg();
QString codeBorder();

// Gradientes dinâmicos opcionais do tema, em 3 BASES reutilizáveis (pedido
// do usuário: "quero gradientes diferentes, para que o tema possa decidir
// se usa ou não em tal lugar" — cada base cobre VÁRIAS áreas da UI de uma
// vez, em vez de um slot por widget isolado):
//   - "primary": chrome do app inteiro — janela (#rootContainer),
//     diálogos, TopUtilityBar, árvore de comandos — E as superfícies da
//     Saída (cartões da aba Requisição/Headers) e os cards reutilizáveis
//     (makeSurfaceCard, CollapsibleSectionCard). "App e saídas".
//   - "secondary": caixinhas de texto — QLineEdit/QComboBox/
//     QPlainTextEdit/QTextEdit.
//   - "tertiary": botões e entalhes — QPushButton[kaiRole="primary"],
//     botão :default de diálogos, botão "+Adicionar" dos cards
//     colapsáveis, logo da Welcome Screen.
// Um tema declara uma base com "gradient_<base>_start"/"..._end" (+
// "..._angle" opcional, em graus, padrão 135); sem essas duas variáveis
// PARA AQUELA BASE, hasGradient(base) retorna false e o chamador deve cair
// num background sólido (surface()/bg()/accent()/etc conforme o caso) —
// cada base entra ou não, tema a tema, independente das outras duas.
// `slot` (nome do parâmetro, preservado por compat) é o nome da base.
bool hasGradient(const QString &slot = QString());
QString gradientStart(const QString &slot = QString());
QString gradientEnd(const QString &slot = QString());
int gradientAngle(const QString &slot = QString());
// QSS pronto (ex: "background-color: qlineargradient(...);") usando
// `property` como nome da propriedade CSS ("background-color" por
// padrão). Retorna string vazia quando o tema não define gradiente PARA
// ESTE SLOT (ou quando gradientes estão desligados globalmente, ver
// setGradientsEnabled) — o chamador decide o fallback.
QString gradientQss(const QString &property = QStringLiteral("background-color"),
                    const QString &slot = QString());

// Liga/desliga TODOS os gradientes globalmente (Configurações -> Aparência
// -> "Gradientes", pedido do usuário: "uma opção que desabilita os
// gradientes"). Refletido em SettingsData::gradientsEnabled; hasGradient()
// (qualquer slot) retorna false enquanto desligado, mesmo que o tema ativo
// declare as variáveis — os chamadores não precisam checar os dois.
void setGradientsEnabled(bool enabled);
bool gradientsEnabled();

// Semânticas de estado
QString successFg();
QString errorFg();
QString warningFg();
QString infoFg();
QString dangerBg();      // hover destrutivo (fechar janela, excluir)

// --- Métricas ---
int radiusSm();          // 4..6  (chips, badges)
int radiusMd();          // 8..10 (inputs, botões)
int radiusLg();          // 12..16 (painéis, diálogos)
int space(int steps);    // grade de 4px: space(1)=4, space(2)=8, space(3)=12...
int iconButtonSize();    // tamanho ÚNICO de botão de ícone (antes 24/30/32)
int controlHeight();     // altura padrão de input/botão

// --- Tipografia (escala; antes era bold/600/11pt/15px avulso) ---
QString fontFamily();
// Lista CSS de fallback pronta pra QSS/stylesheet (ex: "'JetBrains Mono',
// Consolas,monospace") — NUNCA passar isto direto pro construtor de QFont
// (QFont(QString) trata a string INTEIRA como UM nome de família literal,
// não entende a sintaxe de vírgulas do CSS; achado real, reportado:
// "a saída estilo grafana não suporta o char 'ç'" — o glifo sumia porque o
// Qt caía num fallback de fonte imprevisível ao não achar uma família
// chamada literalmente "'JetBrains Mono','Cascadia Code',...monospace").
// Para um QFont de verdade, use monoFamilies() + QFont::setFamilies().
QString monoFamily();
// Mesma lista de monoFamily(), já quebrada em nomes individuais (aspas
// simples removidas) — pronta para QFont::setFamilies(), a API que o Qt
// realmente usa para fallback em cascata entre fontes.
QStringList monoFamilies();
// Atalho pronto: QFont já configurado com monoFamilies() + hint/fixedPitch
// de segurança (se NENHUMA fonte da lista estiver instalada, o Qt ainda
// escolhe um substituto monoespaçado de verdade, não uma proporcional
// qualquer). `pointSize` <= 0 usa fontSizePt().
QFont monoFont(int pointSize = 0);
int fontSizePt();        // corpo
int fontSizeSmallPt();   // legendas/metadados
int fontSizeTitlePt();   // títulos de painel
int titleWeight();       // peso do título (600/700)

// --- Densidade ---
enum class Density { Compact, Comfortable };
Density density();
void setDensity(Density d);

// --- Efeitos opcionais (ligáveis no Settings) ---
struct Effects {
    bool shadows = false;      // QGraphicsDropShadowEffect em painéis/diálogos
    bool translucency = false; // fundo translúcido (base do "material líquido")
    bool blur = false;         // blur nativo: Mica/Acrylic no Win11, KWin no KDE
    bool animations = false;   // fade/slide em painéis e diálogos
    int  cornerStyle = 1;      // 0=reto, 1=suave, 2=arredondado (Material)
};
const Effects &effects();
void setEffects(const Effects &e);

// Estilo de QSS pronto para áreas de código/terminal (evita repetir string
// em 6 arquivos diferentes).
QString codeAreaQss();

} // namespace tokens
} // namespace kai::utils
