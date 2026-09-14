#pragma once

#include "aba-content.h"

namespace kai::ui {
class CodeOutputView;

class OutputStdoutContent : public AbaContent {
    Q_OBJECT

public:
    explicit OutputStdoutContent(QWidget *parent = nullptr);

    QString label() const override;
    QString iconName() const override;

    void append(const QString &text, bool isError = false);
    void clear() override;
    bool hasContent() const override;

    void setViewOptions(const ViewOptions &opts) override;
    void applyTheme() override;

private:
    CodeOutputView *m_view = nullptr;
};

} // namespace kai::ui
