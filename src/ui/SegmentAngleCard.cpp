#include "ui/SegmentAngleCard.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentAngleCommands.h"
#include "document/commands/SegmentPropertyCommands.h"
#include "canvas/CanvasScene.h"   // showToast (公式拒绝切换反馈)
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/FormScaffold.h"

namespace cad::ui {

namespace {
constexpr int kLabelW = 64;   ///< 标签列定宽 (2026-12 去卡框化: 短词列).
constexpr int kFieldH = 30;   ///< 2026-xx 紧凑化 (35→30, 与状态栏对齐).

/// 捕获编辑条命令的完整状态快照 (段名/长度/终点 Polar/跟随角) —— 供
/// applyAngle 自由线分支以「模型现态 + 角度覆盖」构造入栈命令, 非角度
/// 字段原样重放, 命令无副作用。
cad::cmd::SegmentEditBarCommand::State captureEditStripState(
    cad::param::ParamDocument& doc, const QUuid& blockId, const QUuid& segmentId)
{
    cad::cmd::SegmentEditBarCommand::State s;
    auto* b = doc.findBlock(blockId);
    auto* seg = b ? b->findSegment(segmentId) : nullptr;
    if (!b || !seg) return s;
    s.segName = seg->name;
    s.lengthFormula = seg->lengthFormula;
    if (auto* ep = b->findPoint(seg->endPointId)) {
        s.endDistance = ep->distance;
        s.endDistanceFormula = ep->distanceFormula;
        s.endAngle = ep->angle;
        s.endAngleFormula = ep->angleFormula;
        s.endConstraint = static_cast<int>(ep->constraint);
        s.endRefPointId = ep->refPointId;
        if (ep->constraint == cad::param::PointConstraint::OrthoOffset) {
            s.orthoOffsetDist = ep->orthoOffsetDist;
            s.orthoOffsetDistFormula = ep->orthoOffsetDistFormula;
        }
    }
    for (const auto& a : doc.attachments()) {
        if (a.isPin || a.fromBlockId != blockId) continue;
        s.attId = a.id;
        s.followerAngle = a.followerAngle;
        s.followerAngleFormula = a.followerAngleFormula;
        s.arcLength = a.arcLength;
        s.arcLengthFormula = a.arcLengthFormula;
        s.chordLength = a.chordLength;
        s.chordLengthFormula = a.chordLengthFormula;
        s.rotationMode = static_cast<int>(a.rotationMode);
        break;
    }
    return s;
}
} // namespace

SegmentAngleCard::SegmentAngleCard(cad::param::ParamDocument* doc,
                                   CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // 纯行组: 无边框/无底色 (与相邻行一致, 不做"卡中卡")。
    setStyleSheet(QStringLiteral(
        "SegmentAngleCard { background: transparent; border: none; }"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    // ── 第一行: 角度编辑 (原有行) ──
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_lblCaption = new ElaText(QString::fromUtf8("角度"), 11, this);
    m_lblCaption->setFixedWidth(kLabelW);
    row->addWidget(m_lblCaption);
    m_lblFxAngle = new ElaText(
        QStringLiteral("<i style='color:%1;'>fx</i>")
            .arg(cad::ui::Theme::tokens().text2.name()),
        11, this);
    m_lblFxAngle->setVisible(false);
    m_lblFxAngle->setFixedWidth(18);
    row->addWidget(m_lblFxAngle);
    m_editAngle = new ElaLineEdit(this);
    m_editAngle->setFixedWidth(150);
    m_editAngle->setFixedHeight(kFieldH);
    m_editAngle->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
    m_editAngle->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("线段角度"),
        QStringLiteral("自由线：世界绝对角度；跟随线：相对基准线的构造角。逆时针为正，回车确认")));
    row->addWidget(m_editAngle);
    m_lblFollowValue = new ElaText(QString(), 11, this);
    m_lblFollowValue->setObjectName(QStringLiteral("followValueLabel"));
    // 实例样式表必须带 background:transparent —— 否则替换 ElaText 的透明
    // 背景规则, 细字抗锯齿混成灰 (「盖滤镜」, 同分区标题坑)。
    m_lblFollowValue->setStyleSheet(
        QStringLiteral("font-size:11px; background:transparent;"));
    m_lblFollowValue->setVisible(false);
    row->addWidget(m_lblFollowValue);
    m_lblWorldAngle = new ElaText(QString(), 11, this);
    m_lblWorldAngle->setStyleSheet(
        QStringLiteral("font-size:11px; background:transparent;"));
    m_lblWorldAngle->setVisible(false);
    row->addWidget(m_lblWorldAngle);
    m_btnAngleMode = new QPushButton(this);
    m_btnAngleMode->setFixedSize(30, kFieldH);
    m_btnAngleMode->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnAngleMode->setCursor(Qt::PointingHandCursor);
    m_btnAngleMode->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("切换度数/弧长"),
        QStringLiteral("在角度制（°）与对应基准线弧长制（⌒ mm）之间切换显示与输入模式")));
    row->addWidget(m_btnAngleMode);
    row->addStretch();
    col->addLayout(row);

    // (2026-xx §3) 旧「角度基准 P1→P2」读数行已删 —— 对齐锚点 [P1] 与基准
    // 句式由 SegmentRefCard 承担 (换向由朝向箭头表达)。

    connect(m_editAngle, &QLineEdit::textChanged,
            this, &SegmentAngleCard::onAngleDirty);
    connect(m_editAngle, &QLineEdit::editingFinished,
            this, &SegmentAngleCard::applyAngle);
    connect(m_btnAngleMode, &QPushButton::clicked,
            this, &SegmentAngleCard::onModeToggle);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &SegmentAngleCard::onDocResolved);
}

void SegmentAngleCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    populateAngleField();
    refresh();
}

const cad::param::Attachment* SegmentAngleCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

void SegmentAngleCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->blocksView().byId(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    const auto* att = findFollowerAttachment();
    const bool hasAtt = att != nullptr;

    m_lblFollowValue->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);

    // 指向生效/桥接线: 角度由约束决定 → 灰只读 (启用态由下方分支管理)。
    const bool angleGray = (block && !block->endTargetPointId.isNull())
        || (block && block->isBridge);
    m_editAngle->setEnabled(!angleGray);
    m_btnAngleMode->setEnabled(hasAtt && !att->angleIndependent && !angleGray);

    if (!hasAtt) {
        m_btnAngleMode->setText(QStringLiteral("∠"));
        m_lblCaption->setText(QString::fromUtf8("角度"));
        m_lblCaption->setStyleSheet(QString());
        // 自由线世界方向提示 (0~360° 逆时针为正)。
        if (block && seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                double deg = 0.0;
                if (ep->constraint == cad::param::PointConstraint::OrthoOffset) {
                    const double rotDeg = block ? block->transform.rotation * 180.0 / M_PI : 0.0;
                    deg = cad::geo::normalizeDeg360(ep->angle + rotDeg);
                } else {
                    const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                    const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                    deg = cad::geo::normalizeDeg360(
                        std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI);
                }
                const QString text = QString::fromUtf8("= 世界角度 %1°")
                    .arg(cad::geo::Units::formatDegValue(deg));
                if (m_lblWorldAngle->text() != text)
                    m_lblWorldAngle->setText(text);
                m_lblWorldAngle->setVisible(true);
            }
        }
        // 公式时显示当前计算值。
        const cad::param::ParamPoint* epFree =
            seg ? block->findPoint(seg->endPointId) : nullptr;
        const cad::param::ParamPoint* spFree =
            seg ? block->findPoint(seg->startPointId) : nullptr;
        const auto* driven = (epFree && !epFree->angleFormula.isEmpty()) ? epFree
            : ((spFree && !spFree->angleFormula.isEmpty()) ? spFree : epFree);
        if (driven && !driven->angleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                driven->angleFormula, m_doc->parameters(), {});
            if (r.ok) {
                const double rotDeg = block ? block->transform.rotation * 180.0 / M_PI : 0.0;
                const double deg = cad::geo::normalizeDeg360(r.value + rotDeg);
                m_lblFollowValue->setText(QString::fromUtf8("= %1°")
                    .arg(cad::geo::Units::formatDegValue(deg)));
                m_lblFollowValue->setVisible(true);
            }
            m_lblFxAngle->setVisible(true);
        }
        return;
    }

    if (att->angleIndependent) {
        // 独立角度: 位置吸附保持、角度不跟随 (世界方向提示)。
        m_btnAngleMode->setText(QStringLiteral("∠"));
        m_lblCaption->setText(QString::fromUtf8("独立角"));
        m_lblCaption->setStyleSheet(QString());
        if (block && seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                const double deg = cad::geo::normalizeDeg360(
                    std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI);
                const QString text = QString::fromUtf8("= 世界角度 %1°")
                    .arg(cad::geo::Units::formatDegValue(deg));
                if (m_lblWorldAngle->text() != text)
                    m_lblWorldAngle->setText(text);
                m_lblWorldAngle->setVisible(true);
            }
            const auto* driven = (ep && !ep->angleFormula.isEmpty()) ? ep
                : ((sp && !sp->angleFormula.isEmpty()) ? sp : ep);
            if (driven && !driven->angleFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    driven->angleFormula, m_doc->parameters(), {});
                if (r.ok) {
                    const double rotDeg = block ? block->transform.rotation * 180.0 / M_PI : 0.0;
                    const double deg = cad::geo::normalizeDeg360(r.value + rotDeg);
                    m_lblFollowValue->setText(QString::fromUtf8("= %1°")
                        .arg(cad::geo::Units::formatDegValue(deg)));
                    m_lblFollowValue->setVisible(true);
                }
                m_lblFxAngle->setVisible(true);
            }
        }
        return;
    }

    if (att->rotationMode == cad::param::RotationMode::ArcLength) {
        m_btnAngleMode->setText(QStringLiteral("⌒"));
        m_lblCaption->setText(QString::fromUtf8("弧长"));
        m_lblCaption->setStyleSheet(QString());
        double arcMm = att->arcLength;
        if (!att->arcLengthFormula.isEmpty()) {
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                att->arcLengthFormula, m_doc->parameters(), m_doc->conditions(), arcMm);
            const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
            const double alphaDeg = (radius > 1e-9)
                ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
            const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
            m_lblFollowValue->setText(QString::fromUtf8("= %1 cm")
                .arg(cad::geo::Units::formatNumberTrimmed(
                    cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius)))));
            m_lblFollowValue->setVisible(true);
            m_lblFxAngle->setVisible(true);
        }
        updateWorldAngleLabel(*att);
        return;
    }

    if (att->rotationMode == cad::param::RotationMode::ChordLength) {
        m_btnAngleMode->setText(QStringLiteral("↔"));
        m_lblCaption->setText(QString::fromUtf8("开度"));
        m_lblCaption->setStyleSheet(QString());
        double chordMm = att->chordLength;
        if (!att->chordLengthFormula.isEmpty()) {
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                att->chordLengthFormula, m_doc->parameters(), m_doc->conditions(), chordMm);
            const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
            const double alphaDeg = (radius > 1e-9)
                ? cad::geo::chordMmToDeg(chordMm, radius) : 0.0;
            const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
            m_lblFollowValue->setText(QString::fromUtf8("= %1 cm")
                .arg(cad::geo::Units::formatNumberTrimmed(
                    cad::geo::Units::mmToCm(cad::geo::degToChordMm(foldDeg, radius)))));
            m_lblFollowValue->setVisible(true);
            m_lblFxAngle->setVisible(true);
        }
        updateWorldAngleLabel(*att);
        return;
    }

    m_btnAngleMode->setText(QStringLiteral("∠"));
    m_lblCaption->setText(QString::fromUtf8("跟随角"));
    m_lblCaption->setStyleSheet(QString());
    double constDeg = att->followerAngle;
    if (!att->followerAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            att->followerAngleFormula, m_doc->parameters(), m_doc->conditions());
        if (r.ok) constDeg = r.value;
        m_lblFollowValue->setText(QString::fromUtf8("= %1°")
            .arg(cad::geo::Units::formatDegValue(
                cad::geo::normalizeDeg180(constDeg))));
        m_lblFollowValue->setVisible(true);
        m_lblFxAngle->setVisible(true);
    }
    updateWorldAngleLabel(*att);
}

void SegmentAngleCard::populateAngleField()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const QSignalBlocker sb(m_editAngle);

    m_editAngle->setEnabled(!(block && block->isBridge)
                            && !(block && !block->endTargetPointId.isNull()));
    const auto* att = findFollowerAttachment();
    if (att && att->angleIndependent && block && seg) {
        const auto* sp = block->findPoint(seg->startPointId);
        const auto* ep = block->findPoint(seg->endPointId);
        const auto* driven = (ep && !ep->angleFormula.isEmpty()) ? ep
            : ((sp && !sp->angleFormula.isEmpty()) ? sp : ep);
        if (driven && !driven->angleFormula.isEmpty()) {
            m_editAngle->setText(driven->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            angleDeg = cad::geo::normalizeDeg360(angleDeg);
            m_editAngle->setText(cad::geo::Units::formatDegValue(angleDeg));
            m_lblFxAngle->setVisible(false);
        }
        m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
        return;
    }

    if (att) {
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            if (!att->arcLengthFormula.isEmpty()) {
                m_editAngle->setText(att->arcLengthFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double alphaDeg = (radius > 1e-9)
                    ? cad::geo::arcMmToDeg(att->arcLength, radius) : 0.0;
                const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
                m_editAngle->setText(cad::geo::Units::formatNumberTrimmed(
                    cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius))));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
        } else if (att->rotationMode == cad::param::RotationMode::ChordLength) {
            if (!att->chordLengthFormula.isEmpty()) {
                m_editAngle->setText(att->chordLengthFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double alphaDeg = (radius > 1e-9)
                    ? cad::geo::chordMmToDeg(att->chordLength, radius) : 0.0;
                const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
                m_editAngle->setText(cad::geo::Units::formatNumberTrimmed(
                    cad::geo::Units::mmToCm(cad::geo::degToChordMm(foldDeg, radius))));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
        } else {
            if (!att->followerAngleFormula.isEmpty()) {
                m_editAngle->setText(att->followerAngleFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                m_editAngle->setText(cad::geo::Units::formatDegValue(
                    cad::geo::normalizeDeg180(att->followerAngle)));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
        }
        return;
    }

    if (block && seg) {
        const auto* sp = block->findPoint(seg->startPointId);
        const auto* ep = block->findPoint(seg->endPointId);
        const auto* driven = (ep && !ep->angleFormula.isEmpty()) ? ep
            : ((sp && !sp->angleFormula.isEmpty()) ? sp : ep);
        if (driven && !driven->angleFormula.isEmpty()) {
            m_editAngle->setText(driven->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (ep && ep->constraint == cad::param::PointConstraint::OrthoOffset) {
            // 正交拐角偏置：角度输入框显示中心基准轴角度，绝不计算斜边角
            const double rotDeg = block ? block->transform.rotation * 180.0 / M_PI : 0.0;
            const double angleDeg = cad::geo::normalizeDeg360(ep->angle + rotDeg);
            m_editAngle->setText(cad::geo::Units::formatDegValue(angleDeg));
            m_lblFxAngle->setVisible(false);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            angleDeg = cad::geo::normalizeDeg360(angleDeg);
            m_editAngle->setText(cad::geo::Units::formatDegValue(angleDeg));
            m_lblFxAngle->setVisible(false);
        }
        m_editAngle->setPlaceholderText(cad::ui::kPlaceholderAngleOrFormula);
    }
}

void SegmentAngleCard::applyAngle()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const auto parsed = cad::geo::parseNumberOrFormula(m_editAngle->text());
    if (parsed.formula.isEmpty()) return;

    double targetDeg = parsed.value;
    if (!parsed.isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_doc->parameters(), m_doc->conditions());
        if (!r.ok) return;
        targetDeg = r.value;
    }

    bool isFollower = false;
    bool isIndependentAngle = false;
    QUuid attId;
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            isFollower = true;
            isIndependentAngle = att.angleIndependent;
            attId = att.id;
            break;
        }
    }

    if (isFollower && !isIndependentAngle) {
        if (auto* att = m_doc->findAttachment(attId)) {
            // 目标三模态: 当前模字段按输入换算, 其余两模字段保持原值
            // (命令 verbatim 全字段写, 非当前模写入 = 原值重放)。
            double newAngle = att->followerAngle;
            QString newAngleFormula = att->followerAngleFormula;
            double newArc = att->arcLength;
            QString newArcFormula = att->arcLengthFormula;
            double newChord = att->chordLength;
            QString newChordFormula = att->chordLengthFormula;
            if (att->rotationMode == cad::param::RotationMode::ArcLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                const double foldDeg = (radius > 1e-9)
                    ? cad::geo::arcMmToDeg(cad::geo::Units::cmToMm(targetDeg), radius) : 0.0;
                const double alphaDeg = cad::geo::normalizeDeg360(foldDeg);
                newArc = cad::geo::degToArcMm(alphaDeg, radius);
                newArcFormula = parsed.isNumber ? QString() : parsed.formula;
            } else if (att->rotationMode == cad::param::RotationMode::ChordLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                double chordMm = cad::geo::Units::cmToMm(targetDeg);
                if (radius > 1e-9) {
                    chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);
                }
                newChord = chordMm;
                newChordFormula = parsed.isNumber ? QString() : parsed.formula;
            } else {
                newAngle = cad::geo::normalizeDeg360(targetDeg);
                newAngleFormula = parsed.isNumber ? QString() : parsed.formula;
            }
            if (auto* stack = m_doc->undoStack()) {
                // 角度提交入栈 (数值连续编辑经 mergeWith 合并为一步)。
                stack->push(new cad::cmd::SetFollowerAngleCommand(
                    m_doc, attId, newAngle, newAngleFormula, att->rotationMode,
                    newArc, newArcFormula, newChord, newChordFormula));
            } else {
                att->followerAngle = newAngle;
                att->followerAngleFormula = newAngleFormula;
                att->arcLength = newArc;
                att->arcLengthFormula = newArcFormula;
                att->chordLength = newChord;
                att->chordLengthFormula = newChordFormula;
            }
        }
    } else {
        auto* ep = block->findPoint(seg->endPointId);
        if (!ep) return;
        // 目标态 = 当前模型态 + 角度覆盖 (与旧直写逐位一致; 非角度字段
        // 原样重放 = 命令无副作用)。自由→Polar 转换一并入栈。
        cad::cmd::SegmentEditBarCommand::State st = captureEditStripState(*m_doc, m_blockId, m_segmentId);
        if (ep->constraint == cad::param::PointConstraint::OrthoOffset) {
            // 正交拐角偏置：保持 OrthoOffset 约束，直接更新基准轴角度，不改变偏置量和基准长
            const double rotDeg = block->transform.rotation * 180.0 / M_PI;
            const double localDeg = targetDeg - rotDeg;
            st.endAngle = localDeg;
            st.endAngleFormula = (!parsed.isNumber)
                ? ((std::abs(rotDeg) > 1e-9)
                    ? QStringLiteral("(%1)-%2").arg(parsed.formula).arg(rotDeg, 0, 'g', 12)
                    : parsed.formula)
                : QString();
        } else {
            if (ep->constraint != cad::param::PointConstraint::Polar) {
                const auto* sp = block->findPoint(seg->startPointId);
                if (!sp || !sp->resolved || !ep->resolved) return;
                st.endConstraint = static_cast<int>(cad::param::PointConstraint::Polar);
                st.endRefPointId = seg->startPointId;
                st.endDistance = sp->resolvedPos.distanceTo(ep->resolvedPos);
            }
            const double rotDeg = block->transform.rotation * 180.0 / M_PI;
            const double localDeg = targetDeg - rotDeg;
            st.endAngle = localDeg;
            st.endAngleFormula = (!parsed.isNumber)
                ? ((std::abs(rotDeg) > 1e-9)
                    ? QStringLiteral("(%1)-%2").arg(parsed.formula).arg(rotDeg, 0, 'g', 12)
                    : parsed.formula)
                : QString();
        }
        if (auto* stack = m_doc->undoStack()) {
            stack->push(new cad::cmd::SegmentEditBarCommand(
                m_doc, m_blockId, m_segmentId, st));
        } else {
            ep->constraint = static_cast<cad::param::PointConstraint>(st.endConstraint);
            ep->refPointId = st.endRefPointId;
            ep->distance = st.endDistance;
            ep->angle = st.endAngle;
            ep->angleFormula = st.endAngleFormula;
        }
    }

    m_editAngle->setStyleSheet(QString());
    m_lblFxAngle->setVisible(!parsed.isNumber && !parsed.formula.isEmpty());
    // 不在此处重新 populateAngleField(): 写入后几何尚未重解 (resolveAll 由
    // 对话框 refreshScene 延后触发), 此时按 resolvedPos 读世界角会拿到**旧值**
    // 并覆盖用户刚输入的内容 → "输入角度不断跳动" (用户报告 2026-12)。保留
    // 用户输入, 世界角读数由 onDocResolved (resolved 信号) 在重解后刷新。
    refresh();
    emit changed();
}

void SegmentAngleCard::onAngleDirty()
{
    m_editAngle->setStyleSheet(QString());
    const QString text = m_editAngle->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());
    emit angleEdited();
}

void SegmentAngleCard::onModeToggle()
{
    const auto* att = findFollowerAttachment();
    if (!att || !m_doc) return;

    // 未应用的输入先落盘 (数值或公式都经 applyAngle; 公式随后随模式一起
    // 跨域换算 —— 用户刚输入未回车的公式不能被静默丢弃)。
    if (!m_editAngle->text().trimmed().isEmpty())
        applyAngle();

    auto* mutAtt = m_doc->findAttachment(att->id);
    if (!mutAtt) return;

    auto* blk = m_doc->findBlock(m_blockId);
    double radius = blk ? blk->segmentLengthAtPoint(mutAtt->fromPointId) : 0.0;
    cad::param::RotationMode target = cad::param::RotationMode::Angle;
    if (mutAtt->rotationMode == cad::param::RotationMode::Angle) {
        target = cad::param::RotationMode::ArcLength;
    } else if (mutAtt->rotationMode == cad::param::RotationMode::ArcLength) {
        target = cad::param::RotationMode::ChordLength;
    } else {
        target = cad::param::RotationMode::Angle;
    }
    // 2026-12: 公式驱动不再拒绝切换 —— 公式跨域换算保留变量链接 (半径烘焙
    // 为常数), 见 FollowerAngle.h。数值路径保持历史 fmod 语义。
    const auto res = cad::param::followerModeSwitchValues(
        *mutAtt, radius, target, m_doc->parameters(), m_doc->conditions());
    if (auto* stack = m_doc->undoStack()) {
        // 三模切换入栈: 目标模字段 = 跨域换算结果, 其余两模字段保持原值
        // (命令 verbatim 全字段写)。
        stack->push(new cad::cmd::SetFollowerAngleCommand(
            m_doc, mutAtt->id,
            target == cad::param::RotationMode::Angle
                ? res.angle : mutAtt->followerAngle,
            target == cad::param::RotationMode::Angle
                ? res.angleFormula : mutAtt->followerAngleFormula,
            target,
            target == cad::param::RotationMode::ArcLength
                ? res.arcMm : mutAtt->arcLength,
            target == cad::param::RotationMode::ArcLength
                ? res.arcFormula : mutAtt->arcLengthFormula,
            target == cad::param::RotationMode::ChordLength
                ? res.chordMm : mutAtt->chordLength,
            target == cad::param::RotationMode::ChordLength
                ? res.chordFormula : mutAtt->chordLengthFormula));
    } else {
        mutAtt->rotationMode = target;
        if (target == cad::param::RotationMode::ArcLength) {
            mutAtt->arcLength = res.arcMm;
            mutAtt->arcLengthFormula = res.arcFormula;
        } else if (target == cad::param::RotationMode::ChordLength) {
            mutAtt->chordLength = res.chordMm;
            mutAtt->chordLengthFormula = res.chordFormula;
        } else {
            mutAtt->followerAngle = res.angle;
            mutAtt->followerAngleFormula = res.angleFormula;
        }
    }
    m_doc->resolveAll();
    populateAngleField();
    refresh();
    emit changed();
}

void SegmentAngleCard::updateWorldAngleLabel(const cad::param::Attachment& att)
{
    if (!m_doc) { m_lblWorldAngle->setVisible(false); return; }
    // 有效基准方向 = 与 Resolver 同构 (自定义角度基准/两点连线全部生效,
    // 2026-09 审核 F1) —— 此前只取位置宿主出方向, 设了自定义基准后读数与
    // 线实际方向不符。
    const double refWorldDeg = cad::param::effectiveAngleRefWorld(m_doc, att)
        * 180.0 / M_PI;

    double constDeg;
    if (att.rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        (void)cad::param::ConditionEngine::evaluateLengthMm(att.arcLengthFormula, m_doc->parameters(), m_doc->conditions(), arcMm);
        const auto* block = m_doc->blocksView().byId(m_blockId);
        const double radius = block ? block->segmentLengthAtPoint(att.fromPointId) : 0.0;
        constDeg = (radius > 1e-9) ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
        constDeg = cad::geo::normalizeDeg360(constDeg);
    } else if (att.rotationMode == cad::param::RotationMode::ChordLength) {
        double chordMm = att.chordLength;
        (void)cad::param::ConditionEngine::evaluateLengthMm(att.chordLengthFormula, m_doc->parameters(), m_doc->conditions(), chordMm);
        const auto* block = m_doc->blocksView().byId(m_blockId);
        const double radius = block ? block->segmentLengthAtPoint(att.fromPointId) : 0.0;
        constDeg = (radius > 1e-9) ? cad::geo::chordMmToDeg(chordMm, radius) : 0.0;
        constDeg = cad::geo::normalizeDeg360(constDeg);
    } else {
        constDeg = att.followerAngle;
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.followerAngleFormula, m_doc->parameters(), m_doc->conditions());
            if (r.ok) constDeg = r.value;
        }
    }

    const double absDeg = cad::geo::normalizeDeg360(refWorldDeg + 180.0 - constDeg);
    const QString text = QString::fromUtf8("= 绝对角度 %1°")
                             .arg(cad::geo::Units::formatDegValue(absDeg));
    if (m_lblWorldAngle->text() != text)
        m_lblWorldAngle->setText(text);
    m_lblWorldAngle->setVisible(true);
}

void SegmentAngleCard::onDocResolved()
{
    // 外部几何变更 (旋转拖动等) 每帧广播 resolved: 输入框未聚焦时回填
    // 数值/公式 (与 ContextStrip 同款焦点保护 —— 聚焦中回填会打断敲击,
    // 且用户未提交的输入不能被覆盖)。桥接线除外: 其输入框内容由
    // setBridgeReadOnly 维护 (测出的世界角), 回填会覆盖成存储的跟随角。
    if (!m_editAngle->hasFocus()) {
        const auto* blk = m_doc ? m_doc->blocksView().byId(m_blockId) : nullptr;
        if (!(blk && blk->isBridge)) {
            const QSignalBlocker sb(m_editAngle);
            populateAngleField();
            refresh();
            return;
        }
    }
    // 聚焦中: 只刷几何相关读数 (绝对角度/世界角度), 不覆盖输入。
    const auto* att = findFollowerAttachment();
    if (att && !att->angleIndependent) {
        updateWorldAngleLabel(*att);
        return;
    }
    // 自由线/独立角: 世界角度是纯几何读数, 每次重解后重算刷新
    // (换向等模型变更经 resolveAll 广播, 漏刷会让标签消失/滞留旧值)。
    const auto* block = m_doc ? m_doc->blocksView().byId(m_blockId) : nullptr;
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (block && seg) {
        const auto* sp = block->findPoint(seg->startPointId);
        const auto* ep = block->findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            const double deg = cad::geo::normalizeDeg360(
                std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI);
            const QString text = QString::fromUtf8("= 世界角度 %1°")
                .arg(cad::geo::Units::formatDegValue(deg));
            if (m_lblWorldAngle->text() != text)
                m_lblWorldAngle->setText(text);
            m_lblWorldAngle->setVisible(true);
            return;
        }
    }
    m_lblWorldAngle->setVisible(false);
}

void SegmentAngleCard::setBridgeReadOnly(bool bridge)
{
    if (!bridge || !m_doc) return;
    const auto* block = m_doc->blocksView().byId(m_blockId);
    if (!block || !block->isBridge) return;
    const auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
        angleDeg = cad::geo::normalizeDeg360(angleDeg);
        m_editAngle->setText(cad::geo::Units::formatDegValue(angleDeg));
    }
    m_editAngle->setEnabled(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);
    m_lblFollowValue->setVisible(false);
    m_lblCaption->setText(QString::fromUtf8("角度"));
    m_lblCaption->setStyleSheet(QString());
    const QString tip = cad::ui::TooltipFormatter::status(
        QStringLiteral("桥接线（只读）"),
        QStringLiteral("长度与角度完全由两端吸附钉住的宿主几何决定，不可直接编辑"),
        false);
    m_editAngle->setToolTip(tip);
}

} // namespace cad::ui
