#include "ui/shared/file-dialogs.h"

#include <QFileDialog>

#include "utils/last-directory.h"

namespace kai::ui {

QString pickOpenFile(QWidget *parent, const QString &title, const QString &filter, const QString &hint)
{
    const QString path = QFileDialog::getOpenFileName(parent, title, utils::LastDirectory::startFor(hint), filter);
    utils::LastDirectory::remember(path);
    return path;
}

QString pickSaveFile(QWidget *parent, const QString &title, const QString &suggestedName, const QString &filter)
{
    const QString path = QFileDialog::getSaveFileName(parent, title, utils::LastDirectory::saveStartFor(suggestedName), filter);
    utils::LastDirectory::remember(path);
    return path;
}

QString pickDirectory(QWidget *parent, const QString &title, const QString &hint)
{
    const QString path = QFileDialog::getExistingDirectory(
        parent, title, utils::LastDirectory::startFor(hint), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    utils::LastDirectory::remember(path);
    return path;
}

} // namespace kai::ui
