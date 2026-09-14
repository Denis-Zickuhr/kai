#pragma once

#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QStyledItemDelegate>

namespace kai::core {
struct Folder;
}

namespace kai::ui {

// Delegate que desenha o caminho de cada pasta como uma sequência de
// pills/chips coloridas (uma por segmento de hierarquia) em vez de texto
// plano "A › B › C" — feature "Seletor de pastas KAI V2" (spec de
// Autosync, §1.7): cor de fundo = tom do accent, mais claro quanto mais
// profundo o segmento; raio sempre via utils::tokens::radiusSm() (nunca
// hardcoded). Os segmentos de cada item vêm de Qt::UserRole+1 (QStringList),
// preenchidos por FolderPickerWidget::populateItems.
class FolderPickerPillDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit FolderPickerPillDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
              const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// Widget de seleção de pastas com busca e visual em pills coloridas por
// profundidade (Etapa 6 / Seletor de pastas KAI V2). Reaproveita a mesma
// API mínima do QComboBox anterior (selectedFolderId/setSelectedFolderId/
// selectionChanged), pra minimizar o diff nos callers já existentes
// (folder-editor-dialog, collection-editor-dialog, command-editor-dialog,
// export-dialog, project-import-options-dialog, search-directory-row-dialog).
class FolderPickerWidget : public QComboBox {
    Q_OBJECT

public:
    explicit FolderPickerWidget(QWidget *parent = nullptr);

    // Configura a hierarquia de pastas disponíveis.
    void setFolders(const QVector<kai::core::Folder> &folders);

    // Retorna o id da pasta selecionada.
    QString selectedFolderId() const;

    // Define a pasta selecionada pelo id.
    void setSelectedFolderId(const QString &folderId);

    // Adiciona uma opção "None" (pasta raiz/sem pasta) no topo.
    void enableNoneOption(const QString &noneText = QString());

signals:
    // Emitido quando a seleção muda.
    void selectionChanged(const QString &folderId);

private:
    void populateItems();
    // Segmentos de nome (sem separador) do caminho até `folderId` — usado
    // tanto para desenhar as pills (delegate) quanto para o texto de busca
    // (join com espaço, pra o QCompleter casar qualquer segmento do
    // caminho, não só o nome final).
    QStringList pathSegments(const QString &folderId) const;

    QVector<kai::core::Folder> m_folders;
    QMap<int, QString> m_indexToFolderId;
    bool m_noneOptionEnabled = false;
};

} // namespace kai::ui
