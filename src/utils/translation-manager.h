#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>

namespace kai::utils {

// Sistema de language pack (i18n) baseado em JSON, no mesmo espírito dos
// temas do Kai (arquivos editáveis em assets/, carregados em runtime).
//
// Cada idioma é um arquivo assets/i18n/<code>.json com um objeto plano
// de "chave": "texto traduzido" (ex: "menu.file": "Arquivo"). O idioma
// padrão é o inglês (en) — é a fonte da verdade das chaves. A resolução
// de um texto segue a cadeia de fallback:
//   1. idioma ativo (ex: pt)
//   2. idioma padrão (en)
//   3. a própria chave (nunca retorna vazio; deixa o texto "cru" visível,
//      facilitando detectar chaves faltantes durante o desenvolvimento).
//
// TranslationManager é um singleton de processo, acessado via instance().
// A função livre kai::utils::tr(key) é um atalho ergonômico para
// TranslationManager::instance().translate(key), usada em toda a UI.
//
// Nunca crasha: arquivos ausentes ou JSON inválido apenas mantêm o
// dicionário anterior e logam um warning (mesma política de resiliência
// dos temas / spec 07).
class TranslationManager : public QObject {
    Q_OBJECT

public:
    static TranslationManager &instance();

    // Diretório onde os language packs residem, resolvido relativo ao
    // executável instalado ou ao diretório de trabalho (mesma estratégia
    // de resolução de path de assets/themes).
    static QString packsDirPath();

    // Idiomas disponíveis (códigos, ex: {"en", "pt"}), descobertos a
    // partir dos arquivos .json presentes no diretório de packs. Sempre
    // inclui "en" mesmo que o arquivo não exista (idioma padrão embutido).
    QStringList availableLanguages() const;

    // Nome legível de um código de idioma para exibição no seletor
    // (ex: "en" -> "English", "pt" -> "Português"). Códigos desconhecidos
    // retornam o próprio código.
    static QString displayName(const QString &code);

    // Código do idioma ativo (ex: "pt"). Padrão: "en".
    QString currentLanguage() const;

    // Carrega o language pack do idioma indicado (ex: "pt"). Também
    // (re)carrega o pack padrão (en) como base de fallback. Retorna false
    // se o idioma pedido não pôde ser carregado (mantém o anterior).
    // Emite languageChanged() em caso de sucesso.
    bool loadLanguage(const QString &code);

    // Resolve uma chave de tradução para o texto do idioma ativo, com
    // fallback en -> própria chave (ver descrição da classe).
    QString translate(const QString &key) const;

signals:
    // Emitido quando o idioma ativo muda com sucesso, permitindo que a UI
    // se retraduza/reconstrua se desejar (a aplicação inicial ocorre antes
    // de a UI ser construída, então nem todos os consumidores precisam
    // reagir a este sinal).
    void languageChanged(const QString &code);

private:
    explicit TranslationManager(QObject *parent = nullptr);
    Q_DISABLE_COPY(TranslationManager)

    // Lê um arquivo <code>.json do diretório de packs para um QHash plano.
    // Retorna false (e hash vazio) se o arquivo não existir ou for inválido.
    static bool readPack(const QString &code, QHash<QString, QString> &out);

    QString m_currentLanguage = QStringLiteral("en");
    QHash<QString, QString> m_active;   // idioma ativo
    QHash<QString, QString> m_fallback; // idioma padrão (en)
};

// Atalho ergonômico usado em toda a UI: kai::utils::tr("menu.file").
QString tr(const QString &key);

} // namespace kai::utils
