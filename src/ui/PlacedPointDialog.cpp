#include "ui/PlacedPointDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDoubleValidator>

#include "ElaLineEdit.h"
#include "ElaText.h"
#include "ElaPushButton.h"
#include "ui/ElaDialogButtons.h"
#include "ui/FormScaffold.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "canvas/CanvasScene.h"
#include "document/commands/EndpointCommands.h"

namespace cad::ui {

PlacedPointDialog::PlacedPointDialog(const QUuid& blockId,
                                     const QUuid& pointId,
                                     cad::param::ParamDocument* doc,
                                     CanvasScene* scene,
                                     QWidget* parent)
    : ElaDialog(parent)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_doc(doc)
    , m_scene(scene)
{
    setWindowTitle(QString::fromUtf8("放置点属性"));
    setMinimumWidth(380);
    setIsDefaultClosed(false);
    connect(this, &ElaDialog::closeButtonClicked, this, &PlacedPointDialog::onRejected);

    // Capture initial state for cancellation rollback
    if (m_doc) {
        if (auto* blk = m_doc->findBlock(m_blockId)) {
            if (auto* p = blk->findPoint(m_pointId)) {
                m_initialPoint = *p;
            }
        }
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 16, 12);
    mainLayout->setSpacing(10);

    // ── 1. 基本信息 ──
    mainLayout->addWidget(makeFormGroupHeader(
        QStringLiteral("INFO"), QString::fromUtf8("基本信息"), this));

    auto* infoLayout = new QFormLayout();
    infoLayout->setSpacing(6);

    m_lblSerial = new ElaText(this);
    m_lblSerial->setTextFormat(Qt::RichText);
    infoLayout->addRow(QString::fromUtf8("点编号:"), m_lblSerial);

    m_lblHost = new ElaText(this);
    infoLayout->addRow(QString::fromUtf8("基准线:"), m_lblHost);

    m_editName = new ElaLineEdit(this);
    m_editName->setPlaceholderText(QString::fromUtf8("可选名称 (如 P')"));
    infoLayout->addRow(QString::fromUtf8("点名称:"), m_editName);

    mainLayout->addLayout(infoLayout);

    // ── 2. 偏置几何 ──
    mainLayout->addWidget(makeFormGroupHeader(
        QStringLiteral("OFFSET"), QString::fromUtf8("偏置几何"), this));

    auto* geomLayout = new QFormLayout();
    geomLayout->setSpacing(6);

    // 偏置距离
    auto* distRow = new QHBoxLayout();
    m_editOffsetDist = new ElaLineEdit(this);
    m_editOffsetDist->setPlaceholderText(QStringLiteral("0.0"));
    m_editOffsetDistFormula = new ElaLineEdit(this);
    m_editOffsetDistFormula->setPlaceholderText(QString::fromUtf8("公式 (如 w/4)"));
    distRow->addWidget(m_editOffsetDist, 1);
    distRow->addWidget(new ElaText(QStringLiteral("cm"), this));
    distRow->addWidget(m_editOffsetDistFormula, 2);
    geomLayout->addRow(QString::fromUtf8("偏置距离:"), distRow);

    // 偏置角度
    auto* angleRow = new QHBoxLayout();
    m_editOffsetAngle = new ElaLineEdit(this);
    m_editOffsetAngle->setPlaceholderText(QStringLiteral("90.0"));
    m_editOffsetAngleFormula = new ElaLineEdit(this);
    m_editOffsetAngleFormula->setPlaceholderText(QString::fromUtf8("公式"));
    angleRow->addWidget(m_editOffsetAngle, 1);
    angleRow->addWidget(new ElaText(QStringLiteral("°"), this));
    angleRow->addWidget(m_editOffsetAngleFormula, 2);
    geomLayout->addRow(QString::fromUtf8("偏置角度:"), angleRow);

    mainLayout->addLayout(geomLayout);
    mainLayout->addStretch();

    // ── 3. 底部按钮 ──
    auto* btnDelete = new ElaPushButton(QString::fromUtf8("删除该点"), this);
    btnDelete->setFixedHeight(32);
    btnDelete->setCursor(Qt::PointingHandCursor);
    connect(btnDelete, &QPushButton::clicked, this, &PlacedPointDialog::onDeletePoint);

    const auto btns = makeDialogButtons(
        this, QString::fromUtf8("确定"), QString::fromUtf8("取消"));
    // 把删除按钮插入到底部行最左侧
    auto* btnRowLay = qobject_cast<QHBoxLayout*>(btns.row->layout());
    if (btnRowLay) {
        btnRowLay->insertWidget(0, btnDelete);
    }
    mainLayout->addWidget(btns.row);

    connect(btns.ok,     &QPushButton::clicked, this, &PlacedPointDialog::onAccepted);
    connect(btns.cancel, &QPushButton::clicked, this, &PlacedPointDialog::onRejected);

    // 实时更新定时器 (防抖 150ms)
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &PlacedPointDialog::onLiveUpdate);

    auto hookDirty = [this]() {
        if (m_debounce) m_debounce->start();
    };
    connect(m_editName, &QLineEdit::textChanged, this, hookDirty);
    connect(m_editOffsetDist, &QLineEdit::textChanged, this, hookDirty);
    connect(m_editOffsetDistFormula, &QLineEdit::textChanged, this, hookDirty);
    connect(m_editOffsetAngle, &QLineEdit::textChanged, this, hookDirty);
    connect(m_editOffsetAngleFormula, &QLineEdit::textChanged, this, hookDirty);

    populateFromModel();
}

PlacedPointDialog::~PlacedPointDialog() = default;

void PlacedPointDialog::onDeletePoint()
{
    if (m_debounce && m_debounce->isActive()) {
        m_debounce->stop();
    }
    if (m_doc) {
        if (m_doc->undoStack()) {
            m_doc->undoStack()->push(new cad::cmd::RemovePlacedPointCommand(
                m_doc, m_blockId, m_pointId));
        } else {
            auto* blk = m_doc->findBlock(m_blockId);
            if (blk) {
                if (auto* pt = blk->findPoint(m_pointId)) {
                    if (auto* seg = blk->findSegment(pt->hostSegmentId)) {
                        auto& ids = seg->auxPointIds;
                        ids.erase(std::remove(ids.begin(), ids.end(), m_pointId), ids.end());
                    }
                    auto& pts = blk->points;
                    pts.erase(std::remove_if(pts.begin(), pts.end(),
                        [this](const cad::param::ParamPoint& p) { return p.id == m_pointId; }),
                        pts.end());
                    blk->rebuildPointIndex();
                    m_doc->resolveAll();
                }
            }
        }
    }
    if (m_scene) m_scene->refreshAllBlockItems();
    reject();
}

void PlacedPointDialog::populateFromModel()
{
    if (!m_doc) return;
    auto* blk = m_doc->findBlock(m_blockId);
    if (!blk) return;
    auto* pt = blk->findPoint(m_pointId);
    if (!pt) return;

    m_lblSerial->setText(cad::param::Serial::toHtml(pt->serial));
    m_editName->setText(pt->name);

    if (auto* seg = blk->findSegment(pt->hostSegmentId)) {
        m_lblHost->setText(QString::fromUtf8("线段 %1").arg(cad::param::Serial::tag(seg->serial)));
    } else {
        m_lblHost->setText(QString::fromUtf8("无宿主"));
    }

    // Offset distance: internal mm -> UI cm
    m_editOffsetDist->setText(QString::number(pt->interpOffsetDist / 10.0, 'f', 2));
    m_editOffsetDistFormula->setText(pt->interpOffsetDistFormula);

    // Offset angle: degrees
    m_editOffsetAngle->setText(QString::number(pt->interpOffsetAngle, 'f', 1));
    m_editOffsetAngleFormula->setText(pt->interpOffsetAngleFormula);
}

void PlacedPointDialog::applyToPoint(cad::param::ParamPoint& pt) const
{
    pt.name = m_editName->text().trimmed();

    bool ok = false;
    double distCm = m_editOffsetDist->text().toDouble(&ok);
    if (ok) pt.interpOffsetDist = distCm * 10.0;
    pt.interpOffsetDistFormula = m_editOffsetDistFormula->text().trimmed();

    double angleDeg = m_editOffsetAngle->text().toDouble(&ok);
    if (ok) pt.interpOffsetAngle = angleDeg;
    pt.interpOffsetAngleFormula = m_editOffsetAngleFormula->text().trimmed();
}

void PlacedPointDialog::onLiveUpdate()
{
    if (!m_doc) return;
    auto* blk = m_doc->findBlock(m_blockId);
    if (!blk) return;
    auto* pt = blk->findPoint(m_pointId);
    if (!pt) return;

    applyToPoint(*pt);
    m_doc->resolveAll();
    if (m_scene) m_scene->update();
}

void PlacedPointDialog::onAccepted()
{
    if (m_debounce && m_debounce->isActive()) {
        m_debounce->stop();
    }
    if (!m_doc) {
        accept();
        return;
    }

    auto* blk = m_doc->findBlock(m_blockId);
    if (!blk) {
        accept();
        return;
    }
    auto* pt = blk->findPoint(m_pointId);
    if (!pt) {
        accept();
        return;
    }

    cad::param::ParamPoint newPt = *pt;
    applyToPoint(newPt);

    // Revert to initial point first so redo/undo properly applies newPt
    *pt = m_initialPoint;
    if (m_doc->undoStack()) {
        m_doc->undoStack()->push(new cad::cmd::EditPlacedPointCommand(
            m_doc, m_blockId, m_initialPoint, newPt));
    } else {
        *pt = newPt;
        m_doc->resolveAll();
    }
    if (m_scene) m_scene->update();
    accept();
}

void PlacedPointDialog::onRejected()
{
    if (m_debounce && m_debounce->isActive()) {
        m_debounce->stop();
    }
    if (m_doc) {
        if (auto* blk = m_doc->findBlock(m_blockId)) {
            if (auto* pt = blk->findPoint(m_pointId)) {
                *pt = m_initialPoint;
                m_doc->resolveAll();
            }
        }
    }
    if (m_scene) m_scene->update();
    reject();
}

} // namespace cad::ui
