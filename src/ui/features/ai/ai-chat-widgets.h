#pragma once

#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QTextBrowser>

class QPushButton;
class QTimer;
class QToolButton;

namespace kai::ui {

// Peças visuais do chat de IA. Nenhuma sabe de provedores nem de ferramentas: só desenham.

// Contorno de botão visível (a borda sutil do tema some em fundo escuro).
QString aiStrongBorder();

enum class AiButtonKind { Primary, PrimaryDanger, Secondary, Danger, Ghost };
// Aplica o estilo de botão do chat (altura fixa, borda sempre visível, raio do tema).
void applyAiButtonStyle(QPushButton *button, AiButtonKind kind, int height = 32);

// Texto numa linha só, cortado com "…" quando falta largura (nunca estica o layout).
class ElidedLabel : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setElidedText(const QString &text);
    void setTextColor(const QColor &color);
    void setBold(bool bold);
    QString fullText() const { return m_full; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_full;
    QColor m_color;
    bool m_hasColor = false;
};

// Linha de estado ("Pensando…") com um brilho que percorre o texto enquanto algo está em andamento.
class ShimmerLabel : public QLabel {
    Q_OBJECT

public:
    explicit ShimmerLabel(QWidget *parent = nullptr);
    // Troca o texto; `shimmer` liga a animação (só enquanto há texto e o widget está visível).
    void setStatus(const QString &text, bool shimmer = false);
    void setIconName(const QString &name);
    bool shimmering() const { return m_shimmer; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void syncTimer();

    QTimer *m_timer = nullptr;
    qreal m_phase = 0.0;
    bool m_shimmer = false;
    QString m_iconName;
};

// Texto do chat com altura própria e quebra de linha em qualquer ponto: um bloco de código largo ou uma
// palavra gigante quebram em vez de empurrar a conversa para fora da janela.
class ChatTextView : public QTextBrowser {
    Q_OBJECT

public:
    explicit ChatTextView(QWidget *parent = nullptr);

    void setMarkdownText(const QString &markdown);
    void setPlainMessage(const QString &text);
    // O balão do usuário encolhe até o texto; o do assistente ocupa a largura toda.
    void setShrinkWrap(bool shrink);
    QString sourceText() const { return m_source; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateHeight();
    void restyleBlocks();

    QString m_source;
    bool m_shrink = false;
    bool m_updating = false;
    int m_naturalWidth = 0;
};

// Balão da mensagem do usuário. O canto é desenhado à mão: o raio nunca passa da metade da altura (o QSS
// desiste e deixa o canto QUADRADO quando passa, o que acontecia com mensagens de uma linha).
class ChatBubble : public QFrame {
    Q_OBJECT

public:
    explicit ChatBubble(const QString &text, QWidget *parent = nullptr);
    ChatTextView *view() const { return m_view; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ChatTextView *m_view;
};

// Linha do usuário: o balão fica à direita e ocupa no máximo ~80% da largura.
class UserRow : public QWidget {
    Q_OBJECT

public:
    explicit UserRow(const QString &text, QWidget *parent = nullptr);
    ChatBubble *bubble() const { return m_bubble; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    ChatBubble *m_bubble;
};

// Resposta do assistente: texto sem balão e, depois de pronta, o botão de copiar.
class AssistantBlock : public QWidget {
    Q_OBJECT

public:
    explicit AssistantBlock(QWidget *parent = nullptr);
    ChatTextView *view() const { return m_view; }
    void setText(const QString &markdown);
    QString text() const { return m_view->sourceText(); }
    void setFinal(bool final);

private:
    ChatTextView *m_view;
    QToolButton *m_copy;
    QTimer *m_copiedTimer;
};

// Item da lista de conversas: o selecionado ganha fundo, contorno e barra de destaque; os cabeçalhos de
// grupo ("Hoje", "Ontem"…) são só texto.
class ConversationDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum Role { IdRole = Qt::UserRole, SubtitleRole = Qt::UserRole + 1, HeaderRole = Qt::UserRole + 2 };

    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace kai::ui
