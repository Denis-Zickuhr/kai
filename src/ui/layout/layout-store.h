#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include "ui/layout/layout-model.h"

#include <functional>

namespace kai::ui::layout {

// layout.json: o layout atual mais as perspectivas salvas pelo usuário. Fica fora do settings.json de propósito (é estado
// pessoal da janela, e "Restaurar layout" não pode encostar nas outras configurações) e fora do YAML do projeto.
//
// A leitura é síncrona e acontece uma única vez, antes da primeira janela: o arquivo tem poucos centenas de bytes e a
// janela precisa dele para nascer no lugar certo. A escrita nunca roda na interface: cai numa thread, atômica.
class LayoutStore : public QObject {
    Q_OBJECT

public:
    struct Loaded {
        LayoutModel layout;
        QMap<QString, LayoutModel> perspectives;
        bool fileExisted = false;
        // O JSON estava ilegível: foi guardado como .corrupt e o padrão assumiu.
        bool recovered = false;
    };

    // `filePath` vazio = só em memória (testes).
    explicit LayoutStore(const QString &filePath, QObject *parent = nullptr);
    ~LayoutStore() override;

    // `legacy` monta o layout equivalente às configurações antigas (outputPosition) quando ainda não há layout.json.
    Loaded load(const LayoutModel::Validator &isKnown, const std::function<LayoutModel()> &legacy = {});

    // Agenda a gravação (debounce); a última chamada vence.
    void scheduleSave(const LayoutModel &layout, const QMap<QString, LayoutModel> &perspectives);
    // Grava já, de forma síncrona (fim do app).
    void flush();

    QString filePath() const { return m_path; }
    static QByteArray serialize(const LayoutModel &layout, const QMap<QString, LayoutModel> &perspectives);

private:
    void startWrite();

    QString m_path;
    QTimer m_timer;
    QThreadPool m_pool;
    bool m_pending = false;
    LayoutModel m_pendingLayout;
    QMap<QString, LayoutModel> m_pendingPerspectives;
};

} // namespace kai::ui::layout
