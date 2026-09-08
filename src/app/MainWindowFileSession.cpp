#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QUndoStack>

#include "document/DocumentFile.h"
#include "parametric/ParamDocument.h"
#include "ui/ElaMsgBox.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"

namespace {

void showLoadWarnings(QWidget* parent, const QStringList& warnings)
{
    if (warnings.isEmpty()) return;
    constexpr int kMaxShown = 5;
    QStringList shown = warnings.mid(0, kMaxShown);
    if (warnings.size() > kMaxShown)
        shown << QString::fromUtf8("… 另有 %1 项降级").arg(warnings.size() - kMaxShown);
    cad::ui::ElaMsgBox::warning(parent, QString::fromUtf8("文件加载完成"),
        QString::fromUtf8("文件已加载，但有 %1 处内容无法识别，已按默认值恢复：\n\n%2")
            .arg(warnings.size()).arg(shown.join(QStringLiteral("\n"))));
}

} // namespace

bool MainWindow::maybeSave()
{
    if (m_undoStack->isClean())
        return true;

    auto ret = cad::ui::ElaMsgBox::show(this, QString::fromUtf8("野风帖"),
        QString::fromUtf8("文档已修改，是否保存？"),
        QString::fromUtf8("取消"),
        QString::fromUtf8("保存"),
        QString::fromUtf8("不保存"));

    if (ret == cad::ui::ElaMsgBox::Result::Right) {
        onSaveDocument();
        return m_undoStack->isClean();
    }
    if (ret == cad::ui::ElaMsgBox::Result::Middle)
        return true;
    return false;
}

void MainWindow::updateTitle()
{
    QString name = m_currentFilePath.isEmpty()
        ? QString::fromUtf8("未命名")
        : QFileInfo(m_currentFilePath).fileName();
    QString dirty = m_undoStack->isClean() ? QString() : QStringLiteral("*");
    setWindowTitle(QStringLiteral("%1%2 - 野风帖 [P207-ABS]").arg(dirty, name));
}

void MainWindow::clearDocument()
{
    m_paramDoc->clear();
    m_lastWorkingLayer = m_paramDoc->layersView().firstWorkingLayerId();
    if (m_actionToggleAuxLayer)
        m_actionToggleAuxLayer->setChecked(false);
    m_undoStack->clear();
    m_undoStack->setClean();
    m_currentFilePath.clear();
    updateTitle();
}

void MainWindow::onNewDocument()
{
    if (!maybeSave()) return;
    clearDocument();
}

void MainWindow::onOpenDocument()
{
    if (!maybeSave()) return;

    QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("打开文件"), QString(),
        QString::fromUtf8(cad::doc::DocumentFile::kFilter));
    if (path.isEmpty()) return;

    QString error;
    QStringList warnings;
    if (!cad::doc::DocumentFile::load(path, *m_paramDoc, &error, &warnings)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("打开失败"), error);
        return;
    }
    showLoadWarnings(this, warnings);

    m_currentFilePath = path;
    m_undoStack->clear();
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
    onActiveLayerChanged(m_paramDoc->layersView().activeLayer());
}

void MainWindow::onSaveDocument()
{
    if (m_currentFilePath.isEmpty()) {
        onSaveAsDocument();
        return;
    }

    QString error;
    if (!cad::doc::DocumentFile::save(m_currentFilePath, *m_paramDoc, &error)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("保存失败"), error);
        return;
    }

    m_undoStack->setClean();
    addRecentFile(m_currentFilePath);
    updateTitle();
    flashStatus(QStringLiteral("\u2713 已保存 %1")
                    .arg(QFileInfo(m_currentFilePath).fileName()),
                cad::ui::Theme::tokens().success, 2000);
}

void MainWindow::onSaveAsDocument()
{
    QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("另存为"), QString(),
        QString::fromUtf8(cad::doc::DocumentFile::kFilter));
    if (path.isEmpty()) return;

    if (!path.endsWith(QStringLiteral(".gcad"), Qt::CaseInsensitive))
        path += QStringLiteral(".gcad");

    QString error;
    if (!cad::doc::DocumentFile::save(path, *m_paramDoc, &error)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("保存失败"), error);
        return;
    }

    m_currentFilePath = path;
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
    flashStatus(QStringLiteral("\u2713 已保存 %1").arg(QFileInfo(path).fileName()),
                cad::ui::Theme::tokens().success, 2000);
}

void MainWindow::onOpenRecentFile()
{
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    if (!maybeSave()) return;

    QString path = action->data().toString();
    QString error;
    QStringList warnings;
    if (!cad::doc::DocumentFile::load(path, *m_paramDoc, &error, &warnings)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("打开失败"), error);
        m_recentFiles.removeAll(path);
        updateRecentFilesMenu();
        return;
    }
    showLoadWarnings(this, warnings);

    m_currentFilePath = path;
    m_undoStack->clear();
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
}

void MainWindow::addRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10)
        m_recentFiles.removeLast();

    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();

    if (m_recentFiles.isEmpty()) {
        QAction* empty = m_recentFilesMenu->addAction(QString::fromUtf8("(无)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString& path : m_recentFiles) {
        QAction* act = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        act->setData(path);
        act->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("工程文件路径"), path, false));
        connect(act, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }
}
