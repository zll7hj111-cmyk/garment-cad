#include "test_dialog_tabs_helpers.h"

class TestDialogTabsAux : public QObject
{
    Q_OBJECT
private slots:
    void auxPointOutgoingFollowerMountAndDetach();
};

void TestDialogTabsAux::auxPointOutgoingFollowerMountAndDetach()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    doc.setActiveLayer(layerIdAt(doc, 1));
    LineSetup leaderLine = makeLine(doc, 100.0, Vec2(0.0, 0.0));
    LineSetup followerLine = makeLine(doc, 60.0, Vec2(50.0, 50.0));
    const QUuid auxId = addAuxPoint(doc, followerLine.blockId, followerLine.segId);

    // Follower line connects to leaderLine via follower's auxPoint
    Attachment att;
    att.fromBlockId = followerLine.blockId;
    att.fromPointId = auxId;
    att.toBlockId = leaderLine.blockId;
    att.toPointId = leaderLine.startId;
    att.toSegmentId = leaderLine.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        followerLine.blockId, followerLine.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget*");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);

    // 辅助点已作为【线上点】分区嵌入在属性页 (tab 0)
    auto* auxTab = dlg->findChild<cad::ui::SegmentAuxTab*>();
    QVERIFY(auxTab);

    // Click aux list item
    auto* list = auxTab->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());

    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<cad::ui::AuxPointForm*>() != nullptr; }),
             "timed out waiting for AuxPointForm");
    auto* form = dlg->findChild<cad::ui::AuxPointForm*>();
    QVERIFY(form && form->isVisible());

    auto* lblMount = form->findChild<ElaText*>(QStringLiteral("auxMountInfo"));
    auto* btnDetach = form->findChild<QPushButton*>(QStringLiteral("auxDetachBtn"));
    QVERIFY(lblMount && btnDetach);

    const auto* ldrSeg = doc.findBlock(leaderLine.blockId)->findSegment(leaderLine.segId);
    const QString ldrTag = cad::param::Serial::tag(ldrSeg->serial);

    // Verify label contains "跟随" and the leader tag
    QVERIFY2(lblMount->text().contains(QString::fromUtf8("跟随")) &&
             lblMount->text().contains(ldrTag),
             "辅助点挂载信息必须显示跟随的宿主线段");
    QVERIFY2(btnDetach->isEnabled(), "拆开按钮应处于可用状态");

    // Click detach button
    btnDetach->click();
    QVERIFY2(cad::test::waitUntil([&] { return doc.attachments().empty(); }),
             "点击拆开后 attachment 必须被彻底移除");
    QVERIFY2(lblMount->text() == QString::fromUtf8("无挂载"),
             "拆开后辅助点状态必须变为无挂载");
    QVERIFY2(!btnDetach->isEnabled(), "拆开后按钮必须禁用");

    delete dlg;
}


QTEST_MAIN(TestDialogTabsAux)
#include "test_dialog_tabs_aux.moc"
