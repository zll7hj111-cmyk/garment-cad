#include "MeasureTab.h"

#include "ElaScrollArea.h"
#include <QVBoxLayout>
#include "ElaText.h"
#include "Theme.h"
#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "document/commands/VariableCommands.h"
#include "ui/IconHelper.h"
#include "VirtualCardList.h"
#include "MeasureCard.h"
#include "AngleMeasureCard.h"
#include "parametric/PerfProbe.h"

namespace cad::ui {

namespace {

/// Build a "点A ↔ 点B" source label from a measure variable's two points.
QString measureSourceLabel(const cad::param::ParamDocument* doc,
                           const cad::param::MeasureVariable& mv)
{
    auto pointLabel = [doc](const QUuid& blockId, const QUuid& pointId) -> QString {
        const auto* blk = doc->findBlock(blockId);
        const auto* pt = blk ? blk->findPoint(pointId) : nullptr;
        if (!pt) return QStringLiteral("?");
        QString s = pt->serial;
        if (!pt->name.isEmpty())
            s += QStringLiteral("·") + pt->name;
        return s;
    };
    return pointLabel(mv.blockA, mv.pointA)
         + QStringLiteral(" \u2194 ")
         + pointLabel(mv.blockB, mv.pointB);
}

/// Build a "线A ∠ 线B" source label from an angle measure variable's two segments.
QString angleSourceLabel(const cad::param::ParamDocument* doc,
                         const cad::param::AngleMeasureVariable& am)
{
    auto segLabel = [doc](const QUuid& blockId, const QUuid& segmentId) -> QString {
        const auto* blk = doc->findBlock(blockId);
        const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
        if (!seg) return QStringLiteral("?");
        QString s = seg->serial;
        if (!seg->name.isEmpty())
            s += QStringLiteral("·") + seg->name;
        return s;
    };
    return segLabel(am.blockA, am.segmentA)
         + QStringLiteral(" \u2220 ")
         + segLabel(am.blockB, am.segmentB);
}

} // namespace

MeasureTab::MeasureTab(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    m_scroll = new ElaScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    // Recessed list background: 画布纸色 (canvasBg), 与画布背景呼应.
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }")
        .arg(cad::ui::Theme::tokens().canvasBg.name()));
    m_scroll->setObjectName(QStringLiteral("cardListArea"));

    m_container = new QWidget();
    m_container->setStyleSheet(QStringLiteral(
        "background: %1;").arg(cad::ui::Theme::tokens().canvasBg.name()));
    m_container->setObjectName(QStringLiteral("cardListContainer"));
    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_emptyHint = new ElaText(QStringLiteral("暂无测量变量\n使用智能笔连接两个点时自动创建\n（测量两点间距离）\n或使用「角度测量」工具测量两线夹角"), 13, m_container);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setObjectName(QStringLiteral("dimText"));
    layout->addWidget(m_emptyHint);

    // Virtualized card host: list margins live inside the host, and only
    // rows near the viewport are materialized (O(visible) widget count).
    m_host = new VirtualCardList(m_container);
    layout->addWidget(m_host);
    m_host->init(m_scroll);

    layout->addStretch();

    m_scroll->setWidget(m_container);

    auto* wrapper = new QVBoxLayout(this);
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->setSpacing(0);
    wrapper->addWidget(m_scroll);

    // ---- Measure variables (length + angle cards) ----
    m_host->setProviders(
        [this](int row) -> QWidget* {
            const auto& measures = m_doc->measureVars();
            const auto& angles = m_doc->angleMeasures();
            const int m = static_cast<int>(measures.size());
            if (row < 0 || row >= m + static_cast<int>(angles.size()))
                return nullptr;
            if (row < m) {
                auto* card = new MeasureCard(measures[row],
                    measureSourceLabel(m_doc, measures[row]),
                    row % 2 == 1, m_host);
                card->setIndex(row + 1);
                connect(card, &MeasureCard::deleteRequested,
                        this, &MeasureTab::onMeasureDeleted);
                connect(card, &MeasureCard::edited,
                        this, &MeasureTab::onMeasureEdited);
                // The card emits its own measure id — flash the measured
                // points precisely (falls back to a whole-block flash in
                // MainWindow when the points are missing/unresolved).
                connect(card, &MeasureCard::sourceClicked,
                        this, &MeasureTab::highlightMeasureRequested);
                return card;
            }
            const int ai = row - m;
            auto* card = new AngleMeasureCard(angles[ai],
                angleSourceLabel(m_doc, angles[ai]),
                ai % 2 == 1, m_host);
            card->setIndex(ai + 1);
            connect(card, &AngleMeasureCard::deleteRequested,
                    this, &MeasureTab::onAngleMeasureDeleted);
            connect(card, &AngleMeasureCard::edited,
                    this, &MeasureTab::onAngleMeasureEdited);
            connect(card, &AngleMeasureCard::sourceClicked,
                    this, &MeasureTab::highlightBlockRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& measures = m_doc->measureVars();
            const auto& angles = m_doc->angleMeasures();
            const int m = static_cast<int>(measures.size());
            if (row < 0 || row >= m + static_cast<int>(angles.size()))
                return;
            if (row < m) {
                auto* card = static_cast<MeasureCard*>(w);
                card->syncFromModel(
                    measures[row], measureSourceLabel(m_doc, measures[row]));
                card->setIndex(row + 1);
            } else {
                const int ai = row - m;
                auto* card = static_cast<AngleMeasureCard*>(w);
                card->syncFromModel(
                    angles[ai], angleSourceLabel(m_doc, angles[ai]));
                card->setIndex(ai + 1);
            }
        });
}

void MeasureTab::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
}

void MeasureTab::sync()
{
    GCAD_PERF_SCOPE("ui.syncMeasure");
    const auto& measures = m_doc->measureVars();
    const auto& angles = m_doc->angleMeasures();

    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(measures.size() + angles.size()));
    for (const auto& mv : measures)
        keys.append(mv.id);
    for (const auto& am : angles)
        keys.append(am.id);
    m_host->setRows(keys);
    m_emptyHint->setVisible(measures.empty() && angles.empty());
}

void MeasureTab::onMeasureDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveMeasureCommand(m_doc, id));
    else
        m_doc->removeMeasure(id);
}

void MeasureTab::onMeasureEdited(const cad::param::MeasureVariable& mv)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findMeasure(mv.id)) {
        if (cur->name == mv.name && cur->comment == mv.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetMeasureCommand(m_doc, mv));
    else
        m_doc->updateMeasure(mv);
}

void MeasureTab::onAngleMeasureDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveAngleMeasureCommand(m_doc, id));
    else
        m_doc->removeAngleMeasure(id);
}

void MeasureTab::onAngleMeasureEdited(const cad::param::AngleMeasureVariable& am)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findAngleMeasure(am.id)) {
        if (cur->name == am.name && cur->comment == am.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetAngleMeasureCommand(m_doc, am));
    else
        m_doc->updateAngleMeasure(am);
}

} // namespace cad::ui
