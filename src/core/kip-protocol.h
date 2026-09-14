#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <variant>

namespace kai::core {

// KIP — Kai Interface Protocol (specs/11-kip-protocol.md). Este módulo é o
// contrato puro: structs das mensagens, parser de uma linha JSON, serializers
// das mensagens Kai -> programa e o reassembler de linhas. Zero dependência
// de widget/processo.

inline constexpr int kKipProtocolVersion = 1;

// Limites do §5.3 (acima disso: trunca e registra um diagnóstico).
inline constexpr int kKipMaxFieldsPerPrompt = 100;
inline constexpr int kKipMaxOptionsPerField = 5000;
inline constexpr int kKipMaxTableRows = 1000;
inline constexpr int kKipMaxStepItems = 200;
inline constexpr int kKipMaxPageSize = 200;
inline constexpr int kKipMaxChips = 24;
inline constexpr int kKipMaxTextBytes = 64 * 1024;
inline constexpr int kKipMaxLineBytes = 1024 * 1024;

// Máscara usada no lugar de valores secretos (resumo, inspetor).
inline constexpr char16_t kKipSecretMask[] = u"••••";

enum class KipFieldType {
    Text,
    Secret,
    Textarea,
    Number,
    Date,
    Select,
    List,
    Table,
    Filepick,
    Folderpick,
    Flags
};

QString kipFieldTypeToString(KipFieldType type);
// `known` (opcional) diz se o texto era um tipo conhecido; desconhecido cai em Text.
KipFieldType kipFieldTypeFromString(const QString &value, bool *known = nullptr);

struct KipOption {
    QString value;
    QString label;
    QString description;
};

struct KipFlagOption {
    QString name;
    QString label;
    QString description;
    bool defaultValue = false;
};

struct KipColumn {
    QString key;
    QString label;
};

struct KipField {
    QString name;
    KipFieldType type = KipFieldType::Text;
    QString label;
    QString description;
    bool required = false;
    QJsonValue defaultValue = QJsonValue(QJsonValue::Undefined); // Undefined = sem default
    QString placeholder;
    QString group;
    bool watch = false;
    std::optional<bool> remember; // nullopt = herda do prompt

    // number
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    int decimals = 0;

    // date
    QString dateMode = QStringLiteral("date"); // date | time | datetime
    bool range = false;

    // select / list
    QVector<KipOption> options;
    bool multiple = false; // list / table
    // list / table: campo de filtro (nullopt = automático, só acima de 8 itens) e
    // paginação (0 = sem paginação; N = N itens por página, sobre o resultado do filtro).
    std::optional<bool> searchable;
    int pageSize = 0;

    // table
    QVector<KipColumn> columns;
    QJsonArray rows;
    QString rowKey = QStringLiteral("id");

    // filepick / folderpick
    QString filter;
    QString initialDir;
    QString pathFormat = QStringLiteral("native");

    // flags
    QVector<KipFlagOption> flags;

    QJsonObject toJson() const;

private:
    void listViewToJson(QJsonObject &o) const;
};

enum class KipLevel { Info, Success, Warning, Error };
QString kipLevelToString(KipLevel level);
KipLevel kipLevelFromString(const QString &value, bool *known = nullptr);

enum class KipStepState { Pending, Running, Success, Error, Skipped };
QString kipStepStateToString(KipStepState state);
KipStepState kipStepStateFromString(const QString &value, bool *known = nullptr);

struct KipStepItem {
    QString id;
    QString label;
    KipStepState state = KipStepState::Pending;
    QString detail;
};

enum class KipActionType { OpenUrl, Reveal, Copy };

struct KipAction {
    KipActionType type = KipActionType::OpenUrl;
    QString label;
    QString url;        // open_url
    QString path;       // reveal
    QString pathFormat = QStringLiteral("native"); // reveal
    QString value;      // copy
};

// ---- chips: ações efêmeras de uma tela (§21) ----------------------------------
// Um chip é um botão pequeno ao lado dos campos de um prompt. Clicar NÃO envia a
// tela: o Kai manda `chip` ao programa, que responde com `chip_result` (rodando /
// sucesso / erro) e o resultado aparece numa caixa sob os chips, sem sair do passo.
struct KipChipConfirm {
    QString title;
    QString text;         // vazio = pergunta padrão do Kai
    QString confirmLabel; // vazio = "Run"
    QString cancelLabel;  // vazio = "Cancel"
};

struct KipChip {
    QString id;
    QString label;        // vazio = o id
    QString description;  // tooltip
    QString icon;         // nome de ícone Lucide do Kai (opcional)
    bool danger = false;  // visual destrutivo (e botão de confirmação vermelho)
    std::optional<KipChipConfirm> confirm; // pede confirmação antes de executar
    QStringList requiresFields; // campos que precisam estar preenchidos para habilitar o chip

    QJsonObject toJson() const;
};

enum class KipChipState { Running, Success, Error };
QString kipChipStateToString(KipChipState state);
KipChipState kipChipStateFromString(const QString &value, bool *known = nullptr);

// ---- Program -> Kai ---------------------------------------------------------

struct KipHello {
    int kipVersion = kKipProtocolVersion; // vem do envelope
    QString title;
    QString version;
};

struct KipPrompt {
    QString id;
    QString title;
    QString description;
    QString submitLabel;
    bool back = false;
    bool cancellable = true;
    std::optional<bool> remember;
    QVector<KipField> fields;
    QVector<KipChip> chips;
};

struct KipConfirm {
    QString id;
    QString title;
    QString text;
    bool danger = false;
    QString confirmLabel;
    QString cancelLabel;
    bool back = false;
    bool cancellable = true;
};

struct KipPatch {
    QString id;
    int seq = 0;
    QVector<KipField> fields;
    QStringList remove;
    std::optional<QVector<KipChip>> chips; // presente = substitui o conjunto de chips
    // Sem `seq` o patch é ESPONTÂNEO (não responde a um change — ex.: um chip repintando a tabela):
    // vale sempre e não encerra a espera de um change pendente. Com `seq` explícito, um valor
    // menor que o último change enviado é ignorado (stale).
    bool spontaneous = false;
};

struct KipInvalid {
    QString id;
    QMap<QString, QString> errors;
    QString message;
};

struct KipMessageBlock {
    KipLevel level = KipLevel::Info;
    QString text;
};

struct KipMarkdown {
    QString text;
};

struct KipProgress {
    std::optional<double> value; // nullopt = indeterminado
    QString label;
    std::optional<bool> cancellable;
};

struct KipSteps {
    QString id;
    QString title;
    QVector<KipStepItem> items;
};

struct KipStep {
    QString steps;
    QString id;
    KipStepState state = KipStepState::Pending;
    QString detail;
};

struct KipTable {
    QString id;
    QString title;
    QVector<KipColumn> columns;
    QJsonArray rows;
};

struct KipNotify {
    QString title;
    QString text;
    KipLevel level = KipLevel::Info;
};

struct KipSetEnv {
    QString name;
    QString value;
};

struct KipDone {
    QString title;
    QString text;
    KipLevel level = KipLevel::Success;
    QVector<KipAction> actions;
};

// Resposta do programa a um `chip`: atualiza a caixa de execução do chip.
struct KipChipResult {
    QString id;   // prompt (vazio = o prompt aberto)
    QString chip; // id do chip em execução
    KipChipState state = KipChipState::Running;
    QString title; // vazio = o rótulo do chip
    QString text;  // Markdown
};

using KipMessage = std::variant<KipHello, KipPrompt, KipConfirm, KipPatch, KipInvalid, KipMessageBlock,
                                KipMarkdown, KipProgress, KipSteps, KipStep, KipTable, KipNotify,
                                KipSetEnv, KipDone, KipChipResult>;

// Resultado de interpretar UMA linha de stdout.
struct KipParseResult {
    enum class Kind {
        NotProtocol, // não é JSON objeto, ou sem a chave "kip": vai pro log da sessão
        Message,     // mensagem válida em `message`
        Unknown,     // objeto com "kip" mas "type" desconhecido: ignorada (aviso)
        Invalid      // "kip" + tipo conhecido, mas malformada: pulada (aviso)
    };
    Kind kind = Kind::NotProtocol;
    std::optional<KipMessage> message;
    QString type;     // "type" do envelope quando a linha é do protocolo
    int version = 0;  // "kip" do envelope quando a linha é do protocolo
    // Avisos para o log (campo descartado, tipo desconhecido, truncamento...).
    QStringList diagnostics;

    bool isProtocolMessage() const { return kind != Kind::NotProtocol; }
};

KipParseResult parseKipLine(const QString &line);

// Nome do tipo (`"prompt"`, `"progress"`...) de uma mensagem já interpretada.
QString kipMessageTypeName(const KipMessage &message);

// Mensagem -> objeto JSON com o envelope {"kip":1,"type":...}. Fonte única do
// formato: o helper `kai kip` monta structs e serializa por aqui, e o parser
// lê de volta (ida e volta coberta por teste).
QJsonObject kipMessageToJson(const KipMessage &message);
// Uma linha compacta terminada em '\n'.
QByteArray kipSerializeMessage(const KipMessage &message);

// ---- Kai -> programa ---------------------------------------------------------
// Cada serializer devolve UMA linha JSON compacta terminada em '\n'.
QByteArray kipSerializeResponse(const QString &id, const QJsonObject &values);
QByteArray kipSerializeChange(const QString &id, int seq, const QString &field, const QJsonObject &values);
QByteArray kipSerializeChip(const QString &id, const QString &chip, const QJsonObject &values);
QByteArray kipSerializeBack(const QString &id);
QByteArray kipSerializeCancel();

// Cópia de `values` com os campos secretos (por nome) trocados pela máscara —
// usado em tudo que é exibido/registrado (inspetor, logs).
QJsonObject kipRedactValues(const QJsonObject &values, const QVector<KipField> &fields);

// ---- Valores de campo ---------------------------------------------------------
// Valor "vazio" enviado quando o usuário não respondeu: "" / [] / false por
// flag / null para number e date (§6).
QJsonValue kipEmptyValue(const KipField &field);
// Default declarado pelo programa, coagido ao formato do campo (ou o vazio).
QJsonValue kipInitialValue(const KipField &field);
// O valor ainda faz sentido para o campo (opção existe, dentro do intervalo,
// formato certo)? Usado ao aplicar patch e ao reaproveitar valor lembrado.
bool kipIsValueValid(const KipField &field, const QJsonValue &value);
// Texto curto para o resumo de passos respondidos (secret vira a máscara).
QString kipDisplayValue(const KipField &field, const QJsonValue &value);

// Valor de uma célula de tabela como texto (§11): string como veio, número sem
// zeros à toa, bool true/false, null vazio, array/objeto como JSON compacto.
QString kipCellText(const QJsonValue &value);

// http/https apenas (§12.1, §17).
bool kipIsSafeHttpUrl(const QString &url);

// ---- Reassembler de linhas -----------------------------------------------------
// Junta chunks de `outputReady` em linhas completas (um '\r' final é
// descartado). Linha maior que kKipMaxLineBytes é descartada e registrada.
class KipLineBuffer {
public:
    // Devolve as linhas completadas por `chunk`.
    QStringList feed(const QString &chunk);
    // Sobra sem '\n' (processo terminou sem quebra de linha final).
    QString takePending();
    // Diagnósticos acumulados desde a última chamada (ex.: linha descartada).
    QStringList takeDiagnostics();

private:
    QString m_pending;
    bool m_discarding = false;
    QStringList m_diagnostics;
};

} // namespace kai::core
