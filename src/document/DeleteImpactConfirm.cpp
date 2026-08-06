#include "DeleteImpactConfirm.h"

#include <QMessageBox>

#include "parametric/ParamDocument.h"

namespace cad::doc {

namespace {

/// Human-readable line for one impact counter; empty when the counter is 0.
QString impactLine(const QString& label, int count)
{
    if (count <= 0) return {};
    return QString::fromUtf8("\u2022 %1").arg(label).arg(count);
}

} // namespace

bool confirmDeleteImpact(QWidget* parent, const cad::param::ParamDocument* doc,
                         const QList<QUuid>& blockIds)
{
    using cad::param::ParamDocument;
    if (!doc || blockIds.isEmpty()) return true;

    ParamDocument::DeleteImpact total;
    for (const QUuid& id : blockIds)
        total += doc->deleteImpactReport(id);
    if (!total.hasImpact()) return true;

    QStringList lines;
    if (const QString l = impactLine(
            QString::fromUtf8("\u5220\u9664 %1 \u6761\u8fde\u63a5"),
            total.attachmentsRemoved); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u6761\u6865\u63a5\u7ebf\u5c06\u91ca\u653e\u4e3a\u72ec\u7acb\u7ebf\u6bb5\uff08\u4fdd\u7559\u51e0\u4f55\uff09"),
            total.bridgesReleased); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u4ea4\u70b9\u5c06\u51bb\u7ed3\u5728\u5f53\u524d\u4f4d\u7f6e"),
            total.intersectionsFrozen); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u4ea4\u70b9\u5c06\u56de\u9000\u4e3a\u56fa\u5b9a\u89d2\u5ea6\uff08\u6307\u5411\u70b9\u88ab\u5220\uff09"),
            total.intersectionsAimCleared); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u5904\u957f\u5ea6\u5f15\u7528\u5c06\u56fa\u5316\u4e3a\u6570\u503c"),
            total.linkedFrozen); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u5173\u8054\u957f\u5ea6\u53d8\u91cf\u5c06\u88ab\u5220\u9664"),
            total.linkedVarsRemoved); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u6d4b\u91cf\u53d8\u91cf\u5c06\u88ab\u5220\u9664\uff08\u4e0d\u53ef\u6062\u590d\uff09"),
            total.measureVarsRemoved); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u89d2\u5ea6\u6d4b\u91cf\u53d8\u91cf\u5c06\u88ab\u5220\u9664\uff08\u4e0d\u53ef\u6062\u590d\uff09"),
            total.angleVarsRemoved); !l.isEmpty()) lines << l;
    if (const QString l = impactLine(
            QString::fromUtf8("%1 \u4e2a\u516c\u5f0f\u5c06\u56e0\u5f15\u7528\u5931\u6548\u800c\u62a5\u9519"),
            total.formulasBroken); !l.isEmpty()) lines << l;

    const QString text = lines.join(QStringLiteral("\n")) + QStringLiteral("\n\n")
        + QString::fromUtf8("\u786e\u5b9a\u5220\u9664\uff1f");
    const auto ans = QMessageBox::warning(parent, QString::fromUtf8("\u5220\u9664\u5f71\u54cd"),
                                          text, QMessageBox::Yes | QMessageBox::Cancel,
                                          QMessageBox::Cancel);
    return ans == QMessageBox::Yes;
}

} // namespace cad::doc
