#include "ui/shared/help-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/fuzzy-search.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QScrollBar>

#include "utils/translation-manager.h"

namespace kai::ui {

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("help.title")));
    setSizeGripEnabled(true);
    resize(900, 620);
    buildTopics();
    setupUi();
    reloadList();
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    centerOnParent(this);
}

void HelpDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Coluna esquerda: busca fuzzy + lista de tópicos.
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(utils::tr(QStringLiteral("help.search.placeholder")));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &HelpDialog::handleSearchChanged);
    leftLayout->addWidget(m_search);

    m_list = new QListWidget(left);
    m_list->setMinimumWidth(240);
    connect(m_list, &QListWidget::currentRowChanged, this, &HelpDialog::handleTopicSelected);
    leftLayout->addWidget(m_list, 1);

    // Coluna direita: conteúdo do tópico.
    m_browser = new QTextBrowser(splitter);
    m_browser->setOpenExternalLinks(true);
    // Links internos help://<topicId> navegam entre tópicos.
    m_browser->setOpenLinks(false);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QStringLiteral("help")) {
            showTopic(url.host().isEmpty() ? url.path().mid(1) : url.host());
        } else {
            m_browser->setSource(url); // deixa links externos abrirem
        }
    });

    splitter->addWidget(left);
    splitter->addWidget(m_browser);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 640});
    outer->addWidget(splitter, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(buttonBox);
}

void HelpDialog::handleSearchChanged(const QString &query)
{
    reloadList(query);
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void HelpDialog::handleTopicSelected(int row)
{
    if (row < 0 || row >= m_visibleIds.size()) {
        return;
    }
    showTopic(m_visibleIds.at(row));
}

void HelpDialog::reloadList(const QString &query)
{
    m_list->clear();
    m_visibleIds.clear();

    if (query.trimmed().isEmpty()) {
        // Sem busca: mostra todos os tópicos na ordem de definição.
        for (const HelpTopic &t : m_topics) {
            m_list->addItem(t.title);
            m_visibleIds.append(t.id);
        }
        return;
    }

    // Busca fuzzy sobre "título + keywords"; mantém a ordem por relevância.
    QStringList haystack;
    for (const HelpTopic &t : m_topics) {
        haystack << (t.title + QLatin1Char(' ') + t.keywords);
    }
    const auto results = FuzzyMatcher::search(query, haystack);
    for (const auto &r : results) {
        if (r.originalIndex >= 0 && r.originalIndex < m_topics.size()) {
            m_list->addItem(m_topics.at(r.originalIndex).title);
            m_visibleIds.append(m_topics.at(r.originalIndex).id);
        }
    }
}

void HelpDialog::showTopic(const QString &topicId)
{
    for (int i = 0; i < m_topics.size(); ++i) {
        if (m_topics.at(i).id == topicId) {
            m_browser->setHtml(m_topics.at(i).html);
            m_browser->verticalScrollBar()->setValue(0);
            // Sincroniza a seleção da lista se o tópico estiver visível.
            const int visIdx = m_visibleIds.indexOf(topicId);
            if (visIdx >= 0 && m_list->currentRow() != visIdx) {
                m_list->setCurrentRow(visIdx);
            }
            return;
        }
    }
}

} // namespace kai::ui
