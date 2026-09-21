#include "ui/shared/collection-chip-picker.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLineEdit>
#include <QCompleter>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractItemView>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QColor>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QKeyEvent>
#include <QEvent>
#include <QTimer>

#include <algorithm>
#include <functional>

namespace kai::ui {

namespace {
// Quantas sugestões mostrar no popup, no máximo — sem isto, uma coleção
// grande (milhares de linhas) faria o completer construir um popup gigante
// a cada tecla digitada.
constexpr int kMaxSuggestions = 30;
// Lado (px) do botão "x" de remover e do ícone dentro dele — CASADOS de
// propósito (achado real, com foto: "o ícone de excluir a chip está
// descentralizada horizontal"). O bug era usar um ícone de 17px (tamanho
// fixo de makeIconButton, pensado pra botões de 26-30px) dentro de um
// botão encolhido pra 16px: o ícone ficava maior que a própria área
// clicável e cortava de um lado só. Aqui os dois são definidos JUNTOS.
constexpr int kChipCloseButtonSide = 18;
constexpr int kChipCloseIconSide = 12;

// Uma chip: rótulo + botão "x" de remover, pílula com o raio de borda do
// design system (AGENTS.md regra 9: NUNCA hardcode um raio — chips/pills
// usam radiusSm/Md/Lg conforme o token do tema). Fundo em gradiente
// "tertiary" quando o tema declara um (mesmo slot dos botões/entalhes —
// pedido do usuário: "bota a cor com gradientes bonita", chip cinza-seco
// "surface2" era o fallback sem graça); cai pro accent sólido do sistema
// de botões quando não há gradiente no tema.
QWidget *makeChip(QWidget *parent, const QString &label, const QString &entryId,
                   const std::function<void(const QString &)> &onRemove)
{
    auto *chip = new QWidget(parent);
    chip->setObjectName(QStringLiteral("collectionChip"));
    // Mesma causa raiz do card externo: QWidget puro precisa deste
    // atributo pro background-color/gradiente do QSS abaixo realmente
    // pintar.
    chip->setAttribute(Qt::WA_StyledBackground, true);
    const bool grad = utils::tokens::hasGradient(QStringLiteral("tertiary"));
    const QString bgDecl = grad
        ? utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("tertiary"))
        : QStringLiteral("background-color: %1;").arg(utils::tokens::buttonColor());
    const QString textColor = utils::tokens::buttonFg();
    chip->setStyleSheet(QStringLiteral(
        "QWidget#collectionChip { %1 border: none; border-radius: %2px; }")
        .arg(bgDecl).arg(utils::tokens::radiusSm()));
    auto *layout = new QHBoxLayout(chip);
    // Margem SIMÉTRICA nos dois lados (achado real, com foto: "o campinho
    // de apagar ainda ta decentralizado" — a margem direita era menor que
    // a esquerda, então o botão "x" ficava colado no canto arredondado da
    // pílula, sem respiro, mesmo já com o ícone certinho dentro dele).
    layout->setContentsMargins(utils::tokens::space(2), utils::tokens::space(1),
                                utils::tokens::space(2), utils::tokens::space(1));
    layout->setSpacing(utils::tokens::space(1));

    auto *text = new QLabel(label, chip);
    text->setStyleSheet(QStringLiteral("QLabel { color: %1; border: none; background: transparent; }")
        .arg(textColor));
    layout->addWidget(text);

    // Botão dedicado (NÃO makeIconButton — aquele hardcoda ícone de 17px
    // pensado pra um botão de 26-30px, grande demais pra uma chip
    // compacta). Padding zerado explicitamente: o QSS global de
    // QToolButton (app-stylesheet.cpp) aplica um padding próprio que,
    // combinado com um botão tão pequeno, também descentralizava o ícone.
    auto *closeBtn = new QToolButton(chip);
    closeBtn->setAutoRaise(true);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(utils::tr(QStringLiteral("params.collection.chip.remove")));
    closeBtn->setIcon(LucideIcons::icon(QStringLiteral("x"), QColor(textColor), kChipCloseIconSide));
    closeBtn->setIconSize(QSize(kChipCloseIconSide, kChipCloseIconSide));
    closeBtn->setFixedSize(kChipCloseButtonSide, kChipCloseButtonSide);
    // Achado real (com foto, 2 rodadas): mesmo com ícone/botão do MESMO
    // tamanho e padding zerado, o "x" continuava puxado pra esquerda. A
    // causa era o estilo Fusion reservar espaço à direita pro indicador
    // de seta de um MENU — reservado por padrão pra QUALQUER QToolButton
    // (popupMode() default é DelayedPopup), mesmo sem nenhum menu
    // associado. InstantPopup desliga essa reserva.
    closeBtn->setPopupMode(QToolButton::InstantPopup);
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: %1px; padding: 0px; }"
        "QToolButton:hover { background-color: rgba(0, 0, 0, 40); }")
        .arg(utils::tokens::radiusSm()));
    QObject::connect(closeBtn, &QToolButton::clicked, chip, [onRemove, entryId]() { onRemove(entryId); });
    layout->addWidget(closeBtn);

    return chip;
}
} // namespace

CollectionChipPickerWidget::CollectionChipPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("collectionChipPicker"));
    // SEM ISTO o border/background-color do QSS abaixo nunca pinta: um
    // QWidget puro (sem WA_StyledBackground) só recebe o atalho de fundo
    // sólido do Fusion em alguns casos, e aqui nem isso — mesma causa raiz
    // já corrigida em rootContainer/TopUtilityBar nesta sessão (achado
    // real, com foto: "ficou feio pra caralho, sem borda, fundo").
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#collectionChipPicker { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(utils::tokens::bg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd()));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // --- Barra de busca (linha PRÓPRIA, separada das chips — pedido do
    // usuário: "o componente de pesquisa e lupa ficou ali do lado...
    // gostaria que ele tivesse seu próprio local, poderia ser um
    // container superior acima pra ele"). ---
    auto *searchBar = new QWidget(this);
    searchBar->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *searchLayout = new QHBoxLayout(searchBar);
    // ZERO margem/espaçamento (mesmo padrão do campo de arquivo — ver o
    // "fileInputGroup" em parameter-form-dialog.cpp): o botão fica COLADO
    // no campo, e é o border-left dele (abaixo) que desenha o separador
    // "tipo um pipe entre o texto e o botão" (pedido do usuário). Com
    // espaçamento aqui, o pipe ficaria flutuando longe do texto em vez de
    // servir de divisória de verdade.
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(0);

    m_input = new QLineEdit(searchBar);
    m_input->setPlaceholderText(utils::tr(QStringLiteral("params.collection.chip.placeholder")));
    m_input->setFrame(false);
    m_input->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; color: %1; border: none; padding: %2px %3px; }")
        .arg(utils::tokens::fg())
        .arg(utils::tokens::space(1)).arg(utils::tokens::space(3)));
    connect(m_input, &QLineEdit::textEdited, this, &CollectionChipPickerWidget::updateSuggestions);
    // Backspace apaga a última chip quando o campo já está vazio (não dá
    // pra fazer isso com só o textEdited — precisa interceptar a tecla
    // ANTES dela virar "nada digitado, nada mudou").
    m_input->installEventFilter(this);
    searchLayout->addWidget(m_input, 1);

    m_pickButton = makeIconButton(searchBar, QStringLiteral("search"),
        utils::tr(QStringLiteral("params.collection.pick")), QColor(utils::tokens::accent()));
    // Mesmo "pipe" separador que o campo de arquivo já usa entre o texto e
    // o botão de procurar (pedido do usuário: "o campo de file tem uma
    // borda né? adiciona essa borda no campo de pesquisa também").
    m_pickButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
        " border-radius: 0px; }")
        .arg(utils::tokens::borderColor()));
    searchLayout->addWidget(m_pickButton);
    outerLayout->addWidget(searchBar);

    // Separador fino entre a busca e as chips — só aparece quando há pelo
    // menos uma chip (ver rebuildChips), pra não deixar uma linha solta
    // num campo ainda vazio.
    m_separator = new QFrame(this);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setStyleSheet(QStringLiteral("QFrame { color: %1; background-color: %1; max-height: 1px; }")
        .arg(utils::tokens::borderColor()));
    m_separator->setVisible(false);
    outerLayout->addWidget(m_separator);

    // --- Área de chips: QScrollArea (sem moldura própria — a borda já é
    // a do card externo) com altura travada em ~3 linhas; rola em vez de
    // crescer o formulário inteiro sem limite (pedido do usuário: "quando
    // der overflow limitar em três linhas e ter um scroll"). ---
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    // 3 linhas: altura do botão de remover da chip (o elemento mais alto
    // dela) mais o padding vertical da chip e o espaçamento do flow entre
    // linhas.
    const int chipLineHeight = kChipCloseButtonSide + utils::tokens::space(1) * 2;
    m_scrollArea->setMaximumHeight(3 * chipLineHeight + 2 * utils::tokens::space(1));
    m_scrollArea->setVisible(false); // some quando não há nenhuma chip ainda

    m_flowHost = new QWidget(m_scrollArea);
    m_flowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_flow = new FlowLayout(m_flowHost, utils::tokens::space(1),
        utils::tokens::space(1), utils::tokens::space(1));
    m_scrollArea->setWidget(m_flowHost);
    outerLayout->addWidget(m_scrollArea);

    m_completerModel = new QStandardItemModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    // UnfilteredPopupCompletion: NÓS decidimos o conteúdo do modelo a cada
    // tecla (fuzzy match, ver updateSuggestions) — o completer só cuida da
    // parte visual/navegação (popup, setas, Enter, Esc, clique), sem
    // aplicar o próprio filtro de prefixo dele por cima.
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_input->setCompleter(m_completer);
    // O popup do QCompleter é uma JANELA TOP-LEVEL própria (Qt::Popup),
    // separada da hierarquia deste widget — não pega a cascata de QSS do
    // app (mesma classe de bug já vista com o popup do QComboBox nesta
    // sessão: some, cai pro render nativo do SO). Estiliza localmente pra
    // garantir, com o mesmo critério visual usado lá (surface2/border/
    // radiusMd, seleção na cor de accent).
    m_completer->popup()->setStyleSheet(QStringLiteral(
        "QAbstractItemView { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; outline: none; padding: %5px; }"
        "QAbstractItemView::item { padding: %6px %7px; border-radius: %8px; }"
        "QAbstractItemView::item:selected { background-color: %9; color: %2; }")
        .arg(utils::tokens::surface2(), utils::tokens::fg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd()).arg(utils::tokens::space(1))
        .arg(utils::tokens::space(1)).arg(utils::tokens::space(2)).arg(utils::tokens::radiusSm())
        .arg(utils::tokens::selBg()));
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this,
        [this](const QModelIndex &index) {
            const QString entryId = index.data(Qt::UserRole).toString();
            if (entryId.isEmpty()) {
                return;
            }
            addChip(entryId);
            // QLineEdit::setCompleter() conecta, POR BAIXO DOS PANOS, sua
            // própria ligação em activated() que ESCREVE o texto da
            // sugestão escolhida de volta no campo — e essa ligação
            // interna do Qt roda DEPOIS da nossa (associada antes,
            // durante setCompleter() lá em cima), então um clear()
            // síncrono aqui era sobrescrito na sequência (achado real,
            // pedido do usuário: "ao selecionar uma entrada da lista,
            // apague o texto do filtro, vai ser útil para digitar vários
            // rápido" — o clear já existia, mas nunca "pegava" de
            // verdade). Agendar pro próximo ciclo do loop de eventos
            // garante que rodamos DEPOIS de qualquer escrita interna da
            // mesma ativação.
            QTimer::singleShot(0, this, [this]() { m_input->clear(); });
            emit selectionChanged();
        });
}

bool CollectionChipPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Backspace && m_input->text().isEmpty() && !m_selectedIds.isEmpty()) {
            removeEntry(m_selectedIds.last());
            return true; // consumido — não deixa o QCompleter/QLineEdit reagir
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CollectionChipPickerWidget::setEntries(const QVector<core::CollectionEntry> &entries,
                                             const QString &displayField)
{
    m_entries = entries;
    m_displayField = displayField;
}

QString CollectionChipPickerWidget::labelFor(const QString &entryId) const
{
    const auto it = std::find_if(m_entries.constBegin(), m_entries.constEnd(),
        [&entryId](const core::CollectionEntry &e) { return e.id == entryId; });
    if (it == m_entries.constEnd()) {
        return entryId;
    }
    const QString lbl = it->values.value(m_displayField);
    return lbl.isEmpty() ? it->id : lbl;
}

void CollectionChipPickerWidget::setSelectedIds(const QStringList &ids)
{
    m_selectedIds.clear();
    for (const QString &id : ids) {
        if (!m_selectedIds.contains(id)) {
            m_selectedIds << id;
        }
    }
    rebuildChips();
}

void CollectionChipPickerWidget::addChip(const QString &entryId)
{
    if (m_selectedIds.contains(entryId)) {
        return;
    }
    m_selectedIds << entryId;
    rebuildChips();
}

void CollectionChipPickerWidget::removeEntry(const QString &entryId)
{
    m_selectedIds.removeAll(entryId);
    rebuildChips();
    emit selectionChanged();
}

void CollectionChipPickerWidget::rebuildChips()
{
    // m_flow agora só contém chips (busca/lupa têm sua própria barra
    // acima, fora do flow) — remover e reconstruir tudo é seguro e
    // simples, sem precisar preservar nenhum outro widget no meio.
    while (m_flow->count() > 0) {
        QLayoutItem *item = m_flow->takeAt(0);
        delete item->widget();
        delete item;
    }
    for (const QString &id : m_selectedIds) {
        m_flow->addWidget(makeChip(m_flowHost, labelFor(id), id,
            [this](const QString &removedId) { removeEntry(removedId); }));
    }
    const bool hasChips = !m_selectedIds.isEmpty();
    m_scrollArea->setVisible(hasChips);
    m_separator->setVisible(hasChips);
}

void CollectionChipPickerWidget::updateSuggestions(const QString &query)
{
    m_completerModel->clear();
    if (query.trimmed().isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    // Score por entrada (não por campo isolado): mesmo critério de "busca
    // global fuzzy em qualquer campo" do CollectionSelectorDialog — pega o
    // MELHOR score entre todos os campos da entrada.
    struct ScoredEntry { QString id; int score; };
    QVector<ScoredEntry> scored;
    for (const core::CollectionEntry &e : m_entries) {
        if (m_selectedIds.contains(e.id)) {
            continue; // já escolhida — não sugere de novo
        }
        int best = -1;
        for (auto it = e.values.constBegin(); it != e.values.constEnd(); ++it) {
            best = qMax(best, FuzzyMatcher::score(query, it.value()));
        }
        if (best >= 0) {
            scored << ScoredEntry{e.id, best};
        }
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const ScoredEntry &a, const ScoredEntry &b) { return a.score > b.score; });

    for (int i = 0; i < scored.size() && i < kMaxSuggestions; ++i) {
        const QString &entryId = scored.at(i).id;
        auto *item = new QStandardItem(labelFor(entryId));
        item->setData(entryId, Qt::UserRole);
        m_completerModel->appendRow(item);
    }
    if (m_completerModel->rowCount() > 0) {
        // Por padrão o popup do QCompleter sai só do TAMANHO do
        // QLineEdit (m_input), que aqui divide a barra de busca com o
        // botão de lupa — ficava um popup mais estreito que o card
        // inteiro (foto do usuário: "gostaria que a caixinha de seleção
        // fosse maior e ocupasse a horizontalidade total do campo...
        // como a lista normal", comparando com o popup de um QComboBox
        // comum). complete(QRect) aceita a geometria exata do popup,
        // relativa à origem de m_input — alinha a esquerda com O CARD
        // INTEIRO (this), não só com o input, e usa a largura do card
        // inteiro.
        const QPoint inputOriginInCard = m_input->mapTo(this, QPoint(0, 0));
        const QRect popupRect(-inputOriginInCard.x(), m_input->height(), width(), 0);
        m_completer->complete(popupRect);
    } else {
        m_completer->popup()->hide();
    }
}

} // namespace kai::ui
