#include "LinePropertyDialog.h"

#include <algorithm>
#include <cmath>

#include <QTabWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QDebug>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QPushButton>
#include <QFrame>
#include <QMouseEvent>
#include <QColorDialog>
#include <QListWidget>
#include <QApplication>
#include <QClipboard>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/LinkedVariable.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "geometry/Angle.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/VariableCommands.h"
#include "AuxPointForm.h"
#include "IntersectionForm.h"
#include "PointRefEdit.h"

namespace cad::tools {

namespace {

/// Weight presets: 细 / 中 / 粗 / 自定义
constexpr double kWeightThin   = 0.8;
constexpr double kWeightMedium = 1.2;
constexpr double kWeightThick  = 2.0;

/// Format an angle in degrees for display: integers render without a trailing
/// ".0" (e.g. 22 -> "22", 22.5 -> "22.5").
QString formatAngleDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}


/// Shared stylesheet for card group boxes.
const char* kCardStyle =
    "QGroupBox { font-weight: bold; font-size: 12px; color: #37474F;"
    "  border: 1px solid #E0E6EA; border-radius: 6px; margin-top: 10px;"
    "  padding-top: 14px; background: #FDFDFE; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }";

/// One connection at an endpoint of the current segment.
struct ConnEntry {
    bool    isLeader = false;  ///< true = related segment is the leader (基准线).
    QString segSerial;
    QString segName;
    QString pointSerial;
    QString pointName;
    double  angle = 0.0;
    QUuid   blockId;
    QUuid   segmentId;
    QString layerBadge;  ///< Cross-layer badge ("→ 层名"); empty = same layer.
};

/// Cross-layer badge text for an attachment whose follower and leader live on
/// different layer kinds (合法方向: aux follower → working leader): returns
/// "→ <leader 所在层名>"; empty for same-layer attachments.
QString crossLayerBadge(cad::param::ParamDocument* doc,
                        const cad::param::Attachment& att)
{
    if (!doc) return QString();
    const cad::param::Block* from = doc->findBlock(att.fromBlockId);
    const cad::param::Block* to   = doc->findBlock(att.toBlockId);
    if (!from || !to) return QString();
    if (doc->isAuxBlock(*from) == doc->isAuxBlock(*to)) return QString();
    const auto& layers = doc->layers();
    if (to->layer < 0 || to->layer >= static_cast<int>(layers.size()))
        return QString();
    return QStringLiteral("\u2192 ")  // → <层名>
         + layers[static_cast<size_t>(to->layer)].name;
}

/// Toast text when a freshly established attachment crosses layers:
/// "已建立跨层连接（测量层→操作层1）" (real layer names). Empty when
/// same-layer or blocks are gone.
QString crossLayerToast(cad::param::ParamDocument* doc,
                        const cad::param::Block& from,
                        const cad::param::Block& to)
{
    if (!doc) return QString();
    if (doc->isAuxBlock(from) == doc->isAuxBlock(to)) return QString();
    const auto& layers = doc->layers();
    auto name = [&layers](int idx) {
        return (idx >= 0 && idx < static_cast<int>(layers.size()))
            ? layers[static_cast<size_t>(idx)].name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(from.layer), name(to.layer));
}

/// Clickable bubble card describing one connection. Single-click selects the
/// related segment on canvas; double-click jumps the dialog to edit it.
class ConnCard : public QFrame
{
public:
    ConnCard(const ConnEntry& e, CanvasScene* scene,
             LinePropertyDialog* dlg, QWidget* parent)
        : QFrame(parent), m_scene(scene), m_dlg(dlg)
        , m_blockId(e.blockId), m_segmentId(e.segmentId)
    {
        setStyleSheet(QStringLiteral(
            "border:1px solid #c9d6e0; border-radius:5px; background:#f7fafc;"));
        setCursor(Qt::PointingHandCursor);

        const QString role = e.isLeader ? QString::fromUtf8("\u57fa\u51c6\u7ebf")   // 基准线
                                        : QString::fromUtf8("\u8ddf\u968f\u7ebf");  // 跟随线
        const QString roleColor = e.isLeader ? QStringLiteral("#0078d7")
                                             : QStringLiteral("#b8860b");
        const QString angleLabel = e.isLeader
            ? QString::fromUtf8("跟随角度")        // 跟随角度（本线所有）
            : QString::fromUtf8("其跟随角度");    // 其跟随角度（跟随线所有）
        // Cross-layer badge ("→ 操作层1") appended after the arrow; empty for
        // same-layer connections (no markup, card keeps its original look).
        const QString badgeHtml = e.layerBadge.isEmpty() ? QString()
            : QStringLiteral(" <span style='color:#8e44ad;background:#f3e8ff;"
                             "border-radius:3px;padding:0 4px;font-size:10px;'>%1</span>")
                .arg(e.layerBadge.toHtmlEscaped());
        // Segment label: NAME first, serial as the fallback when unnamed
        // (有名称显示名称，无名称显示编号); same rule for the point.
        const QString segLabel = e.segName.isEmpty()
            ? cad::param::Serial::toHtml(e.segSerial)
            : e.segName.toHtmlEscaped();
        const QString pointLabel = e.pointName.isEmpty()
            ? cad::param::Serial::toHtml(e.pointSerial)
            : e.pointName.toHtmlEscaped();
        const QString html = QStringLiteral(
            "<div style='margin:2px;'>"
            "<div><b style='color:%1;'>[%2]</b> %3 &nbsp;%4 &nbsp;<span style='color:#0078d7;'>&rarr;</span>%9</div>"
            "<div style='color:#555;'>\u70b9 %5 &middot; %6 &middot; %7 &ang;%8&deg;</div>"
            "</div>")
            .arg(roleColor, role,
                 segLabel,
                 e.segName.isEmpty() ? QStringLiteral("") : cad::param::Serial::toHtml(e.segSerial),
                 cad::param::Serial::toHtml(e.pointSerial),
                 pointLabel,
                 angleLabel,
                 QString::number(e.angle, 'f', 1),
                 badgeHtml);

        setToolTip(e.isLeader
            ? QStringLiteral("基准线：当前线（跟随线）的端点吸附于该线。\n"
                             "跟随角度归属于当前线：以基准线在吸附点处的延长方向为 0°（直行），逆时针为正。")
            : QStringLiteral("跟随线：该线的端点吸附于当前线（基准线）。\n"
                             "显示的跟随角度归属于该跟随线；双击卡片可跳转编辑。"));

        auto* label = new QLabel(html, this);
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(5, 3, 5, 3);
        lay->addWidget(label);
    }

protected:
    void mousePressEvent(QMouseEvent*) override
    {
        if (m_scene && !m_blockId.isNull())
            m_scene->selectBlock(m_blockId);
    }
    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        if (m_dlg && !m_blockId.isNull() && !m_segmentId.isNull())
            m_dlg->setTarget(m_blockId, m_segmentId);
    }

private:
    CanvasScene* m_scene;
    LinePropertyDialog* m_dlg;
    QUuid m_blockId;
    QUuid m_segmentId;
};

} // namespace

LinePropertyDialog::LinePropertyDialog(const QUuid& blockId, const QUuid& segmentId,
                                       cad::param::ParamDocument* paramDoc,
                                       CanvasScene* scene,
                                       QWidget* parent,
                                       bool isCreation)
    : QDialog(parent)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_paramDoc(paramDoc)
    , m_isCreation(isCreation)
    , m_scene(scene)
{
    setWindowTitle(QString::fromUtf8("\u7ebf\u6761\u5c5e\u6027"));  // 线条属性
    setMinimumWidth(480);

    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    buildPage1(m_tabs);
    buildAnchorTab(m_tabs);
    buildPage3(m_tabs);
    buildPage4(m_tabs);
    mainLayout->addWidget(m_tabs);

    auto* buttons = new QDialogButtonBox(this);
    auto* btnClose = buttons->addButton(
        QString::fromUtf8("\u5173\u95ed"), QDialogButtonBox::AcceptRole);  // 关闭
    auto* btnRevert = buttons->addButton(
        QString::fromUtf8("\u64a4\u9500\u5168\u90e8"), QDialogButtonBox::RejectRole);  // 撤销全部
    mainLayout->addWidget(buttons);

    connect(btnClose,  &QPushButton::clicked, this, &LinePropertyDialog::onAccepted);
    connect(btnRevert, &QPushButton::clicked, this, &LinePropertyDialog::onRejected);

    // Global debounce timer: 200ms after last keystroke → auto-apply
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, &LinePropertyDialog::onDebounceTimeout);

    populateFromModel();

    // Give the dialog a sane initial size. Without this Qt sizes it from the
    // (not-yet-populated) layout sizeHint at show() time, which can collapse
    // to just the tab-bar height and trigger a QWindowsWindow::setGeometry
    // warning when the platform clamps to the (much taller) minimum size.
    resize(600, 900);
    connectLiveSignals();
    applyCanvasHighlight();

    // Canvas→Panel sync: when the document resolves (e.g. handle dragged on
    // canvas), refresh the anchor edit fields so the panel tracks the canvas.
    connect(m_paramDoc, &cad::param::ParamDocument::resolved, this, [this]() {
        if (!m_anchorList || m_anchorPointIds.empty()) return;
        int row = m_anchorList->currentRow();
        if (row >= 0)
            refreshAnchorFields(row);
    });
}

LinePropertyDialog::~LinePropertyDialog()
{
    clearCanvasHighlight();
}

void LinePropertyDialog::applyCanvasHighlight()
{
    if (m_scene && !m_blockId.isNull())
        m_scene->selectBlock(m_blockId);
}

void LinePropertyDialog::clearCanvasHighlight()
{
    if (!m_scene) return;
    if (auto* item = m_scene->findBlockItem(m_blockId))
        item->setSelected(false);
}

void LinePropertyDialog::connectLiveSignals()
{
    // Live update for non-text-input fields (immediate)
    connect(m_editName,      &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbRole,       &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowName,   &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowLength, &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkVisible,    &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editStartName, &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowStartName, &QCheckBox::toggled,         this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editEndName,   &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowEndName, &QCheckBox::toggled,           this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbStyle,      &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_spinWeight,    &QDoubleSpinBox::valueChanged,   this, &LinePropertyDialog::onLiveUpdate);
    connect(m_btnColor,      &QPushButton::clicked,           this, &LinePropertyDialog::onColorPick);
    connect(m_cmbWeight,     &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onWeightPresetChanged);

    // Length: textChanged restarts debounce; editingFinished (Enter/focus-loss) applies immediately
    connect(m_editLength, &QLineEdit::textChanged,     this, &LinePropertyDialog::onLengthDirty);
    connect(m_editLength, &QLineEdit::editingFinished,  this, &LinePropertyDialog::onLengthApply);

    // Angle: same pattern as length
    connect(m_editAngle, &QLineEdit::textChanged,     this, &LinePropertyDialog::onAngleDirty);
    connect(m_editAngle, &QLineEdit::editingFinished, this, &LinePropertyDialog::onAngleApply);

    // Tension (curve only): apply on Enter/focus-loss
    connect(m_editTension, &QLineEdit::editingFinished, this, [this] {
        if (!m_paramDoc) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        if (!seg || !seg->isCurve()) return;
        bool ok = false;
        double val = m_editTension->text().toDouble(&ok);
        if (ok && std::abs(val - seg->tension) > 1e-9) {
            seg->tension = val;
            m_paramDoc->resolveAll();
            refreshScene();
            populateFromModel();
        }
    });

    // Angle mode toggle (angle ↔ arc length) for follower lines.
    connect(m_btnAngleMode, &QPushButton::clicked, [this] {
        const cad::param::Attachment* att = findFollowerAttachment();
        if (!att || !m_paramDoc) return;
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        cad::param::Attachment* mutAtt = nullptr;
        for (auto& a : atts) { if (a.id == att->id) { mutAtt = &a; break; } }
        if (!mutAtt) return;

        // Geometry-preserving switch.
        cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
        double radius = blk ? blk->segmentLengthAtPoint(mutAtt->fromPointId) : 0.0;
        double curDeg = mutAtt->followerAngle;
        if (mutAtt->rotationMode == cad::param::RotationMode::ArcLength) {
            double arcMm = mutAtt->arcLength;
            if (!mutAtt->arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    mutAtt->arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
            }
            // Arc is measured from the REVERSE direction: 弧长 0 = 角度 180°.
            // Normalized to [0, 360°) so multi-turn arcs never overflow the
            // angle when switching back (用户报告: 400°+ 爆表回归 2026-08).
            curDeg = (radius > 1e-9) ? 180.0 + (arcMm / radius) * 180.0 / M_PI : 180.0;
            curDeg = std::fmod(curDeg, 360.0);
            if (curDeg < 0.0) curDeg += 360.0;
        } else if (!mutAtt->followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                mutAtt->followerAngleFormula, m_paramDoc->parameters(), {});
            if (r.ok) curDeg = r.value;
        }

        if (mutAtt->rotationMode == cad::param::RotationMode::Angle) {
            mutAtt->rotationMode = cad::param::RotationMode::ArcLength;
            // Arc measured from the REVERSE direction (弧长 0 = 角度 180°),
            // normalized to [0, 360°).
            double degFromReverse = std::fmod(curDeg - 180.0, 360.0);
            if (degFromReverse < 0.0) degFromReverse += 360.0;
            mutAtt->arcLength = degFromReverse * M_PI / 180.0 * radius;
            mutAtt->arcLengthFormula.clear();
        } else {
            mutAtt->rotationMode = cad::param::RotationMode::Angle;
            mutAtt->followerAngle = curDeg;
            mutAtt->followerAngleFormula.clear();
        }
        m_paramDoc->resolveAll();
        refreshScene();
        populateAngleField();
    });

    // ── 跟随角度·连接 card signals ──
    connect(m_refConnPoint, &PointRefEdit::pointResolved,
            this, &LinePropertyDialog::onConnPointResolved);
    connect(m_btnClearConn, &QPushButton::clicked,
            this, &LinePropertyDialog::onConnClear);
    connect(m_refConnectTo, &PointRefEdit::pointResolved,
            this, &LinePropertyDialog::onConnectToResolved);
    connect(m_chkFollowHost, &QCheckBox::toggled,
            this, &LinePropertyDialog::onFollowHostToggled);
    connect(m_chkLockConn, &QCheckBox::toggled,
            this, &LinePropertyDialog::onLockConnToggled);

    // ── 终点指向 card signals ──
    connect(m_refAimTarget, &PointRefEdit::pointResolved,
            this, &LinePropertyDialog::onAimTargetResolved);
    connect(m_editAimOffset, &QLineEdit::editingFinished,
            this, &LinePropertyDialog::onAimOffsetApply);
    connect(m_btnClearAim, &QPushButton::clicked,
            this, &LinePropertyDialog::onAimClear);
    connect(m_chkAimHost, &QCheckBox::toggled,
            this, &LinePropertyDialog::onAimHostToggled);
}

void LinePropertyDialog::buildPage1(QTabWidget* tabs)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(6);

    // ─── Card: 基本信息 ───
    auto* grpIdentity = new QGroupBox(QString::fromUtf8("\u57fa\u672c\u4fe1\u606f"), page);
    grpIdentity->setStyleSheet(kCardStyle);
    auto* idLayout = new QHBoxLayout(grpIdentity);

    idLayout->addWidget(new QLabel(QString::fromUtf8("\u540d\u79f0:"), grpIdentity));  // 名称:
    m_editName = new QLineEdit(grpIdentity);
    m_editName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u80a9\u7ebf\u201d\u201c\u4fa7\u7f1d\u201d"));  // 如"肩线""侧缝"
    idLayout->addWidget(m_editName, 1);

    idLayout->addWidget(new QLabel(QString::fromUtf8("\u7c7b\u578b:"), grpIdentity));  // 类型:
    m_cmbRole = new QComboBox(grpIdentity);
    m_cmbRole->addItem(QString::fromUtf8("\u8f6e\u5ed3\u7ebf"));  // 轮廓线
    m_cmbRole->addItem(QString::fromUtf8("\u5185\u90e8\u7ebf"));  // 内部线
    m_cmbRole->addItem(QString::fromUtf8("\u8f85\u52a9\u7ebf"));  // 辅助线
    idLayout->addWidget(m_cmbRole);

    idLayout->addWidget(new QLabel(QString::fromUtf8("\u7f16\u53f7:"), grpIdentity));  // 编号:
    m_lblSegId = new QLabel(grpIdentity);
    m_lblSegId->setTextFormat(Qt::RichText);
    m_lblSegId->setStyleSheet("color:#999; font-size:11px;");
    idLayout->addWidget(m_lblSegId);

    layout->addWidget(grpIdentity);

    // ─── Card: 几何 ───
    auto* grpGeom = new QGroupBox(QString::fromUtf8("\u51e0\u4f55"), page);  // 几何
    grpGeom->setStyleSheet(kCardStyle);
    auto* geomGrid = new QGridLayout(grpGeom);
    geomGrid->setHorizontalSpacing(8);
    geomGrid->setVerticalSpacing(6);

    // Row 0 — 长度: [fx] [input stretches] [填入]
    geomGrid->addWidget(new QLabel(QString::fromUtf8("\u957f\u5ea6(cm):"), grpGeom), 0, 0);  // 长度(cm):
    m_lblFx = new QLabel(QStringLiteral("<i style='color:#0078d7;'>fx</i>"), grpGeom);
    m_lblFx->setVisible(false);
    m_lblFx->setFixedWidth(18);
    geomGrid->addWidget(m_lblFx, 0, 1);
    m_editLength = new QLineEdit(grpGeom);
    m_editLength->setMinimumWidth(140);
    m_editLength->setPlaceholderText(
        QString::fromUtf8("\u6570\u503c(cm)\u6216\u516c\u5f0f"));  // 数值(cm)或公式
    geomGrid->addWidget(m_editLength, 0, 2);
    geomGrid->setColumnStretch(2, 1);
    auto* btnPasteLen = new QPushButton(QStringLiteral("填入"), grpGeom);
    btnPasteLen->setToolTip(QStringLiteral("清空输入框并粘贴剪切板内容"));
    connect(btnPasteLen, &QPushButton::clicked, this, [this] {
        const QString clean = QString(QApplication::clipboard()->text())
                                  .remove(QLatin1Char('\r'))
                                  .remove(QLatin1Char('\n'))
                                  .trimmed();
        if (!clean.isEmpty())
            m_editLength->setText(clean);
    });
    geomGrid->addWidget(btnPasteLen, 0, 3, Qt::AlignLeft | Qt::AlignVCenter);

    // Row 1 — [x] 显示长度标注  实际长度(只读) …… [发布长度参数]
    auto* geomActionRow = new QHBoxLayout();
    m_chkShowLength = new QCheckBox(QString::fromUtf8("显示长度标注"), grpGeom);
    geomActionRow->addWidget(m_chkShowLength);
    // Read-only resolved length: when the length field holds a formula /
    // reference name (e.g. M_xxx) the actual value is not visible at a glance.
    m_lblActualLength = new QLabel(grpGeom);
    m_lblActualLength->setStyleSheet(
        "color:#9E9E9E; font-size:11px; background:transparent;");
    m_lblActualLength->setToolTip(QString::fromUtf8("当前实际长度（只读）"));
    geomActionRow->addWidget(m_lblActualLength);
    geomActionRow->addStretch();
    m_btnPublishLen = new QPushButton(QString::fromUtf8("发布长度参数"), grpGeom);
    m_btnPublishLen->setToolTip(QString::fromUtf8(
        "将此线段的长度发布为关联参数，其他公式可引用"));
    m_btnPublishLen->setCursor(Qt::PointingHandCursor);
    m_btnPublishLen->setStyleSheet(
        "QPushButton { font-size: 11px; color: #26A69A; background: #E0F2F1;"
        "  border: 1px solid #B2DFDB; border-radius: 3px; padding: 3px 8px; }"
        "QPushButton:hover { background: #B2DFDB; }"
        "QPushButton:disabled { color: #B0BEC5; background: #ECEFF1; border-color: #CFD8DC; }");
    geomActionRow->addWidget(m_btnPublishLen);
    connect(m_btnPublishLen, &QPushButton::clicked,
            this, &LinePropertyDialog::onPublishLength);
    geomGrid->addLayout(geomActionRow, 1, 0, 1, 4);

    // Row 2 — 弧长 (curve only, read-only)
    m_arcRow = new QWidget(grpGeom);
    auto* arcLayout = new QHBoxLayout(m_arcRow);
    arcLayout->setContentsMargins(0, 0, 0, 0);
    arcLayout->addWidget(new QLabel(QString::fromUtf8("弧长(cm):"), m_arcRow));
    m_lblArcLength = new QLabel(QStringLiteral("—"), m_arcRow);
    m_lblArcLength->setStyleSheet("color:#00695C; font-weight:bold;");
    arcLayout->addWidget(m_lblArcLength, 1);
    m_arcRow->setVisible(false);
    geomGrid->addWidget(m_arcRow, 2, 0, 1, 4);

    // Row 3 — 张力 (curve only)
    m_tensionRow = new QWidget(grpGeom);
    auto* tensionLayout = new QHBoxLayout(m_tensionRow);
    tensionLayout->setContentsMargins(0, 0, 0, 0);
    m_lblTension = new QLabel(QString::fromUtf8("张力:"), m_tensionRow);
    tensionLayout->addWidget(m_lblTension);
    m_editTension = new QLineEdit(m_tensionRow);
    m_editTension->setMaximumWidth(80);
    m_editTension->setPlaceholderText(QStringLiteral("0"));
    m_editTension->setToolTip(QString::fromUtf8("0=平滑(Catmull-Rom)  >0更紧  <0更松"));
    tensionLayout->addWidget(m_editTension);
    tensionLayout->addStretch();
    m_tensionRow->setVisible(false);
    geomGrid->addWidget(m_tensionRow, 3, 0, 1, 4);

    // Row 4 — 转换按钮
    m_btnConvert = new QPushButton(grpGeom);
    m_btnConvert->setCursor(Qt::PointingHandCursor);
    m_btnConvert->setStyleSheet(
        "QPushButton { font-size: 11px; color: #5C6BC0; background: #E8EAF6;"
        "  border: 1px solid #C5CAE9; border-radius: 3px; padding: 3px 8px; }"
        "QPushButton:hover { background: #C5CAE9; }");
    geomGrid->addWidget(m_btnConvert, 4, 0, 1, 2);
    connect(m_btnConvert, &QPushButton::clicked, this, [this] {
        if (!m_paramDoc) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        // 仅支持 曲线 → 直线（一键移除全部曲线点）。直线 → 曲线请直接用
        // 智能笔点击线身添加曲线点——旧的"转为曲线"会插入一个 Interpolated
        // 过点（旧曲线笔模型遗留），与新范式的 CurveAnchor 冲突，已移除。
        if (!seg || !seg->isCurve()) return;

        for (const auto& ppId : seg->passPointIds) {
            auto& pts = block->points;
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&](const cad::param::ParamPoint& p) { return p.id == ppId; }),
                pts.end());
        }
        block->rebuildPointIndex();
        seg->passPointIds.clear();
        seg->type = cad::param::SegmentType::Line;

        m_paramDoc->resolveAll();
        refreshScene();
        populateFromModel();
    });

    layout->addWidget(grpGeom);

    // ─── Card: 跟随角度 · 连接 ───
    m_grpAngle = new QGroupBox(QString::fromUtf8("跟随角度 · 连接"), page);
    m_grpAngle->setStyleSheet(kCardStyle);
    auto* angleLayout = new QVBoxLayout(m_grpAngle);
    angleLayout->setSpacing(6);

    // Connected-state row: 基准线 label + 指向点 ref (editable).
    m_connRow = new QWidget(m_grpAngle);
    auto* connLayout = new QHBoxLayout(m_connRow);
    connLayout->setContentsMargins(0, 0, 0, 0);
    connLayout->addWidget(new QLabel(QString::fromUtf8("\u57fa\u51c6\u7ebf:"), m_connRow));  // 基准线:
    m_lblLeaderRef = new QLabel(m_connRow);
    m_lblLeaderRef->setStyleSheet("color:#0078d7; font-size:12px;");
    connLayout->addWidget(m_lblLeaderRef, 1);
    m_lblLayerBadge = new QLabel(m_connRow);
    // Stylesheet set ONCE at construction (每帧 setStyleSheet 会致卡顿).
    m_lblLayerBadge->setStyleSheet(QStringLiteral(
        "color:#8e44ad; background:#f3e8ff; border-radius:3px;"
        "padding:0 4px; font-size:11px;"));
    m_lblLayerBadge->setToolTip(QString::fromUtf8(
        "\u8de8\u5c42\u8fde\u63a5\uff1a\u57fa\u51c6\u7ebf\u4f4d\u4e8e\u53e6\u4e00\u56fe\u5c42"));  // 跨层连接：基准线位于另一图层
    m_lblLayerBadge->setVisible(false);
    connLayout->addWidget(m_lblLayerBadge);
    connLayout->addWidget(new QLabel(QString::fromUtf8("\u6307\u5411\u70b9:"), m_connRow));  // 指向点:
    m_refConnPoint = new PointRefEdit(m_paramDoc, m_connRow);
    m_refConnPoint->setMaximumWidth(150);
    connLayout->addWidget(m_refConnPoint);
    m_btnClearConn = new QPushButton(QString::fromUtf8("\u6e05\u9664"), m_connRow);  // 清除
    m_btnClearConn->setToolTip(QString::fromUtf8(
        "\u62c6\u9664\u8fde\u63a5\uff08\u5220\u9664\u9644\u7740\uff09\uff0c\u7ebf\u6bb5\u6062\u590d\u4e3a\u81ea\u7531\u72b6\u6001"));  // 拆除连接（删除附着），线段恢复为自由状态
    m_btnClearConn->setCursor(Qt::PointingHandCursor);
    connLayout->addWidget(m_btnClearConn);
    angleLayout->addWidget(m_connRow);

    // Free-state row: 跟随宿主 input (typed P number establishes the follow).
    m_freeConnRow = new QWidget(m_grpAngle);
    auto* freeConnLayout = new QHBoxLayout(m_freeConnRow);
    freeConnLayout->setContentsMargins(0, 0, 0, 0);
    auto* lblConnectTo = new QLabel(
        QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b:"), m_freeConnRow);  // 跟随宿主:
    lblConnectTo->setToolTip(QString::fromUtf8(
        "\u8f93\u5165\u76ee\u6807\u70b9 P \u7f16\u53f7\u5e76\u56de\u8f66\uff0c\u5373\u53ef\u5c06\u672c\u7ebf\u8d34\u9644\u5230\u8be5\u70b9\uff08\u5efa\u7acb\u8ddf\u968f\u89d2\u5ea6\u8fde\u63a5\uff09\uff1b\u4e0b\u65b9\u201c\u8ddf\u968f\u5bbf\u4e3b\u201d\u590d\u9009\u6846\u53ef\u4e00\u952e\u65ad\u5f00"));
    // 输入目标点 P 编号并回车，即可将本线贴附到该点（建立跟随角度连接）；下方“跟随宿主”复选框可一键断开
    freeConnLayout->addWidget(lblConnectTo);
    m_refConnectTo = new PointRefEdit(m_paramDoc, m_freeConnRow);
    m_refConnectTo->setMaximumWidth(150);
    freeConnLayout->addWidget(m_refConnectTo);
    freeConnLayout->addStretch();
    angleLayout->addWidget(m_freeConnRow);

    // Angle value row: [mode] [caption] [fx] [input] [world-angle readout]
    auto* angleRow = new QHBoxLayout();
    angleRow->setSpacing(6);
    m_btnAngleMode = new QPushButton(m_grpAngle);
    m_btnAngleMode->setFixedSize(26, 22);
    m_btnAngleMode->setCursor(Qt::PointingHandCursor);
    m_btnAngleMode->setToolTip(QString::fromUtf8("\u5207\u6362\u89d2\u5ea6/\u5f27\u957f\u6a21\u5f0f"));  // 切换角度/弧长模式
    m_btnAngleMode->setVisible(false);
    angleRow->addWidget(m_btnAngleMode);
    m_lblAngleCaption = new QLabel(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"), m_grpAngle);  // 角度(°):
    angleRow->addWidget(m_lblAngleCaption);
    m_lblFxAngle = new QLabel(QStringLiteral("<i style='color:#0078d7;'>fx</i>"), m_grpAngle);
    m_lblFxAngle->setVisible(false);
    m_lblFxAngle->setFixedWidth(18);
    angleRow->addWidget(m_lblFxAngle);
    m_editAngle = new QLineEdit(m_grpAngle);
    m_editAngle->setMinimumWidth(120);
    m_editAngle->setPlaceholderText(
        QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
    m_editAngle->setToolTip(QString::fromUtf8(
        "\u81ea\u7531\u7ebf\uff1a\u4e16\u754c\u89d2\u5ea6\uff1b\u8ddf\u968f\u7ebf\uff1a\u6784\u9020\u89d2\u3002\u9006\u65f6\u9488\u4e3a\u6b63\uff0c\u56de\u8f66\u786e\u8ba4"));
    angleRow->addWidget(m_editAngle, 1);
    m_lblWorldAngle = new QLabel(m_grpAngle);
    m_lblWorldAngle->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
    m_lblWorldAngle->setVisible(false);
    angleRow->addWidget(m_lblWorldAngle);
    angleLayout->addLayout(angleRow);

    // Follow-host checkbox (ALL lines): checked = start point follows the
    // connection target; unchecking detaches (visible via refreshAngleCard).
    m_chkFollowHost = new QCheckBox(m_grpAngle);
    m_chkFollowHost->setStyleSheet("color:#00838F;");
    angleLayout->addWidget(m_chkFollowHost);

    // Lock checkbox (ALL lines, no hidden items): checked = the connection is
    // LOCKED (锁定连接/焊接) — dragging cannot tear it apart, dragging either
    // side moves the whole pair. Disabled while the line is free (no
    // connection to lock); refreshAngleCard syncs text/state.
    m_chkLockConn = new QCheckBox(QString::fromUtf8("\u9501\u5b9a\u8fde\u63a5"), m_grpAngle);  // 锁定连接
    m_chkLockConn->setStyleSheet("color:#00838F;");
    m_chkLockConn->setToolTip(QString::fromUtf8(
        "\u52fe\u9009\u540e\u8fde\u63a5\u9501\u5b9a\uff1a\u62d6\u52a8\u4efb\u4e00\u7aef\u65f6\u6574\u4e2a\u5bf9\u4e00\u8d77\u79fb\u52a8\uff0c\u4e0d\u4f1a\u88ab\u62d6\u62c6\uff1b"
        "\u53d6\u6d88\u52fe\u9009\u89e3\u9501\u540e\u62d6\u52a8\u8ddf\u968f\u7ebf\u5373\u53ef\u62c6\u6563\u3002\u8f85\u52a9\u5c42\u7684\u8fde\u63a5\u9ed8\u8ba4\u9501\u5b9a\u3002"));
    // 勾选后连接锁定：拖动任一端时整个对一起移动，不会被拖拆；取消勾选解锁后拖动跟随线即可拆散。辅助层的连接默认锁定。
    angleLayout->addWidget(m_chkLockConn);

    layout->addWidget(m_grpAngle);

    // ─── Card: 终点指向 ───
    auto* grpAim = new QGroupBox(QString::fromUtf8("\u7ec8\u70b9\u6307\u5411"), page);  // 终点指向
    grpAim->setStyleSheet(kCardStyle);
    auto* aimLayout = new QVBoxLayout(grpAim);
    aimLayout->setSpacing(6);

    auto* aimRow = new QHBoxLayout();
    aimRow->setSpacing(6);
    auto* lblAimCaption = new QLabel(QString::fromUtf8("\u6307\u5411\u70b9:"), grpAim);  // 指向点:
    lblAimCaption->setStyleSheet(QStringLiteral("color:#E65100;"));
    aimRow->addWidget(lblAimCaption);
    m_refAimTarget = new PointRefEdit(m_paramDoc, grpAim);
    m_refAimTarget->setMaximumWidth(150);
    m_refAimTarget->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9\u65b9\u5411\u59cb\u7ec8\u6307\u5411\u8be5\u70b9\uff08\u914d\u5408\u957f\u5ea6\u53ef\u8ba9\u7ec8\u70b9\u843d\u5728\u76ee\u6807\u4e0a\uff09\u3002\u8f93\u5165 P \u7f16\u53f7\u56de\u8f66\u8bbe\u7f6e"));
    // 终点方向始终指向该点（配合长度可让终点落在目标上）。输入 P 编号回车设置
    aimRow->addWidget(m_refAimTarget);
    aimRow->addWidget(new QLabel(QString::fromUtf8("\u504f\u79fb(\u00b0):"), grpAim));  // 偏移(°):
    m_editAimOffset = new QLineEdit(grpAim);
    m_editAimOffset->setMaximumWidth(70);
    m_editAimOffset->setToolTip(QString::fromUtf8(
        "\u76f8\u5bf9\u7cbe\u786e\u6307\u5411\u65b9\u5411\u7684\u504f\u79fb\u89d2\uff0c0=\u7cbe\u786e\u6307\u5411\u76ee\u6807\u70b9"));
    aimRow->addWidget(m_editAimOffset);
    m_btnClearAim = new QPushButton(QString::fromUtf8("\u6e05\u9664"), grpAim);  // 清除
    m_btnClearAim->setToolTip(QString::fromUtf8("\u89e3\u9664\u7ec8\u70b9\u6307\u5411\u7ea6\u675f\uff0c\u89d2\u5ea6\u6062\u590d\u4e3a\u81ea\u7531\u4e16\u754c\u89d2"));
    m_btnClearAim->setCursor(Qt::PointingHandCursor);
    aimRow->addWidget(m_btnClearAim);
    aimRow->addStretch();
    aimLayout->addLayout(aimRow);

    // Aim-host checkbox (ALL lines): checked = the end point always aims at
    // the target; unchecking releases the aim (visible via refreshAimCard).
    m_chkAimHost = new QCheckBox(grpAim);
    m_chkAimHost->setStyleSheet("color:#00838F;");
    aimLayout->addWidget(m_chkAimHost);

    layout->addWidget(grpAim);

    // ─── Card: 外观 ───
    auto* grpAppear = new QGroupBox(QString::fromUtf8("\u5916\u89c2"), page);  // 外观
    grpAppear->setStyleSheet(kCardStyle);
    auto* appearLayout = new QFormLayout(grpAppear);

    m_cmbStyle = new QComboBox(grpAppear);
    m_cmbStyle->addItem(QString::fromUtf8("\u5b9e\u7ebf"));   // 实线
    m_cmbStyle->addItem(QString::fromUtf8("\u865a\u7ebf"));   // 虚线
    m_cmbStyle->addItem(QString::fromUtf8("\u70b9\u7ebf"));   // 点线
    appearLayout->addRow(QString::fromUtf8("\u7ebf\u578b:"), m_cmbStyle);  // 线型:

    auto* weightRow = new QHBoxLayout();
    m_cmbWeight = new QComboBox(grpAppear);
    m_cmbWeight->addItem(QString::fromUtf8("\u7ec6 (0.8)"), kWeightThin);     // 细
    m_cmbWeight->addItem(QString::fromUtf8("\u4e2d (1.2)"), kWeightMedium);   // 中
    m_cmbWeight->addItem(QString::fromUtf8("\u7c97 (2.0)"), kWeightThick);    // 粗
    m_cmbWeight->addItem(QString::fromUtf8("\u81ea\u5b9a\u4e49"));            // 自定义
    weightRow->addWidget(m_cmbWeight);

    m_spinWeight = new QDoubleSpinBox(grpAppear);
    m_spinWeight->setRange(0.5, 10.0);
    m_spinWeight->setSingleStep(0.2);
    m_spinWeight->setDecimals(1);
    m_spinWeight->setVisible(false);
    weightRow->addWidget(m_spinWeight);
    appearLayout->addRow(QString::fromUtf8("\u7c97\u7ec6:"), weightRow);  // 粗细:

    m_btnColor = new QPushButton(grpAppear);
    m_btnColor->setFixedSize(60, 22);
    m_btnColor->setCursor(Qt::PointingHandCursor);
    appearLayout->addRow(QString::fromUtf8("\u989c\u8272:"), m_btnColor);  // 颜色:

    m_chkVisible = new QCheckBox(QString::fromUtf8("\u53ef\u89c1"), grpAppear);  // 可见
    m_chkVisible->setChecked(true);
    appearLayout->addRow(QString(), m_chkVisible);

    m_chkShowName = new QCheckBox(QString::fromUtf8("\u5728\u753b\u5e03\u4e0a\u663e\u793a\u540d\u79f0"), grpAppear);  // 在画布上显示名称
    appearLayout->addRow(QString(), m_chkShowName);

    layout->addWidget(grpAppear);

    // ─── 端点 section: 起点 | 终点 side by side ───
    auto* ptRow = new QHBoxLayout();

    // Start point card
    auto* grpStart = new QGroupBox(QString::fromUtf8("\u8d77\u70b9"), page);  // 起点
    grpStart->setStyleSheet(kCardStyle);
    auto* startLayout = new QFormLayout(grpStart);
    m_lblStartPtId = new QLabel(grpStart);
    m_lblStartPtId->setTextFormat(Qt::RichText);
    m_lblStartPtId->setStyleSheet("color:#999; font-size:11px;");
    startLayout->addRow(QString::fromUtf8("\u7f16\u53f7:"), m_lblStartPtId);  // 编号:
    m_editStartName = new QLineEdit(grpStart);
    m_editStartName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u80a9\u70b9\u201d"));  // 如"肩点"
    startLayout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editStartName);  // 名称:
    m_chkShowStartName = new QCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), grpStart);  // 显示名称
    startLayout->addRow(QString(), m_chkShowStartName);
    m_editStartAnno = new QLineEdit(grpStart);
    startLayout->addRow(QString::fromUtf8("\u5907\u6ce8:"), m_editStartAnno);  // 备注:
    ptRow->addWidget(grpStart);

    // End point card
    auto* grpEnd = new QGroupBox(QString::fromUtf8("\u7ec8\u70b9"), page);  // 终点
    grpEnd->setStyleSheet(kCardStyle);
    auto* endLayout = new QFormLayout(grpEnd);
    m_lblEndPtId = new QLabel(grpEnd);
    m_lblEndPtId->setTextFormat(Qt::RichText);
    m_lblEndPtId->setStyleSheet("color:#999; font-size:11px;");
    endLayout->addRow(QString::fromUtf8("\u7f16\u53f7:"), m_lblEndPtId);  // 编号:
    m_editEndName = new QLineEdit(grpEnd);
    m_editEndName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u9888\u70b9\u201d"));  // 如"颈点"
    endLayout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editEndName);  // 名称:
    m_chkShowEndName = new QCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), grpEnd);  // 显示名称
    endLayout->addRow(QString(), m_chkShowEndName);
    m_editEndAnno = new QLineEdit(grpEnd);
    endLayout->addRow(QString::fromUtf8("\u5907\u6ce8:"), m_editEndAnno);  // 备注:
    ptRow->addWidget(grpEnd);

    layout->addLayout(ptRow);
    layout->addStretch();

    tabs->addTab(page, QString::fromUtf8("属性"));  // 属性
}

void LinePropertyDialog::buildAnchorTab(QTabWidget* tabs)
{
    m_anchorTab = new QWidget(this);
    auto* layout = new QVBoxLayout(m_anchorTab);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(QString::fromUtf8("锚点列表（起点 + 曲线点 + 终点）:"), m_anchorTab));

    m_anchorList = new QListWidget(m_anchorTab);
    m_anchorList->setMaximumHeight(100);
    layout->addWidget(m_anchorList);

    // --- Tangent mode row ---
    auto* tanRow = new QHBoxLayout();
    tanRow->addWidget(new QLabel(QString::fromUtf8("切线模式:"), m_anchorTab));
    m_cmbTanMode = new QComboBox(m_anchorTab);
    m_cmbTanMode->addItem(QString::fromUtf8("自动 (C2)"));
    m_cmbTanMode->addItem(QString::fromUtf8("手动 (Bézier)"));
    tanRow->addWidget(m_cmbTanMode, 1);
    layout->addLayout(tanRow);

    // --- Tangent-In card ---
    auto* inRow = new QHBoxLayout();
    inRow->addWidget(new QLabel(QString::fromUtf8("入切线角(°):"), m_anchorTab));
    m_spinTanAngleIn = new QDoubleSpinBox(m_anchorTab);
    m_spinTanAngleIn->setRange(-360.0, 360.0);
    m_spinTanAngleIn->setDecimals(1);
    m_spinTanAngleIn->setSuffix(QString::fromUtf8("°"));
    inRow->addWidget(m_spinTanAngleIn, 1);
    inRow->addWidget(new QLabel(QString::fromUtf8("长度:"), m_anchorTab));
    m_spinTanLenIn = new QDoubleSpinBox(m_anchorTab);
    m_spinTanLenIn->setRange(0.0, 999.0);
    m_spinTanLenIn->setDecimals(2);
    m_spinTanLenIn->setSuffix(QStringLiteral(" cm"));
    inRow->addWidget(m_spinTanLenIn, 1);
    layout->addLayout(inRow);

    // --- Tangent-Out card ---
    auto* outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(QString::fromUtf8("出切线角(°):"), m_anchorTab));
    m_spinTanAngleOut = new QDoubleSpinBox(m_anchorTab);
    m_spinTanAngleOut->setRange(-360.0, 360.0);
    m_spinTanAngleOut->setDecimals(1);
    m_spinTanAngleOut->setSuffix(QString::fromUtf8("°"));
    outRow->addWidget(m_spinTanAngleOut, 1);
    outRow->addWidget(new QLabel(QString::fromUtf8("长度:"), m_anchorTab));
    m_spinTanLenOut = new QDoubleSpinBox(m_anchorTab);
    m_spinTanLenOut->setRange(0.0, 999.0);
    m_spinTanLenOut->setDecimals(2);
    m_spinTanLenOut->setSuffix(QStringLiteral(" cm"));
    outRow->addWidget(m_spinTanLenOut, 1);
    layout->addLayout(outRow);

    // --- Smooth/Corner ---
    auto* optRow = new QHBoxLayout();
    m_chkTanLocked = new QCheckBox(QString::fromUtf8("平滑(共线)"), m_anchorTab);
    m_chkTanLocked->setChecked(true);
    optRow->addWidget(m_chkTanLocked);
    optRow->addStretch();
    layout->addLayout(optRow);

    // --- Follow info + release button (curve points only) ---
    auto* followRow = new QHBoxLayout();
    m_lblFollowInfo = new QLabel(m_anchorTab);
    m_lblFollowInfo->setStyleSheet("color:#555; font-size:11px;");
    m_lblFollowInfo->setWordWrap(true);
    followRow->addWidget(m_lblFollowInfo, 1);
    m_btnReleaseFollow = new QPushButton(QString::fromUtf8("释放"), m_anchorTab);
    m_btnReleaseFollow->setCursor(Qt::PointingHandCursor);
    m_btnReleaseFollow->setMaximumWidth(60);
    followRow->addWidget(m_btnReleaseFollow);
    layout->addLayout(followRow);
    connect(m_btnReleaseFollow, &QPushButton::clicked, this, [this]() {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[row]);
        if (!pt) return;
        // Release the follow connection.
        pt->followBlockId = QUuid();
        pt->followPointId = QUuid();
        pt->followOffset = cad::geo::Vec2::zero();
        m_paramDoc->resolveAll();
        refreshScene();
        refreshAnchorFields(row);
    });

    // Reset button
    m_btnResetTan = new QPushButton(QString::fromUtf8("重置为自动切线"), m_anchorTab);
    m_btnResetTan->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_btnResetTan);

    // Info label
    m_lblTanInfo = new QLabel(m_anchorTab);
    m_lblTanInfo->setStyleSheet("color:#666; font-size:11px;");
    m_lblTanInfo->setWordWrap(true);
    layout->addWidget(m_lblTanInfo);

    layout->addStretch();

    // --- Signals ---
    connect(m_cmbTanMode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[row]);
        if (!pt) return;
        pt->autoTangent = (idx == 0);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        refreshScene();
    });
    connect(m_btnResetTan, &QPushButton::clicked, this, [this] {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[row]);
        if (!pt) return;
        pt->autoTangent = true;
        pt->tangentIn = cad::geo::Vec2::zero();
        pt->tangentOut = cad::geo::Vec2::zero();
        m_cmbTanMode->setCurrentIndex(0);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        refreshScene();
    });
    connect(m_chkTanLocked, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[row]);
        if (!pt) return;
        pt->tangentLocked = on;
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        refreshScene();
    });
    // Tangent angle/length spin boxes → apply to model
    auto applyTanSpin = [this]() {
        if (!m_paramDoc || !m_anchorList) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        if (!seg) return;
        int row = m_anchorList->currentRow();
        if (row < 0 || row >= static_cast<int>(m_anchorPointIds.size())) return;
        auto* pt = block->findPoint(m_anchorPointIds[row]);
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
        double angIn = m_spinTanAngleIn->value() + chordAngle;
        double lenIn = cad::geo::Units::cmToMm(m_spinTanLenIn->value());
        double radIn = angIn * M_PI / 180.0;
        pt->tangentIn = cad::geo::Vec2(std::cos(radIn) * lenIn, std::sin(radIn) * lenIn);
        // Tangent-out.
        double angOut = m_spinTanAngleOut->value() + chordAngle;
        double lenOut = cad::geo::Units::cmToMm(m_spinTanLenOut->value());
        double radOut = angOut * M_PI / 180.0;
        pt->tangentOut = cad::geo::Vec2(std::cos(radOut) * lenOut, std::sin(radOut) * lenOut);
        ++block->geometryEpoch;
        m_paramDoc->resolveAll();
        refreshScene();
    };
    connect(m_spinTanAngleIn, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanLenIn, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanAngleOut, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    connect(m_spinTanLenOut, &QDoubleSpinBox::valueChanged, this, applyTanSpin);
    // Selection change → refresh edit fields
    connect(m_anchorList, &QListWidget::currentRowChanged, this, [this](int row) {
        refreshAnchorFields(row);
    });

    tabs->addTab(m_anchorTab, QString::fromUtf8("锚点"));
}

void LinePropertyDialog::refreshAnchorFields(int row)
{
    if (!m_paramDoc || row < 0 || row >= static_cast<int>(m_anchorPointIds.size()))
        return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;
    auto* pt = block->findPoint(m_anchorPointIds[row]);
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
            tanIn = c2[myIndex];
            tanOut = c2[myIndex];
        }
    }

    // Always show values and keep editable (editing switches to manual).
    double angIn = std::atan2(tanIn.y, tanIn.x) * 180.0 / M_PI;
    double relAngIn = angIn - chordAngle;
    while (relAngIn > 180.0) relAngIn -= 360.0;
    while (relAngIn < -180.0) relAngIn += 360.0;
    m_spinTanAngleIn->setValue(tanIn.lengthSquared() > 1e-12 ? relAngIn : 0.0);
    m_spinTanLenIn->setValue(cad::geo::Units::mmToCm(tanIn.length()));

    double angOut = std::atan2(tanOut.y, tanOut.x) * 180.0 / M_PI;
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
            QString fLabel = fPt->name.isEmpty() ? fPt->serial : fPt->name;
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

void LinePropertyDialog::buildPage3(QTabWidget* tabs)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    // --- Aux point list ---
    m_auxList = new QListWidget(page);
    m_auxList->setMaximumHeight(120);
    m_auxList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_auxList);

    // --- Add / Remove buttons ---
    auto* btnRow = new QHBoxLayout();
    auto* btnAdd = new QPushButton(QString::fromUtf8("+ \u6dfb\u52a0"), page);  // + 添加
    auto* btnRemove = new QPushButton(QString::fromUtf8("\u2212 \u5220\u9664"), page);  // − 删除
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnRemove);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(btnAdd,    &QPushButton::clicked, this, &LinePropertyDialog::onAuxAdd);
    connect(btnRemove, &QPushButton::clicked, this, &LinePropertyDialog::onAuxRemove);
    connect(m_auxList, &QListWidget::itemSelectionChanged, this, &LinePropertyDialog::onAuxSelectionChanged);

    // --- Edit forms: aux (Interpolated) and intersection, toggled by type ---
    m_auxForm = new AuxPointForm(page);
    m_auxForm->setVisible(false);
    layout->addWidget(m_auxForm);

    m_ixForm = new IntersectionForm(page);
    m_ixForm->setVisible(false);
    layout->addWidget(m_ixForm);

    // Field commits apply immediately; text changes restart the global debounce.
    connect(m_auxForm, &AuxPointForm::dirty, this,
            [this]() { if (m_debounce) m_debounce->start(); });
    connect(m_auxForm, &AuxPointForm::edited,
            this, &LinePropertyDialog::onAuxLiveUpdate);
    connect(m_ixForm, &IntersectionForm::dirty, this,
            [this]() { if (m_debounce) m_debounce->start(); });
    connect(m_ixForm, &IntersectionForm::edited,
            this, &LinePropertyDialog::onAuxLiveUpdate);
connect(m_ixForm, &IntersectionForm::aimCleared, this, [this]() {
    if (m_currentAuxId.isNull() || !m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    cad::param::ParamPoint* pt = block ? block->findPoint(m_currentAuxId) : nullptr;
    if (pt) pt->interAimPointId = QUuid();
    onAuxLiveUpdate();
});

    // --- Hint ---
    auto* hint = new QLabel(
        QString::fromUtf8("\u00b7 \u8f85\u52a9\u70b9\u4f4d\u7f6e = \u8ba1\u91cf\u7aef\u70b9 + \u65b9\u5411 \u00d7 (\u8ddd\u79bb\u00d7\u767e\u5206\u6bd4 + \u5e38\u91cf) + \u504f\u79fb(\u89d2\u5ea6,\u8ddd\u79bb)\n"
                          "\u00b7 \u8ba1\u7b97\u65b9\u5411\u51b3\u5b9a\u4ece\u8d77\u70b9\u8fd8\u662f\u7ec8\u70b9\u5f00\u59cb\u8ba1\u91cf\uff0c\u504f\u8f6c\u89d2\u4ee5\u8be5\u65b9\u5411\u4e3a 0\u00b0\n"
                          "\u00b7 \u767e\u5206\u6bd4\u53ef\u8d85\u51fa [0,1] \u5b9e\u73b0\u5916\u63d2\n"
                          "\u00b7 \u8f85\u52a9\u70b9\u53ef\u4f5c\u4e3a\u5176\u4ed6\u7ebf\u6bb5\u7684\u7aef\u70b9\u6216\u9644\u7740\u76ee\u6807"),
        page);
    hint->setStyleSheet("color:#888; font-size:11px;");
    layout->addWidget(hint);
    layout->addStretch();

    tabs->addTab(page, QString::fromUtf8("\u8f85\u52a9\u70b9"));  // 辅助点
}

void LinePropertyDialog::buildPage4(QTabWidget* tabs)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto* header = new QLabel(
        QString::fromUtf8("\u8fde\u63a5\u5173\u7cfb\uff1a"), page);
    // 连接关系：
    header->setStyleSheet("color:#555;");
    layout->addWidget(header);

    auto* scrollContent = new QWidget(page);
    m_auxConnLayout = new QVBoxLayout(scrollContent);
    m_auxConnLayout->setContentsMargins(0, 0, 0, 0);
    m_auxConnLayout->setSpacing(6);
    m_auxConnLayout->addStretch();
    layout->addWidget(scrollContent, 1);

    auto* hint = new QLabel(
        QString::fromUtf8("\u00b7 \u5355\u51fb\u5361\u7247\u9009\u4e2d\u5bf9\u5e94\u7ebf\u6761\uff0c\u53cc\u51fb\u8df3\u8f6c\u7f16\u8f91\n"
                          "\u00b7 \u6784\u9020\u89d2\u4ee5\u5bbf\u4e3b\u7ebf\u6bb5\u65b9\u5411\u4e3a 0\u00b0 \u57fa\u51c6"),
        page);
    hint->setStyleSheet("color:#888; font-size:11px;");
    layout->addWidget(hint);

    tabs->addTab(page, QString::fromUtf8("\u70b9\u8fde\u63a5"));  // 点连接
}

void LinePropertyDialog::refreshAuxConnTab()
{
    if (!m_auxConnLayout) return;

    // Clear existing cards (keep trailing stretch)
    while (m_auxConnLayout->count() > 1) {
        QLayoutItem* it = m_auxConnLayout->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Points that may host an incoming connection: both ENDPOINTS and every
    // auxiliary point of this segment (端点 + 辅助点).
    QList<QUuid> hostPoints;
    hostPoints << seg->startPointId << seg->endPointId;
    for (const auto& auxId : seg->auxPointIds)
        hostPoints << auxId;

    bool anyFound = false;

    // Effective follower angle for display (弧长模式从反向起量).
    auto makeCard = [&](bool isLeader, const cad::param::Block* otherBlk,
                        const cad::param::Segment* otherSeg,
                        const cad::param::ParamPoint* hostPt,
                        const cad::param::Attachment& att) {
        double dispAngle = att.followerAngle;
        if (att.rotationMode == cad::param::RotationMode::ArcLength) {
            double arcMm = att.arcLength;
            if (!att.arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att.arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
            }
            // Radius = the FOLLOWER's segment length at its connection point
            // (incoming: otherBlk is the follower; outgoing: THIS block is).
            const cad::param::Block* followerBlk = isLeader ? block : otherBlk;
            const double radius = followerBlk->segmentLengthAtPoint(att.fromPointId);
            dispAngle = (radius > 1e-9)
                ? 180.0 + (arcMm / radius) * 180.0 / M_PI : 180.0;
        } else if (!att.followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.followerAngleFormula, m_paramDoc->parameters(), {});
            if (r.ok) dispAngle = r.value;
        }
        auto* card = new ConnCard(
            ConnEntry{isLeader, otherSeg->serial, otherSeg->name,
                      hostPt->serial, hostPt->name,
                      dispAngle, otherBlk->id, otherSeg->id,
                      crossLayerBadge(m_paramDoc, att)},
            m_scene, this, m_auxConnLayout->widget());
        m_auxConnLayout->insertWidget(m_auxConnLayout->count() - 1, card);
    };

    // Incoming: other segments attach to THIS segment's points (被哪条线段连接).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.toBlockId != m_blockId) continue;
        if (!hostPoints.contains(att.toPointId)) continue;

        const cad::param::ParamPoint* hostPt = block->findPoint(att.toPointId);
        if (!hostPt) continue;
        anyFound = true;
        const cad::param::Block* fb = m_paramDoc->findBlock(att.fromBlockId);
        if (!fb || fb->segments.empty()) continue;
        const cad::param::Segment& fseg = fb->segments.front();
        makeCard(false, fb, &fseg, hostPt, att);
    }

    // Outgoing: THIS segment hangs on a leader (本线连接了谁). The related
    // segment shown is the LEADER; the point is our own connection point.
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId) continue;
        if (att.isPin) continue;   // 桥 pin 无跟随角度，不在点连接列表
        if (!hostPoints.contains(att.fromPointId)) continue;

        const cad::param::ParamPoint* hostPt = block->findPoint(att.fromPointId);
        if (!hostPt) continue;
        anyFound = true;
        const cad::param::Block* lb = m_paramDoc->findBlock(att.toBlockId);
        if (!lb || lb->segments.empty()) continue;
        const cad::param::Segment* lseg = att.toSegmentId.isNull()
            ? nullptr : lb->findSegment(att.toSegmentId);
        if (!lseg) lseg = &lb->segments.front();
        makeCard(true, lb, lseg, hostPt, att);
    }

    if (!anyFound) {
        auto* empty = new QLabel(
            QString::fromUtf8("\uff08\u65e0\u70b9\u8fde\u63a5\uff09"),
            m_auxConnLayout->widget());  // （无点连接）
        empty->setStyleSheet("color:#aaa; padding:8px;");
        empty->setAlignment(Qt::AlignCenter);
        m_auxConnLayout->insertWidget(m_auxConnLayout->count() - 1, empty);
    }
}

const cad::param::Attachment* LinePropertyDialog::findFollowerAttachment() const
{
    if (!m_paramDoc) return nullptr;
    for (const auto& att : m_paramDoc->attachments()) {
        // Position pins (bridge lines) are not construction-angle followers.
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 跟随角度·连接 card
// ---------------------------------------------------------------------------

QString LinePropertyDialog::leaderRefLabel(const cad::param::Attachment& att) const
{
    if (!m_paramDoc) return QString();

    QString segPart;
    const cad::param::Block* leader = m_paramDoc->findBlock(att.toBlockId);
    if (leader) {
        const cad::param::Segment* lseg = leader->findSegment(att.toSegmentId);
        if (lseg) {
            segPart = cad::param::Serial::tag(lseg->serial);
            if (!lseg->name.isEmpty())
                segPart += QStringLiteral("\u00b7") + lseg->name;
        }
    }
    return segPart.isEmpty() ? QStringLiteral("?") : segPart;
}

void LinePropertyDialog::refreshAngleCard()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const cad::param::Attachment* att = findFollowerAttachment();

    if (att) {
        // ── Connected state ──
        m_grpAngle->setTitle(QString::fromUtf8("跟随角度 · 连接"));  // 跟随角度 · 连接
        m_connRow->setVisible(true);
        m_freeConnRow->setVisible(false);
        m_lblLeaderRef->setText(leaderRefLabel(*att));
        // Cross-layer badge ("→ leader 层名"); hidden for same-layer.
        const QString badge = crossLayerBadge(m_paramDoc, *att);
        m_lblLayerBadge->setText(badge);
        m_lblLayerBadge->setVisible(!badge.isEmpty());
        m_refConnPoint->setExcludeBlock(m_blockId);
        m_refConnPoint->setPoint(att->toBlockId, att->toPointId);
        m_btnAngleMode->setVisible(true);

        // Mode-dependent caption + world-angle hint.
        const cad::param::Block* leader = m_paramDoc->findBlock(att->toBlockId);
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            m_btnAngleMode->setText(QStringLiteral("\xe2\x8c\x92"));  // ⌒
            m_lblAngleCaption->setText(QString::fromUtf8("\u5f27\u957f(cm):"));  // 弧长(cm):
            m_lblAngleCaption->setStyleSheet(QStringLiteral("color:#0078d7;"));

            double arcMm = att->arcLength;
            if (!att->arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att->arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
            }
            double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
            // Arc is measured from the REVERSE direction (弧长 0 = 角度 180°,
            // the fold-back direction), same as the resolver/HUD — the absolute
            // angle is refWorld + 180° + arc-angle. (缺少 180° 曾导致面板对照
            // 与 HUD 方向相反, 用户推断弧长起始点在直行方向的 2026-08 回归.)
            double constDeg = (radius > 1e-9) ? 180.0 + (arcMm / radius) * 180.0 / M_PI : 180.0;
            if (leader) {
                const double refWorldDeg = (leader->transform.rotation
                    + leader->exitDirectionAtPoint(att->toPointId, att->toSegmentId))
                    * 180.0 / M_PI;
                // 显示归一化 [0, 360°)：绝对角度不爆表。
                double absDeg = std::fmod(refWorldDeg + constDeg, 360.0);
                if (absDeg < 0.0) absDeg += 360.0;
                m_lblWorldAngle->setText(QString::fromUtf8("= 绝对角度 %1°")
                    .arg(formatAngleDeg(absDeg)));
                m_lblWorldAngle->setVisible(true);
            } else {
                m_lblWorldAngle->setVisible(false);
            }
        } else {
            m_btnAngleMode->setText(QStringLiteral("\xe2\x88\xa0"));  // ∠
            m_lblAngleCaption->setText(QString::fromUtf8("跟随角度(°):"));  // 跟随角度(°):
            m_lblAngleCaption->setStyleSheet(QStringLiteral("color:#0078d7;"));

            double constDeg = att->followerAngle;
            if (!att->followerAngleFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att->followerAngleFormula, m_paramDoc->parameters(), {});
                if (r.ok) constDeg = r.value;
            }
            if (leader) {
                const double refWorldDeg = (leader->transform.rotation
                    + leader->exitDirectionAtPoint(att->toPointId, att->toSegmentId))
                    * 180.0 / M_PI;
                // 显示归一化 [0, 360°)：绝对角度不爆表。
                double absDeg = std::fmod(refWorldDeg + constDeg, 360.0);
                if (absDeg < 0.0) absDeg += 360.0;
                m_lblWorldAngle->setText(QString::fromUtf8("= 绝对角度 %1°")
                    .arg(formatAngleDeg(absDeg)));
                m_lblWorldAngle->setVisible(true);
            } else {
                m_lblWorldAngle->setVisible(false);
            }
        }
    } else {
        // ── Free state ──
        // 自由线显示绝对角度（相对水平方向），与跟随角度明确区分 —— 旋转工具
        // 无论以起点还是终点为锚心，改的都是这个绝对角度；卡片标题与 caption
        // 直接标注“绝对角度”，不再只藏在 toolTip 里。
        m_grpAngle->setTitle(QString::fromUtf8("绝对角度 · 连接"));  // 绝对角度 · 连接
        m_connRow->setVisible(false);
        m_freeConnRow->setVisible(true);
        m_lblLayerBadge->setVisible(false);
        m_refConnectTo->setExcludeBlock(m_blockId);
        m_refConnectTo->clearPoint();
        m_btnAngleMode->setVisible(false);
        m_lblAngleCaption->setText(QString::fromUtf8("绝对角度(°):"));  // 绝对角度(°):
        m_lblAngleCaption->setStyleSheet(QString());
        m_lblAngleCaption->setToolTip(QString::fromUtf8(
            "自由线段的绝对角度：相对水平方向（+X 轴），"
            "逆时针为正。旋转工具中按 X 切换起点/终点锚心："
            "起点锚心时 HUD 角度 = 线的方向；终点锚心时 HUD 角度 = "
            "从终点指向线的方向（= 线的方向 + 180°），线始终跟随光标。"
            "本面板显示的是线的客观方向（起点→终点），"
            "终点锚心旋转后与 HUD 差 180° 属正常视角差异。"));
        // 面板显示线的客观方向（起点→终点）；旋转工具终点锚心的 HUD 显示
        // 从终点指向线的方向（线方向+180°），两者差 180° 是视角差异。
        m_lblWorldAngle->setVisible(false);
    }

    // ── Follow-host checkbox (all lines): host = the connection target, or
    // the free-state 跟随宿主 input's resolved value when not yet connected.
    if (block && seg) {
        const QUuid hostBlock = att ? att->toBlockId : m_refConnectTo->resolvedBlockId();
        const QUuid hostPoint = att ? att->toPointId : m_refConnectTo->resolvedPointId();
        const auto* hostBlk = !hostBlock.isNull() ? m_paramDoc->findBlock(hostBlock) : nullptr;
        const auto* hostPt = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

        const QSignalBlocker b(m_chkFollowHost);
        m_chkFollowHost->setText(hostPt
            ? QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b %1")  // 跟随宿主 %1
                .arg(cad::param::Serial::tag(hostPt->serial))
            : (att ? QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b\uff08\u5df2\u5220\u9664\uff09")  // 跟随宿主（已删除）
                   : QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b")));  // 跟随宿主
        m_chkFollowHost->setChecked(att != nullptr);
        m_chkFollowHost->setEnabled(true);
        m_chkFollowHost->setToolTip(QString::fromUtf8(
            "\u52fe\u9009\u540e\u8d77\u70b9\u8fde\u63a5\u5230\u5bbf\u4e3b\u70b9\uff0c\u5bbf\u4e3b\u79fb\u52a8\u65f6\u6574\u7ebf\u5e73\u79fb\u8ddf\u968f\uff1b"
            "\u53d6\u6d88\u52fe\u9009\u65ad\u5f00\u8fde\u63a5\u3002\u672a\u8fde\u63a5\u65f6\u5728\u4e0a\u65b9\u8f93\u5165\u5bbf\u4e3b\u70b9 P \u7f16\u53f7\u56de\u8f66\u5373\u53ef\u5efa\u7acb\u8ddf\u968f"));
        // 勾选后起点连接到宿主点，宿主移动时整线平移跟随；取消勾选断开连接。未连接时在上方输入宿主点 P 编号回车即可建立跟随
        m_chkFollowHost->setVisible(true);

        // Lock checkbox: connected → shows the attachment's locked state and
        // is usable; free → disabled (nothing to lock yet). Always VISIBLE
        // (统一显示, no hidden items).
        const QSignalBlocker lb(m_chkLockConn);
        m_chkLockConn->setChecked(att && att->isLocked);
        m_chkLockConn->setEnabled(att != nullptr);
        m_chkLockConn->setVisible(true);
    }
}

// ---------------------------------------------------------------------------
// 锁定连接 toggle
// ---------------------------------------------------------------------------

void LinePropertyDialog::onLockConnToggled(bool on)
{
    if (!m_paramDoc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || att->isLocked == on) { refreshAngleCard(); return; }
    m_paramDoc->setAttachmentLocked(att->id, on);
    refreshFollowerState();
    refreshScene();
}

void LinePropertyDialog::populateAngleField()
{
    cad::param::Block* block = m_paramDoc ? m_paramDoc->findBlock(m_blockId) : nullptr;
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const cad::param::Attachment* att = findFollowerAttachment();
    if (att) {
        // Follower: show the follower angle or arc length depending on mode.
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            if (!att->arcLengthFormula.isEmpty()) {
                m_editAngle->setText(att->arcLengthFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                m_editAngle->setText(QString::number(
                    cad::geo::Units::mmToCm(att->arcLength), 'f', 2));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(cm)\u6216\u516c\u5f0f"));  // 数值(cm)或公式
        } else {
            if (!att->followerAngleFormula.isEmpty()) {
                m_editAngle->setText(att->followerAngleFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                // 显示归一化到 [0, 360°)（存储保持原值，显示不爆表）。
                double deg = std::fmod(att->followerAngle, 360.0);
                if (deg < 0.0) deg += 360.0;
                m_editAngle->setText(formatAngleDeg(deg));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
        }
    } else if (block && seg) {
        // Free block: stored endpoint angle formula or numeric world angle.
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (ep && !ep->angleFormula.isEmpty()) {
            m_editAngle->setText(ep->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            m_editAngle->setText(QString::number(angleDeg, 'f', 1));
            m_lblFxAngle->setVisible(false);
        }
    }

    refreshAngleCard();
}

void LinePropertyDialog::populateFromModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // 基本信息
    m_lblSegId->setText(cad::param::Serial::toHtml(seg->serial));
    m_editName->setText(seg->name);
    m_cmbRole->setCurrentIndex(static_cast<int>(seg->role));

    // 几何
    m_chkShowLength->setChecked(seg->showLength);
    if (!seg->lengthFormula.isEmpty()) {
        m_editLength->setText(seg->lengthFormula);
        m_lblFx->setVisible(true);
    } else {
        m_lblFx->setVisible(false);
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
            double lenCm = cad::geo::Units::mmToCm(lenMm);
            m_editLength->setText(QString::number(lenCm, 'f', 2));
        }
    }
    refreshActualLengthLabel();

    // 曲线专有字段
    const bool isCurve = seg->isCurve();
    m_arcRow->setVisible(isCurve);
    m_tensionRow->setVisible(isCurve);
    // 转换按钮仅对曲线显示（一键转回直线）。直线→曲线请用智能笔添加曲线点。
    m_btnConvert->setVisible(isCurve);
    m_btnConvert->setText(QString::fromUtf8("转为直线"));
    if (isCurve) {
        // Arc length (read-only)
        double arcLen = block->segmentLengthAtPoint(seg->startPointId);
        m_lblArcLength->setText(QString::number(cad::geo::Units::mmToCm(arcLen), 'f', 2));
        // Tension
        m_editTension->setText(QString::number(seg->tension, 'f', 1));
        // Length label becomes "弦长"
    }

    // 锚点 Tab
    if (m_anchorTab && m_tabs) {
        int tabIdx = -1;
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->widget(i) == m_anchorTab)
                { tabIdx = i; break; }
        }
        if (tabIdx >= 0)
            m_tabs->setTabEnabled(tabIdx, isCurve);
    }
    if (isCurve && m_anchorList) {
        m_anchorList->clear();
        m_anchorPointIds.clear();
        // Start point
        const auto* sp = block->findPoint(seg->startPointId);
        QString spLabel = sp ? (sp->name.isEmpty() ? sp->serial : sp->name) : QStringLiteral("?");
        m_anchorList->addItem(QString::fromUtf8("起点 ") + spLabel);
        m_anchorPointIds.push_back(seg->startPointId);
        // Pass points
        for (const auto& ppId : seg->passPointIds) {
            const auto* pp = block->findPoint(ppId);
            QString label = pp ? (pp->name.isEmpty() ? pp->serial : pp->name) : QStringLiteral("?");
            m_anchorList->addItem(QString::fromUtf8("曲线点 ") + label);
            m_anchorPointIds.push_back(ppId);
        }
        // End point
        const auto* ep = block->findPoint(seg->endPointId);
        QString epLabel = ep ? (ep->name.isEmpty() ? ep->serial : ep->name) : QStringLiteral("?");
        m_anchorList->addItem(QString::fromUtf8("终点 ") + epLabel);
        m_anchorPointIds.push_back(seg->endPointId);
        if (m_anchorList->count() > 0)
            m_anchorList->setCurrentRow(0);
    }

    // Angle: follower shows the follower angle, free block shows the world
    // angle. Also refreshes the angle card.
    populateAngleField();

    // 外观
    m_cmbStyle->setCurrentIndex(static_cast<int>(seg->lineStyle));
    m_chkVisible->setChecked(seg->visible);
    m_chkShowName->setChecked(seg->showName);
    m_currentColor = seg->color;
    m_btnColor->setStyleSheet(QStringLiteral(
        "background-color: %1; border:1px solid #999; border-radius:3px;")
        .arg(m_currentColor.name()));

    // Weight: match to preset or show custom
    updateWeightCombo();

    // 端点
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_lblStartPtId->setText(cad::param::Serial::toHtml(sp->serial));
        m_editStartName->setText(sp->name);
        m_chkShowStartName->setChecked(sp->showName);
        m_editStartAnno->setText(sp->annotation);
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_lblEndPtId->setText(cad::param::Serial::toHtml(ep->serial));
        m_editEndName->setText(ep->name);
        m_chkShowEndName->setChecked(ep->showName);
        m_editEndAnno->setText(ep->annotation);
    }

    // Follower-state snapshot for 撤销全部: taken ONCE at open time so the
    // revert always restores the pre-dialog state (retargets / connects made
    // inside this dialog are undone too).
    m_snapshot.followerAtt.reset();
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != m_blockId) continue;
        m_snapshot.followerAtt = att;
        break;
    }

    // Aux connections tab
    refreshAuxConnTab();

    // Aux points tab: refresh direction labels, save snapshots, refresh list
    refreshAuxDirLabels();
    m_auxSnapshots.clear();
    m_auxAddedIds.clear();
    for (const auto& auxId : seg->auxPointIds) {
        const cad::param::ParamPoint* apt = block->findPoint(auxId);
        if (!apt) continue;
        AuxSnapshot snap;
        snap.pointId          = apt->id;
        snap.percent          = apt->interpPercent;
        snap.percentFormula   = apt->interpPercentFormula;
        snap.constant         = apt->interpConstant;
        snap.constantFormula  = apt->interpConstantFormula;
        snap.fromEnd          = apt->interpFromEnd;
        snap.showName         = apt->showName;
        snap.name             = apt->name;
        m_auxSnapshots.push_back(snap);
    }
    refreshAuxList();

    // Endpoint-aim card.
    refreshAimCard();

    // Save snapshot for cancel-revert
    m_snapshot.segName       = seg->name;
    m_snapshot.showName      = seg->showName;
    m_snapshot.showLength    = seg->showLength;
    m_snapshot.visible       = seg->visible;
    m_snapshot.role          = static_cast<int>(seg->role);
    m_snapshot.lengthFormula = seg->lengthFormula;
    m_snapshot.color         = seg->color;
    m_snapshot.tension       = seg->tension;
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.distance = ep->distance;
        m_snapshot.distanceFormula = ep->distanceFormula;
        m_snapshot.angle = ep->angle;
        m_snapshot.angleFormula = ep->angleFormula;
        m_snapshot.constraint = static_cast<int>(ep->constraint);
        m_snapshot.refPointId = ep->refPointId;
    }
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_snapshot.startName = sp->name;
        m_snapshot.startShowName = sp->showName;
        m_snapshot.startAnno = sp->annotation;
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.endName = ep->name;
        m_snapshot.endShowName = ep->showName;
        m_snapshot.endAnno = ep->annotation;
    }
    m_snapshot.lineStyle = static_cast<int>(seg->lineStyle);
    m_snapshot.weight    = seg->weight;
    m_snapshot.endTargetBlockId = block->endTargetBlockId;
    m_snapshot.endTargetPointId = block->endTargetPointId;
    m_snapshot.endTargetOffset  = block->endTargetOffset;
    m_snapshot.endTargetOffsetFormula = block->endTargetOffsetFormula;

    // Sync publish button state: disable if already published.
    if (m_btnPublishLen) {
        const bool published =
            m_paramDoc->findLinkedBySource(m_blockId, m_segmentId) != nullptr;
        m_btnPublishLen->setEnabled(!published);
        m_btnPublishLen->setText(published
            ? QString::fromUtf8("已发布")
            : QString::fromUtf8("发布长度参数"));
    }

    // Bridge lines: length/angle are passive measurements — lock the editors.
    applyBridgeReadOnly();
}

void LinePropertyDialog::applyBridgeReadOnly()
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Show the measured world length / angle.
    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        const double lenCm = cad::geo::Units::mmToCm(w1.distanceTo(w2));
        m_editLength->setText(QString::number(lenCm, 'f', 2));
        const double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
        m_editAngle->setText(QString::number(angleDeg, 'f', 1));
    }

    m_editLength->setEnabled(false);
    m_editAngle->setEnabled(false);
    m_lblFx->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);

    m_lblAngleCaption->setText(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"));  // 角度(°):
    m_lblAngleCaption->setStyleSheet(QString());
    const QString tip = QString::fromUtf8(
        "\u6865\u63a5\u7ebf\uff1a\u957f\u5ea6\u4e0e\u89d2\u5ea6\u7531\u4e24\u7aef"
        "\u9489\u4f4f\u7684\u5bbf\u4e3b\u70b9\u51b3\u5b9a\uff0c\u4e0d\u53ef\u7f16\u8f91");  // 桥接线：长度与角度由两端钉住的宿主点决定，不可编辑
    m_editLength->setToolTip(tip);
    m_editAngle->setToolTip(tip);
}

void LinePropertyDialog::applyToModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    seg->name = m_editName->text().trimmed();
    // Keep the owned measure variable's name in sync (测量对象名称 → 测量变量
    // 名称): renaming a bridge/measure line renames its measurement variable.
    m_paramDoc->setOwnerMeasureName(m_blockId, seg->name);
    seg->role = static_cast<cad::param::SegmentRole>(m_cmbRole->currentIndex());
    seg->showName = m_chkShowName->isChecked();
    seg->showLength = m_chkShowLength->isChecked();
    seg->visible = m_chkVisible->isChecked();
    seg->color = m_currentColor;

    // Length: parse as cm number or treat as formula (formula result is in cm)
    QString lenText = m_editLength->text().trimmed();
    bool isNumber = false;
    double numCm = lenText.toDouble(&isNumber);
    if (isNumber) {
        double numMm = cad::geo::Units::cmToMm(numCm);
        seg->lengthFormula.clear();
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula.clear();
            ep->distance = numMm;
        }
    } else if (!lenText.isEmpty()) {
        seg->lengthFormula = lenText;
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula = lenText;
        }
    }

    // Point names/annotations
    if (auto* sp = block->findPoint(seg->startPointId)) {
        sp->name = m_editStartName->text().trimmed();
        sp->showName = m_chkShowStartName->isChecked();
        sp->annotation = m_editStartAnno->text().trimmed();
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        ep->name = m_editEndName->text().trimmed();
        ep->showName = m_chkShowEndName->isChecked();
        ep->annotation = m_editEndAnno->text().trimmed();
    }

    seg->lineStyle = static_cast<cad::param::LineStyle>(m_cmbStyle->currentIndex());
    seg->weight = m_spinWeight->value();

    // NOTE: The follower angle (跟随角度) is intentionally NOT written here.
    // It is owned by the follower's attachment and is applied exclusively by
    // onAngleApply().
}

void LinePropertyDialog::refreshScene()
{
    if (m_paramDoc) m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
    refreshActualLengthLabel();
}

void LinePropertyDialog::refreshActualLengthLabel()
{
    if (!m_lblActualLength || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        const double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
        m_lblActualLength->setText(cad::geo::Units::formatLength(lenMm));
    } else {
        m_lblActualLength->setText(QStringLiteral("—"));
    }
}

void LinePropertyDialog::onLiveUpdate()
{
    applyToModel();
    refreshScene();
}

void LinePropertyDialog::onLengthDirty()
{
    m_editLength->setStyleSheet(
        "QLineEdit { border-left: 3px solid #F5A623; }");

    QString text = m_editLength->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFx->setVisible(!isNumber && !text.isEmpty());

    if (m_debounce) m_debounce->start();
}

void LinePropertyDialog::onLengthApply()
{
    applyToModel();
    refreshScene();
    m_editLength->setStyleSheet(QString());
}

void LinePropertyDialog::onAngleDirty()
{
    m_editAngle->setStyleSheet(
        "QLineEdit { border-left: 3px solid #F5A623; }");

    QString text = m_editAngle->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());

    if (m_debounce) m_debounce->start();
}

void LinePropertyDialog::onAngleApply()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    QString text = m_editAngle->text().trimmed();
    if (text.isEmpty()) return;

    // Evaluate: number or formula
    bool isNumber = false;
    double targetDeg = text.toDouble(&isNumber);
    if (!isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            text, m_paramDoc->parameters(), {});
        if (!r.ok) return;
        targetDeg = r.value;
    }

    // Check if this block is a follower
    bool isFollower = false;
    QUuid attId;
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            isFollower = true;
            attId = att.id;
            break;
        }
    }

    if (isFollower) {
        // The angle field edits the FOLLOWER ANGLE or ARC LENGTH directly.
        auto& attachments = const_cast<std::vector<cad::param::Attachment>&>(
            m_paramDoc->attachments());
        for (auto& att : attachments) {
            if (att.id == attId) {
                if (att.rotationMode == cad::param::RotationMode::ArcLength) {
                    att.arcLength = cad::geo::Units::cmToMm(targetDeg);
                    att.arcLengthFormula = isNumber ? QString() : text;
                } else {
                    att.followerAngle = targetDeg;
                    att.followerAngleFormula = isNumber ? QString() : text;
                }
                break;
            }
        }
    } else {
        // Free block: set endpoint's Polar angle directly.
        cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (!ep) return;

        if (ep->constraint != cad::param::PointConstraint::Polar) {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            double dist = sp->resolvedPos.distanceTo(ep->resolvedPos);
            ep->constraint = cad::param::PointConstraint::Polar;
            ep->refPointId = seg->startPointId;
            ep->distance = dist;
        }

        const double rotDeg = block->transform.rotation * 180.0 / M_PI;
        const double localDeg = targetDeg - rotDeg;
        ep->angle = localDeg;
        ep->angleFormula.clear();

        if (!isNumber) {
            ep->angleFormula = (std::abs(rotDeg) > 1e-9)
                ? QStringLiteral("(%1)-%2").arg(text).arg(rotDeg, 0, 'g', 12)
                : text;
        }
    }

    refreshScene();
    refreshAngleCard();

    // Clear dirty indicator (do NOT overwrite the user's input text)
    m_editAngle->setStyleSheet(QString());
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());
}

void LinePropertyDialog::onColorPick()
{
    QColor chosen = QColorDialog::getColor(m_currentColor, this,
        QString::fromUtf8("\u9009\u62e9\u7ebf\u6761\u989c\u8272"));  // 选择线条颜色
    if (!chosen.isValid()) return;

    m_currentColor = chosen;
    m_btnColor->setStyleSheet(QStringLiteral(
        "background-color: %1; border:1px solid #999; border-radius:3px;")
        .arg(chosen.name()));

    onLiveUpdate();
}

void LinePropertyDialog::onWeightPresetChanged(int index)
{
    const bool isCustom = (index == m_cmbWeight->count() - 1);
    m_spinWeight->setVisible(isCustom);

    if (!isCustom) {
        double val = m_cmbWeight->currentData().toDouble();
        m_spinWeight->setValue(val);
    }
}

void LinePropertyDialog::updateWeightCombo()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    double w = seg->weight;

    const bool oldComboState = m_cmbWeight->blockSignals(true);
    const bool oldSpinState  = m_spinWeight->blockSignals(true);

    m_spinWeight->setValue(w);

    if (std::abs(w - kWeightThin) < 0.05) {
        m_cmbWeight->setCurrentIndex(0);
        m_spinWeight->setVisible(false);
    } else if (std::abs(w - kWeightMedium) < 0.05) {
        m_cmbWeight->setCurrentIndex(1);
        m_spinWeight->setVisible(false);
    } else if (std::abs(w - kWeightThick) < 0.05) {
        m_cmbWeight->setCurrentIndex(2);
        m_spinWeight->setVisible(false);
    } else {
        m_cmbWeight->setCurrentIndex(3);  // 自定义
        m_spinWeight->setVisible(true);
    }

    m_cmbWeight->blockSignals(oldComboState);
    m_spinWeight->blockSignals(oldSpinState);
}

void LinePropertyDialog::onAccepted()
{
    applyToModel();
    refreshScene();
    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = true;
    accept();
}

void LinePropertyDialog::onRejected()
{
    // Creation state (smart pen just drew the line): 撤销全部 = 取消线段创建
    // — delete the line entirely (creation itself never entered the undo
    // stack, so the symmetric removal is a plain removeBlock).
    if (m_isCreation && m_paramDoc && m_paramDoc->findBlock(m_blockId)) {
        m_paramDoc->removeBlock(m_blockId);
        if (m_scene) m_scene->notifyGroupInfoChanged();
        m_confirmed = false;
        reject();
        return;
    }

    // Edit state: revert to snapshot (取消本次改动).
    if (m_paramDoc) {
        cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
        if (block) {
            if (auto* seg = block->findSegment(m_segmentId)) {
                seg->name = m_snapshot.segName;
                seg->showName = m_snapshot.showName;
                seg->showLength = m_snapshot.showLength;
                seg->visible = m_snapshot.visible;
                seg->role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
                seg->lengthFormula = m_snapshot.lengthFormula;
                seg->lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
                seg->weight = m_snapshot.weight;
                seg->color = m_snapshot.color;
                seg->tension = m_snapshot.tension;
            }
            if (auto* ep = block->findSegment(m_segmentId)
                    ? block->findPoint(block->findSegment(m_segmentId)->endPointId) : nullptr) {
                ep->distance = m_snapshot.distance;
                ep->distanceFormula = m_snapshot.distanceFormula;
                ep->angle = m_snapshot.angle;
                ep->angleFormula = m_snapshot.angleFormula;
                ep->constraint = static_cast<cad::param::PointConstraint>(m_snapshot.constraint);
                ep->refPointId = m_snapshot.refPointId;
                ep->name = m_snapshot.endName;
                ep->showName = m_snapshot.endShowName;
                ep->annotation = m_snapshot.endAnno;
            }
            if (auto* sp = block->findSegment(m_segmentId)
                    ? block->findPoint(block->findSegment(m_segmentId)->startPointId) : nullptr) {
                sp->name = m_snapshot.startName;
                sp->showName = m_snapshot.startShowName;
                sp->annotation = m_snapshot.startAnno;
            }

            // Revert endpoint-aim state.
            block->endTargetBlockId = m_snapshot.endTargetBlockId;
            block->endTargetPointId = m_snapshot.endTargetPointId;
            block->endTargetOffset  = m_snapshot.endTargetOffset;
            block->endTargetOffsetFormula = m_snapshot.endTargetOffsetFormula;
        }
        // Revert the follower attachment: remove whatever exists now (it may
        // have been created, retargeted, or replaced during this session) and
        // restore the open-time snapshot (if the line was connected then).
        {
            auto& attachments = const_cast<std::vector<cad::param::Attachment>&>(
                m_paramDoc->attachments());
            std::erase_if(attachments, [this](const cad::param::Attachment& a) {
                return !a.isPin && a.fromBlockId == m_blockId;
            });
            if (m_snapshot.followerAtt)
                m_paramDoc->addAttachment(*m_snapshot.followerAtt);
        }

        // Revert aux points: remove added ones, restore snapshots.
        if (auto* seg2 = block ? block->findSegment(m_segmentId) : nullptr) {
            for (const auto& addedId : m_auxAddedIds) {
                auto& ids = seg2->auxPointIds;
                ids.erase(std::remove(ids.begin(), ids.end(), addedId), ids.end());
                auto& pts = block->points;
                pts.erase(std::remove_if(pts.begin(), pts.end(),
                    [&addedId](const cad::param::ParamPoint& p) { return p.id == addedId; }),
                    pts.end());
            }
            for (const auto& snap : m_auxSnapshots) {
                cad::param::ParamPoint* apt = block->findPoint(snap.pointId);
                if (!apt) continue;
                apt->interpPercent          = snap.percent;
                apt->interpPercentFormula   = snap.percentFormula;
                apt->interpConstant         = snap.constant;
                apt->interpConstantFormula  = snap.constantFormula;
                apt->interpFromEnd          = snap.fromEnd;
                apt->showName               = snap.showName;
                apt->name                   = snap.name;
            }
            block->rebuildPointIndex();
        }

        refreshScene();
    }

    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = false;
    reject();
}

void LinePropertyDialog::refreshFollowerState()
{
    // NOTE: does NOT re-snapshot — the 撤销全部 snapshot is taken once at
    // dialog-open time (populateFromModel). This only refreshes the card.
    refreshAngleCard();
}

void LinePropertyDialog::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    populateFromModel();
    applyCanvasHighlight();

    if (m_paramDoc) {
        if (auto* b = m_paramDoc->findBlock(m_blockId)) {
            if (const auto* s = b->findSegment(m_segmentId)) {
                setWindowTitle(QString::fromUtf8("\u7ebf\u6761\u5c5e\u6027 - %1")  // 线条属性 - %1
                    .arg(s->name.isEmpty()
                             ? cad::param::Serial::tag(s->serial)
                             : s->name));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Auxiliary point management (Page 3)
// ---------------------------------------------------------------------------

void LinePropertyDialog::refreshAuxList()
{
    if (!m_auxList) return;
    m_auxList->clear();

    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    for (const auto& auxId : seg->auxPointIds) {
        const cad::param::ParamPoint* pt = block->findPoint(auxId);
        if (!pt) continue;
        QString label = cad::param::Serial::tag(pt->serial);
        if (!pt->name.isEmpty())
            label += QStringLiteral(" \u00b7 ") + pt->name;
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, auxId);
        m_auxList->addItem(item);
    }

    m_auxForm->setVisible(m_auxList->count() > 0 && m_auxList->currentRow() >= 0);
}

void LinePropertyDialog::populateAuxFields()
{
    if (!m_auxList || m_auxList->currentRow() < 0) {
        m_auxForm->setVisible(false);
        m_ixForm->setVisible(false);
        return;
    }

    auto* item = m_auxList->currentItem();
    if (!item) { m_auxForm->setVisible(false); m_ixForm->setVisible(false); return; }

    QUuid auxId = item->data(Qt::UserRole).toUuid();
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const cad::param::ParamPoint* pt = block->findPoint(auxId);
    if (!pt) { m_auxForm->setVisible(false); m_ixForm->setVisible(false); return; }

    const cad::param::Segment* seg = block->findSegment(m_segmentId);

    // --- Intersection point → IntersectionForm ---
    if (pt->constraint == cad::param::PointConstraint::Intersection) {
        m_auxForm->setVisible(false);

        QString originLabel = QString::fromUtf8("\u5df2\u5220\u9664");  // 已删除
        for (const auto& ob : m_paramDoc->blocks()) {
            const auto* op = ob.findPoint(pt->refPointA);
            if (op) {
                originLabel = cad::param::Serial::tag(op->serial);
                if (!op->name.isEmpty())
                    originLabel += QStringLiteral(" \u00b7 ") + op->name;
                break;
            }
        }
        m_ixForm->setOriginLabel(originLabel);

        // Aim point (指向点) label, searched across all blocks.
        QString aimLabel;
        if (!pt->interAimPointId.isNull()) {
            for (const auto& ob : m_paramDoc->blocks()) {
                const auto* ap = ob.findPoint(pt->interAimPointId);
                if (ap) {
                    aimLabel = cad::param::Serial::tag(ap->serial);
                    if (!ap->name.isEmpty())
                        aimLabel += QStringLiteral(" \u00b7 ") + ap->name;
                    break;
                }
            }
        }
        m_ixForm->setAimLabel(aimLabel);

        double segWorldDir = 0.0;
        if (seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                segWorldDir = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            }
        }
        m_ixForm->setSegmentWorldDir(segWorldDir);

        m_ixForm->loadFrom(*pt);
        m_ixForm->setVisible(true);
        return;
    }

    // --- Interpolated (aux) point → AuxPointForm ---
    m_ixForm->setVisible(false);

    if (seg) {
        std::vector<std::pair<QUuid, QString>> refPts;
        for (const auto& aid : seg->auxPointIds) {
            if (aid == auxId) continue;
            const auto* ap = block->findPoint(aid);
            if (!ap) continue;
            QString label = cad::param::Serial::tag(ap->serial);
            if (!ap->name.isEmpty())
                label += QStringLiteral(" \u00b7 ") + ap->name;
            refPts.emplace_back(ap->id, label);
        }
        m_auxForm->setRefPointList(refPts);
    }

    m_auxForm->loadFrom(*pt);
    m_auxForm->setVisible(true);
}

void LinePropertyDialog::refreshAuxDirLabels()
{
    if (!m_auxForm || !m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    const cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    m_auxForm->setEndpointLabels(block->findPoint(seg->startPointId),
                                 block->findPoint(seg->endPointId));
}

void LinePropertyDialog::onAuxSelectionChanged()
{
    m_currentAuxId = QUuid();
    if (m_auxList && m_auxList->currentItem())
        m_currentAuxId = m_auxList->currentItem()->data(Qt::UserRole).toUuid();
    populateAuxFields();
}

void LinePropertyDialog::onAuxAdd()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = m_segmentId;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.interpPercent = 0.5;
    pt.interpConstant = 0.0;
    pt.interpOffsetAngle = 0.0;
    pt.interpOffsetDist = 0.0;
    pt.interpFromEnd = false;

    pt.serial = m_paramDoc->newPointSerial();

    QUuid ptId = block->addPoint(pt);
    seg->auxPointIds.push_back(ptId);
    m_auxAddedIds.push_back(ptId);

    refreshAuxList();
    refreshScene();

    m_auxList->setCurrentRow(m_auxList->count() - 1);
}

void LinePropertyDialog::onAuxRemove()
{
    if (!m_auxList || m_auxList->currentRow() < 0) return;
    if (!m_paramDoc) return;

    auto* item = m_auxList->currentItem();
    if (!item) return;

    QUuid auxId = item->data(Qt::UserRole).toUuid();
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    auto& ids = seg->auxPointIds;
    ids.erase(std::remove(ids.begin(), ids.end(), auxId), ids.end());

    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [&auxId](const cad::param::ParamPoint& p) { return p.id == auxId; }),
        pts.end());
    block->rebuildPointIndex();

    auto addedIt = std::find(m_auxAddedIds.begin(), m_auxAddedIds.end(), auxId);
    if (addedIt != m_auxAddedIds.end())
        m_auxAddedIds.erase(addedIt);

    refreshAuxList();
    refreshScene();
}

void LinePropertyDialog::onDebounceTimeout()
{
    onLengthApply();
    onAngleApply();
    onAuxLiveUpdate();
}

void LinePropertyDialog::onAuxLiveUpdate()
{
    if (m_currentAuxId.isNull()) return;
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::ParamPoint* pt = block->findPoint(m_currentAuxId);
    if (!pt) return;

    if (pt->constraint == cad::param::PointConstraint::Intersection)
        m_ixForm->applyTo(*pt);
    else
        m_auxForm->applyTo(*pt);

    if (m_auxList) {
        for (int i = 0; i < m_auxList->count(); ++i) {
            if (m_auxList->item(i)->data(Qt::UserRole).toUuid() == m_currentAuxId) {
                QString label = cad::param::Serial::tag(pt->serial);
                if (!pt->name.isEmpty())
                    label += QStringLiteral(" \u00b7 ") + pt->name;
                m_auxList->item(i)->setText(label);
                break;
            }
        }
    }

    refreshScene();
}

void LinePropertyDialog::onPublishLength()
{
    if (!m_paramDoc) return;

    if (m_paramDoc->findLinkedBySource(m_blockId, m_segmentId)) return;

    const auto* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk) return;
    const auto* seg = blk->findSegment(m_segmentId);
    if (!seg) return;

    cad::param::LinkedVariable lv = cad::param::LinkedVariable::fromSegment(*blk, *seg);

    auto* stack = m_paramDoc->undoStack();
    if (stack)
        stack->push(new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    else
        m_paramDoc->addLinked(lv);

    m_btnPublishLen->setEnabled(false);
    m_btnPublishLen->setText(QString::fromUtf8("已发布"));
}

// ---------------------------------------------------------------------------
// 跟随角度·连接 card actions
// ---------------------------------------------------------------------------

void LinePropertyDialog::onConnPointResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;

    // Locate the mutable follower attachment.
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(
        m_paramDoc->attachments());
    cad::param::Attachment* att = nullptr;
    for (auto& a : atts) {
        if (!a.isPin && a.fromBlockId == m_blockId) { att = &a; break; }
    }
    if (!att) return;

    // Validate: would the re-targeted attachment create a cycle?
    cad::param::Attachment candidate = *att;
    candidate.toBlockId = blockId;
    candidate.toPointId = pointId;
    std::vector<cad::param::Attachment> others;
    for (const auto& a : atts)
        if (a.id != att->id) others.push_back(a);
    if (cad::param::checkAttachment(others, candidate)
            != cad::param::AttachmentIssue::Ok) {
        refreshAngleCard();  // Revert the widget display.
        return;
    }

    const auto* leader = m_paramDoc->findBlock(blockId);
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!leader || !block) { refreshAngleCard(); return; }

    // Re-target.
    att->toBlockId = blockId;
    att->toPointId = pointId;
    att->toSegmentId = leader->exitSegmentAtPoint(pointId);

    // Back-solve the follower angle so the CURRENT world direction is
    // preserved (no visual jump on re-attach).
    if (auto* seg = block->findSegment(m_segmentId)) {
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(pointId, att->toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att->followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);
    }
    att->followerAngleFormula.clear();
    att->rotationMode = cad::param::RotationMode::Angle;
    att->arcLength = 0.0;
    att->arcLengthFormula.clear();

    refreshFollowerState();
    populateAngleField();
    refreshAuxConnTab();
    refreshScene();
}

void LinePropertyDialog::onConnClear()
{
    if (!m_paramDoc) return;

    // Locate the mutable follower attachment (same search as onConnPointResolved).
    const cad::param::Attachment* found = nullptr;
    for (const auto& a : m_paramDoc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { found = &a; break; }
    }
    if (!found) { refreshAngleCard(); return; }

    // Remove the attachment: the line becomes free (world angle preserved —
    // removeAttachment resolves, so the block simply stops being driven).
    m_paramDoc->removeAttachment(found->id);

    refreshFollowerState();
    refreshAngleCard();
    populateAngleField();
    refreshAuxConnTab();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void LinePropertyDialog::onConnectToResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Block* leader = m_paramDoc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refreshAngleCard(); return; }

    // Build the new attachment (same back-solve as onFollowHostToggled).
    cad::param::Attachment att;
    att.fromBlockId = m_blockId;
    att.fromPointId = seg->startPointId;
    att.toBlockId   = blockId;
    att.toPointId   = pointId;
    att.toSegmentId = leader->exitSegmentAtPoint(pointId);

    const double refWorld = leader->transform.rotation
        + leader->exitDirectionAtPoint(pointId, att.toSegmentId);
    const double localDir = block->directionAtPoint(seg->startPointId);
    att.followerAngle = cad::param::backSolveFollowerAngle(
        block->transform.rotation, localDir, refWorld);

    // May be rejected (cycle / conflicting follower).
    const bool added = m_paramDoc->addAttachment(att);
    if (added && m_scene) {
        // Toast only for genuinely NEW cross-layer connections.
        if (const QString toast = crossLayerToast(m_paramDoc, *block, *leader);
            !toast.isEmpty())
            m_scene->showToast(toast);
    }

    refreshFollowerState();
    populateAngleField();
    refreshAuxConnTab();
    refreshScene();
}

void LinePropertyDialog::onFollowHostToggled(bool on)
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) { refreshAngleCard(); return; }

    const cad::param::Attachment* existing = findFollowerAttachment();
    if (on) {
        // Host = the current connection target, or the free-state 跟随宿主
        // input's resolved value (typed P number).
        const QUuid hostBlock = existing
            ? existing->toBlockId : m_refConnectTo->resolvedBlockId();
        const QUuid hostPoint = existing
            ? existing->toPointId : m_refConnectTo->resolvedPointId();
        const cad::param::Block* leader = m_paramDoc->findBlock(hostBlock);
        if (!leader || !leader->findPoint(hostPoint)) {
            refreshAngleCard();
            return;  // no host yet — the check rolls back
        }

        cad::param::Attachment att;
        att.fromBlockId = m_blockId;
        att.fromPointId = seg->startPointId;
        att.toBlockId   = hostBlock;
        att.toPointId   = hostPoint;
        att.toSegmentId = leader->exitSegmentAtPoint(hostPoint);
        // Back-solve the follower angle so the CURRENT world direction is
        // preserved (no jump on attach).
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(hostPoint, att.toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att.followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);

        const bool added = m_paramDoc->addAttachment(att);
        if (added && m_scene && leader) {
            if (const QString toast = crossLayerToast(m_paramDoc, *block, *leader);
                !toast.isEmpty())
                m_scene->showToast(toast);
        }
    } else {
        if (existing)
            m_paramDoc->removeAttachment(existing->id);
    }

    refreshFollowerState();
    populateAngleField();
    refreshAuxConnTab();
    refreshAngleCard();
    refreshScene();
}

// ---------------------------------------------------------------------------
// 终点指向 card
// ---------------------------------------------------------------------------

const cad::param::MeasureVariable* LinePropertyDialog::findBridgeMeasure() const
{
    if (!m_paramDoc) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return nullptr;

    const QString formula = seg->lengthFormula.trimmed();
    if (formula.isEmpty()) return nullptr;
    // Case-insensitive match: reference names are uppercase by convention
    // (CopyChip force-uppercases them when the user types/copies a ref name),
    // while MeasureVariable refNames are generated lowercase ("M_" + random
    // prefix). The formula evaluator resolves variables case-insensitively, so
    // a lengthFormula of "M_KTIEY" still evaluates against measure "M_ktiey" —
    // the measure-line detection here must match that same behaviour, or the
    // follow-host / aim-host controls silently disappear for such segments.
    for (const auto& mv : m_paramDoc->measureVars())
        if (mv.refName.compare(formula, Qt::CaseInsensitive) == 0) return &mv;
    return nullptr;
}

void LinePropertyDialog::refreshAimCard()
{
    if (!m_refAimTarget || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);

    if (!block || block->endTargetPointId.isNull()) {
        // No aim constraint: empty ref (placeholder), offset disabled.
        m_refAimTarget->setExcludeBlock(m_blockId);
        m_refAimTarget->clearPoint();
        m_editAimOffset->setEnabled(false);
        m_editAimOffset->clear();
        m_btnClearAim->setEnabled(false);
    } else {
        m_refAimTarget->setExcludeBlock(m_blockId);
        m_refAimTarget->setPoint(block->endTargetBlockId, block->endTargetPointId);
        m_editAimOffset->setEnabled(true);
        m_btnClearAim->setEnabled(true);

        const QSignalBlocker b(m_editAimOffset);
        if (!block->endTargetOffsetFormula.isEmpty())
            m_editAimOffset->setText(block->endTargetOffsetFormula);
        else
            m_editAimOffset->setText(QString::number(block->endTargetOffset, 'g', 6));
    }

    // ── Aim-host checkbox (all lines): host = the aim target, or the aim
    // input's resolved value when no aim constraint exists yet.
    if (block) {
        const QUuid hostBlock = !block->endTargetBlockId.isNull()
            ? block->endTargetBlockId : m_refAimTarget->resolvedBlockId();
        const QUuid hostPoint = !block->endTargetPointId.isNull()
            ? block->endTargetPointId : m_refAimTarget->resolvedPointId();
        const auto* hostBlk = !hostBlock.isNull() ? m_paramDoc->findBlock(hostBlock) : nullptr;
        const auto* hostB = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

        const bool endFollowing = !block->endTargetPointId.isNull();

        const QSignalBlocker b(m_chkAimHost);
        m_chkAimHost->setText(hostB
            ? QString::fromUtf8("\u7ec8\u70b9\u6307\u5411\u5bbf\u4e3b %1")  // 终点指向宿主 %1
                .arg(cad::param::Serial::tag(hostB->serial))
            : QString::fromUtf8("\u7ec8\u70b9\u6307\u5411\u5bbf\u4e3b"));  // 终点指向宿主
        m_chkAimHost->setChecked(endFollowing);
        m_chkAimHost->setEnabled(hostB != nullptr);
        m_chkAimHost->setToolTip(hostB
            ? QString::fromUtf8(
                  "\u52fe\u9009\u540e\u7ec8\u70b9\u59cb\u7ec8\u6307\u5411\u5bbf\u4e3b\u70b9 %1\uff1b"
                  "\u914d\u5408\u6d4b\u91cf\u957f\u5ea6\u4e0e\u8d77\u70b9\u8ddf\u968f\uff0c\u7ec8\u70b9\u59cb\u7ec8\u843d\u5728\u5bbf\u4e3b\u70b9\u4e0a")
                  .arg(cad::param::Serial::tag(hostB->serial))
            : QString::fromUtf8(
                  "\u52fe\u9009\u540e\u7ec8\u70b9\u6307\u5411\u4e0a\u65b9\u8f93\u5165\u7684\u76ee\u6807\u70b9\uff1b\u53d6\u6d88\u52fe\u9009\u91ca\u653e\u6307\u5411"));
        // 勾选后终点始终指向宿主点；配合测量长度与起点跟随，终点始终落在宿主点上
        m_chkAimHost->setVisible(true);
    }

    // Group protection: endpoint-aim drives the block's ROTATION — it would
    // break group rigidity, so aim editing is read-only on grouped members
    // (组成员不可编辑终点指向, 请先解散组).
    if (block && !m_paramDoc->groupOfBlock(m_blockId).isNull()) {
        const QString tip = QString::fromUtf8(
            "\xe7\xbb\x84\xe6\x88\x90\xe5\x91\x98\xe4\xb8\x8d\xe5\x8f\xaf"
            "\xe7\xbc\x96\xe8\xbe\x91\xe7\xbb\x88\xe7\x82\xb9\xe6\x8c\x87"
            "\xe5\x90\x91\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe8\xa7\xa3"
            "\xe6\x95\xa3\xe7\xbb\x84");
        m_refAimTarget->setEnabled(false);
        m_editAimOffset->setEnabled(false);
        m_btnClearAim->setEnabled(false);
        m_chkAimHost->setEnabled(false);
        m_refAimTarget->setToolTip(tip);
        m_editAimOffset->setToolTip(tip);
        m_btnClearAim->setToolTip(tip);
        m_chkAimHost->setToolTip(tip);
    }
}

void LinePropertyDialog::onAimTargetResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    // The aux layer is sealed: a working-layer block cannot aim at an
    // aux-layer point (and vice versa).
    if (const auto* target = m_paramDoc->findBlock(blockId)) {
        if (m_paramDoc->isAuxBlock(*target) != m_paramDoc->isAuxBlock(*block))
            return;
    }

    block->endTargetBlockId = blockId;
    block->endTargetPointId = pointId;
    // Keep the existing offset unchanged (user may have a deliberate offset).

    refreshScene();
    refreshAimCard();
    populateAngleField();
}

void LinePropertyDialog::onAimOffsetApply()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) return;

    const QString text = m_editAimOffset->text().trimmed();
    bool isNum = false;
    const double val = text.toDouble(&isNum);
    if (isNum) {
        block->endTargetOffset = val;
        block->endTargetOffsetFormula.clear();
    } else if (!text.isEmpty()) {
        block->endTargetOffsetFormula = text;
    }

    refreshScene();
    refreshAimCard();
}

void LinePropertyDialog::onAimClear()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    block->endTargetBlockId = QUuid();
    block->endTargetPointId = QUuid();
    block->endTargetOffset = 0.0;
    block->endTargetOffsetFormula.clear();

    refreshScene();
    refreshAimCard();
    populateAngleField();
}

void LinePropertyDialog::onAimHostToggled(bool on)
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) { refreshAimCard(); return; }

    if (on) {
        // Host = the current aim target, or the aim input's resolved value.
        const QUuid hostBlock = !block->endTargetBlockId.isNull()
            ? block->endTargetBlockId : m_refAimTarget->resolvedBlockId();
        const QUuid hostPoint = !block->endTargetPointId.isNull()
            ? block->endTargetPointId : m_refAimTarget->resolvedPointId();
        const cad::param::Block* host = m_paramDoc->findBlock(hostBlock);
        if (!host || !host->findPoint(hostPoint)) { refreshAimCard(); return; }
        // Aux-layer seal: never aim a working-layer block at an aux point.
        if (m_paramDoc->isAuxBlock(*host) != m_paramDoc->isAuxBlock(*block)) {
            refreshAimCard(); return;
        }
        block->endTargetBlockId = hostBlock;
        block->endTargetPointId = hostPoint;
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
    } else {
        // Freeze the aimed direction before releasing: when a follower
        // attachment drives the rotation, back-solve its follower angle
        // from the CURRENT (aimed) rotation so the line doesn't jump.
        if (const auto* fatt = findFollowerAttachment()) {
            if (const auto* leader = m_paramDoc->findBlock(fatt->toBlockId)) {
                auto& atts = const_cast<std::vector<cad::param::Attachment>&>(
                    m_paramDoc->attachments());
                for (auto& a : atts) {
                    if (a.id != fatt->id) continue;
                    const double refWorld = leader->transform.rotation
                        + leader->exitDirectionAtPoint(a.toPointId, a.toSegmentId);
                    const double localDir = block->directionAtPoint(a.fromPointId);
                    a.followerAngle = cad::param::backSolveFollowerAngle(
                        block->transform.rotation, localDir, refWorld);
                    a.followerAngleFormula.clear();
                    a.rotationMode = cad::param::RotationMode::Angle;
                    a.arcLength = 0.0;
                    a.arcLengthFormula.clear();
                    break;
                }
            }
        }
        block->endTargetBlockId = QUuid();
        block->endTargetPointId = QUuid();
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
    }

    refreshScene();
    refreshFollowerState();
    populateAngleField();
    refreshAimCard();
}

} // namespace cad::tools
