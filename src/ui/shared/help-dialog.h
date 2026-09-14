#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QListWidget;
class QTextBrowser;
class QLineEdit;
class QLabel;

namespace kai::ui {

// Um tópico da ajuda: id estável, título exibido, palavras-chave para a
// busca fuzzy e o corpo em HTML (com exemplos). Conteúdo estático.
struct HelpTopic {
    QString id;
    QString group;    // chave do grupo (cabeçalho da lista): "start", "commands"...
    QString title;
    QString keywords; // termos extras para a busca (sinônimos, comandos)
    QString html;
};

// Um grupo da lista lateral: o cabeçalho (help.group.<key>) e os tópicos HTML
// dele, na ordem de exibição. A ordem dos grupos E dos ids é a ordem da ajuda.
struct HelpTopicGroup {
    QString key;
    QStringList ids;
};
const QVector<HelpTopicGroup> &helpTopicGroups();
// Todos os ids dos tópicos HTML (assets/help/<idioma>/<id>.html), na ordem de exibição.
QStringList helpTopicIds();

// Help Page v2 (feedback do usuário): navegação por tópicos (um por
// função) com barra de busca FUZZY à esquerda e o conteúdo renderizado à
// direita, com seções e exemplos. Substitui o modal simples anterior.
class HelpDialog : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget *parent = nullptr);
    // Para os testes.
    int currentTopicRow() const;

private slots:
    void handleSearchChanged(const QString &query);
    void handleTopicSelected(int row);

private:
    void setupUi();
    void buildTopics();
    void reloadList(const QString &query = QString());
    void selectFirstTopic();
    void showTopic(const QString &topicId);

    QVector<HelpTopic> m_topics;
    // Ordem atual exibida na lista (ids), para mapear a seleção.
    QVector<QString> m_visibleIds;

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QTextBrowser *m_browser = nullptr;
    QLabel *m_emptyLabel = nullptr;
};

} // namespace kai::ui
