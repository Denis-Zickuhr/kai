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

// Carrega, resolve e aplica temas JSON (variáveis base + expressões ${var ± #hex} + blocos CSS por componente,
// traduzidos para QSS). Live reload via QFileSystemWatcher emite themeReloaded(). Erros de parsing nunca
// crasham: o tema anterior é mantido e um warning é logado.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);
    ~ThemeManager() override;

    QString themesDirPath() const;

    // Carrega um tema pelo nome (sem extensão) de ~/.config/kai/themes/. false se o arquivo não existir ou for inválido (mantém o atual).
    bool loadTheme(const QString &themeName);

    bool loadThemeFromFile(const QString &filePath);

    const ResolvedTheme &currentTheme() const;
    bool hasTheme() const;

    void setLiveReloadEnabled(bool enabled);

signals:
    // Emitido a cada (re)carga bem-sucedida, inclusive por live reload.
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
