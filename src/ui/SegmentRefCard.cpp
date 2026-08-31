#include "ui/SegmentRefCard.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QPushButton>
#include <QTimer>

#include "ElaText.h"

#include "ui/FormScaffold.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/Serial.h"
#include "canvas/CanvasScene.h"
#include "ui/PointRefEdit.h"
#include "ui/Theme.h"
#include "document/commands/AttachmentCommands.h"

namespace cad::ui {

namespace {
constexpr int kFieldH = 30;   ///< 2026-xx 紧凑化 (35→30, 与状态栏对齐).
constexpr int kBtnW = 48;     ///< 二字按钮统一宽 (2026-xx 紧凑 58→48).
constexpr int kRefEditW = 104; ///< 点1/点2 输入框宽 (句式行内).
constexpr int kAlignEditW = 64; ///< 对齐点输入框宽 (本线端点, 短 tag).
} // namespace

SegmentRefCard::SegmentRefCard(cad::param::ParamDocument* doc,
                               CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // 2026-09 起启用 scene: 点2 非法输入 (本线自身成员/重复点1) 的 toast 反馈
    // —— 此前静默刷回, 用户看到"输入框 2 填不进内容"。
    // 纯行组 (2026-12 去卡框化): 无边框/无标题, 嵌入属性页「摆放」角度区。
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // ── 句式行 (2026-09 设计修正): 对齐点【PointRefEdit】 方向：点1→点2 [独立] ──
    // 对齐点 = 本线段的哪个端点钉在目标点上 (Attachment::fromPointId), 只
    // 允许本线端点 (P3/P4 互选), **与换向 (start/end 身份) 完全无关** —— 旧
    // 实现是绑 startPointId 的只读 tag, 换向后乱跳, 且用户无法选择对端。
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    auto* lblAlign = new ElaText(QString::fromUtf8("对齐点"), 11, this);
    lblAlign->setFixedWidth(40);
    lblAlign->setStyleSheet(QStringLiteral("background: transparent;"));
    lblAlign->setToolTip(QString::fromUtf8(
        "对齐点：本线段的哪个端点钉在目标点上（输入本线端点 P#；"
        "与调换进/出无关）"));
    row->addWidget(lblAlign);

    m_alignPointEdit = new PointRefEdit(m_doc, this);
    m_alignPointEdit->setObjectName(QStringLiteral("alignPointEdit"));
    m_alignPointEdit->setFixedWidth(kAlignEditW);
    m_alignPointEdit->setPlaceholderText(QStringLiteral("P#"));
    m_alignPointEdit->setToolTip(QString::fromUtf8(
        "对齐点：本线段的哪个端点钉在目标点上。只接受本线端点（P#）；"
        "与调换进/出无关。"));
    row->addWidget(m_alignPointEdit);

    auto mkSentence = [&](const QString& text) -> ElaText* {
        auto* t = new ElaText(text, 11, this);
        t->setStyleSheet(QStringLiteral("background: transparent;"));
        return t;
    };
    auto* lblDirWord = mkSentence(QString::fromUtf8("方向："));
    lblDirWord->setFixedWidth(34);
    m_lblDirWord = lblDirWord;
    row->addWidget(m_lblDirWord);
    m_angleRefPoint = new PointRefEdit(m_doc, this);
    m_angleRefPoint->setObjectName(QStringLiteral("angleRefPointEdit"));
    m_angleRefPoint->setFixedWidth(kRefEditW);
    m_angleRefPoint->setToolTip(QString::fromUtf8(
        "方向点1：输入 P#/L#/名称。与点2 的连线方向 = 角度基准；"
        "只填点1 = 该点出口方向；都留空 = 自动跟随所连的线。"));
    row->addWidget(m_angleRefPoint);
    row->addWidget(mkSentence(QString::fromUtf8("→")));
    m_angleRefPoint2 = new PointRefEdit(m_doc, this);
    m_angleRefPoint2->setObjectName(QStringLiteral("angleRefPoint2Edit"));
    m_angleRefPoint2->setFixedWidth(kRefEditW);
    m_angleRefPoint2->setToolTip(QString::fromUtf8(
        "方向点2：与点1的连线方向作为角度基准（可为任意块上的任意点）。"
        "自动跟随态回显宿主线段另一端。"));
    row->addWidget(m_angleRefPoint2);
    row->addStretch();

    // [独立] (§6.4, checkable): 勾选 = 清空点1/点2 → 世界角度; 再点 = 还原
    // 上次内容 (模型 ref 字段即缓存, 反算零跳变)。objectName 沿用旧契约。
    m_btnIndependent = new QPushButton(QString::fromUtf8("独立"), this);
    m_btnIndependent->setObjectName(QStringLiteral("angleBaseToggleBtn"));
    m_btnIndependent->setCheckable(true);
    m_btnIndependent->setFixedSize(kBtnW, kFieldH);
    m_btnIndependent->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnIndependent->setCursor(Qt::PointingHandCursor);
    m_btnIndependent->setToolTip(QString::fromUtf8(
        "独立：角度改用世界角度（不跟任何线）；再点还原上次的角度基准。"));
    row->addWidget(m_btnIndependent);
    lay->addLayout(row);

    connect(m_alignPointEdit, &PointRefEdit::pointResolved,
            this, &SegmentRefCard::onAlignPointResolved);
    connect(m_angleRefPoint, &PointRefEdit::pointResolved,
            this, &SegmentRefCard::onAngleRefPointResolved);
    connect(m_angleRefPoint2, &PointRefEdit::pointResolved,
            this, &SegmentRefCard::onAngleRefPoint2Resolved);
    connect(m_btnIndependent, &QPushButton::toggled,
            this, &SegmentRefCard::onIndependentToggled);
}

void SegmentRefCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
}

const cad::param::Attachment* SegmentRefCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

void SegmentRefCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* att = findFollowerAttachment();

    // 方向段 (点1/点2/[独立]) 在"角度由约束决定"时无意义 → 隐藏:
    //   · 终点指向 (endTarget): 旋转由 Resolver Step 7 驱动;
    //   · 桥接线 (pin+pin): 角度由两点决定。
    // **对齐点段恒显示** (2026-09 规则表): 自由线灰显默认进点、已连接真实
    // 钉点、桥接/指向禁用 (无进点语义或锁定 start 端)。
    // 省道线: 计算线, 整卡隐藏。判定收口到本函数 (2026-09 审核 F5)。
    const bool hasEnd = block && !block->endTargetPointId.isNull();
    const bool isBridge = block && block->isBridge;
    const bool isDart = block && block->isDart();
    setVisible(!isDart);
    if (isDart) return;
    const bool dirHidden = hasEnd || isBridge;
    if (m_lblDirWord) m_lblDirWord->setVisible(!dirHidden);
    if (m_angleRefPoint) m_angleRefPoint->setVisible(!dirHidden);
    if (m_angleRefPoint2) m_angleRefPoint2->setVisible(!dirHidden);
    if (m_btnIndependent) m_btnIndependent->setVisible(!dirHidden);

    // 预填自动落库 (用户 2026-12): 自由态先选好引用点, 连入后第一次 refresh
    // 自动写为角度基准 —— 一次生效 (落库后 angleRefBlockId 非空即停)。
    // 2026-09 设计修正: 自动态回填的点1/点2 (autoEcho) 不算用户意图 ——
    // 必须跳过, 否则每次 refresh 都把自动回显值当预填提交 (自动态被误固化
    // 为自定义)。
    if (att && att->angleRefBlockId.isNull() && !att->angleIndependent) {
        const bool has1 = m_angleRefPoint->resolvedBlockId().isNull() ? false
            : !m_angleRefPoint->resolvedPointId().isNull()
              && !m_angleRefPoint->isAutoEcho();
        const bool has2 = !m_angleRefPoint2->resolvedBlockId().isNull()
            && !m_angleRefPoint2->resolvedPointId().isNull()
            && !m_angleRefPoint2->isAutoEcho()
            && m_angleRefPoint2->resolvedBlockId() != att->fromBlockId;
        const QUuid rb = m_angleRefPoint->resolvedBlockId();
        const QUuid rp = m_angleRefPoint->resolvedPointId();
        const QUuid r2b = m_angleRefPoint2->resolvedBlockId();
        const QUuid r2p = m_angleRefPoint2->resolvedPointId();
        const QUuid b1 = has1 ? rb : att->toBlockId;
        const QUuid p1 = has1 ? rp : att->toPointId;
        if (has2 && !(r2b == b1 && r2p == p1)) {
            const auto* refBlk = m_doc->findBlock(b1);
            const QUuid s1 = refBlk ? refBlk->exitSegmentAtPoint(p1) : QUuid();
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                    m_doc, att->id, b1, s1, p1, r2b, r2p));
            else
                m_doc->setAttachmentAngleRef(att->id, b1, s1, p1, r2b, r2p);
        } else if (has1
                   && (rb != att->toBlockId || rp != att->toPointId)) {
            const auto* refBlk = m_doc->findBlock(rb);
            const QUuid rs = refBlk ? refBlk->exitSegmentAtPoint(rp) : QUuid();
            if (!rs.isNull()) {
                if (auto* stack = m_doc->undoStack())
                    stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                        m_doc, att->id, rb, rs, rp));
                else
                    m_doc->setAttachmentAngleRef(att->id, rb, rs, rp);
            }
        }
    }

    refreshAngleRefRow(att);
}

void SegmentRefCard::refreshAngleRefRow(const cad::param::Attachment* att)
{
    // 对齐点 (2026-09 设计修正): 显示/编辑 Attachment::fromPointId —— 本线
    // 段的哪个端点钉在目标点上。受限为本块点 (P3/P4 互选), 与换向无关
    // (模型字段由连接语义决定, 不随 start/end 身份翻转)。
    // 2026-09 规则表 (用户拍板): 进点 = 被钉住的那端。
    //   · 已连接: 真实钉点 (fromPointId)。
    //   · 自由线: 灰显默认进点 (start 端 = 创建序先建点), 连接后变真实钉点。
    //   · 桥接 (pin+pin): 无进点语义 → 显示默认 + 禁用。
    //   · 有 endTarget (起点连接+终点指向): 进点锁定 start 端 (方案 A) → 禁用。
    m_alignPointEdit->setRestrictToBlock(m_blockId);
    m_alignPointEdit->setAutoEcho(false);
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    const bool isBridge = block && block->isBridge;
    const bool hasEndTarget = block && !block->endTargetPointId.isNull();

    if (att && !att->fromPointId.isNull() && !isBridge && !hasEndTarget) {
        m_alignPointEdit->setPoint(att->fromBlockId, att->fromPointId);
    } else if (seg) {
        // 自由线/桥接/有指向: 显示默认进点 (start 端); 桥接与指向态禁用。
        m_alignPointEdit->setPoint(m_blockId, seg->startPointId);
        m_alignPointEdit->setAutoEcho(true);
    } else {
        m_alignPointEdit->clearPoint();
    }
    const bool alignLocked = isBridge || hasEndTarget;
    m_alignPointEdit->setEnabled(!alignLocked);
    m_alignPointEdit->setToolTip(alignLocked
        ? (isBridge
               ? QString::fromUtf8("桥接线：两端都钉在宿主点上，没有进点（长度由两点距离决定）")
               : QString::fromUtf8("终点指向生效：进点锁定在起点（终点端用于指向目标位置）"))
        : QString::fromUtf8(
              "对齐点 = 进点：本线段的哪个端点钉在目标点上（输入本线端点 P#）。"
              "与调换进/出无关——箭头只是身份标签，对齐点才是实际钉点。"));

    m_angleRefPoint->setExcludeBlock(m_blockId);
    m_angleRefPoint2->setExcludeBlock(m_blockId);
    const bool hasAtt = att != nullptr;
    const bool independent = hasAtt && att->angleIndependent;

    // [独立] 按钮: 勾选态随模型 (程序化 setChecked 不回环 toggled)。
    const QSignalBlocker ib(m_btnIndependent);
    m_btnIndependent->setChecked(independent);
    m_btnIndependent->setEnabled(hasAtt);
    if (independent) {
        m_btnIndependent->setToolTip(QString::fromUtf8(
            "还原上次的角度基准：恢复角度跟随（反算零跳变）"));
    } else {
        m_btnIndependent->setToolTip(hasAtt
            ? QString::fromUtf8("独立：角度改用世界角度（不跟任何线）；输入框随之清空。")
            : QString::fromUtf8("独立：需要先建立连接（自由线的角度本就是世界角度）。"));
    }

    // 点1/点2 启用态: 有连接才可编辑 (自由线保持可输入 = 预填, 连入后自动落库)。
    m_angleRefPoint->setEnabled(true);
    m_angleRefPoint2->setEnabled(true);
    m_angleRefPoint->setAutoEcho(false);
    m_angleRefPoint2->setAutoEcho(false);

    if (!att) return;   // 自由态: 保留用户已填的预填内容 (不 clear/不重写)。

    if (independent) {
        // 独立角: 输入框清空+禁用 (模型 ref 字段保留 = 还原缓存)。
        m_angleRefPoint->clearPoint();
        m_angleRefPoint2->clearPoint();
        m_angleRefPoint->setEnabled(false);
        m_angleRefPoint2->setEnabled(false);
        return;
    }

    if (att->angleRefBlockId.isNull()) {
        // 自动态 (默认): 灰显回显 目标点 P2 与 宿主线段另一端 P1 —— 方向 =
        // 宿主在吸附点处的出口方向 ("P3 对齐 P2, 以 P2→P1 为基准", 2026-09
        // 设计修正: 此前点2 恒空, 用户看不到基准方向的另一端)。
        m_angleRefPoint->setPoint(att->toBlockId, att->toPointId);
        m_angleRefPoint->setAutoEcho(true);
        if (const auto* host = m_doc->findBlock(att->toBlockId)) {
            if (const auto* hostSeg = host->findSegment(att->toSegmentId)) {
                const QUuid other = (hostSeg->startPointId == att->toPointId)
                    ? hostSeg->endPointId : hostSeg->startPointId;
                m_angleRefPoint2->setPoint(att->toBlockId, other);
                m_angleRefPoint2->setAutoEcho(true);
            } else {
                m_angleRefPoint2->clearPoint();
            }
        } else {
            m_angleRefPoint2->clearPoint();
        }
        return;
    }

    // 自定义: 点1 (必填) + 点2 (可选, 两点连线方向)。
    m_angleRefPoint->setPoint(att->angleRefBlockId, att->angleRefPointId);
    if (!att->angleRef2BlockId.isNull() && !att->angleRef2PointId.isNull())
        m_angleRefPoint2->setPoint(att->angleRef2BlockId, att->angleRef2PointId);
    else
        m_angleRefPoint2->clearPoint();
}

void SegmentRefCard::onAngleRefPointResolved(const QUuid& blockId,
                                             const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) {
        // 自由态: 预填（连入后由 refresh 自动落库）。
        m_angleRefPoint->setPoint(blockId, pointId);
        refreshAngleRefRow(nullptr);
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refresh(); return; }
    if (blockId == att->fromBlockId) {
        // 本线自身成员: 作为基准方向无意义 (零向量/自引用), 静默刷回会让
        // 用户以为"点2 填不进" —— 2026-09 起 toast 明示 (E:\4.gcad L2 报告)。
        rejectRefInput(QString::fromUtf8("该点是本线自身的点，不能作为基准方向点"));
        return;
    }

    // §6.4: 点1 是任意点 —— 不再要求属于某条基准线段。出口线段尽力而为
    // (端点/插值点/曲线锚点有; 其余点为空 = 需配合点2 定方向)。
    const QUuid segId = leader->exitSegmentAtPoint(pointId);

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, blockId, segId, pointId));
    else
        m_doc->setAttachmentAngleRef(att->id, blockId, segId, pointId);

    refresh();
    emit changed();
}

void SegmentRefCard::onAngleRefPoint2Resolved(const QUuid& blockId,
                                               const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) {
        // 自由态: 预填（连入后由 refresh 自动落库）。
        m_angleRefPoint2->setPoint(blockId, pointId);
        refreshAngleRefRow(nullptr);
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refresh(); return; }
    if (blockId == att->fromBlockId) {
        // 本线自身成员: 作为基准方向无意义, 静默刷回 = "填不进" 假象。
        rejectRefInput(QString::fromUtf8("该点是本线自身的点，不能作为基准方向点"));
        return;
    }

    // 与有效点1相同 = 零向量方向 (atan2(0,0)), 拒绝。自动态的有效点1是
    // 将被固化的所连点, 自定义态是已落库的 angleRef 点。
    const bool autoMode = att->angleRefBlockId.isNull();
    const QUuid ref1Block = autoMode ? att->toBlockId : att->angleRefBlockId;
    const QUuid ref1Point = autoMode ? att->toPointId : att->angleRefPointId;
    if (blockId == ref1Block && pointId == ref1Point) {
        rejectRefInput(QString::fromUtf8("点2 与点1 相同：两点连线为零长度，请换一个点"));
        return;
    }

    if (autoMode) {
        // 自动态 (默认): 用户填点2 = 明确的自定义意图 —— 自动跟随的点1
        // (所连点) 固化为显式点1, 与点2 一起进入两点连线方向。此前 ref2
        // 单独落库而 angleRefBlockId 仍为 null, Resolver 两点分支
        // (angleRefBlockId 非空门控) 直接忽略 → 点2 静默失效 (2026-08-31 修)。
        const auto* host = m_doc->findBlock(att->toBlockId);
        const QUuid hostSeg = host ? host->exitSegmentAtPoint(att->toPointId)
                                   : QUuid();
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id,
                att->toBlockId, hostSeg, att->toPointId, blockId, pointId));
        else
            m_doc->setAttachmentAngleRef(att->id, att->toBlockId, hostSeg,
                                         att->toPointId, blockId, pointId);
    } else {
        // 自定义态: 保持点1, 更新点2 (两点连线方向)。
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id,
                att->angleRefBlockId, att->angleRefSegmentId, att->angleRefPointId,
                blockId, pointId));
        else
            m_doc->setAttachmentAngleRef(att->id,
                                         att->angleRefBlockId,
                                         att->angleRefSegmentId,
                                         att->angleRefPointId,
                                         blockId, pointId);
    }
    refresh();
    emit changed();
}

// ─── onAlignPointResolved (2026-09 设计修正) ───
// 对齐点 = 本线段的哪个端点钉在目标点上。输入受限于本块点 (P3/P4 互选);
// 写入 Attachment::fromPointId + 反算 followerAngle 零跳变 —— 与换向
// (start/end 身份) 无关, fromPointId 由连接语义决定。
void SegmentRefCard::onAlignPointResolved(const QUuid& blockId,
                                          const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) {
        // 自由态: 无连接, 对齐点无意义 (位置维度独立由「连接到」行管理) ——
        // 还原 + 提示, 不写模型。
        rejectRefInput(QString::fromUtf8("请先建立连接，再设置对齐点"));
        return;
    }
    if (blockId != att->fromBlockId || pointId == att->fromPointId) {
        // 非法/同点: 还原 (restrictToBlock 已把输入限制在本块, 此处兜底)。
        refreshAngleRefRow(att);
        return;
    }
    if (auto* stack = m_doc->undoStack()) {
        stack->push(new cad::cmd::SetAlignPointCommand(
            m_doc, att->id, pointId));
    } else {
        auto* mut = m_doc->findAttachment(att->id);
        if (mut) {
            cad::cmd::SetAlignPointCommand cmd(m_doc, att->id, pointId);
            cmd.redo();
        }
    }
    refresh();
    emit changed();
}

void SegmentRefCard::rejectRefInput(const QString& reason)
{
    // 拒绝理由 toast (画布底部) —— 此前两条拒绝路径静默 refresh() 刷回,
    // 用户看到"点2 填不进内容"且无任何解释 (2026-09, E:\4.gcad L2 报告)。
    if (m_scene)
        m_scene->showToast(reason);
    // 对齐点输入框红闪 900ms, 与拒绝动作绑定 (toast 是场景级, 对话框内
    // 未必醒目)。
    if (m_alignPointEdit) {
        const QString saved = m_alignPointEdit->styleSheet();
        m_alignPointEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { border: 1px solid %1; border-radius: 3px;"
            " background: rgba(220,38,38,32); }")
            .arg(cad::ui::Theme::tokens().danger.name()));
        QTimer::singleShot(900, this, [this, saved] {
            if (m_alignPointEdit) m_alignPointEdit->setStyleSheet(saved);
        });
    }
    refresh();
}

void SegmentRefCard::onIndependentToggled(bool checked)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) { refresh(); return; }   // 无连接: 按钮本就禁用, 回弹。
    // [独立] (§6.4): 勾选 = 世界角度 (模型 ref 字段保留 = 唯一缓存);
    // 取消 = 还原上次基准 (SetAttachmentAngleIndependentCommand(false)
    // 反算回原基准/位置宿主, 零跳变)。不碰位置维度 (angleOnly)。
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleIndependentCommand(
            m_doc, att->id, /*angleIndependent=*/checked));
    else
        m_doc->setAttachmentAngleIndependent(att->id, checked);
    refresh();
    emit changed();
}

} // namespace cad::ui
