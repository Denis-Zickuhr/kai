#pragma once

#include <QTranslator>

namespace kai::utils {

// Traduz os textos que o próprio Qt desenha (menu de copiar/colar dos campos de texto, botões padrão de
// QMessageBox/QDialogButtonBox) com o language pack do Kai, sem depender dos qtbase_*.qm — que o Kai não leva
// consigo. Só responde aos textos conhecidos; o resto cai no texto original do Qt.
class QtBuiltinTranslator : public QTranslator {
    Q_OBJECT

public:
    explicit QtBuiltinTranslator(QObject *parent = nullptr) : QTranslator(parent) {}

    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;
    // Um tradutor "vazio" é ignorado pelo QCoreApplication.
    bool isEmpty() const override { return false; }
};

} // namespace kai::utils
