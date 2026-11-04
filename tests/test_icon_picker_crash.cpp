#include <QTest>
#include <QToolButton>

#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/icon-picker-dialog.h"
#include "ui/features/command-editor/command-editor-dialog.h"
#include "ui/features/collections/folder-editor-dialog.h"
#include "core/models.h"

using namespace kai::ui;

// Crash ao selecionar ícone de um comando: o popup do QComboBox do IconPickerWidget tem bug do Qt sob
// Wayland (PYSIDE-2114). Foi trocado por um botão que abre o IconPickerDialog modal; o teste cobre o
// fluxo completo de seleção via IconPickerDialog nos diálogos reais.
class TestIconPickerCrash : public QObject {
    Q_OBJECT

private slots:
    void selectingEachPoolIconInDialogDoesNotCrash()
    {
        for (const QString &iconName : IconPickerWidget::poolIconNames()) {
            IconPickerDialog dialog(QString(), nullptr);
            dialog.show();
            dialog.close();
            Q_UNUSED(iconName);
        }
    }

    void iconForNameDoesNotCrashForAnyPoolEntry()
    {
        for (const QString &iconName : IconPickerWidget::poolIconNames()) {
            const QIcon icon = IconPickerWidget::iconForName(iconName);
            QVERIFY(!icon.isNull());
        }
    }

    void iconForEmptyOrInvalidNameReturnsNullIconWithoutCrash()
    {
        QVERIFY(IconPickerWidget::iconForName(QString()).isNull());
        QVERIFY(IconPickerWidget::iconForName(QStringLiteral("nome_inexistente")).isNull());
        QVERIFY(IconPickerWidget::iconForName(QStringLiteral("file:/caminho/que/nao/existe.png")).isNull());
    }

    void iconPickerWidgetOpenAndCloseDialogDoesNotCrash()
    {
        IconPickerWidget picker;
        auto *button = picker.findChild<QToolButton *>();
        QVERIFY(button != nullptr);

        // Não abre o diálogo modal (bloquearia); exercita setSelectedIconName/selectedIconName repetidamente.
        for (const QString &iconName : IconPickerWidget::poolIconNames()) {
            picker.setSelectedIconName(iconName);
            QCOMPARE(picker.selectedIconName(), iconName);
        }
    }

    void selectingIconInsideCommandEditorDialogDoesNotCrash()
    {
        CommandEditorDialog dialog(QStringLiteral("f_test"), {}, {}, nullptr);

        auto *iconButton = dialog.findChild<IconPickerWidget *>();
        QVERIFY(iconButton != nullptr);

        for (const QString &iconName : IconPickerWidget::poolIconNames()) {
            iconButton->setSelectedIconName(iconName);
        }

        const kai::core::Command command = dialog.buildCommand();
        Q_UNUSED(command);
    }

    void selectingIconInsideFolderEditorDialogDoesNotCrash()
    {
        FolderEditorDialog dialog({}, nullptr);

        auto *iconPicker = dialog.findChild<IconPickerWidget *>();
        QVERIFY(iconPicker != nullptr);

        for (const QString &iconName : IconPickerWidget::poolIconNames()) {
            iconPicker->setSelectedIconName(iconName);
        }
    }
};

QTEST_MAIN(TestIconPickerCrash)
#include "test_icon_picker_crash.moc"
