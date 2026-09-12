#include "app/HardpointModel.h"

#include "io/LinkageTemplate.h"

#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>

using namespace suspkin;

namespace {

HardpointTable twoPoints()
{
    HardpointTable table;
    Hardpoint inner;
    inner.name = QStringLiteral("F_LCA_IF");
    inner.coord[0] = -2068.622;
    inner.coord[1] = 271.5;
    inner.coord[2] = 118.0;
    Hardpoint outer;
    outer.name = QStringLiteral("F_LCA_O");
    outer.coord[0] = -2000.0;
    outer.coord[1] = 560.0;
    outer.coord[2] = 110.0;
    table.points = { inner, outer };
    return table;
}

const QString kGround = BodyCatalog::ground();
const QString kUpright = QStringLiteral("Upright");
const QString kLowerWishbone = QStringLiteral("Lower wishbone");

} // namespace

class TestHardpointModel : public QObject {
    Q_OBJECT

private slots:
    void init();

    void theNumberIsNotEditableAndTheNameIs();
    void aNameThatWouldNotReadBackIsRefused();
    void aRenameCarriesWhatWasStoredUnderTheName();
    void rowsGoInAndOutWithoutAReset();
    void removingAPointDropsWhatDescribedIt();
    void anEditThatCannotMeanAnythingNeverReachesTheStore();
    void anUnfinishedEditIsStoredAndFlagged();
    void anEditTouchesOnlyItsOwnRow();
    void repeatingAnEditIsNotAChange();
    void theTypeColumnRoundTripsThroughItsChip();
    void theBodyColumnsOfferTheCatalogue();
    void aRigidJointReadsAsADash();
    void aCoordinateIsStillHandedOverUnrounded();
    void clearingTheTableClearsWhatDescribedIt();

private:
    QModelIndex cell(int row, int column) const { return m_model->index(row, column); }

    HardpointModel* m_model = nullptr;
};

void TestHardpointModel::init()
{
    delete m_model;
    m_model = new HardpointModel(this);
    m_model->setTable(twoPoints());
    m_model->setBodyCatalog(bodyCatalog(builtinLinkageTemplate()));
}

void TestHardpointModel::theNumberIsNotEditableAndTheNameIs()
{
    // The number is where the point sits, which nothing typed into it changes.
    QVERIFY(!(m_model->flags(cell(0, HardpointModel::IndexColumn)) & Qt::ItemIsEditable));
    QVERIFY(m_model->flags(cell(0, HardpointModel::NameColumn)) & Qt::ItemIsEditable);
    QVERIFY(m_model->flags(cell(0, HardpointModel::XColumn)) & Qt::ItemIsEditable);
    QVERIFY(m_model->flags(cell(0, HardpointModel::TypeColumn)) & Qt::ItemIsEditable);
    QVERIFY(m_model->flags(cell(0, HardpointModel::BushingColumn)) & Qt::ItemIsEditable);

    QSignalSpy renamed(m_model, &HardpointModel::pointRenamed);
    QVERIFY(m_model->setData(cell(0, HardpointModel::NameColumn), QStringLiteral("F_LCA_FRONT"),
                             Qt::EditRole));
    QCOMPARE(m_model->table().points[0].name, QStringLiteral("F_LCA_FRONT"));
    QCOMPARE(renamed.size(), 1);
    QCOMPARE(renamed.first().at(1).toString(), QStringLiteral("F_LCA_IF"));
    QCOMPARE(renamed.first().at(2).toString(), QStringLiteral("F_LCA_FRONT"));

    // What was typed around the name is not part of it: the workbook reader
    // would trim it off on the way back in.
    QVERIFY(m_model->setData(cell(0, HardpointModel::NameColumn), QStringLiteral("  F_LCA_F  "),
                             Qt::EditRole));
    QCOMPARE(m_model->table().points[0].name, QStringLiteral("F_LCA_F"));
}

void TestHardpointModel::aNameThatWouldNotReadBackIsRefused()
{
    QSignalSpy rejected(m_model, &HardpointModel::editRejected);
    const QModelIndex name = cell(0, HardpointModel::NameColumn);

    // Empty, taken, and one character from being the x of some other point.
    QVERIFY(!m_model->setData(name, QString(), Qt::EditRole));
    QVERIFY(!m_model->setData(name, QStringLiteral("F_LCA_O"), Qt::EditRole));
    QVERIFY(!m_model->setData(name, QStringLiteral("F_LCA_IF_x"), Qt::EditRole));
    QVERIFY(!m_model->setData(name, QStringLiteral("F_LCA_IF.Z"), Qt::EditRole));
    QCOMPARE(rejected.size(), 4);
    for (const QList<QVariant>& refusal : rejected) QVERIFY(!refusal.at(1).toString().isEmpty());
    QCOMPARE(m_model->table().points[0].name, QStringLiteral("F_LCA_IF"));

    // The same name again is not an edit, and says nothing either way.
    QVERIFY(!m_model->setData(name, QStringLiteral("F_LCA_IF"), Qt::EditRole));
    QCOMPARE(rejected.size(), 4);

    // And the rule is the core's, so a dialog can ask it before the model does.
    QVERIFY(hardpointNameProblem(QStringLiteral("F_LCA_IF"), m_model->table(), 0).isEmpty());
    QVERIFY(!hardpointNameProblem(QStringLiteral("F_LCA_IF"), m_model->table()).isEmpty());
    QVERIFY(!hardpointNameProblem(QStringLiteral(" A"), m_model->table()).isEmpty());
    QVERIFY(!hardpointNameProblem(QStringLiteral("A\tB"), m_model->table()).isEmpty());
    QVERIFY(hardpointNameProblem(QStringLiteral("F_Rocker_AxisPoint"), m_model->table()).isEmpty());
}

void TestHardpointModel::aRenameCarriesWhatWasStoredUnderTheName()
{
    QVERIFY(m_model->setData(cell(1, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));

    HardpointTable table = m_model->table();
    Hardpoint mirror = table.points[1];
    mirror.name = QStringLiteral("F_LCA_O_R");
    mirror.coord[1] = -mirror.coord[1];
    mirror.mirrorOf = QStringLiteral("F_LCA_O");
    table.points.push_back(mirror);
    const HardpointConfigMap config = m_model->config();
    m_model->setTable(table);
    m_model->setConfig(config);

    QVERIFY(m_model->renamePoint(1, QStringLiteral("F_LCA_OUT")));

    // The name is the key, so what was kept under it follows it.
    QVERIFY(!m_model->config().contains(QStringLiteral("F_LCA_O")));
    QCOMPARE(m_model->config().value(QStringLiteral("F_LCA_OUT")).part1, kLowerWishbone);
    QCOMPARE(m_model->configAt(1).part1, kLowerWishbone);
    // And a point mirrored from it is still known to be its mirror.
    QCOMPARE(m_model->table().points[2].mirrorOf, QStringLiteral("F_LCA_OUT"));
}

void TestHardpointModel::rowsGoInAndOutWithoutAReset()
{
    // The panel sorts and filters through a proxy. A reset would throw away its
    // selection and scroll position on every add; row signals do not.
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(m_model);
    proxy.setSortRole(Qt::EditRole);
    proxy.sort(HardpointModel::NameColumn);

    QSignalSpy reset(m_model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(m_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(m_model, &QAbstractItemModel::rowsRemoved);

    Hardpoint point;
    point.name = QStringLiteral("F_AAA");
    point.coord[2] = 42.0;
    QVERIFY(m_model->insertPoint(1, point));
    QCOMPARE(m_model->rowCount(), 3);
    QCOMPARE(m_model->table().points[1].name, QStringLiteral("F_AAA"));
    QCOMPARE(inserted.size(), 1);
    // Sorted by name it is first, whatever row it went in at.
    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxy.index(0, HardpointModel::NameColumn).data().toString(),
             QStringLiteral("F_AAA"));

    // A taken name is refused here too: two points under one key cannot both
    // be written back.
    QVERIFY(!m_model->insertPoint(0, point));
    QCOMPARE(m_model->rowCount(), 3);

    // Out of order and with a repeat, as a multi-selection hands them over.
    m_model->removePoints({ 0, 2, 0 });
    QCOMPARE(m_model->rowCount(), 1);
    QCOMPARE(m_model->table().points[0].name, QStringLiteral("F_AAA"));
    QCOMPARE(removed.size(), 2);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(reset.size(), 0);
}

void TestHardpointModel::removingAPointDropsWhatDescribedIt()
{
    QVERIFY(m_model->setData(cell(1, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));
    QVERIFY(m_model->config().contains(QStringLiteral("F_LCA_O")));

    m_model->removePoints({ 1 });
    // A description of a point that is not there is not worth keeping -- and a
    // point added later under that name should be inferred afresh, not handed
    // an old one.
    QVERIFY(!m_model->config().contains(QStringLiteral("F_LCA_O")));
}

void TestHardpointModel::anEditThatCannotMeanAnythingNeverReachesTheStore()
{
    QVERIFY(m_model->setData(cell(0, HardpointModel::Part1Column), kUpright, Qt::EditRole));

    QSignalSpy rejected(m_model, &HardpointModel::editRejected);
    QSignalSpy changed(m_model, &HardpointModel::configChanged);

    // The same body on both sides of the joint is not a joint.
    QVERIFY(!m_model->setData(cell(0, HardpointModel::Part2Column), kUpright, Qt::EditRole));
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(rejected.first().at(0).toInt(), 0);
    QVERIFY(!rejected.first().at(1).toString().isEmpty());
    QCOMPARE(changed.size(), 0);
    // And the store still holds what it held before.
    QVERIFY(m_model->configAt(0).part2.isEmpty());

    // So is a bushing index outside the range the format allows.
    QVERIFY(!m_model->setData(cell(0, HardpointModel::BushingColumn), kMaxBushingIndex + 1,
                              Qt::EditRole));
    QCOMPARE(m_model->configAt(0).bushing, kNoBushing);
    QCOMPARE(rejected.size(), 2);
}

void TestHardpointModel::anUnfinishedEditIsStoredAndFlagged()
{
    QSignalSpy rejected(m_model, &HardpointModel::editRejected);

    // A point on its way to being described: typed, but nothing named yet.
    // Refusing this would be standing in the way of the work.
    QVERIFY(m_model->setData(cell(0, HardpointModel::TypeColumn),
                             static_cast<int>(PointType::ToBody), Qt::EditRole));
    QCOMPARE(rejected.size(), 0);
    QCOMPARE(m_model->configAt(0).type, PointType::ToBody);

    const std::vector<ConfigIssue> issues = m_model->issuesAt(0);
    QCOMPARE(issues.size(), std::size_t(1));
    QVERIFY(!hasError(issues));
    QCOMPARE(m_model->issueCount(), 1);
    QCOMPARE(cell(0, HardpointModel::NameColumn).data(HardpointModel::IssueLevelRole).toInt(),
             static_cast<int>(ConfigIssueLevel::Warning));

    // Finishing it takes the flag away again.
    QVERIFY(m_model->setData(cell(0, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));
    QVERIFY(m_model->setData(cell(0, HardpointModel::Part2Column), kGround, Qt::EditRole));
    QVERIFY(m_model->issuesAt(0).empty());
    QCOMPARE(cell(0, HardpointModel::NameColumn).data(HardpointModel::IssueLevelRole).toInt(), -1);
}

void TestHardpointModel::anEditTouchesOnlyItsOwnRow()
{
    QSignalSpy changed(m_model, &QAbstractItemModel::dataChanged);

    QVERIFY(m_model->setData(cell(1, HardpointModel::TypeColumn),
                             static_cast<int>(PointType::Solved), Qt::EditRole));

    // One row's worth of repainting, not the table's. The annotation on the
    // other cells of that row can change, and nothing beyond it can.
    QCOMPARE(changed.size(), 1);
    const auto topLeft = changed.first().at(0).value<QModelIndex>();
    const auto bottomRight = changed.first().at(1).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 1);
    QCOMPARE(bottomRight.row(), 1);
    QCOMPARE(topLeft.column(), int(HardpointModel::IndexColumn));
    QCOMPARE(bottomRight.column(), int(HardpointModel::ColumnCount - 1));
}

void TestHardpointModel::repeatingAnEditIsNotAChange()
{
    QVERIFY(m_model->setData(cell(0, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));

    QSignalSpy changed(m_model, &HardpointModel::configChanged);
    // Opening an editor and pressing Enter is not an edit, and must not mark
    // the project dirty.
    QVERIFY(!m_model->setData(cell(0, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));
    QCOMPARE(changed.size(), 0);
}

void TestHardpointModel::theTypeColumnRoundTripsThroughItsChip()
{
    QVERIFY(m_model->setData(cell(0, HardpointModel::TypeColumn),
                             static_cast<int>(PointType::Dependent), Qt::EditRole));

    const QModelIndex type = cell(0, HardpointModel::TypeColumn);
    QCOMPARE(type.data(Qt::DisplayRole).toString(), pointTypeLabel(PointType::Dependent));
    QCOMPARE(type.data(Qt::EditRole).toInt(), static_cast<int>(PointType::Dependent));
    // What the chip is painted from.
    QCOMPARE(type.data(HardpointModel::PointTypeRole).toInt(),
             static_cast<int>(PointType::Dependent));

    // Something that is not one of the four is a bug on the way in, not a value.
    QVERIFY(!m_model->setData(type, 99, Qt::EditRole));
}

void TestHardpointModel::theBodyColumnsOfferTheCatalogue()
{
    const QStringList bodies =
        cell(0, HardpointModel::Part1Column).data(HardpointModel::ChoicesRole).toStringList();
    QCOMPARE(bodies, m_model->bodyCatalog().bodies);
    QVERIFY(bodies.contains(kGround));

    const QStringList types =
        cell(0, HardpointModel::TypeColumn).data(HardpointModel::ChoicesRole).toStringList();
    QCOMPARE(types.size(), int(std::size(kPointTypes)));
    QCOMPARE(types.first(), pointTypeLabel(PointType::Unassigned));
}

void TestHardpointModel::aRigidJointReadsAsADash()
{
    const QModelIndex bushing = cell(0, HardpointModel::BushingColumn);
    QCOMPARE(bushing.data(Qt::EditRole).toInt(), kNoBushing);
    QCOMPARE(bushing.data(Qt::DisplayRole).toString(), QString(QChar(0x2014)));

    QVERIFY(m_model->setData(bushing, 7, Qt::EditRole));
    QCOMPARE(bushing.data(Qt::DisplayRole).toString(), QStringLiteral("7"));
}

void TestHardpointModel::aCoordinateIsStillHandedOverUnrounded()
{
    // The table shows three decimals; what an editor is opened on is the value
    // the workbook actually holds, or closing the editor would change it.
    QCOMPARE(cell(0, HardpointModel::XColumn).data(Qt::EditRole).toDouble(), -2068.622);
    QVERIFY(m_model->setData(cell(0, HardpointModel::XColumn), -2068.6225, Qt::EditRole));
    QCOMPARE(m_model->table().points[0].coord[0], -2068.6225);
}

void TestHardpointModel::clearingTheTableClearsWhatDescribedIt()
{
    QVERIFY(m_model->setData(cell(0, HardpointModel::Part1Column), kLowerWishbone, Qt::EditRole));
    m_model->clear();

    QCOMPARE(m_model->rowCount(), 0);
    QVERIFY(m_model->config().isEmpty());
}

QTEST_MAIN(TestHardpointModel)
#include "test_hardpoint_model.moc"
