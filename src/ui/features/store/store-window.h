#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QPixmap>
#include <QVector>
#include <QDialog>

#include "core/models.h"
#include "core/store-local.h"
#include "core/store-model.h"
#include "core/store-preview.h"
#include "engine/store-client.h"
#include "engine/store-installer.h"

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QCheckBox;
class QTextBrowser;
class QKeyEvent;
class QResizeEvent;
class QVBoxLayout;

namespace kai::ui {

class FlowLayout;

// Janela da Kai Store (File → Store…): provedor, categorias, busca, grade de cards, detalhe e instalação. Só contata a
// rede ao abrir, ao atualizar e ao instalar. A importação em si é do app (Host::install): esta janela baixa, verifica,
// mostra a prévia e monta o pedido.
class StoreWindow : public QDialog {
    Q_OBJECT

public:
    struct Host {
        std::function<bool(const core::StoreInstallRequest &, QString &)> install;
        std::function<bool(const QString &folderId, QString &)> uninstall;
        std::function<core::CommandsData()> commands;
        QString configDir;
    };

    StoreWindow(const QVector<core::StoreProviderConfig> &providers, const Host &host, QWidget *parent = nullptr);
    ~StoreWindow() override;

    // Recarrega o catálogo do provedor atual (Atualizar).
    void reload();
    // Painel do quadro (Exibir → Store): sem moldura de diálogo, e Esc/fechar não o escondem.
    void setEmbedded(bool embedded);
    // Os provedores mudaram nas configurações: refaz o seletor e abre o primeiro.
    void setProviders(const QVector<core::StoreProviderConfig> &providers);

protected:
    void done(int result) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct InstalledInfo {
        bool installed = false;
        QString version;
        QString folderId;
        bool update = false;
        QString updateVersion;
    };

    void setupUi();
    // Telas pequenas: filtros numa segunda linha, categorias num seletor e lista/detalhe um de cada vez (com "Voltar").
    void applyResponsive();
    void selectProvider(int index);
    void rebuildCategories();
    void rebuildGrid();
    QWidget *buildCard(const core::StoreIndexEntry &entry);
    void showDetail(const QString &packageId);
    void showGrid();
    void applySelection();
    void showPlaceholder();
    InstalledInfo installedInfo(const core::StoreIndexEntry &entry) const;
    bool matchesFilter(const core::StoreIndexEntry &entry) const;
    QString categoryName(const QString &id) const;
    void setStatus(const QString &text);
    void fetchIcon(const core::StoreIndexEntry &entry, QLabel *target);

    void beginInstall(const core::StoreIndexEntry &entry, const core::StoreVersionEntry &version,
                      const QString &replaceFolderId);
    void finishInstall(const engine::StoreStagedPackage &staged, const QString &replaceFolderId);
    void confirmUninstall(const core::StoreIndexEntry &entry, const QString &folderId);
    void cancelInstall();

    // Aba "Instalados": gestão (detalhes, desinstalar, remover da lista) de tudo o que a Store instalou.
    void showManage();
    void rebuildManage();
    void recordsChanged();
    void removeRecords(const QVector<core::StoreInstalledRecord> &records);

    QVector<core::StoreProviderConfig> m_providers;
    Host m_host;
    engine::StoreClient *m_client = nullptr;
    std::unique_ptr<engine::StoreInstaller> m_installer;
    engine::StoreCatalogResult m_result;
    QVector<core::StoreInstalledRecord> m_records;
    QString m_category; // vazio = todas
    QString m_detailId;
    int m_providerIndex = -1;
    bool m_busy = false;
    QHash<QString, QPixmap> m_icons;
    bool m_embedded = false;
    bool m_compact = false;

    QComboBox *m_providerCombo = nullptr;
    QComboBox *m_kindFilter = nullptr;
    QComboBox *m_statusFilter = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QHBoxLayout *m_topRow = nullptr;
    QWidget *m_filterRow = nullptr;
    QHBoxLayout *m_filterLayout = nullptr;
    QWidget *m_gridCard = nullptr;
    QWidget *m_detailCard = nullptr;
    QPushButton *m_backButton = nullptr;
    QTabBar *m_tabs = nullptr;
    QLabel *m_banner = nullptr;
    QWidget *m_manageHost = nullptr;
    QVBoxLayout *m_manageLayout = nullptr;
    QPushButton *m_uninstallSelected = nullptr;
    QCheckBox *m_selectAll = nullptr;
    QVector<QPair<QCheckBox *, core::StoreInstalledRecord>> m_manageChecks;
    QLineEdit *m_search = nullptr;
    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_gridHost = nullptr;
    QVBoxLayout *m_gridLayout = nullptr;
    QWidget *m_detailHost = nullptr;
    QVBoxLayout *m_detailLayout = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace kai::ui
