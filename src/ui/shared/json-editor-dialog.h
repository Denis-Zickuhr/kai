#pragma once

#include <QDialog>
#include <QString>
#include <QJsonObject>

namespace kai::ui {

class FoldableJsonView;

// Editor genérico de JSON cru (modo avançado) de Pastas e Coleções. Recebe um objeto e um título; valida o
// parse ao aceitar e devolve o objeto editado. Usa FoldableJsonView, Formatar/Minificar e um botão de volta ao modo simples.
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
