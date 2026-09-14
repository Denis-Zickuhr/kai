#pragma once

#include <QDialog>
#include <QString>
#include <QJsonObject>

namespace kai::ui {

class FoldableJsonView;

// Editor de JSON cru GENÉRICO (modo avançado), reutilizado por Pastas e
// Coleções — mesma ideia do CommandJsonEditorDialog, mas parametrizável.
// Recebe um objeto JSON inicial e um título; valida o parse ao aceitar e
// devolve o objeto editado. Realce de sintaxe + dobras estilo JetBrains
// (FoldableJsonView, mesmo componente do visualizador de resposta HTTP —
// pedido do usuário: "o modo avançado [...] deve seguir a estilização
// json ala jetbrains") + Formatar + Minificar + botão visível de volta ao
// modo simples (antes só havia atalho de teclado, sem afordance visual).
class JsonEditorDialog : public QDialog {
    Q_OBJECT

public:
    JsonEditorDialog(const QString &title, const QJsonObject &initial, QWidget *parent = nullptr);

    // Objeto resultante (válido — só após exec() == Accepted).
    QJsonObject result() const { return m_result; }

private slots:
    void handleAccept();
    void handleFormat();
    void handleMinify();

private:
    void setupUi(const QJsonObject &initial);

    FoldableJsonView *m_jsonField = nullptr;
    QJsonObject m_result;
};

} // namespace kai::ui
