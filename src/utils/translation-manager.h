#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>

namespace kai::utils {

// Language packs (i18n) em JSON, como os temas (assets/i18n/<code>.json, objeto plano "chave": "texto", carregados
// em runtime). O padrão é o inglês (en), fonte da verdade das chaves. Resolução: idioma ativo, depois en, depois
// a própria chave (nunca vazio; deixa o texto cru visível para achar chaves faltantes). Singleton de processo
// (instance()); kai::utils::tr(key) é o atalho usado em toda a UI. Nunca crasha: arquivo ausente ou JSON
// inválido mantém o dicionário anterior e loga um warning.
class TranslationManager : public QObject {
    Q_OBJECT

public:
    static TranslationManager &instance();

    // Diretório dos packs, resolvido relativo ao executável instalado ou ao diretório de trabalho (como assets/themes).
    static QString packsDirPath();

    // Códigos descobertos dos .json no diretório de packs; sempre inclui "en", mesmo sem o arquivo.
    QStringList availableLanguages() const;

    // Nome legível para o seletor (ex.: "pt" -> "Português"); código desconhecido volta como está.
    static QString displayName(const QString &code);

    QString currentLanguage() const;

    // Carrega o pack de `code` e (re)carrega o en como fallback. false se o idioma pedido não carregou (mantém o anterior); emite languageChanged() em sucesso.
    bool loadLanguage(const QString &code);

    QString translate(const QString &key) const;

signals:
    // Emitido quando o idioma ativo muda (a aplicação inicial ocorre antes da UI, então nem todo consumidor precisa reagir).
    void languageChanged(const QString &code);

private:
    explicit TranslationManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(TranslationManager)

    // Lê <code>.json do diretório de packs para um QHash plano; false (e hash vazio) se ausente ou inválido.
    static bool readPack(const QString &code, QHash<QString, QString> &out);

    QString m_currentLanguage = QStringLiteral("en");
    QHash<QString, QString> m_active;   // idioma ativo
    QHash<QString, QString> m_fallback; // idioma padrão (en)
};

// Atalho ergonômico usado em toda a UI: kai::utils::tr("menu.file").
QString tr(const QString &key);

} // namespace kai::utils
