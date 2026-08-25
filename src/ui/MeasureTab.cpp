#include "MeasureTab.h"

#include "ElaScrollArea.h"
#include <QVBoxLayout>
#include "ElaText.h"
#include "Theme.h"
#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Serial.h"
#include "document/commands/VariableCommands.h"
#include "ui/IconHelper.h"
#include "VirtualCardList.h"
#include "MeasureCard.h"
#include "AngleMeasureCard.h"
#include "parametric/PerfProbe.h"

namespace cad::ui {

namespace {

/// Display name of the layer containing @p blockId ("" when the block is gone
/// — its layer cannot be resolved).
QString blockLayerName(const cad::param::ParamDocument* doc, const QUuid& blockId)
{
    const auto* blk = doc->findBlock(blockId);
    if (!blk) return QString();
    const auto* layer = doc->layerById(blk->layer);
    return layer ? layer->name : QString();
}

/// Friendly endpoint label: serial tag without the dedup prefix ("P1") plus
/// the optional user name ("P1·肩点"); "?" when the point is missing.
QString pointTagLabel(const cad::param::ParamDocument* doc,
                      const QUuid& blockId, const QUuid& pointId)
{
    const auto* blk = doc->findBlock(blockId);
    const auto* pt = blk ? blk->findPoint(pointId) : nullptr;
    if (!pt) return QStringLiteral("?");
    QString s = cad::param::Serial::tag(pt->serial);
    if (!pt->name.isEmpty())
        s += QStringLiteral("·") + pt->name;
    return s;
}

/// Same as pointTagLabel but for segments (angle measures): "L3" / "L3·名称".
QString segTagLabel(const cad::param::ParamDocument* doc,
                    const QUuid& blockId, const QUuid& segmentId)
{
    const auto* blk = doc->findBlock(blockId);
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!seg) return QStringLiteral("?");
    QString s = cad::param::Serial::tag(seg->serial);
    if (!seg->name.isEmpty())
        s += QStringLiteral("·") + seg->name;
    return s;
}

/// "层名 · 标签"; falls back to the bare label when the layer is unknown
/// (block deleted / layer missing) — a dangling end shows only "?".
QString endpointWithLayer(const QString& layerName, const QString& label)
{
    if (layerName.isEmpty()) return label;
    return layerName + QStringLiteral(" · ") + label;
}

/// Build a "层 · P1 ↔ P3" source label from a measure variable's two points.
/// Same layer → the layer name appears once; cross-layer → each endpoint
/// carries its own layer ("前片 · P1 ↔ 后片 · P3").
QString measureSourceLabel(const cad::param::ParamDocument* doc,
                           const cad::param::MeasureVariable& mv)
{
    const QString la = blockLayerName(doc, mv.blockA);
    const QString lb = blockLayerName(doc, mv.blockB);
    const QString pa = pointTagLabel(doc, mv.blockA, mv.pointA);
    const QString pb = pointTagLabel(doc, mv.blockB, mv.pointB);
    if (!la.isEmpty() && la == lb)
        return la + QStringLiteral(" · ") + pa + QStringLiteral(" \u2194 ") + pb;
    return endpointWithLayer(la, pa)
         + QStringLiteral(" \u2194 ")
         + endpointWithLayer(lb, pb);
}

/// Build a "层 · L1 ∠ L3" source label from an angle measure variable's two
/// segments (same same-layer merge rule as measureSourceLabel).
QString angleSourceLabel(const cad::param::ParamDocument* doc,
                         const cad::param::AngleMeasureVariable& am)
{
    const QString la = blockLayerName(doc, am.blockA);
    const QString lb = blockLayerName(doc, am.blockB);
    const QString sa = segTagLabel(doc, am.blockA, am.segmentA);
    const QString sb = segTagLabel(doc, am.blockB, am.segmentB);
    if (!la.isEmpty() && la == lb)
        return la + QStringLiteral(" · ") + sa + QStringLiteral(" \u2220 ") + sb;
    return endpointWithLayer(la, sa)
         + QStringLiteral(" \u2220 ")
         + endpointWithLayer(lb, sb);
}

/// A lightweight signature of every non-value field shown on the measure
/// cards (name/comment/ref/source/kind). Values are intentionally excluded so
/// the per-frame resolve path can stay value-only.
QString measureMetaSignature(const cad::param::ParamDocument* doc)
{
    QString sig;
    for (const auto& mv : doc->measureVars()) {
        sig += mv.id.toString(QUuid::WithoutBraces);
        sig += QLatin1Char('|') + mv.name + QLatin1Char('\x1f') + mv.refName +
               QLatin1Char('\x1f') + mv.comment + QLatin1Char('\x1f') +
               QString::number(static_cast<int>(mv.kind)) + QLatin1Char('\x1f') +
               measureSourceLabel(doc, mv) + QLatin1Char('\n');
    }
    for (const auto& am : doc->angleMeasures()) {
        sig += am.id.toString(QUuid::WithoutBraces);
        sig += QLatin1Char('|') + am.name + QLatin1Char('\x1f') + am.refName +
               QLatin1Char('\x1f') + am.comment + QLatin1Char('\n') +
               angleSourceLabel(doc, am) + QLatin1Char('\n');
    }
    return sig;
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

    m_emptyHint = new ElaText(QStringLiteral("暂无测量变量\n使用智能笔连接两个点时自动创建\n（测量两点间距离）\n或使用「角度测量」工具测量两线夹角\n测量工具中按 W 可切换 距离/水平/垂直"), 13, m_container);
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
                    this, &MeasureTab::highlightAngleMeasureRequested);
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
                card->setAlternate(row % 2 == 1);  // 缓存复用: 奇偶随新行号重设
            } else {
                const int ai = row - m;
                auto* card = static_cast<AngleMeasureCard*>(w);
                card->syncFromModel(
                    angles[ai], angleSourceLabel(m_doc, angles[ai]));
                card->setIndex(ai + 1);
                card->setAlternate(ai % 2 == 1);  // 缓存复用: 奇偶随新行号重设
            }
        });
    // 2026-09 性能: resolved 每帧触发 sync() → setRows(同 keys) → 值级刷新
    // (只有 measure/angle 值标签), 不再整卡 rebind。
    m_host->setValueBinder([this](int row, QWidget* w) {
        const auto& measures = m_doc->measureVars();
        const auto& angles = m_doc->angleMeasures();
        const int m = static_cast<int>(measures.size());
        if (row < 0 || row >= m + static_cast<int>(angles.size()))
            return;
        if (row < m) {
            auto* card = static_cast<MeasureCard*>(w);
            card->refreshValue(measures[row].value, measures[row].dangling);
        } else {
            const int ai = row - m;
            auto* card = static_cast<AngleMeasureCard*>(w);
            card->refreshValue(angles[ai].value, angles[ai].dangling);
        }
    });
}

void MeasureTab::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
}

void MeasureTab::notifyMeasureDataChanged()
{
    m_metaDirty = true;
    sync();
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

    if (m_metaDirty) {
        const bool structuralChanged = (keys != m_lastKeys);
        const bool metaChanged = (measureMetaSignature(m_doc) != m_lastMetaSignature);

        m_host->setRows(keys);

        // VirtualCardList only runs value-level refresh when the key list is
        // unchanged. Metadata edits (name/comment/ref/source/kind) therefore
        // need an explicit full rebind so dialogs outside the card (e.g. the
        // measure-result dialog) immediately update the visible card.
        if (metaChanged && !structuralChanged)
            m_host->rebindAll();

        m_lastKeys = keys;
        m_lastMetaSignature = measureMetaSignature(m_doc);
        m_metaDirty = false;
    } else {
        // Fast path: structure/metadata unchanged, only values may have moved.
        m_host->setRows(keys);
    }

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
