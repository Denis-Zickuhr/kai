# Especificação de Padronização de Abas — Kai v2.0

> Análise de padronização visual, estrutural e de UX para todas as abas do Kai,
> com proposta de separação em arquivos dedicados por tipo de conteúdo e
> reorganização de métricas HTTP.
>
> Versão: 1.0 (inicial)  
> Data: 2026-09-21  
> Escopo: UI/UX de abas dinâmicas, abas de output, temas e consistência visual

> **Regra não-negociável (aprendida com um bug real na feature de Autosync
> em paralelo):** todo nome de ícone citado abaixo (`"terminal"`,
> `"braces"`, `"send"`, `"list"`, ou qualquer um novo que você precisar
> introduzir) precisa ser confirmado com
> `ls assets/icons/lucide/ | grep -i <termo>` **antes** de usar no código.
> Os nomes citados neste documento foram verificados e existem, mas se
> você introduzir um ícone novo (ex: para a aba de Environment sugerida
> na seção 4.1) e não confirmar que o `.svg` existe, ele vai renderizar
> **vazio, sem erro de build** — exatamente o que aconteceu na tela de
> Autosync com o nome `"sync"`.

---

## 0. Resumo executivo

**Problema**: As abas do Kai hoje têm inconsistência visual (cores, bordas,
espaçamento), estrutura monolítica (todo conteúdo concentrado em `output-panel.cpp`)
e as **métricas de resposta HTTP ficam fora das abas**, separadas em um label
superior — deixando a interface fragmentada.

**Solução proposta**:
1. **Padronização visual**: 4 tipos de aba (dinâmicas de pasta, output, request,
   métricas HTTP) com cores, bordas, espaçamento e temas consistentes.
2. **Separação estrutural**: cada tipo de conteúdo vira um arquivo próprio
   (`aba-output-content.h/cpp`, `aba-request-content.h/cpp`,
   `aba-metrics-content.h/cpp`, etc.), promovendo reuso e manutenção.
3. **Métricas dentro da aba**: as métricas (status, tempo, tamanho) migram de um
   label solto para um **cabeçalho compacto dentro da aba de resposta HTTP**,
   eliminando visual fragmentado.

**Resultado esperado**: interface mais coesa, código mais modular, UX mais clara.

---

## 1. Análise de abas atuais

### 1.1 Tipos de aba existentes (mapeamento)

Hoje existem **5 tipos de aba** no Kai, misturadas em contextos/arquivos distintos:

| Tipo | Contexto | Arquivo atual | Conteúdo | Visual |
|---|---|---|---|---|
| **Dinâmica de pasta** | MainWindow | `command-tree-widget.cpp` | Árvore de comandos de uma pasta raiz | Aba "flutuante", texto + ícone, cor neutra |
| **Output / Stdout** | OutputPanel | `output-panel.cpp` | stdout/stderr com ANSI colors, números de linha | Aba de abas, fundo `surface2()`, texto monoespaço |
| **JSON** | OutputPanel | `output-panel.cpp` / `json-viewer-widget.cpp` | JSON estruturado (navegável) | Mesma aba, syntax highlight, fundo `codeBg()` |
| **Request HTTP** | OutputPanel | `output-panel.cpp` + local anônimo | URL, method badge, headers, body | Mesma aba, method badge colorido, headers em tabela |
| **Headers HTTP** | OutputPanel | `output-panel.cpp` + `makeKeyValueTable` | Headers de resposta (chave/valor) | Tabela padrão, `alternatingRowColors()`, `surface()` |

**Além disso, fora das abas:**
| Elemento | Arquivo | Conteúdo | Problema |
|---|---|---|---|
| **Métricas HTTP** | `output-panel.cpp` | Status code, tempo decorrido, tamanho do corpo | 🔴 Exibido num label SOLTO acima da barra de abas — fragmenta a UI |

### 1.2 Problemas de padronização

#### 1.2.1 Visual/Temas
- **Cores inconsistentes**: headers em branco sobre `surface()`, JSON em `codeBg()`,
  abas em `surface2()` — sem harmonia visual.
- **Bordas hardcoded**: `border-radius: 8px` em vários lugares (§9 do AGENTS.md
  proíbe hardcode — todas devem ser `radiusSm/Md/Lg`). Validar:
  - `output-panel.cpp:131` → `border-radius: %2px` → convertido pra `radiusMd()` ✓
  - `updateHttpVerbPill` → method badge → validar se tem hardcode de raio.
- **Espaçamento**: nenhuma métrica clara de padding/margin entre elementos de aba;
  alguns lugares usam `contentsMargins(3, 3, 3, 3)`, outros não declaram nada.
- **Fonte**: output usa monospace global, JSON usa o mesmo tamanho,
  request/headers usam tamanho padrão de label — sem hierarquia clara.

#### 1.2.2 Estrutural
- **Tudo em output-panel.cpp**: as 5 abas (output, JSON, request, headers + o
  label de métricas) são gerenciadas num único arquivo (~1000 linhas). Manutenção
  é difícil.
- **Sem reuso**: `makeKeyValueTable` é uma function local anônima; outros
  painéis de configuração que querem tabelas similares não conseguem reaproveitar.
- **Estado fragmentado**: cada aba tem seu próprio `m_hasJson`, `m_hasRequest`,
  `m_hasHeaders`, `m_stdoutTabEnabled` — sem modelo unificado de visibilidade.

#### 1.2.3 UX
- **Métricas fora do contexto**: o label de status/tempo/tamanho fica acima da
  barra de abas, separado — o usuário não sabe imediatamente que aquilo é parte
  da resposta HTTP.
- **Ordenação de abas**: hoje é output, JSON, request, headers (hardcoded em
  `updateTabVisibility`). Proposta: reordenar de forma mais lógica (ver §2.2).

---

## 2. Especificação de padronização

### 2.1 Paleta de cores e temas (revisado)

**Define**: cada tipo de aba tem uma cor de fundo, borda e texto programáticos —
sempre via `utils::tokens::*()`, nunca hardcode.

#### Aba "Output / Stdout"
- **Fundo**: `tokens::codeBg()` (já existe, escuro/terminal-like).
- **Texto**: `tokens::codeFg()`.
- **Borda**: `tokens::borderColor()`, raio `radiusSmMd()`.
- **Espaçamento**: padding interno 8px (gap entre borda e texto).

#### Aba "JSON"
- **Fundo**: `tokens::codeBg()` (mesmo que output, ambas são código).
- **Texto**: `tokens::codeFg()`.
- **Destaque (keys)**: `tokens::accent()`.
- **Borda**: `tokens::borderColor()`, raio `radiusMd()`.
- **Espaçamento**: padding interno 8px.

#### Aba "Request HTTP"
- **Fundo**: `tokens::surface()` (genérico, dados estruturados).
- **Método badge**: fundo semi-transparente do verbo (GET verde, POST azul,
  DELETE vermelho), raio `radiusSm()`, sem hardcode.
- **URL label**: `tokens::fg()`, monospace.
- **Headers/Body**: tabelas com `alternatingRowColors()`, fundo alternando entre
  `tokens::surface()` e `tokens::surface2()`.
- **Borda**: `tokens::borderColor()`, raio `radiusMd()`.

#### Aba "Headers HTTP" (Response)
- **Fundo**: `tokens::surface()`.
- **Tabela**: `alternatingRowColors()` com `surface()`/`surface2()`.
- **Chave**: `tokens::fg()`, peso bold.
- **Valor**: `tokens::mutedFg()`, quebrável se muito longa.
- **Borda**: `tokens::borderColor()`, raio `radiusMd()`.

#### Abas dinâmicas (de pasta)
- **Fundo selecionado**: `tokens::selBg()` (já é via CSS do Qt, manter).
- **Fundo não-selecionado**: `tokens::altBg()`.
- **Texto**: `tokens::fg()`.
- **Badge de pasta raiz**: ícone + nome, sem borda, padding 4px lateral.
- **Borda de separação entre abas**: `tokens::borderColor()`, 1px.

#### Métricas HTTP (novo cabeçalho dentro da aba)
- **Fundo**: `tokens::surface2()` (container dentro da resposta, ligeiramente
  elevado).
- **Texto**: dependendo do status (verde sucesso, vermelho erro, amarelo aviso),
  usar `successFg()`, `errorFg()`, `warningFg()`.
- **Raio**: `radiusSm()`.
- **Espaçamento**: padding 6px vertical, 8px horizontal; margin 4px embaixo da
  métrica até o conteúdo.

### 2.2 Estrutura de arquivos (nova)

**Princípio**: cada **tipo de conteúdo** de aba vira uma classe/widget próprio em um
arquivo dedicado, agregando formato + dados + comportamento.

#### Arquivos novos (módulo `src/ui/features/output/`)

```
src/ui/features/output/
├── output-panel.h                    (existente, refatorado)
├── output-panel.cpp                  (existente, refatorado — orquestra abas)
├── aba-content.h                     (base abstrata para todas as abas)
├── output-stdout-content.h/.cpp      (aba Output: stdout/stderr)
├── output-json-content.h/.cpp        (aba JSON)
├── output-http-request-content.h/.cpp (aba Request)
├── output-http-metrics-content.h/.cpp (aba Metrics — NOVO)
├── output-http-headers-content.h/.cpp (aba Headers)
├── output-metrics-header.h/.cpp      (cabeçalho compacto de métricas — NOVO)
└── [existentes] ...
```

#### Base abstrata — `aba-content.h`

```cpp
namespace kai::ui {

// Interface base para conteúdo de uma aba — padroniza layout, tema,
// visibilidade e limpeza.
class AbaContent : public QWidget {
    Q_OBJECT
public:
    explicit AbaContent(QWidget *parent = nullptr);

    // Retorna o rótulo da aba (ex: "Saída", "JSON").
    virtual QString label() const = 0;

    // Retorna o ícone da aba (ex: "terminal", "braces").
    virtual QString iconName() const = 0;

    // Limpa todo conteúdo e reseta para estado vazio.
    virtual void clear() = 0;

    // Retorna true se há algo pra exibir (o OutputPanel usa isto
    // para mostrar/ocultar a aba automaticamente).
    virtual bool hasContent() const = 0;

    // Define as ViewOptions globais (números de linha, quebra, timestamp, etc).
    virtual void setViewOptions(const OutputPanel::ViewOptions &opts) {}

    // Aplica o tema atualmente ativo (fontes, cores). Chamado ao
    // mudar de tema.
    virtual void applyTheme() {}
};

} // namespace kai::ui
```

#### Exemplo — `output-stdout-content.h`

```cpp
class OutputStdoutContent : public AbaContent {
    Q_OBJECT
public:
    explicit OutputStdoutContent(QWidget *parent = nullptr);

    QString label() const override { return utils::tr(QStringLiteral("output.tab.stdout")); }
    QString iconName() const override { return QStringLiteral("terminal"); }

    void append(const QString &text, bool isError = false);
    void clear() override;
    bool hasContent() const override;

    void setViewOptions(const OutputPanel::ViewOptions &opts) override;
    void applyTheme() override;

private:
    CodeOutputView *m_view = nullptr; // já existe
    OutputStatus m_lastStatus = OutputStatus::Idle;
};
```

#### Novo — `output-metrics-header.h` (cabeçalho dentro da aba HTTP)

```cpp
// Cabeçalho compacto exibido no TOPO da aba de resposta HTTP,
// mostrando status code, tempo decorrido, tamanho do corpo.
class OutputMetricsHeader : public QWidget {
    Q_OBJECT
public:
    explicit OutputMetricsHeader(QWidget *parent = nullptr);

    void setMetrics(int statusCode, const QString &reasonPhrase,
                    qint64 elapsedMs, qint64 bodySize, bool success);
    void clear();

    void applyTheme();

private:
    QLabel *m_statusBadge = nullptr;    // "200 OK" ou "404 Not Found"
    QLabel *m_timeBadge = nullptr;      // "125 ms"
    QLabel *m_sizeBadge = nullptr;      // "2.5 KB"
};
```

### 2.3 Reorganização de OutputPanel

**Antes**:
```
OutputPanel {
  QVBoxLayout {
    m_metricsLabel       (label solto acima das abas ❌)
    m_tabBar             (abas: Output, JSON, Request, Headers)
    m_tabStack {
      m_outputContainer
      m_jsonView
      m_requestView
      m_headersView
    }
  }
}
```

**Depois**:
```
OutputPanel {
  QVBoxLayout {
    m_tabBar             (abas: Output, JSON, Request, Headers)
    m_tabStack {
      OutputStdoutContent {
        CodeOutputView
      }
      OutputJsonContent {
        JsonViewerWidget
      }
      OutputHttpRequestContent {
        QVBoxLayout {
          OutputMetricsHeader  (✅ AQUI: status, tempo, tamanho)
          RequestMethodBadge
          RequestUrlLabel
          RequestHeadersTable
          RequestBodyView
        }
      }
      OutputHttpHeadersContent {
        HeadersTable (response)
      }
    }
  }
}
```

**Benefício**: as métricas ficam visualmente **parte da resposta HTTP**, não um
elemento solto. Mais intuitivo.

---

## 3. Guia de implementação

### 3.1 Etapas (por ordem)

1. **Base e tokens** (etapa 1 — 2-3 horas)
   - Criar `aba-content.h` (classe abstrata).
   - Validar/corrigir todos os hardcodes de border-radius nos arquivos existentes
     (converter pra `radiusSm/Md/Lg`).
   - Criar `output-metrics-header.h/.cpp` (widget novo para métricas).

2. **Extrair abas existentes** (etapa 2 — 4-5 horas)
   - Criar `output-stdout-content.h/.cpp` (move `m_outputView` de
     `output-panel.cpp`).
   - Criar `output-json-content.h/.cpp` (move `m_jsonView` + lógica).
   - Criar `output-http-request-content.h/.cpp` (move `m_requestView` +
     `m_requestMethodBadge`, etc.).
   - Criar `output-http-headers-content.h/.cpp` (move `m_headersView`).
   - Refatorar `output-panel.cpp` pra apenas orquestrar as abas.

3. **Integrar métricas** (etapa 3 — 2 horas)
   - Mover `m_metricsLabel` → incluir `OutputMetricsHeader` dentro de
     `OutputHttpRequestContent`.
   - Conectar `setHttpResult` pra atualizar o `OutputMetricsHeader`.

4. **Testes + tema** (etapa 4 — 2-3 horas)
   - Validar que nenhum elemento tem cor/raio hardcoded.
   - Testar mudanças de tema (escuro/claro) aplicam a todas as abas.
   - Teste manual: abrir um projeto, fazer um HTTP request, validar que métricas
     aparecem dentro da aba de request.

### 3.2 Padrão de implementação (exemplo: `output-stdout-content.cpp`)

```cpp
#include "output-stdout-content.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;

OutputStdoutContent::OutputStdoutContent(QWidget *parent)
    : AbaContent(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); // sin fronteras
    
    m_view = new CodeOutputView(this);
    m_view->setReadOnly(true);
    layout->addWidget(m_view);
    
    applyTheme();
}

void OutputStdoutContent::append(const QString &text, bool isError)
{
    m_view->appendOutput(text, isError);
}

void OutputStdoutContent::clear()
{
    m_view->clear();
}

bool OutputStdoutContent::hasContent() const
{
    return !m_view->toPlainText().isEmpty();
}

void OutputStdoutContent::applyTheme()
{
    // Define fundo/texto via CSS, nunca hardcode.
    this->setStyleSheet(QString(
        "OutputStdoutContent { background-color: %1; color: %2; }"
    ).arg(tk::codeBg()).arg(tk::codeFg()));
}

} // namespace kai::ui
```

### 3.3 Ordem de cores — resumo rápido

| Tipo de aba | Fundo | Texto | Borda | Raio |
|---|---|---|---|---|
| Output/Stdout | `codeBg()` | `codeFg()` | `borderColor()` | `radiusMd()` |
| JSON | `codeBg()` | `codeFg()` | `borderColor()` | `radiusMd()` |
| Request HTTP | `surface()` | `fg()` | `borderColor()` | `radiusMd()` |
| Headers HTTP | `surface()` | `fg()` | `borderColor()` | `radiusMd()` |
| Métricas (header) | `surface2()` | `successFg()` / `errorFg()` / `warningFg()` | `borderColor()` | `radiusSm()` |
| Abas dinâmicas | `selBg()` / `altBg()` | `fg()` | `borderColor()` | `radiusSm()` |

---

## 4. Impacto e benefícios

### 4.1 Benefícios
- ✅ **Código mais limpo**: cada aba é uma classe própria (~100-150 linhas),
  fácil de testar e manter.
- ✅ **Reuso**: `OutputStdoutContent`, `OutputMetricsHeader` etc. podem ser
  reaproveitas noutras telas (ex: janela detached, tela de histórico de execução).
- ✅ **UX mais coesa**: métricas dentro da resposta, não flutuando soltas.
- ✅ **Tema consistente**: todas as abas usam `tokens::*()` — ao trocar de tema,
  tudo muda junto, sem surpresas.
- ✅ **Extensibilidade**: adicionar uma nova aba (ex: aba de "Environment" com
  variáveis resolvidas) é só criar `output-env-content.h/.cpp` que herda de
  `AbaContent`.

### 4.2 Esforço estimado
- **Implementação + testes**: ~15-20 horas.
- **Compatibilidade**: mudança é interna; nenhum impacto na API pública de
  `OutputPanel`.
- **Risco**: médio-baixo — código está isolado, alterações não afetam core/engine.

---

## 5. Checklist de implementação

### Fase 1: Base
- [ ] Criar `aba-content.h` (classe abstrata).
- [ ] Criar `output-metrics-header.h/.cpp`.
- [ ] Validar hardcodes de `border-radius` em todo `output-panel.cpp`.
- [ ] Converter para `radiusSm/Md/Lg`.

### Fase 2: Extrair abas
- [ ] Criar `output-stdout-content.h/.cpp`.
- [ ] Criar `output-json-content.h/.cpp`.
- [ ] Criar `output-http-request-content.h/.cpp`.
- [ ] Criar `output-http-headers-content.h/.cpp`.
- [ ] Refatorar `OutputPanel` para orquestrar classes novas.
- [ ] Build limpo, testes passando.

### Fase 3: Integrar métricas
- [ ] Mover `m_metricsLabel` → incluir dentro de `OutputHttpRequestContent`.
- [ ] Conectar `setHttpResult` → `OutputMetricsHeader::setMetrics`.
- [ ] Testar visualmente no app.

### Fase 4: Temas e finalização
- [ ] Validar all-abas aplicam o tema corretamente.
- [ ] `test_i18n_sync` passar (se houver strings novas).
- [ ] Build limpo com `-Wall -Wextra`.
- [ ] Checklist manual de testes (ver seção 6).

---

## 6. Checklist de teste manual

### Testes de estrutura
- [ ] Abrir um projeto HTTP.
- [ ] Disparar um GET request.
- [ ] Validar que a aba "Request" mostra:
  - [ ] Status code + razão (ex: "200 OK") em badge no TOPO da aba.
  - [ ] Tempo decorrido (ex: "125 ms") em badge.
  - [ ] Tamanho do corpo (ex: "2.5 KB") em badge.
  - [ ] **Todos 3 badges dentro da aba**, não soltos acima.

### Testes de tema
- [ ] Trocar para tema claro (Appearance → tema claro).
- [ ] Validar que backgrounds, textos e bordas mudam corretamente.
- [ ] Trocar de volta pro tema escuro.

### Testes de abas
- [ ] Disparar um request que retorna JSON.
- [ ] Validar que abas aparecem na ordem correta:
  - [ ] Output (se houver stdout/stderr).
  - [ ] JSON (se houver JSON na resposta).
  - [ ] Request (sempre que há request HTTP).
  - [ ] Headers (sempre que há headers de resposta).
- [ ] Clicar em cada aba — conteúdo renderiza corretamente.

### Testes de responsividade
- [ ] Redimensionar a janela do Kai.
- [ ] Validar que abas e métricas não quebram layout.
- [ ] Abrir a janela de output detached.
- [ ] Validar que métricas também aparecem lá.

---

## 7. Exemplo visual (antes/depois)

### Antes (layout problema)
```
┌─────────────────────────────────────┐
│ MainWindow                          │
├─────────────────────────────────────┤
│ Árvore de comandos  │  Output Panel  │
│                     │                │
│                     │ 200 OK • 125ms │ ← Métrica SOLTA
│                     │ • 2.5 KB       │
│                     │                │
│                     │ [Saída][JSON]  │ ← Abas de tabs
│                     │ [Request]      │
│                     │ [Headers]      │
│                     │                │
│                     │ <conteúdo>     │ ← Conteúdo muda
│                     │ conforme aba   │
│                     │ selecionada    │
└─────────────────────────────────────┘
```

### Depois (layout melhorado)
```
┌─────────────────────────────────────┐
│ MainWindow                          │
├─────────────────────────────────────┤
│ Árvore de comandos  │  Output Panel  │
│                     │                │
│                     │ [Saída][JSON]  │ ← Abas de tabs
│                     │ [Request]      │   (sem métrica solta)
│                     │ [Headers]      │
│                     │                │
│                     │ ┌─────────────┐│ ← Aba de Request
│                     │ │ 200 OK      ││   com métricas
│                     │ │ 125ms • 2.5KB││   integradas
│                     │ ├─────────────┤│
│                     │ │ GET https://││
│                     │ │ ...         ││
│                     │ │ Headers:    ││
│                     │ │ ...         ││
│                     │ └─────────────┘│
└─────────────────────────────────────┘
```

---

## 8. Próximos passos

1. **Revisão desta spec** com o usuário — validar se a estrutura de
   separação de arquivos e a posição das métricas fazem sentido.
2. **Passagem pro haiku** para implementar nas 4 fases (etapas 1-4,
   ~15-20 horas no total).
3. **Após merge**, marcar como base para outras features que
   possam reaproveitar `AbaContent` (ex: aba de Environments,
   aba de Logs estruturados, etc.).

---

**Fim da especificação.**
