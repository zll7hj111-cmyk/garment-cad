#include "SegmentAnchorTab.h"

#include <cmath>

#include "ElaTabWidget.h"
#include "ElaTabWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ElaText.h"
#include <QListWidget>
#include "ElaComboBox.h"
#include "ElaDoubleSpinBox.h"
#include "ElaCheckBox.h"
#include "ElaPushButton.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"

namespace cad::tools {

SegmentAnchorTab::SegmentAnchorTab(cad::param::ParamDocument* doc,
                                   const std::function<void()>& sceneRefresh,
                                   QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(doc)
    , m_sceneRefresh(sceneRefresh)
{
    // Canvas→Panel sync: when the document resolves (e.g. handle dragged on
    // canvas), refresh the anchor edit fields so the panel tracks the canvas.
    connect(m_paramDoc, &cad::param::ParamDocument::resolved, this, [this]() {
        if (!m_anchorList || m_anchorPointIds.empty()) return;
        const int row = m_anchorList->currentRow();
        if (row >= 0)
            refreshFields(row);
    });
}

void SegmentAnchorTab::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
}

void SegmentAnchorTab::build(ElaTabWidget* tabs)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(6);

    layout->addWidget(new ElaText(QString::fromUtf8("锚点列表（起点 + 曲线点 + 终点）:"), 13, this));

    m_anchorList = new QListWidget(this);
    m_anchorList->setMaximumHeight(100);
    layout->addWidget(m_anchorList);

    // --- Tangent mode row ---
    auto* tanRow = new QHBoxLayout();
    tanRow->addWidget(new ElaText(QString::fromUtf8("切线模式:"), 13, this));
    m_cmbTanMode = new ElaComboBox(this);
    m_cmbTanMode->addItem(QString::fromUtf8("自动 (C2)"));
    m_cmbTanMode->addItem(QString::fromUtf8("手动 (Bézier)"));
    tanRow->addWidget(m_cmbTanMode, 1);
    layout->addLayout(tanRow);

    // --- Tangent-In card ---
    auto* inRow = new QHBoxLayout();
    inRow->addWidget(new ElaText(QString::fromUtf8("入切线角(°):"), 13, this));
    m_spinTanAngleIn = new ElaDoubleSpinBox(this);
    m_spinTanAngleIn->setRange(-360.0, 360.0);
    m_spinTanAngleIn->setDecimals(1);
    m_spinTanAngleIn->setSuffix(QString::fromUtf8("°"));
    inRow->addWidget(m_spinTanAngleIn, 1);
    inRow->addWidget(new ElaText(QString::fromUtf8("长度:"), 13, this));
    m_spinTanLenIn = new ElaDoubleSpinBox(this);
    m_spinTanLenIn->setRange(0.0, 999.0);
    m_spinTanLenIn->setDecimals(2);
    m_spinTanLenIn->setSuffix(QStringLiteral(" cm"));
    inRow->addWidget(m_spinTanLenIn, 1);
    layout->addLayout(inRow);

    // --- Tangent-Out card ---
    auto* outRow = new QHBoxLayout();
    outRow->addWidget(new ElaText(QString::fromUtf8("出切线角(°):"), 13, this));
    m_spinTanAngleOut = new ElaDoubleSpinBox(this);
    m_spinTanAngleOut->setRange(-360.0, 360.0);
    m_spinTanAngleOut->setDecimals(1);
    m_spinTanAngleOut->setSuffix(QString::fromUtf8("°"));
    outRow->addWidget(m_spinTanAngleOut, 1);
    outRow->addWidget(new ElaText(QString::fromUtf8("长度:"), 13, this));
    m_spinTanLenOut = new ElaDoubleSpinBox(this);
    m_spinTanLenOut->setRange(0.0, 999.0);
    m_spinTanLenOut->setDecimals(2);
    m_spinTanLenOut->setSuffix(QStringLiteral(" cm"));
    outRow->addWidget(m_spinTanLenOut, 1);
    layout->addLayout(outRow);

    // --- Smooth/Corner ---
    auto* optRow = new QHBoxLayout();
    m_chkTanLocked = new ElaCheckBox(QString::fromUtf8("平滑(共线)"), this);
    m_chkTanLocked->setChecked(true);
    optRow->addWidget(m_chkTanLocked);
    optRow->addStretch();
    layout->addLayout(optRow);

    // --- Follow info + release button (curve points only) ---
    auto* followRow = new QHBoxLayout();
    m_lblFollowInfo = new ElaText(QString(), 13, this);
    m_lblFollowInfo->setObjectName(QStringLiteral("mutedText"));
    m_lblFollowInfo->setWordWrap(true);
    followRow->addWidget(m_lblFollowInfo, 1);
    m_btnReleaseFollow = new ElaPushButton(QString::fromUtf8("释放"), this);
    m_btnReleaseFollow->setCursor(Qt::PointingHandCursor);
    m_btnReleaseFollow->setMaximumWidth(60);
    followRow->addWidget(m_btnReleaseFollow);
    layout->addLayout(followRow);
    connect(m_btnReleaseFollow, &QPushButton::clicked, this, [this]() {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        const int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
        if (!pt) return;
        // Release the follow connection.
        pt->followBlockId = QUuid();
        pt->followPointId = QUuid();
        pt->followOffset = cad::geo::Vec2::zero();
        m_paramDoc->resolveAll();
        m_sceneRefresh();
        refreshFields(row);
    });

    // Reset button
    m_btnResetTan = new ElaPushButton(QString::fromUtf8("重置为自动切线"), this);
    m_btnResetTan->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_btnResetTan);

    // Info label
    m_lblTanInfo = new ElaText(QString(), 13, this);
    m_lblTanInfo->setObjectName(QStringLiteral("mutedText"));
    m_lblTanInfo->setWordWrap(true);
    layout->addWidget(m_lblTanInfo);

    layout->addStretch();

    // --- Signals ---
    connect(m_cmbTanMode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        const int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
        if (!pt) return;
        pt->autoTangent = (idx == 0);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        m_sceneRefresh();
    });
    connect(m_btnResetTan, &QPushButton::clicked, this, [this] {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        const int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
        if (!pt) return;
        pt->autoTangent = true;
        pt->tangentIn = cad::geo::Vec2::zero();
        pt->tangentOut = cad::geo::Vec2::zero();
        m_cmbTanMode->setCurrentIndex(0);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        m_sceneRefresh();
    });
    connect(m_chkTanLocked, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        const int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
        if (!pt) return;
        pt->tangentLocked = on;
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        m_sceneRefresh();
    });
    // Tangent angle/length spin boxes → apply to model
    auto applyTanSpin = [this]() {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        if (!seg) return;
        const int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
        if (!pt) return;
        // Editing switches to manual mode.
        if (pt->autoTangent) {
            pt->autoTangent = false;
            m_cmbTanMode->setCurrentIndex(1);
        }
        // Chord direction for relative angle computation.
        const auto* sp = block->findPoint(seg->startPointId);
        const auto* ep = block->findPoint(seg->endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) return;
        const cad::geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
        const double chordAngle = std::atan2(chord.y, chord.x) * 180.0 / M_PI;
        // Tangent-in: angle relative to chord, length in cm→mm.
        const double angIn = m_spinTanAngleIn->value() + chordAngle;
        const double lenIn = cad::geo::Units::cmToMm(m_spinTanLenIn->value());
        const double radIn = angIn * M_PI / 180.0;
        pt->tangentIn = cad::geo::Vec2(std::cos(radIn) * lenIn, std::sin(radIn) * lenIn);
        // Tangent-out.
        const double angOut = m_spinTanAngleOut->value() + chordAngle;
        const double lenOut = cad::geo::Units::cmToMm(m_spinTanLenOut->value());
        const double radOut = angOut * M_PI / 180.0;
        pt->tangentOut = cad::geo::Vec2(std::cos(radOut) * lenOut, std::sin(radOut) * lenOut);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        m_sceneRefresh();
    };
    connect(m_spinTanAngleIn, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanLenIn, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanAngleOut, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanLenOut, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    // Selection change → refresh edit fields
    connect(m_anchorList, &QListWidget::currentRowChanged, this, [this](int row) {
        refreshFields(row);
    });

    tabs->addTab(this, QString::fromUtf8("锚点"));
}

void SegmentAnchorTab::populateList(const cad::param::Block* block,
                                    const cad::param::Segment* seg)
{
    if (!m_anchorList) return;
    m_anchorList->clear();
    m_anchorPointIds.clear();

    // Start point
    const auto* sp = block->findPoint(seg->startPointId);
    const QString spLabel = sp ? (sp->name.isEmpty() ? sp->serial : sp->name) : QStringLiteral("?");
    m_anchorList->addItem(QString::fromUtf8("起点 ") + spLabel);
    m_anchorPointIds.push_back(seg->startPointId);
    // Pass points
    for (const auto& ppId : seg->passPointIds) {
        const auto* pp = block->findPoint(ppId);
        const QString label = pp ? (pp->name.isEmpty() ? pp->serial : pp->name) : QStringLiteral("?");
        m_anchorList->addItem(QString::fromUtf8("曲线点 ") + label);
        m_anchorPointIds.push_back(ppId);
    }
    // End point
    const auto* ep = block->findPoint(seg->endPointId);
    const QString epLabel = ep ? (ep->name.isEmpty() ? ep->serial : ep->name) : QStringLiteral("?");
    m_anchorList->addItem(QString::fromUtf8("终点 ") + epLabel);
    m_anchorPointIds.push_back(seg->endPointId);
    if (m_anchorList->count() > 0)
        m_anchorList->setCurrentRow(0);
}

void SegmentAnchorTab::refreshFields(int row)
{
    if (!m_paramDoc || row < 0 || row >= static_cast<int>(m_anchorPointIds.size()))
        return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;
    auto* pt = block->findPoint(m_anchorPointIds[static_cast<size_t>(row)]);
    if (!pt) return;

    // Block signals while populating to avoid feedback loops.
    const bool wasBlocked = m_spinTanAngleIn->blockSignals(true);
    m_spinTanLenIn->blockSignals(true);
    m_spinTanAngleOut->blockSignals(true);
    m_spinTanLenOut->blockSignals(true);
    m_chkTanLocked->blockSignals(true);
    m_cmbTanMode->blockSignals(true);

    m_cmbTanMode->setCurrentIndex(pt->autoTangent ? 0 : 1);
    m_chkTanLocked->setChecked(pt->tangentLocked);

    // Compute tangent angles relative to the chord direction.
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    double chordAngle = 0.0;
    if (sp && ep && sp->resolved && ep->resolved) {
        const cad::geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
        chordAngle = std::atan2(chord.y, chord.x) * 180.0 / M_PI;
    }

    // Get effective tangents: stored manual values, or C2 auto-solved values.
    cad::geo::Vec2 tanIn = pt->tangentIn;
    cad::geo::Vec2 tanOut = pt->tangentOut;
    if (pt->autoTangent && sp && ep && sp->resolved && ep->resolved) {
        // Replicate the C2 solve to show the auto-computed values.
        std::vector<cad::geo::Vec2> pts;
        std::vector<bool> isAuto;
        std::vector<cad::geo::Vec2> tIn, tOut;
        int myIndex = -1;
        auto addPt = [&](const cad::param::ParamPoint* p) {
            if (p->id == pt->id) myIndex = static_cast<int>(pts.size());
            pts.push_back(p->resolvedPos);
            isAuto.push_back(p->autoTangent);
            tIn.push_back(p->tangentIn);
            tOut.push_back(p->tangentOut);
        };
        addPt(sp);
        for (const auto& ppId : seg->passPointIds) {
            const auto* pp = block->findPoint(ppId);
            if (pp && pp->resolved) addPt(pp);
        }
        addPt(ep);
        if (myIndex >= 0 && pts.size() >= 2) {
            auto c2 = cad::geo::solveC2Tangents(pts, isAuto, tIn, tOut);
            tanIn = c2[static_cast<size_t>(myIndex)];
            tanOut = c2[static_cast<size_t>(myIndex)];
        }
    }

    // Always show values and keep editable (editing switches to manual).
    const double angIn = std::atan2(tanIn.y, tanIn.x) * 180.0 / M_PI;
    double relAngIn = angIn - chordAngle;
    while (relAngIn > 180.0) relAngIn -= 360.0;
    while (relAngIn < -180.0) relAngIn += 360.0;
    m_spinTanAngleIn->setValue(tanIn.lengthSquared() > 1e-12 ? relAngIn : 0.0);
    m_spinTanLenIn->setValue(cad::geo::Units::mmToCm(tanIn.length()));

    const double angOut = std::atan2(tanOut.y, tanOut.x) * 180.0 / M_PI;
    double relAngOut = angOut - chordAngle;
    while (relAngOut > 180.0) relAngOut -= 360.0;
    while (relAngOut < -180.0) relAngOut += 360.0;
    m_spinTanAngleOut->setValue(tanOut.lengthSquared() > 1e-12 ? relAngOut : 0.0);
    m_spinTanLenOut->setValue(cad::geo::Units::mmToCm(tanOut.length()));

    m_spinTanAngleIn->setEnabled(true);
    m_spinTanLenIn->setEnabled(true);
    m_spinTanAngleOut->setEnabled(true);
    m_spinTanLenOut->setEnabled(true);

    // Follow info (only for CurveAnchor pass points).
    if (pt->constraint == cad::param::PointConstraint::CurveAnchor
        && !pt->followPointId.isNull()) {
        const auto* fBlk = m_paramDoc->findBlock(pt->followBlockId);
        const auto* fPt = fBlk ? fBlk->findPoint(pt->followPointId) : nullptr;
        if (fPt) {
            const QString fLabel = fPt->name.isEmpty() ? fPt->serial : fPt->name;
            m_lblFollowInfo->setText(
                QString::fromUtf8("跟随连接: %1").arg(fLabel));
        } else {
            m_lblFollowInfo->setText(QString::fromUtf8("跟随连接: (无效)"));
        }
        m_lblFollowInfo->setVisible(true);
        if (m_btnReleaseFollow) m_btnReleaseFollow->setVisible(true);
    } else {
        m_lblFollowInfo->setVisible(false);
        if (m_btnReleaseFollow) m_btnReleaseFollow->setVisible(false);
    }

    // Info text.
    m_lblTanInfo->setText(pt->autoTangent
        ? QString::fromUtf8("切线由 C2 算法自动计算。修改数值将切换为手动模式。")
        : QString::fromUtf8("切线已手动覆盖。拖动手柄或重置为自动以恢复平滑。"));

    m_spinTanAngleIn->blockSignals(wasBlocked);
    m_spinTanLenIn->blockSignals(wasBlocked);
    m_spinTanAngleOut->blockSignals(wasBlocked);
    m_spinTanLenOut->blockSignals(wasBlocked);
    m_chkTanLocked->blockSignals(wasBlocked);
    m_cmbTanMode->blockSignals(wasBlocked);
}

} // namespace cad::tools
