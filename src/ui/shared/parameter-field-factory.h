#pragma once

#include "core/kip-protocol.h"

#include <QDateTime>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace kai::ui {

class InlineCodeField;

// ============================================================================
// FÁBRICA DE CAMPOS (spec 11 §18)
// ----------------------------------------------------------------------------
// Os widgets de campo que o ParameterFormDialog sempre montou à mão, extraídos
// para um lugar só — o formulário de parâmetros e as telas KIP precisam ter a
// MESMA cara e o MESMO comportamento. Duas camadas:
//
//  1) fields::make*  — construtores de widget puros (sem saber de
//     core::Parameter nem de KIP). É o código que estava dentro do
//     ParameterFormDialog::setupUi, movido sem mudança de comportamento.
//  2) KipFieldEditor — adaptador de um core::KipField: escolhe o construtor
//     certo e dá uma interface uniforme (valor como QJsonValue, sinal de
//     mudança, checagem de obrigatório).
// ============================================================================
namespace fields {

// Rótulo empilhado ACIMA do campo (Top Label), com asterisco sutil quando
// obrigatório. Tira `field` do pai atual e o põe dentro do container devolvido.
QWidget *wrapWithLabel(QWidget *parent, const QString &labelText, QWidget *field, bool required = false);

// Opção de escolha: rótulo exibido x valor devolvido.
struct ChoiceOption {
    QString label;
    QString value;
    QString description; // KIP: linha secundária; vazio nos parâmetros
};
// "rótulo:valor" ou só "valor" (formato das options de core::Parameter).
ChoiceOption parseParamOption(const QString &option);

// --- Texto ---
QLineEdit *makeTextField(QWidget *parent, const QString &initial, const QString &placeholder);
// Campo de texto mascarado com botão mostrar/ocultar (KIP `secret`).
QLineEdit *makeSecretField(QWidget *parent, const QString &initial, const QString &placeholder);
// Multi-linha que cresce e tem botão de expandir.
InlineCodeField *makeTextareaField(QWidget *parent, const QString &editorTitle, const QString &placeholder,
                                   const QString &initial);
struct JsonField {
    QWidget *container = nullptr; // rótulo + formatar/minificar + campo
    InlineCodeField *field = nullptr;
};
JsonField makeJsonField(QWidget *parent, const QString &label, const QString &initial);

// --- Escolhas ---
// Combo de escolha única, editável com busca "contém". As opções mais usadas
// (`usageHistory`, do mais recente ao mais antigo) vêm primeiro.
QComboBox *makeSelectCombo(QWidget *parent, const QVector<ChoiceOption> &options, const QStringList &usageHistory,
                           const QString &initialValue);
// Como acima, mas com uma primeira entrada "vazia" (valor "") — para um select
// KIP sem valor inicial.
QComboBox *makeOptionalSelectCombo(QWidget *parent, const QVector<ChoiceOption> &options, const QString &emptyLabel,
                                   const QString &initialValue);

// Filtro de texto + paginação sobre as linhas de uma lista/tabela (KIP
// `searchable` / `page_size`). Esconde as linhas que não casam com o filtro ou
// que caem fora da página corrente — os dados (e a seleção) ficam intactos.
class ListPager : public QObject {
    Q_OBJECT

public:
    using Matcher = std::function<bool(int row, const QString &needle)>;
    using Hider = std::function<void(int row, bool hidden)>;
    // pageSize <= 0: sem paginação (só filtro).
    ListPager(int rowCount, int pageSize, Matcher matches, Hider hide, QObject *parent);

    void setFilter(const QString &text);
    void setPage(int page); // 0-based, limitado ao intervalo válido
    void next() { setPage(m_page + 1); }
    void prev() { setPage(m_page - 1); }
    int page() const { return m_page; }
    int pageCount() const { return m_pageCount; }
    int pageSize() const { return m_pageSize; }
    int matchCount() const { return m_matchCount; }
    bool paged() const { return m_pageSize > 0; }

signals:
    void changed();

private:
    void apply();

    int m_rowCount;
    int m_pageSize;
    Matcher m_matches;
    Hider m_hide;
    QString m_needle;
    int m_page = 0;
    int m_pageCount = 1;
    int m_matchCount = 0;
};
// Barra "‹  Página 2 de 5 · 47 itens  ›"; some quando cabe numa página só.
QWidget *makePagerBar(QWidget *parent, ListPager *pager);

struct ChoiceListField {
    QWidget *container = nullptr; // filtro (se houver) + lista
    QListWidget *list = nullptr;
    QLineEdit *filter = nullptr;  // nullptr quando a lista não tem filtro
    ListPager *pager = nullptr;   // filtro + paginação (sempre existe)
    QWidget *pagerBar = nullptr;  // nullptr quando não há paginação
};
// Lista de múltipla escolha com checkboxes (e a tecla Espaço alternando o item
// com foco). `withFilter`: mostra o campo "Filtrar opções..." acima.
ChoiceListField makeCheckList(QWidget *parent, const QVector<ChoiceOption> &options, const QStringList &checkedValues,
                              bool withFilter, int pageSize = 0);
// Lista de escolha única (KIP `list` sem `multiple`). Item sem seleção = valor "".
ChoiceListField makeSingleChoiceList(QWidget *parent, const QVector<ChoiceOption> &options,
                                     const QString &selectedValue, bool withFilter, int pageSize = 0);

// --- Bool / número ---
QCheckBox *makeSwitchField(QWidget *parent, const QString &text, bool checked);
QSpinBox *makeSpinField(QWidget *parent, const QString &initialValue);

// --- Arquivo / pasta ---
struct PathPickField {
    QWidget *container = nullptr; // campo + botão fundidos numa borda só
    QLineEdit *edit = nullptr;
};
// `pickMode`: "file" | "folder" | "both" (o botão abre um menu perguntando qual).
// `filter`: filtro de nome do Qt para o seletor de arquivo (KIP `filter`).
PathPickField makePathPickField(QWidget *parent, const QString &initial, const QString &pickMode,
                                const QString &initialDir, const QString &pathFormat,
                                const QString &filter = QString());

// --- Data ---
struct DatePickField {
    QWidget *container = nullptr;
    QLineEdit *edit = nullptr; // SOMENTE LEITURA: o valor só muda pela janelinha
};
// Propriedades gravadas no QLineEdit pela janelinha (lidas por quem consome).
inline constexpr char kDatePropStart[] = "kaiDateStart";
inline constexpr char kDatePropEnd[] = "kaiDateEnd";
inline constexpr char kDatePropStartFormatted[] = "kaiDateStartFormatted";
inline constexpr char kDatePropEndFormatted[] = "kaiDateEndFormatted";
using DateFormatter = std::function<QString(const QDateTime &)>;
// Data/hora no formato ISO do KIP: "yyyy-MM-dd", "HH:mm:ss" ou "yyyy-MM-ddTHH:mm:ss".
QString isoDateText(const QString &mode, const QDateTime &value);
DatePickField makeDatePickField(QWidget *parent, const QString &initialText, const QString &mode, bool range,
                                const DateFormatter &format);
// Grava início/fim no campo (propriedades + texto), como se tivessem vindo da janelinha.
void setDatePickValues(QLineEdit *edit, const QDateTime &start, const QDateTime &end, bool range,
                       const DateFormatter &format);

// --- Tabela (KIP) ---
// Modos: células somente leitura com cópia (bloco `table`), ou seleção de linha.
enum class DataTableMode { ReadOnly, SelectRow, SelectRows };
// Tabela de dados com colunas {key,label} e linhas-objeto. A chave da linha
// fica em Qt::UserRole da 1ª célula (ver `rowKey`).
QTableWidget *makeDataTable(QWidget *parent, const QVector<core::KipColumn> &columns, const QJsonArray &rows,
                            const QString &rowKey, DataTableMode mode);
// Altura que mostra `visibleRows` linhas inteiras + cabeçalho + moldura.
int dataTableHeight(const QTableWidget *table, int visibleRows);
// Fixa a altura para `visibleRows` linhas inteiras e a mantém correta quando o
// tema/estilo é aplicado depois (o header muda de altura).
void fitDataTableHeight(QTableWidget *table, int visibleRows);
struct TableChoiceField {
    QWidget *container = nullptr; // filtro (se houver) + tabela
    QTableWidget *table = nullptr;
    QLineEdit *filter = nullptr;
    ListPager *pager = nullptr;
    QWidget *pagerBar = nullptr;
};
TableChoiceField makeTableChoice(QWidget *parent, const core::KipField &field, bool withFilter, int pageSize = 0);
// Chaves das linhas selecionadas (na ordem da tabela).
QStringList selectedRowKeys(const QTableWidget *table);
void selectRowKeys(QTableWidget *table, const QStringList &keys);

// Grupo de checkboxes (KIP `flags`).
struct FlagsField {
    QWidget *container = nullptr;
    QVector<QCheckBox *> boxes; // na ordem de core::KipField::flags
};
FlagsField makeFlagsField(QWidget *parent, const QVector<core::KipFlagOption> &flags, const QJsonObject &values);

// Número com decimais/limites/passo; null = o mínimo "vazio" mostra `emptyText`.
QDoubleSpinBox *makeNumberField(QWidget *parent, const core::KipField &field);

// Quantas opções/linhas passam do limite a partir do qual aparece o filtro (§6).
inline constexpr int kFilterThreshold = 8;

} // namespace fields

// ----------------------------------------------------------------------------
// Adaptador de um campo KIP: valor como QJsonValue, sinais, obrigatório.
// ----------------------------------------------------------------------------
class KipFieldEditor : public QObject {
    Q_OBJECT

public:
    KipFieldEditor(const core::KipField &field, const QJsonValue &initial, QWidget *parent);

    const core::KipField &field() const { return m_field; }
    // O input pronto para ir num layout (sem rótulo).
    QWidget *widget() const { return m_widget; }
    // Widget que recebe o foco ao "entrar" no campo.
    QWidget *focusTarget() const { return m_focus; }

    QJsonValue value() const;
    // Troca o valor sem emitir userEdited().
    void setValue(const QJsonValue &value);
    // Conta como "preenchido" para o `required` (mesma regra do formulário de
    // parâmetros: valor não vazio).
    bool isFilled() const;
    void setReadOnly(bool readOnly);
    // Campos cuja mudança é debounced no `watch` (texto livre).
    bool isTextLike() const;
    // Filtro/paginação de um list ou table (nullptr nos demais tipos).
    fields::ListPager *pager() const { return m_pager; }
    QLineEdit *filterEdit() const { return m_filter; }

signals:
    // Qualquer mudança de valor (inclusive por setValue()).
    void valueChanged();
    // Mudança feita pelo usuário (é o que dispara `change` nos campos `watch`).
    void userEdited();

private:
    void build(const QJsonValue &initial);
    void notify(bool fromUser);

    core::KipField m_field;
    QWidget *m_widget = nullptr;
    QWidget *m_focus = nullptr;
    bool m_applying = false;

    QLineEdit *m_line = nullptr;          // text / secret / filepick / folderpick / date
    InlineCodeField *m_code = nullptr;    // textarea
    QDoubleSpinBox *m_spin = nullptr;     // number
    QComboBox *m_combo = nullptr;         // select
    QListWidget *m_list = nullptr;        // list
    QTableWidget *m_table = nullptr;      // table
    QVector<QCheckBox *> m_flagBoxes;     // flags
    QLineEdit *m_filter = nullptr;
    fields::ListPager *m_pager = nullptr;
    double m_numberEmpty = 0;             // valor sentinela do spin = vazio
    QJsonValue m_lastValue;               // último valor notificado (evita eco de sinais duplicados do Qt)
};

} // namespace kai::ui
