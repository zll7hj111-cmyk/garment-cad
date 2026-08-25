#include "SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QComboBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "PointRefEdit.h"
#include "tools/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {


// ── refreshCard 分块 (2026-08 拆分)：模式下拉 / 连接态 / 自由态 / ──
// 开关组 / 角度基准行 / 指向行。纯剪切自原 381 行 refreshCard。

void SegmentConnectionCard::refreshModeCombo(const cad::param::Attachment* att)
{
    // 统一状态模型 (用户 2026-12 拍板): 「跟随」严格 = 这条线真的在跟。
    //   · 有连接 → 跟随 (含 独立角度/仅角度/滑轨/双基准 子状态);
    //   · 无连接 → 独立线段 (自由视图, 位置吸附行是建立连接的唯一入口)。
    // 独立角度改为面板勾选，不再作为独立模式。m_userMode 已删除 —— 自由态
    // 不存在“用户选择了跟随”的历史状态。
    {
        const QSignalBlocker mb(m_cmbMode);
        const int modeIndex = att ? 0 : 1;
        m_cmbMode->setCurrentIndex(modeIndex);
        // 「跟随」条目仅当 ①已连接 或 ②有缓存可恢复 时可选 —— 自由线没有
        // “可准备的跟随”态; 置灰 = 不可能状态 (成熟表单惯例)。
        if (auto* model = qobject_cast<QStandardItemModel*>(m_cmbMode->model())) {
            if (model->item(0))
                model->item(0)->setEnabled(att != nullptr || m_modeCache.has_value());
        }
    }
}

void SegmentConnectionCard::refreshConnectedState(const cad::param::Attachment* att,
                                                 cad::param::Block* block,
                                                 cad::param::Segment* seg)
{
    // ── Connected state ──
    m_titleLabel->setText(att->angleIndependent
        ? QString::fromUtf8("独立角度 · 连接")
        : QString::fromUtf8("跟随 · 连接"));

    // 连接行在「连接线段:」形态 (自由视图用同一行转「位置吸附:」形态)。
    m_connRow->setVisible(true);
    m_lblConnLabel->setText(QString::fromUtf8("连接线段:"));
    m_lblLeaderRef->setVisible(true);
    m_lblConnSub->setVisible(true);
    m_refConnPoint->setToolTip(QString::fromUtf8(
        "连接点: 输入 P 编号回车切换到该点 (重定向, 角度反算无跳变)"));
    if (att->angleOnly)
        m_lblLeaderRef->clear();
    else
        m_lblLeaderRef->setText(leaderRefLabel(*att));
    m_btnClearConn->setVisible(true);
    m_btnClearConn->setEnabled(true);
    // 仅角度态「拆开」无意义 (已是拆开态) → 禁用.
    m_btnAngleOnly->setEnabled(!att->angleOnly);
      // 简化卡片: 不再显示跨层/仅角度徽章。
      m_lblLayerBadge->setVisible(false);
      m_lblAngleOnlyBadge->setVisible(false);
    // 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): selector 与 badge 同步.
    // angleOnly (拆开) 与滑轨互斥 — 选择器保持「全连接」并禁用.
    {
        const QSignalBlocker cb(m_cmbSlideMode);
        m_cmbSlideMode->setCurrentIndex(static_cast<int>(att->slideMode));
        m_cmbSlideMode->setEnabled(!att->angleOnly);
        // 禁用态提示 (2026-09): 拆开 (仅角度) 与滑轨互斥 — 先恢复全连接.
        const QString slideTip = QString::fromUtf8(att->angleOnly
            ? "\u62c6\u5f00\u72b6\u6001\uff08\u4ec5\u89d2\u5ea6\uff09\u4e0e\u6ed1\u8f68\u4e92\u65a5\uff1a"
              "\u5148\u52fe\u9009\u201c\u4f4d\u7f6e\u5438\u9644\u201d\u6062\u590d\u5168\u8fde\u63a5\u540e\u53ef\u7528\u3002"
            : "\u6ed1\u8f68\u6a21\u5f0f\uff08\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff09\uff1a"
              "\u5168\u8fde\u63a5 = \u4f4d\u7f6e\u5438\u9644 + \u89d2\u5ea6\u8ddf\u968f\uff08\u9ed8\u8ba4\uff09\uff1b"
              "\u6cbf\u7ebf\u6ed1\u52a8 = \u4ec5\u6cbf\u57fa\u51c6\u7ebf\u65b9\u5411\u53ef\u6ed1\uff1b"
              "\u5782\u76f4\u62c9\u51fa = \u4ec5\u5782\u76f4\u57fa\u51c6\u7ebf\u53ef\u62c9\u3002"
              "\u8fdb\u5165\u6ed1\u8f68\u540e\u62d6\u52a8\u8ddf\u968f\u7ebf\u53ea\u6cbf\u5bf9\u5e94\u65b9\u5411\u52a8\uff0c"
              "\u89d2\u5ea6\u8ddf\u968f\u59cb\u7ec8\u4fdd\u7559\u3002\u4e0e\u300c\u62c6\u5f00\uff08\u4fdd\u7559\u89d2\u5ea6\uff09\u300d\u4e92\u65a5\u3002");
        if (m_cmbSlideMode->toolTip() != slideTip)
            m_cmbSlideMode->setToolTip(slideTip);
    }
      m_lblSlideBadge->setVisible(false);  // 简化卡片: 不显示滑轨徽章.
    // 滑轨与 1~4 模式共享显示；具体是否可编辑由 onSlideOffsetEdited 处理。
    m_slideRow->setVisible(true);
    {
        const QSignalBlocker sb1(m_editSlideAlong);
        const QSignalBlocker sb2(m_editSlidePerp);
        const bool hasSlide = att->slideMode != cad::param::SlideMode::None;
        m_editSlideAlong->setText(hasSlide
            ? QString::number(att->slideAlongMm / 10.0, 'f', 2) : QString());
        m_editSlidePerp->setText(hasSlide
            ? QString::number(att->slidePerpMm / 10.0, 'f', 2) : QString());
    }
    m_refConnPoint->setExcludeBlock(m_blockId);
    if (att->angleOnly)
        m_refConnPoint->clearPoint();
    else
        m_refConnPoint->setPoint(att->toBlockId, att->toPointId);
    m_btnAngleMode->setVisible(true);   // 1~4 共享面板，保留弧长/角度切换入口。

    // Mode-dependent caption + world-angle hint.
    if (att->angleIndependent) {
        // 独立角度：不使用任何线段为基准，但不是简单“世界角度”，
        // 名称改为“独立角度”。
        m_lblAngleCaption->setText(QString::fromUtf8("独立角度(°):"));
        m_lblAngleCaption->setStyleSheet(QString());
        m_lblFollowValue->setVisible(false);
        // 显示当前实际世界方向提示。
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
        }
    } else

    if (att->rotationMode == cad::param::RotationMode::ArcLength) {
        m_btnAngleMode->setText(QStringLiteral("\xe2\x8c\x92"));  // ⌒
        m_lblAngleCaption->setText(QString::fromUtf8("\u5f27\u957f(cm):"));  // 弧长(cm):
        m_lblAngleCaption->setStyleSheet(QString());

        double arcMm = att->arcLength;
        if (!att->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att->arcLengthFormula, m_doc->parameters(), {});
            if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
            // 输入框是公式：右侧显示当前计算值（表达式不直观）。
            // 显示 = 带符号折角弧长（v3 定稿，与旋转 HUD 一致）。
            const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
            const double alphaDeg = (radius > 1e-9)
                ? (arcMm / radius) * 180.0 / M_PI : 0.0;
            const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
            m_lblFollowValue->setText(QString::fromUtf8("= %1 cm")
                .arg(foldDeg * M_PI / 180.0 * radius * 0.1, 0, 'f', 2));
            m_lblFollowValue->setVisible(true);
        } else {
            m_lblFollowValue->setVisible(false);
        }
        updateWorldAngleLabel(*att);
    } else {
        m_btnAngleMode->setText(QStringLiteral("∠"));  // ∠
        m_lblAngleCaption->setText(QString::fromUtf8("跟随角度(°):"));  // 跟随角度(°):
        m_lblAngleCaption->setStyleSheet(QString());

        double constDeg = att->followerAngle;
        if (!att->followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att->followerAngleFormula, m_doc->parameters(), {});
            if (r.ok) constDeg = r.value;
            // 输入框是公式：右侧显示当前计算值（表达式不直观）。
            // 显示 = 带符号折角（v3 定稿，与旋转 HUD 一致）。
            m_lblFollowValue->setText(QString::fromUtf8("= %1°")
                .arg(cad::geo::Units::formatDegValue(
                    cad::geo::normalizeDeg180(constDeg))));
            m_lblFollowValue->setVisible(true);
        } else {
            m_lblFollowValue->setVisible(false);
        }
        // 绝对角度提示 (闭合基准): 世界角 = refWorld + 180° − 线夹角.
        updateWorldAngleLabel(*att);
    }
}

void SegmentConnectionCard::refreshFreeState(cad::param::Block* block,
                                           cad::param::Segment* seg)
{
    // ── 统一自由视图 (用户 2026-12 拍板: 跟随 = 真的在跟) ──
    // 无连接 = 「独立线段」: 标题恒为独立线段; 连接行转「位置吸附:」形态
    // (输入 P# 回车 = 建立跟随连接, 事后下拉自动切到「跟随」)。
    m_titleLabel->setText(QString::fromUtf8("独立线段"));
    m_connRow->setVisible(true);
    m_lblConnLabel->setText(QString::fromUtf8("位置吸附:"));
    m_lblLeaderRef->setVisible(false);
    m_lblConnSub->setVisible(false);
    m_btnAngleOnly->setEnabled(false);   // 拆开需要连接
    m_refConnPoint->setToolTip(QString::fromUtf8(
        "输入目标点 P 编号并回车，将本线贴附到该点（建立跟随连接）；"
        "断开后记忆最近宿主点，可用下拉「跟随」或重新输入恢复。"));
    m_slideRow->setVisible(false);
    m_lblLayerBadge->setVisible(false);
    m_lblAngleOnlyBadge->setVisible(false);
    m_btnClearConn->setVisible(false);
    {
        const QSignalBlocker lb(m_lblLeaderRef);
        const QSignalBlocker cb(m_refConnPoint);
        m_lblLeaderRef->clear();
        m_refConnPoint->setExcludeBlock(m_blockId);
        m_refConnPoint->clearPoint();
    }
    m_btnAngleMode->setVisible(false);
    // 自由线角度 = 世界角 (角度(°) 中性标签)。
    m_lblAngleCaption->setText(QString::fromUtf8("角度(°):"));
    m_lblAngleCaption->setStyleSheet(QString());
    // 显示当前世界方向提示。
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
    }
    // 绝对角度表达式：输入框是公式时右侧显示当前计算值。
    const cad::param::ParamPoint* epFree =
        seg ? block->findPoint(seg->endPointId) : nullptr;
    if (epFree && !epFree->angleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            epFree->angleFormula, m_doc->parameters(), {});
        if (r.ok) {
            const double rotDeg = block->transform.rotation * 180.0 / M_PI;
            const double deg = cad::geo::normalizeDeg360(r.value + rotDeg);
            m_lblFollowValue->setText(QString::fromUtf8("= %1°")
                .arg(cad::geo::Units::formatDegValue(deg)));
            m_lblFollowValue->setVisible(true);
        } else {
            m_lblFollowValue->setVisible(false);
        }
    } else {
        m_lblFollowValue->setVisible(false);
    }
}

void SegmentConnectionCard::refreshConnectionToggles(const cad::param::Attachment* att)
{
    const QUuid hostBlock = att ? att->toBlockId : m_refConnPoint->resolvedBlockId();
    const QUuid hostPoint = att ? att->toPointId : m_refConnPoint->resolvedPointId();
    const auto* hostBlk = !hostBlock.isNull() ? m_doc->findBlock(hostBlock) : nullptr;
    const auto* hostPt = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

    const QSignalBlocker b(m_chkFollowHost);
    m_chkFollowHost->setText(hostPt
        ? QString::fromUtf8("\u4f4d\u7f6e\u5438\u9644 %1")  // 位置吸附 %1
            .arg(cad::param::Serial::tag(hostPt->serial))
        : (att ? QString::fromUtf8("\u4f4d\u7f6e\u5438\u9644\uff08\u5df2\u5220\u9664\uff09")  // 位置吸附（已删除）
               : QString::fromUtf8("\u4f4d\u7f6e\u5438\u9644")));  // 位置吸附
    if (att && att->angleOnly)
        m_chkFollowHost->setText(m_chkFollowHost->text()
            + QString::fromUtf8("\uff08\u4ec5\u89d2\u5ea6\uff09"));  // （仅角度）
    m_chkFollowHost->setChecked(att != nullptr);
    m_chkFollowHost->setEnabled(true);
    // 新语义 (用户拍板 2026-08 复旧): 勾选 = 连接 (位置+角度跟随, 默认
    // 焊接 = 拖动保护勾选; 拆散 = D 键快拆 / 取消「拖动保护」);
    // 取消勾选 = 彻底断开 (删 attachment)。断开后记忆最近宿主, 重新勾选一键恢复。
    m_chkFollowHost->setToolTip(QString::fromUtf8(
        "\u52fe\u9009\u540e\u8d77\u70b9\u5438\u9644\u5230\u5bbf\u4e3b\u70b9\u5e76\u5efa\u7acb\u89d2\u5ea6\u8ddf\u968f"
        "\uff08\u9ed8\u8ba4\u710a\u63a5\uff1a\u62d6\u4efb\u4e00\u7aef\u6574\u5bf9\u79fb\u52a8\u4e0d\u62c6\uff0c\u62c6\u6563\u8d70 D \u952e\u5feb\u62c6\uff09\uff1b"
        "\u53d6\u6d88\u52fe\u9009 = \u5f7b\u5e95\u65ad\u5f00\uff08\u4f4d\u7f6e\u5438\u9644\u4e0e\u89d2\u5ea6\u8ddf\u968f\u4e00\u8d77\u89e3\u9664\uff09\u3002"
        "\u65ad\u5f00\u540e\u8bb0\u5fc6\u6700\u8fd1\u5bbf\u4e3b\u70b9\uff0c\u518d\u6b21\u52fe\u9009\u5373\u53ef\u4e00\u952e\u6062\u590d\uff08\u89d2\u5ea6\u53cd\u7b97\u3001\u65e0\u8df3\u53d8\uff09\uff1b"
        "\u4ec5\u89d2\u5ea6\u72b6\u6001\u7531\u62d6\u62c6 / D \u952e\u5feb\u62c6\u5f15\u5165\u3002"));
    // 勾选后起点连接到宿主点并建立角度跟随（默认焊接：拖任一端整对移动不拆；拆散 = D 键快拆）；取消勾选 = 彻底断开，断开后记忆最近宿主可一键恢复
    m_chkFollowHost->setVisible(false);  // 已由“模式”下拉统一表达.

    // Lock checkbox (拖动保护, 语义 2026-08 复旧): checked = 焊接
    // (isLocked): 拖任一端整对移动不拆。**新建连接默认勾选 (焊接)**;
    // 取消 = 解焊仍完整连接 (拖跟随线可拆散, 拆散 = D 键快拆)。仅角度态
    // 勾选 = 恢复完整连接 (位置重新吸附回宿主点) + 焊接。无连接 (断开态)
    // 禁用; 滑轨态 (slideMode != None) 位置留一个自由度, 与焊接互斥 → 禁用。
    const QSignalBlocker lb(m_chkLockConn);
    m_chkLockConn->setChecked(att != nullptr && att->isLocked);
    const bool lockEnabled = att != nullptr
        && att->slideMode == cad::param::SlideMode::None;
    m_chkLockConn->setEnabled(lockEnabled);
    // 禁用/状态提示: 说明为什么拖动保护不可用或当前状态的意义.
    {
        QString lockTip;
        if (!lockEnabled) {
            if (!att)
                lockTip = QString::fromUtf8(
                    "\u81ea\u7531\u7ebf\u6ca1\u6709\u8fde\u63a5\uff0c\u65e0\u62d6\u52a8\u4fdd\u62a4\u53ef\u8bbe\u3002");  // 自由线没有连接，无拖动保护可设。
            else
                lockTip = QString::fromUtf8(
                    "\u6ed1\u8f68\u72b6\u6001\u4f4d\u7f6e\u4fdd\u7559\u4e00\u4e2a\u81ea\u7531\u5ea6\uff0c\u4e0e\u62d6\u52a8\u4fdd\u62a4\u4e92\u65a5\u3002");  // 滑轨状态位置保留一个自由度，与拖动保护互斥。
        } else if (att->isLocked) {
            lockTip = QString::fromUtf8(
                "\u5f53\u524d\u5df2\u710a\u63a5\uff1a\u62d6\u52a8\u4efb\u4e00\u7aef\u65f6\u6574\u4e2a\u5bf9\u4e00\u8d77\u79fb\u52a8\uff0c\u4e0d\u4f1a\u88ab\u62d6\u62c6\u3002"
                "\u53d6\u6d88 = \u89e3\u9664\u710a\u63a5\uff08\u8fde\u63a5\u4fdd\u6301\uff0c\u62d6\u8ddf\u968f\u7ebf\u5373\u53ef\u62c6\u6563\uff0c\u62c6\u6563\u8d70 D \u952e\u5feb\u62c6\uff09\u3002");  // 当前已焊接：拖动任一端时整个对一起移动，不会被拖拆。取消 = 解除焊接（连接保持，拖跟随线即可拆散。拆散 = D 键快拆）。
        } else if (att->angleOnly) {
            lockTip = QString::fromUtf8(
                "\u5f53\u524d\u4ec5\u89d2\u5ea6\u5438\u9644\uff1a\u4f4d\u7f6e\u81ea\u7531\u3001\u89d2\u5ea6\u4ecd\u8ddf\u968f\u57fa\u51c6\u7ebf\u3002"
                "\u52fe\u9009\u6062\u590d\u5b8c\u6574\u8fde\u63a5\uff08\u4f4d\u7f6e\u91cd\u65b0\u5438\u9644\u56de\u5bbf\u4e3b\u70b9 + \u91cd\u65b0\u710a\u63a5\uff09\u3002");  // 当前仅角度吸附：位置自由、角度仍跟随基准线。勾选恢复完整连接（位置重新吸附回宿主点 + 重新焊接）。
        } else {
            lockTip = QString::fromUtf8(
                "\u9ed8\u8ba4\u710a\u63a5\uff1a\u62d6\u52a8\u4efb\u4e00\u7aef\u65f6\u6574\u4e2a\u5bf9\u4e00\u8d77\u79fb\u52a8\uff0c\u4e0d\u4f1a\u88ab\u62d6\u62c6\uff1b"
                "\u53d6\u6d88\u52fe\u9009 = \u89e3\u710a\uff08\u62d6\u8ddf\u968f\u7ebf\u5373\u53ef\u62c6\u6563\uff0c\u62c6\u6563\u8d70 D \u952e\u5feb\u62c6\uff09\u3002"
                "\u591a\u7ebf\u6574\u4f53\u79fb\u52a8\u4e5f\u53ef\u7528\u7ec4\u4ef6\u8fbe\u6210\u3002");  // 默认焊接：拖任一端时整个对一起移动，不会被拖拆；取消勾选 = 解焊（拖跟随线即可拆散，拆散 = D 键快拆）。多线整体移动也可用组件达成。
        }
        if (m_chkLockConn->toolTip() != lockTip)
            m_chkLockConn->setToolTip(lockTip);
    }
    m_chkLockConn->setVisible(false);  // 不再单独展示“拖动保护”，与“位置连接”合并为单一连接开关.

      // 角度独立 (用户新需求 2026): 只有完整连接 (非仅角度/非滑轨) 时可用。
      const QSignalBlocker ab(m_chkAngleIndependent);
      m_chkAngleIndependent->setChecked(att && att->angleIndependent);
      const bool aiEnabled = att && !att->angleOnly
          && att->slideMode == cad::param::SlideMode::None;
      m_chkAngleIndependent->setEnabled(aiEnabled);
      m_chkAngleIndependent->setVisible(att != nullptr);  // 跟随面板内的“独立角度”勾选项.
      if (!aiEnabled) {
          const QString tip = att
              ? QString::fromUtf8(
                    "当前不是普通全连接（仅角度/滑轨态与角度独立互斥），"
                    "请先恢复全连接。")
              : QString::fromUtf8("当前是自由线，无连接可设。");
          if (m_chkAngleIndependent->toolTip() != tip)
              m_chkAngleIndependent->setToolTip(tip);
      } else {
          m_chkAngleIndependent->setToolTip(QString::fromUtf8(
              "勾选后：位置仍吸附在基准点，但本线角度不再跟随基准线，"
              "可用旋转/角度公式自由控制。"
              "取消勾选恢复角度跟随（自动反算当前角度，无跳变）。"));
      }
}

void SegmentConnectionCard::refreshAngleRefRow(const cad::param::Attachment* att)
{
    // 角度基准分离: 位置锚点与角度基准可不同。统一状态模型下两种用途:
    //   · 有连接 → 显示/编辑当前角度基准 (双基准);
    //   · 无连接 → 预填行: 先选好引用线段/点, 从「位置吸附」连入时自动落库
    //     (onConnectToResolved)。预填内容在 refresh 时**不覆盖**。
    m_angleRefRow->setVisible(true);
    m_angleRefPoint->setEnabled(true);
    m_angleRefPoint->setExcludeBlock(m_blockId);
    m_btnClearAngleRef->setEnabled(att != nullptr
        && !att->angleRefBlockId.isNull());
    if (!att) return;   // 自由态: 保留用户已填的预填内容 (不 clear/不重写)。

    {
        const QSignalBlocker ar(m_angleRefPoint);
        const auto* refBlk = !att->angleRefBlockId.isNull()
            ? m_doc->findBlock(att->angleRefBlockId) : nullptr;
        if (refBlk && !att->angleRefSegmentId.isNull()) {
            const auto* refSeg = refBlk->findSegment(att->angleRefSegmentId);
            QUuid showPoint = att->angleRefPointId;
            if (showPoint.isNull() && refSeg)
                showPoint = refSeg->startPointId;
            m_angleRefPoint->setPoint(att->angleRefBlockId, showPoint);
        } else if (!att->angleIndependent) {
            // 未设置独立角度基准时，自动填入位置宿主作为角度引用，
            // 保证“完整跟随/仅角度”下面也有内容（与上面一致）。
            m_angleRefPoint->setPoint(att->toBlockId, att->toPointId);
        } else {
            m_angleRefPoint->clearPoint();
        }
        if (refBlk && !att->angleRefSegmentId.isNull()) {
            if (const auto* refSeg2 = refBlk->findSegment(att->angleRefSegmentId)) {
                QString t = cad::param::Serial::tag(refSeg2->serial);
                if (!refSeg2->name.isEmpty())
                    t += QStringLiteral("·") + refSeg2->name;
                m_lblAngleRefSeg->setText(t);
            } else {
                m_lblAngleRefSeg->setText(QString());
            }
        } else if (!att->angleIndependent) {
            // 完整跟随/仅角度未设独立角度基准时，下面也填成位置/角度宿主线段。
            m_lblAngleRefSeg->setText(leaderRefLabel(*att));
        } else {
            m_lblAngleRefSeg->setText(QString());
        }
    }
}

void SegmentConnectionCard::refreshAimRow(const cad::param::Block* block)
{
    // ── 指向（终点指向）并入连接卡片 ──
    const bool hasAim = !block->endTargetPointId.isNull();
    if (hasAim) {
        m_refAimPoint->setExcludeBlock(m_blockId);
        m_refAimPoint->setPoint(block->endTargetBlockId, block->endTargetPointId);
        m_editAimOffset->setEnabled(true);
        m_btnClearAim->setEnabled(true);
        const QSignalBlocker ab2(m_editAimOffset);
        if (!block->endTargetOffsetFormula.isEmpty())
            m_editAimOffset->setText(block->endTargetOffsetFormula);
        else
            m_editAimOffset->setText(QString::number(block->endTargetOffset, 'g', 6));
    } else {
        m_refAimPoint->setExcludeBlock(m_blockId);
        m_refAimPoint->clearPoint();
        m_editAimOffset->setEnabled(false);
        m_editAimOffset->clear();
        m_btnClearAim->setEnabled(false);
    }
    // 指向生效后，角度编辑显示为灰色（终点方向由指向决定）。
    const bool angleGray = hasAim || (block && block->isBridge);
    m_editAngle->setEnabled(!angleGray);
    m_btnAngleMode->setEnabled(!angleGray);
    m_lblAngleCaption->setStyleSheet(angleGray
        ? QStringLiteral("color:%1;").arg(cad::ui::Theme::tokens().text3.name())
        : QString());
    m_aimRow->setVisible(true);
}

} // namespace cad::tools
