

#include "ui/features/docs/doc-browser.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace docview;

bool DocViewer::isDirty() const
{
    return m_editing && m_pane->isModified();
}

bool DocViewer::canEdit() const
{
    return m_editable;
}

void DocViewer::setEditable(bool editable)
{
    m_editable = editable;
    m_editBar->setEditable(editable);
    m_editBar->setNoteMode(editable && !m_noteId.isEmpty());
    positionSearchBar();
}

void DocViewer::enterEditMode(bool takeFocus)
{
    if (!m_editable || m_editing || m_currentFile.isEmpty()) {
        return;
    }
    const texttools::Language language = currentLanguage();
    // Lendo no editor (JSON/YAML/XML), o texto já está lá: reaproveitá-lo guarda o que o usuário dobrou.
    if (!m_codeView) {
        m_pane->setContent(m_rawText.contains(QLatin1Char('\r')) ? QString(m_rawText).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) : m_rawText,
                           language);
    }
    m_pane->editor()->setReadOnly(false);
    m_pane->editor()->setZoomPercent(m_zoom);
    m_editing = true;
    m_stack->setCurrentWidget(m_pane);
    m_editBar->setFoldable(folding::supports(language));
    m_editBar->setEditing(true);
    m_editBar->setDirty(false);
    if (takeFocus) {
        m_pane->editor()->setFocus();
    }
    runSearch(false); // a busca aberta passa a valer no texto do editor
    emit editingChanged(true);
}

void DocViewer::leaveEditMode(bool rerender)
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_editBar->setEditing(false);
    m_pane->editor()->setSearchSelections({});
    if (m_codeView) {
        // Continua no editor, agora só para leitura. Descartar devolve o último texto salvo; o resto mantém as dobras.
        if (rerender && m_pane->isModified()) {
            m_pane->setContent(m_rawText.contains(QLatin1Char('\r')) ? QString(m_rawText).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) : m_rawText,
                               currentLanguage());
        }
        m_pane->editor()->setReadOnly(true);
    } else {
        m_stack->setCurrentWidget(m_browser);
        if (rerender) {
            render(QString()); // volta a mostrar o texto (salvo) já formatado
        }
    }
    runSearch(false);
    if (m_codeView) {
        m_pane->editor()->setFocus();
    } else {
        m_browser->setFocus();
    }
    emit editingChanged(false);
}

// O lápis com o modo ligado: sai da edição, perguntando antes se há alterações.
void DocViewer::requestLeaveEdit()
{
    if (!m_editing) {
        return;
    }
    if (!isDirty()) {
        leaveEditMode(true);
        captureState();
        return;
    }
    const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Leaving) : UnsavedDecision::Cancel;
    if (decision == UnsavedDecision::Save) {
        saveCurrent([this](bool ok) {
            if (ok) {
                leaveEditMode(true);
                captureState();
            }
        });
    } else if (decision == UnsavedDecision::Discard) {
        leaveEditMode(true);
        captureState();
    }
    m_editBar->setEditing(m_editing); // Cancelar: o lápis continua ligado
}

void DocViewer::discardChanges()
{
    if (!m_editing) {
        return;
    }
    if (isDirty()) {
        const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Discarding) : UnsavedDecision::Cancel;
        if (decision != UnsavedDecision::Discard) {
            return;
        }
    }
    leaveEditMode(true); // o texto lido continua o último salvo: as alterações somem
    captureState();      // e o rascunho guardado também
}

// Antes de sair do arquivo (navegar, abrir outro): se está editando com alterações, pergunta; `proceed` roda quando for
// liberado (salvar é assíncrono, então pode ser depois).
void DocViewer::guardedLeave(std::function<void()> proceed)
{
    if (m_store) {
        // Com a memória do leitor, sair do arquivo nunca pergunta nem perde nada: o texto não salvo vira rascunho e a
        // edição retoma de onde parou quando o arquivo voltar.
        captureState();
        if (m_editing) {
            leaveEditMode(false);
            m_captureFrozen = true;
        }
        proceed();
        return;
    }
    if (!m_editing) {
        proceed();
        return;
    }
    if (!isDirty()) {
        leaveEditMode(false);
        proceed();
        return;
    }
    const UnsavedDecision decision = m_unsavedHandler ? m_unsavedHandler(UnsavedContext::Leaving) : UnsavedDecision::Cancel;
    if (decision == UnsavedDecision::Cancel) {
        return;
    }
    if (decision == UnsavedDecision::Discard) {
        leaveEditMode(false);
        proceed();
        return;
    }
    saveCurrent([this, proceed = std::move(proceed)](bool ok) {
        if (ok) {
            leaveEditMode(false);
            proceed();
        }
    });
}

// Grava em segundo plano (QSaveFile: o arquivo antigo só é trocado se a escrita inteira der certo), preservando o fim de
// linha que o arquivo já tinha.
void DocViewer::saveCurrent(std::function<void(bool)> done)
{
    if (!m_editing || m_saving) {
        if (done) {
            done(false);
        }
        return;
    }
    if (!m_noteId.isEmpty()) {
        // Nota: quem grava é o app (notes.json); sem arquivo nem thread.
        const QString text = m_pane->text();
        const bool ok = m_noteSaver ? m_noteSaver(m_noteId, text) : false;
        if (ok) {
            for (DocNote &note : m_notes) {
                if (note.id == m_noteId) {
                    note.content = text;
                }
            }
        }
        finishSave(ok, ok ? QString() : utils::tr(QStringLiteral("doc.notes.save_failed")), text, done);
        return;
    }
    m_saving = true;
    QString text = m_pane->text();
    QString onDisk = text;
    if (m_lineEnding == QLatin1String("\r\n")) {
        onDisk.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    }
    const QString path = m_currentFile;
    QPointer<DocViewer> guard(this);
    QThread *thread = QThread::create([guard, path, onDisk, done]() {
        QSaveFile file(path);
        bool ok = file.open(QIODevice::WriteOnly);
        QString error = ok ? QString() : file.errorString();
        if (ok) {
            const QByteArray bytes = onDisk.toUtf8();
            ok = file.write(bytes) == bytes.size() && file.commit();
            if (!ok) {
                error = file.errorString();
            }
        }
        QMetaObject::invokeMethod(guard.data(), [guard, ok, error, onDisk, done]() {
            if (guard) {
                guard->finishSave(ok, error, onDisk, done);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocViewer::finishSave(bool ok, const QString &error, const QString &savedText, const std::function<void(bool)> &done)
{
    m_saving = false;
    if (ok) {
        m_rawText = savedText; // o texto "lido" passa a ser o salvo (discard e re-render partem dele)
        m_pane->markSaved();
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.saved")), false);
        captureState(); // salvo: o rascunho deixa de existir
        emit fileSaved(m_currentFile);
    } else {
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.save_failed")).arg(error), true);
        emit saveFailed(error);
    }
    if (done) {
        done(ok);
    }
}

void DocViewer::showToolsMenu(const QPoint &globalPosition)
{
    if (!m_editing) {
        return;
    }
    DocToolsMenu menu(m_pane->editor()->language(), this);
    connect(&menu, &DocToolsMenu::toolChosen, this, &DocViewer::runTool);
    menu.exec(globalPosition);
}

void DocViewer::showNoteActionsMenu(const QPoint &globalPosition)
{
    if (m_noteId.isEmpty()) {
        return;
    }
    const QColor accent(tk::accent());
    QMenu menu(this);
    QAction *saveAs = menu.addAction(LucideIcons::icon(QStringLiteral("download"), accent, 16),
                                     utils::tr(QStringLiteral("ctx.note.save_as")));
    QAction *openCopy = menu.addAction(LucideIcons::icon(QStringLiteral("external-link"), accent, 16),
                                       utils::tr(QStringLiteral("ctx.note.open_copy")));
    menu.addSeparator();
    menu.addAction(LucideIcons::icon(QStringLiteral("copy"), accent, 16), utils::tr(QStringLiteral("ctx.note.copy_content")));
    QAction *chosen = menu.exec(globalPosition);
    if (!chosen) {
        return;
    }
    // O que está na tela (editor com alterações não salvas, ou o texto da nota).
    const QString content = m_editing ? m_pane->editor()->toPlainText() : m_rawText;
    const core::NoteFileAction action = chosen == saveAs ? core::NoteFileAction::SaveAs
        : chosen == openCopy                              ? core::NoteFileAction::OpenCopy
                                                          : core::NoteFileAction::CopyContent;
    emit noteFileActionRequested(m_noteId, action, content);
}

void DocViewer::runTool(const QString &toolId)
{
    const DocTool *tool = findDocTool(toolId);
    if (!m_editing || !tool) {
        return;
    }
    DocCodeEditor *editor = m_pane->editor();
    const QString label = utils::tr(tool->labelKey);
    if (tool->validateOnly) {
        const texttools::Result verdict = tool->run(editor->toPlainText());
        if (verdict.ok) {
            m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_valid")).arg(label), false);
        } else {
            // Primeiro leva o cursor ao erro (mexer o cursor apaga a mensagem rápida), depois mostra o motivo.
            if (verdict.line > 0) {
                editor->goToLine(verdict.line, std::max(1, verdict.column));
            }
            m_pane->showMessage(verdict.line > 0
                    ? utils::tr(QStringLiteral("doc.edit.error_at")).arg(verdict.line).arg(std::max(1, verdict.column)).arg(verdict.error)
                    : verdict.error, true);
        }
        return;
    }
    const texttools::Result result = editor->transform(tool->run);
    if (result.ok) {
        m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_done")).arg(label), false);
        return;
    }
    if (result.line > 0 && !editor->textCursor().hasSelection()) {
        editor->goToLine(result.line, std::max(1, result.column));
    }
    m_pane->showMessage(utils::tr(QStringLiteral("doc.edit.tool_failed")).arg(label, result.error), true);
}

} // namespace kai::ui
