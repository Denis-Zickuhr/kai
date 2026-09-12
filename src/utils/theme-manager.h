#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <memory>

class QFileSystemWatcher;

namespace kai::utils {

// Representa um tema totalmente resolvido: variáveis expandidas e QSS final
// pronto para ser aplicado via QApplication::setStyleSheet.
struct ResolvedTheme {
    QString name;
    QMap<QString, QString> variables;
    QString qss;
};

// Carrega, resolve e aplica temas no formato JSON descrito na
// (inspirado no modelo do CopyQ): variáveis base + expressões de cor
// ${var ± #hex} + blocos de CSS por componente, traduzidos para QSS.
//
// Suporta live reload via QFileSystemWatcher: qualquer alteração no arquivo
// do tema ativo é reprocessada automaticamente, emitindo themeReloaded().
// Erros de parsing nunca crasham a aplicação — o tema anterior é mantido e
// um warning é logado (mesma política de resiliência).
class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);
    ~ThemeManager() override;

    // Diretório onde os temas do usuário residem: ~/.config/kai/themes/.
    QString themesDirPath() const;

    // Carrega um tema pelo nome (sem extensão), ex: "dracula" ->
    // ~/.config/kai/themes/dracula.json. Retorna false se o arquivo não
    // existir ou for inválido (mantém o tema atual, se houver).
    bool loadTheme(const QString &themeName);

    // Carrega um tema a partir de um caminho de arquivo absoluto.
    bool loadThemeFromFile(const QString &filePath);

    const ResolvedTheme &currentTheme() const;
    bool hasTheme() const;

    // Habilita/desabilita o watcher de live reload do arquivo atualmente
    // carregado.
    void setLiveReloadEnabled(bool enabled);

signals:
    // Emitido sempre que um tema é (re)carregado com sucesso, seja pela
    // primeira carga ou por live reload.
    void themeReloaded(const ResolvedTheme &theme);
    void themeLoadFailed(const QString &filePath, const QString &errorMessage);

private slots:
    void handleFileChanged(const QString &path);

private:
    // Resolve recursivamente expressões ${var} e ${var ± #hex} dentro de
    // uma string, usando o mapa de variáveis já processado.
    static QString resolveExpressions(const QString &input, const QMap<QString, QString> &variables);

    // Aplica uma operação de aritmética de cor: "#rrggbb" +/- "#rrggbb",
    // com clamp por canal em [0x00, 0xff].
    static QString applyColorOp(const QString &baseColor, QChar op, const QString &hexOperand);

    static bool looksLikeHexColor(const QString &value);

    QString buildQss(const QMap<QString, QString> &variables, const QMap<QString, QString> &components) const;

    std::unique_ptr<QFileSystemWatcher> m_watcher;
    ResolvedTheme m_currentTheme;
    QString m_currentFilePath;
    bool m_hasTheme = false;
    bool m_liveReloadEnabled = true;
};

} // namespace kai::utils
