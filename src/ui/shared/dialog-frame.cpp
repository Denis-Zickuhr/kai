#include "ui/shared/dialog-frame.h"

#include "ui/shared/native-window-corners.h"
#include "ui/shared/window-control-buttons.h"
#include "utils/design-tokens.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegion>
#include <QStyle>
#include <QToolButton>
#include <QWindow>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {
constexpr int kTitleBarHeight = 30;
// Faixa da borda (em px) onde o clique inicia o redimensionamento nativo.
constexpr int kResizeMargin = 6;
constexpr const char *kFramedProperty = "kaiFramed";
constexpr const char *kBarName = "kaiDialogTitleBar";

// A barra de título: arrastar move o diálogo.
class DialogTitleBar : public QWidget {
public:
    explicit DialogTitleBar(QDialog *dialog)
        : QWidget(dialog)
    {
        setObjectName(QLatin1String(kBarName));
        setAttribute(Qt::WA_StyledBackground, true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (QWindow *handle = window()->windowHandle()) {
                handle->startSystemMove();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }
};

// Contorno do diálogo, por cima de tudo: uma borda de 1px mais contrastante que a
// do tema e uma sombra INTERNA suave. Um diálogo escuro sobre a janela escura se
// confundia com ela; sem translucidez não há sombra por fora, então o destaque
// vem de dentro. Só as faixas das bordas são pintadas; não captura o mouse.
class DialogEdge : public QWidget {
public:
    explicit DialogEdge(QDialog *dialog)
        : QWidget(dialog)
    {
        setObjectName(QStringLiteral("kaiDialogEdge"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const int radius = tk::radiusMd();
        const QColor bg(tk::bg());
        const bool dark = bg.lightnessF() < 0.5;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);

        // Sombra interna: faixas de 1px do canto para dentro, a opacidade cai rápido.
        constexpr int kDepth = 6;
        const int peak = dark ? 90 : 45;
        for (int i = 1; i <= kDepth; ++i) {
            const double fade = 1.0 - double(i - 1) / kDepth;
            painter.setPen(QPen(QColor(0, 0, 0, int(peak * fade * fade)), 1));
            const QRectF r = QRectF(rect()).adjusted(i + 0.5, i + 0.5, -i - 0.5, -i - 0.5);
            painter.drawRoundedRect(r, qMax(0, radius - i), qMax(0, radius - i));
        }

        // Borda externa: a cor de borda do tema puxada para o texto, para se destacar.
        QColor line(tk::borderColor());
        const QColor fg(tk::mutedFg());
        line.setRgb((line.red() * 2 + fg.red()) / 3, (line.green() * 2 + fg.green()) / 3,
                    (line.blue() * 2 + fg.blue()) / 3);
        painter.setPen(QPen(line, 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
};

// Controlador de UM diálogo (filho dele: morre junto).
class DialogFrame : public QObject {
public:
    // Fase 1 (no polish, antes do primeiro show): o diálogo fica sem moldura do
    // sistema. A janela nativa já foi criada (o Qt 6 a cria antes do polish);
    // mudar o hint a recria, ainda sem ter sido mapeada.
    explicit DialogFrame(QDialog *dialog)
        : QObject(dialog)
        , m_dialog(dialog)
    {
        // Os hints são explícitos: o FramelessWindowHint faz o Qt tratar os flags
        // como "customizados" e não aplicar os padrões (ver AppWindowFrame).
        dialog->setWindowFlags(dialog->windowFlags() | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint
                               | Qt::WindowCloseButtonHint);
        dialog->setAttribute(Qt::WA_StyledBackground, true);
        // NUNCA translúcido: o atributo ligado depois de a janela nativa existir (e o
        // WSLg não honra janelas com alfa) deixa o fundo com lixo gráfico. Os cantos
        // são só a máscara de região; a separação do fundo vem da borda/sombra
        // interna desenhadas por DialogEdge.
        dialog->installEventFilter(this);
    }

    ~DialogFrame() override { clearResizeCursor(); }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_dialog) {
            switch (event->type()) {
            case QEvent::WindowTitleChange:
                if (m_title) {
                    syncTitle();
                }
                break;
            case QEvent::Resize:
                if (m_bar) {
                    layoutBar();
                }
                updateShape();
                break;
            case QEvent::Show:
                complete();
                layoutBar();
                updateShape();
                if (m_resizable && m_completed) {
                    qApp->installEventFilter(this);
                }
                break;
            case QEvent::Hide:
                qApp->removeEventFilter(this);
                clearResizeCursor();
                break;
            default:
                break;
            }
            return false;
        }
        return handleResizeEdges(watched, event);
    }

private:
    // Fase 2 (primeiro show, quando o diálogo já montou o layout e o tamanho):
    // a barra ocupa o topo e o conteúdo desce a altura dela.
    void complete()
    {
        if (m_completed) {
            return;
        }
        m_completed = true;
        if (QLayout *layout = m_dialog->layout()) {
            QMargins margins = layout->contentsMargins();
            margins.setTop(margins.top() + kTitleBarHeight);
            layout->setContentsMargins(margins);
        }
        // O tamanho que o diálogo definiu (adjustSize/resize/fixo) valia só
        // para o conteúdo: cresce a altura da barra.
        const bool isFixed = m_dialog->minimumSize() == m_dialog->maximumSize();
        if (isFixed) {
            const QSize fixed = m_dialog->minimumSize();
            m_dialog->setFixedSize(fixed.width(), fixed.height() + kTitleBarHeight);
        } else {
            m_dialog->resize(m_dialog->width(), m_dialog->height() + kTitleBarHeight);
        }
        m_resizable = !isFixed;
        buildBar();
        m_bar->show(); // criados depois do showChildren do diálogo: não aparecem sozinhos
        m_edge->show();
        syncTitle();
        if (m_dialog->layout()) {
            m_dialog->layout()->activate();
        }
    }

    void buildBar()
    {
        m_bar = new DialogTitleBar(m_dialog);
        auto *row = new QHBoxLayout(m_bar);
        row->setContentsMargins(8, 2, 6, 0);
        row->setSpacing(6);
        auto *logo = new QLabel(m_bar);
        const QPixmap pixmap = loadAppLogoPixmap();
        if (!pixmap.isNull()) {
            logo->setPixmap(pixmap.scaledToHeight(18, Qt::SmoothTransformation));
        } else {
            logo->setText(QStringLiteral("Kai"));
            logo->setStyleSheet(QStringLiteral("font-weight: bold;"));
        }
        m_title = new QLabel(m_bar);
        m_title->setObjectName(QStringLiteral("kaiDialogTitle"));
        m_title->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
        auto *close = makeWindowControlButton(m_bar, WindowControl::Close);
        QObject::connect(close, &QToolButton::clicked, m_dialog, &QDialog::reject);
        row->addWidget(logo);
        row->addWidget(m_title);
        row->addStretch(1);
        row->addWidget(close, 0, Qt::AlignVCenter);
        m_edge = new DialogEdge(m_dialog);
        layoutBar();
    }

    void layoutBar()
    {
        // Pela borda de 1px do QSS: a barra fica por dentro dela.
        m_bar->setGeometry(1, 1, qMax(0, m_dialog->width() - 2), kTitleBarHeight);
        m_bar->raise();
        m_edge->setGeometry(m_dialog->rect());
        m_edge->raise(); // por cima de tudo (o contorno não cobre cliques)
    }

    void syncTitle() { m_title->setText(m_dialog->windowTitle()); }

    // Mesma regra da janela principal (MainWindow::updateWindowShape): raio MÉDIO
    // do token; no Windows 11 os cantos vêm do DWM; senão o QSS pinta o arco e uma
    // máscara de região recorta onde a transparência da janela não é honrada.
    void updateShape()
    {
        const int radius = tk::radiusMd();
#if defined(Q_OS_WIN)
        const int cornerStyle = tk::effects().cornerStyle;
        if (m_dialog->isVisible() && m_nativeCornerStyle != cornerStyle) {
            m_nativeCorners = applyNativeWindowCorners(m_dialog, cornerStyle);
            m_nativeCornerStyle = cornerStyle;
        }
#endif
        if (m_dialog->property("kaiFlat").toBool() != m_nativeCorners) {
            m_dialog->setProperty("kaiFlat", m_nativeCorners);
            m_dialog->style()->unpolish(m_dialog);
            m_dialog->style()->polish(m_dialog);
        }
        if (m_nativeCorners || radius <= 0) {
            m_dialog->clearMask();
            return;
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(m_dialog->rect()), radius, radius);
        m_dialog->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    Qt::Edges edgesAt(const QPoint &pos) const
    {
        Qt::Edges edges;
        if (pos.x() < 0 || pos.x() >= m_dialog->width() || pos.y() < 0 || pos.y() >= m_dialog->height()) {
            return edges;
        }
        if (pos.x() <= kResizeMargin) edges |= Qt::LeftEdge;
        if (pos.x() >= m_dialog->width() - kResizeMargin) edges |= Qt::RightEdge;
        if (pos.y() <= kResizeMargin) edges |= Qt::TopEdge;
        if (pos.y() >= m_dialog->height() - kResizeMargin) edges |= Qt::BottomEdge;
        return edges;
    }

    void clearResizeCursor()
    {
        if (m_resizeCursorActive) {
            QApplication::restoreOverrideCursor();
            m_resizeCursorActive = false;
        }
    }

    // Filtro global: os filhos cobrem as bordas, então só vemos o clique aqui.
    bool handleResizeEdges(QObject *watched, QEvent *event)
    {
        if (!m_dialog->isActiveWindow()
            || (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonPress)) {
            return false;
        }
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget->window() != m_dialog) {
            return false;
        }
        auto *mouse = static_cast<QMouseEvent *>(event);
        const Qt::Edges edges = edgesAt(m_dialog->mapFromGlobal(mouse->globalPosition().toPoint()));
        if (edges == Qt::Edges()) {
            if (event->type() == QEvent::MouseMove) {
                clearResizeCursor();
            }
            return false;
        }
        if (event->type() == QEvent::MouseMove && mouse->buttons() == Qt::NoButton) {
            Qt::CursorShape shape = Qt::SizeVerCursor;
            if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
                shape = Qt::SizeFDiagCursor;
            } else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
                shape = Qt::SizeBDiagCursor;
            } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
                shape = Qt::SizeHorCursor;
            }
            clearResizeCursor();
            QApplication::setOverrideCursor(shape);
            m_resizeCursorActive = true;
        } else if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            if (QWindow *handle = m_dialog->windowHandle()) {
                clearResizeCursor();
                handle->startSystemResize(edges);
                return true;
            }
        }
        return false;
    }

    QDialog *m_dialog;
    bool m_resizable = true;
    bool m_completed = false;
    QWidget *m_bar = nullptr;
    QWidget *m_edge = nullptr;
    QLabel *m_title = nullptr;
    bool m_nativeCorners = false;
    int m_nativeCornerStyle = -1;
    bool m_resizeCursorActive = false;
};

// Filtro de aplicação: todo QDialog novo é vestido no primeiro polish.
class DialogFrameInstaller : public QObject {
public:
    using QObject::QObject;

    // No primeiro show o Qt 6 cria a janela nativa e SÓ DEPOIS faz o polish; o
    // Polish é o primeiro momento em que o diálogo está inteiro (aplicar flags no
    // meio da construção derruba o QMessageBox, que ainda não montou o ícone) e
    // a janela ainda não foi mapeada — recriá-la com o novo hint não pisca.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish && watched->isWidgetType()) {
            if (auto *dialog = qobject_cast<QDialog *>(watched)) {
                applyDialogFrame(dialog);
            }
        }
        return false;
    }
};
} // namespace

bool hasDialogFrame(const QDialog *dialog)
{
    return dialog && dialog->property(kFramedProperty).toBool();
}

void applyDialogFrame(QDialog *dialog)
{
    if (!dialog || hasDialogFrame(dialog) || !dialog->isWindow()
        || (dialog->windowFlags() & Qt::WindowType_Mask) != Qt::Dialog
        || dialog->windowFlags().testFlag(Qt::FramelessWindowHint) || dialog->isVisible()) {
        return;
    }
    // A propriedade vem ANTES do DialogFrame (marca "já tratado" antes de qualquer
    // evento que a criação dele possa gerar).
    dialog->setProperty(kFramedProperty, true);
    new DialogFrame(dialog);
}

void installDialogFrames()
{
    static QPointer<DialogFrameInstaller> installer;
    if (installer || !qApp) {
        return;
    }
    installer = new DialogFrameInstaller(qApp);
    qApp->installEventFilter(installer);
}

} // namespace kai::ui
