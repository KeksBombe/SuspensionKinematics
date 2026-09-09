#include "app/HardpointModel.h"

#include "io/LinkageTemplate.h"

#include <QSignalSpy>
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

    void theNameAndTheNumberAreNotEditable();
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

void TestHardpointModel::theNameAndTheNumberAreNotEditable()
{
    // The name is the key the workbook is written back through and the key the
    // configuration is stored under. Editing it here would break both.
    QVERIFY(!(m_model->flags(cell(0, HardpointModel::NameColumn)) & Qt::ItemIsEditable));
    QVERIFY(!(m_model->flags(cell(0, HardpointModel::IndexColumn)) & Qt::ItemIsEditable));
    QVERIFY(m_model->flags(cell(0, HardpointModel::XColumn)) & Qt::ItemIsEditable);
    QVERIFY(m_model->flags(cell(0, HardpointModel::TypeColumn)) & Qt::ItemIsEditable);
    QVERIFY(m_model->flags(cell(0, HardpointModel::BushingColumn)) & Qt::ItemIsEditable);
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
