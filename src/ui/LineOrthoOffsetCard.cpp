#include "ui/LineOrthoOffsetCard.h"

#include <cmath>
#include <numbers>
#include <algorithm>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {

namespace {

constexpr int kLabelW = 64;
constexpr int kFieldH = 30;

ElaLineEdit* makeCompactEdit(QWidget* parent, int width)
{
    auto* e = new ElaLineEdit(parent);
    e->setFixedHeight(kFieldH);
    e->setMaximumWidth(width);
    e->setStyleSheet(QStringLiteral("font-size: 11px;"));
    return e;
}

} // namespace

LineOrthoOffsetCard::LineOrthoOffsetCard(QWidget* parent)
    : QWidget(parent)
{
    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = QStringLiteral("font-size: 11px; font-family: 'Consolas', monospace; color: %1;")
        .arg(cad::ui::Theme::tokens().text2.name());

    auto* orthoLayout = new QHBoxLayout(this);
    orthoLayout->setContentsMargins(0, 0, 0, 0);
    orthoLayout->setSpacing(6);

    auto* lblOrtho = new ElaText(QString::fromUtf8("拐角偏置"), 11, this);
    lblOrtho->setFixedWidth(kLabelW);
    lblOrtho->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("正交拐角偏置"),
        QStringLiteral("沿主轴走基准长度，向左或向右折 90° 偏置，端点直连拐点（省道构造）")));
    orthoLayout->addWidget(lblOrtho);

    m_lblOrthoFx = new ElaText(
        QStringLiteral("<i style='color:%1;'>fx</i>")
            .arg(cad::ui::Theme::tokens().text2.name()),
        11, this);
    m_lblOrthoFx->setVisible(false);
    m_lblOrthoFx->setFixedWidth(18);
    orthoLayout->addWidget(m_lblOrthoFx);

    m_editOrthoDist = makeCompactEdit(this, 110);
    m_editOrthoDist->setObjectName(QStringLiteral("editOrthoDist"));
    m_editOrthoDist->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
    connect(m_editOrthoDist, &QLineEdit::editingFinished, this, &LineOrthoOffsetCard::onOrthoDistEdited);
    orthoLayout->addWidget(m_editOrthoDist);

    m_btnOrthoNone = new QPushButton(QString::fromUtf8("无"), this);
    m_btnOrthoNone->setCheckable(true);
    m_btnOrthoNone->setChecked(true);
    m_btnOrthoNone->setFixedSize(36, kFieldH);
    m_btnOrthoNone->setStyleSheet(chips);
    m_btnOrthoNone->setCursor(Qt::PointingHandCursor);

    m_btnOrthoLeft = new QPushButton(QString::fromUtf8("左"), this);
    m_btnOrthoLeft->setCheckable(true);
    m_btnOrthoLeft->setFixedSize(36, kFieldH);
    m_btnOrthoLeft->setStyleSheet(chips);
    m_btnOrthoLeft->setCursor(Qt::PointingHandCursor);

    m_btnOrthoRight = new QPushButton(QString::fromUtf8("右"), this);
    m_btnOrthoRight->setCheckable(true);
    m_btnOrthoRight->setFixedSize(36, kFieldH);
    m_btnOrthoRight->setStyleSheet(chips);
    m_btnOrthoRight->setCursor(Qt::PointingHandCursor);

    m_orthoDirGroup = new QButtonGroup(this);
    m_orthoDirGroup->setObjectName(QStringLiteral("orthoDirGroup"));
    m_orthoDirGroup->addButton(m_btnOrthoNone, 0);
    m_orthoDirGroup->addButton(m_btnOrthoLeft, 1);
    m_orthoDirGroup->addButton(m_btnOrthoRight, 2);
    connect(m_orthoDirGroup, &QButtonGroup::idClicked, this, &LineOrthoOffsetCard::onOrthoDirChanged);

    orthoLayout->addWidget(m_btnOrthoNone);
    orthoLayout->addWidget(m_btnOrthoLeft);
    orthoLayout->addWidget(m_btnOrthoRight);

    m_btnToggleAxis = new QPushButton(QString::fromUtf8("👁 基准轴"), this);
    m_btnToggleAxis->setObjectName(QStringLiteral("btnToggleAxis"));
    m_btnToggleAxis->setCheckable(true);
    m_btnToggleAxis->setChecked(true);
    m_btnToggleAxis->setFixedHeight(kFieldH);
    m_btnToggleAxis->setStyleSheet(chips);
    m_btnToggleAxis->setCursor(Qt::PointingHandCursor);
    m_btnToggleAxis->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("基准中心轴虚线"),
        QStringLiteral("在画布上显示/隐藏从起点到主轴拐点的中心参考虚线")));
    connect(m_btnToggleAxis, &QPushButton::clicked, this, &LineOrthoOffsetCard::onToggleCenterAxis);
    orthoLayout->addWidget(m_btnToggleAxis);

    m_lblOrthoHypot = new ElaText(QString(), 11, this);
    m_lblOrthoHypot->setObjectName(QStringLiteral("lblOrthoHypot"));
    m_lblOrthoHypot->setStyleSheet(dimMono);
    m_lblOrthoHypot->setVisible(false);
    orthoLayout->addWidget(m_lblOrthoHypot);

    orthoLayout->addStretch();
    setVisible(false);
}

void LineOrthoOffsetCard::setContext(cad::param::ParamDocument* doc, const QUuid& blockId, const QUuid& segmentId)
{
    m_paramDoc = doc;
    m_blockId = blockId;
    m_segmentId = segmentId;
}

void LineOrthoOffsetCard::populate(const cad::param::Block& block, const cad::param::Segment& seg, bool isCurve)
{
    const auto* ep = block.findPoint(seg.endPointId);
    setVisible(!isCurve);

    if (!isCurve && ep && ep->constraint == cad::param::PointConstraint::OrthoOffset) {
        const QSignalBlocker bOrtho(m_editOrthoDist);
        const QSignalBlocker bGroup(m_orthoDirGroup);
        const QSignalBlocker bAxis(m_btnToggleAxis);

        if (!ep->orthoOffsetDistFormula.isEmpty()) {
            m_editOrthoDist->setText(ep->orthoOffsetDistFormula);
            m_lblOrthoFx->setVisible(true);
        } else {
            m_lblOrthoFx->setVisible(false);
            double distCm = cad::geo::Units::mmToCm(std::abs(ep->orthoOffsetDist));
            m_editOrthoDist->setText(cad::geo::Units::formatNumberTrimmed(distCm));
        }

        if (std::abs(ep->orthoOffsetDist) < 1e-6 && ep->orthoOffsetDistFormula.isEmpty()) {
            m_btnOrthoNone->setChecked(true);
        } else if (ep->orthoOffsetDist < 0.0 || ep->orthoOffsetDistFormula.startsWith(QLatin1Char('-'))) {
            m_btnOrthoRight->setChecked(true);
        } else {
            m_btnOrthoLeft->setChecked(true);
        }

        if (m_btnToggleAxis) {
            m_btnToggleAxis->setChecked(seg.showOrthoAxis);
            m_btnToggleAxis->setVisible(true);
        }
    } else {
        const QSignalBlocker bOrtho(m_editOrthoDist);
        const QSignalBlocker bGroup(m_orthoDirGroup);
        m_editOrthoDist->clear();
        m_lblOrthoFx->setVisible(false);
        m_btnOrthoNone->setChecked(true);
        if (m_btnToggleAxis) m_btnToggleAxis->setVisible(false);
    }

    refreshHypotLabel(block, seg);
}

void LineOrthoOffsetCard::refreshHypotLabel(const cad::param::Block& block, const cad::param::Segment& seg)
{
    const auto* ep = block.findPoint(seg.endPointId);
    if (ep && ep->constraint == cad::param::PointConstraint::OrthoOffset &&
        (std::abs(ep->orthoOffsetDist) > 1e-6 || !ep->orthoOffsetDistFormula.isEmpty())) {
        const double baseMm = ep->distance;
        const double offsetMm = std::abs(ep->orthoOffsetDist);
        const double hypotMm = std::sqrt(baseMm * baseMm + offsetMm * offsetMm);
        m_lblOrthoHypot->setText(QString::fromUtf8("斜: %1").arg(cad::geo::Units::formatLength(hypotMm)));
        m_lblOrthoHypot->setVisible(true);
    } else {
        m_lblOrthoHypot->setVisible(false);
    }
}

void LineOrthoOffsetCard::apply(cad::param::Block* block, cad::param::Segment* seg)
{
    if (!block || !seg) return;
    auto* ep = block->findPoint(seg->endPointId);
    if (!ep) return;

    const int dirId = m_orthoDirGroup->checkedId();
    if (dirId > 0) {
        // 首次开启拐角偏置时锁死当前几何基准角与基准长
        if (ep->constraint != cad::param::PointConstraint::OrthoOffset) {
            const auto* sp = block->findPoint(seg->startPointId);
            if (sp && sp->resolved && ep->resolved) {
                cad::geo::Vec2 delta = ep->resolvedPos - sp->resolvedPos;
                double curLen = delta.length();
                double curAngleDeg = std::atan2(delta.y, delta.x) * 180.0 / std::numbers::pi;
                if (!ep->refSegmentId.isNull()) {
                    if (const auto* rseg = block->findSegment(ep->refSegmentId)) {
                        const auto* rsp = block->findPoint(rseg->startPointId);
                        const auto* rep = block->findPoint(rseg->endPointId);
                        if (rsp && rep && rsp->resolved && rep->resolved) {
                            cad::geo::Vec2 rdir = rep->resolvedPos - rsp->resolvedPos;
                            double rdeg = std::atan2(rdir.y, rdir.x) * 180.0 / std::numbers::pi;
                            curAngleDeg = cad::geo::normalizeDeg180(curAngleDeg - rdeg);
                        }
                    }
                }
                ep->angle = curAngleDeg;
                ep->angleFormula.clear();
                ep->distance = curLen;
                ep->distanceFormula.clear();
            }
        }
        ep->constraint = cad::param::PointConstraint::OrthoOffset;
        if (ep->refPointId.isNull()) {
            ep->refPointId = seg->startPointId;
        }

        const auto parsedOrtho = cad::geo::parseNumberOrFormula(m_editOrthoDist->text());
        double sign = (dirId == 2) ? -1.0 : 1.0; // 1=左(正), 2=右(负)
        if (parsedOrtho.isNumber) {
            ep->orthoOffsetDist = sign * std::abs(cad::geo::Units::cmToMm(parsedOrtho.value));
            ep->orthoOffsetDistFormula.clear();
        } else if (!parsedOrtho.formula.isEmpty()) {
            ep->orthoOffsetDistFormula = (dirId == 2)
                ? QStringLiteral("-(%1)").arg(parsedOrtho.formula)
                : parsedOrtho.formula;
        }

        seg->showOrthoAxis = m_btnToggleAxis ? m_btnToggleAxis->isChecked() : true;
    } else if (ep->constraint == cad::param::PointConstraint::OrthoOffset) {
        ep->constraint = cad::param::PointConstraint::Polar;
        ep->orthoOffsetDist = 0.0;
        ep->orthoOffsetDistFormula.clear();
    }
}

void LineOrthoOffsetCard::onOrthoDistEdited()
{
    if (!m_editOrthoDist->text().trimmed().isEmpty() && m_orthoDirGroup->checkedId() == 0) {
        const QSignalBlocker b(m_orthoDirGroup);
        m_btnOrthoLeft->setChecked(true);
    }
    emit orthoChanged();
}

void LineOrthoOffsetCard::onOrthoDirChanged(int /*id*/)
{
    emit orthoChanged();
}

void LineOrthoOffsetCard::onToggleCenterAxis()
{
    if (!m_paramDoc) return;
    auto* b = m_paramDoc->findBlock(m_blockId);
    if (!b) return;
    auto* s = b->findSegment(m_segmentId);
    if (!s) return;

    s->showOrthoAxis = m_btnToggleAxis ? m_btnToggleAxis->isChecked() : !s->showOrthoAxis;
    b->touchGeometry();

    emit liveUpdated();
    emit sceneRefreshRequested();
}

} // namespace cad::ui
