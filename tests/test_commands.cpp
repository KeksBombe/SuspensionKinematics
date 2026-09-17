#include "app/Ribbon.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/RibbonPages.h"

#include <QAction>
#include <QTest>

using namespace suspkin;

namespace {

/// A command with only what the case under test cares about filled in.
CommandSpec named(const QString& id, const QString& text = QStringLiteral("Command"))
{
    CommandSpec spec;
    spec.id = id;
    spec.text = text;
    return spec;
}

RibbonSlot slot(const QString& page, const QString& group, int order,
                RibbonButton button = RibbonButton::Small)
{
    RibbonSlot placement;
    placement.page = page;
    placement.group = group;
    placement.button = button;
    placement.order = order;
    return placement;
}

} // namespace

/// The layer a feature registers into: what it keeps, what it asks, and what it
/// tells the ribbon.
class TestCommands : public QObject {
    Q_OBJECT

private slots:
    void aRegisteredCommandIsFoundAgainByItsId();
    void everyRegisteredCommandIsInTheListThatArmsTheShortcuts();
    void aCommandDecidesForItselfWhetherItCanBeUsed();
    void aRuleIsAskedOnceWhenItIsGivenRatherThanAnEventLoopLater();
    void aCommandWithNoRuleIsNeverDisabled();
    void aCommandsNameCanFollowTheStateAndItsTooltipFollowsIt();
    void aCheckableCommandHidesItsIconInMenusAndKeepsItsDefault();
    void aCommandMayAppearInMoreThanOnePlace();
    void aCommandThatSaidNothingAboutTheRibbonIsNotOnIt();
    void groupsFollowTheOrderOfTheirEarliestCommand();
    void aPageNoFeaturePutAnythingOnIsNotDrawn();
};

void TestCommands::aRegisteredCommandIsFoundAgainByItsId()
{
    QObject owner;
    CommandRegistry commands(&owner);

    QAction* action = commands.add(named(QStringLiteral("a.one"), QStringLiteral("One")));
    QCOMPARE(commands.action(QStringLiteral("a.one")), action);
    QCOMPARE(action->text(), QStringLiteral("One"));

    // How one feature reaches another's command -- the File menu asks for
    // ProjectFeature's commands this way -- without either including the other.
    QCOMPARE(commands.action(QStringLiteral("nobody.registered.this")), nullptr);
}

void TestCommands::everyRegisteredCommandIsInTheListThatArmsTheShortcuts()
{
    QObject owner;
    CommandRegistry commands(&owner);

    // A shortcut is live only while a widget the action is on is visible, so
    // the window puts every command on itself. That list used to be written out
    // by hand, which is a list that quietly loses a command the day somebody
    // adds one and forgets.
    QAction* described = commands.add(named(QStringLiteral("a.one")));
    auto* byHand = new QAction(QStringLiteral("Two"), &owner);
    commands.adopt(byHand, QStringLiteral("a.two"), {});

    QCOMPARE(commands.all().size(), 2);
    QVERIFY(commands.all().contains(described));
    QVERIFY(commands.all().contains(byHand));
}

void TestCommands::aCommandDecidesForItselfWhetherItCanBeUsed()
{
    QObject owner;
    CommandRegistry commands(&owner);

    bool thereArePoints = false;
    CommandSpec spec = named(QStringLiteral("a.needsPoints"));
    spec.enabledWhen = [&thereArePoints] { return thereArePoints; };
    QAction* action = commands.add(std::move(spec));

    QVERIFY(!action->isEnabled());

    thereArePoints = true;
    commands.refreshEnabled();
    QVERIFY(action->isEnabled());

    thereArePoints = false;
    commands.refreshEnabled();
    QVERIFY(!action->isEnabled());
}

void TestCommands::aRuleIsAskedOnceWhenItIsGivenRatherThanAnEventLoopLater()
{
    QObject owner;
    CommandRegistry commands(&owner);

    CommandSpec spec = named(QStringLiteral("a.never"));
    spec.enabledWhen = [] { return false; };
    // Not enabled for the length of one event loop turn and then not: a command
    // that starts unusable starts that way.
    QVERIFY(!commands.add(std::move(spec))->isEnabled());
}

void TestCommands::aCommandWithNoRuleIsNeverDisabled()
{
    QObject owner;
    CommandRegistry commands(&owner);

    QAction* action = commands.add(named(QStringLiteral("a.always")));
    QVERIFY(action->isEnabled());
    commands.refreshEnabled();
    QVERIFY(action->isEnabled());
}

void TestCommands::aCommandsNameCanFollowTheStateAndItsTooltipFollowsIt()
{
    QObject owner;
    CommandRegistry commands(&owner);

    QString step;
    CommandSpec spec = named(QStringLiteral("edit.undo"), QStringLiteral("&Undo"));
    spec.iconText = QStringLiteral("Undo");
    spec.textWhen = [&step] {
        return step.isEmpty() ? QStringLiteral("&Undo") : QStringLiteral("&Undo %1").arg(step);
    };
    QAction* action = commands.add(std::move(spec));

    step = QStringLiteral("Move F_UCA_IF");
    commands.refreshText();
    QCOMPARE(action->text(), QStringLiteral("&Undo Move F_UCA_IF"));
    QVERIFY(action->toolTip().contains(QStringLiteral("Undo Move F_UCA_IF")));
    // The ribbon's label stays what it was, so the button keeps its width.
    QCOMPARE(action->iconText(), QStringLiteral("Undo"));

    step.clear();
    commands.refreshText();
    QCOMPARE(action->text(), QStringLiteral("&Undo"));
    QVERIFY(!action->toolTip().contains(QStringLiteral("F_UCA_IF")));

    // A command that never asked keeps the name it was given.
    QAction* plain = commands.add(named(QStringLiteral("a.plain"), QStringLiteral("Plain")));
    commands.refreshText();
    QCOMPARE(plain->text(), QStringLiteral("Plain"));
}

void TestCommands::aCheckableCommandHidesItsIconInMenusAndKeepsItsDefault()
{
    QObject owner;
    CommandRegistry commands(&owner);

    int toggles = 0;
    CommandSpec spec = named(QStringLiteral("a.show"));
    spec.checkable = true;
    spec.checkedByDefault = true;
    spec.onToggled = [&toggles](bool) { ++toggles; };
    QAction* action = commands.add(std::move(spec));

    QVERIFY(action->isCheckable());
    QVERIFY(action->isChecked());
    // A menu marks a checkable entry with its tick, not with an icon.
    QVERIFY(!action->isIconVisibleInMenu());
    // Starting checked is not the user having just switched it on.
    QCOMPARE(toggles, 0);

    action->setChecked(false);
    QCOMPARE(toggles, 1);
}

void TestCommands::aCommandMayAppearInMoreThanOnePlace()
{
    QObject owner;
    CommandRegistry commands(&owner);

    // Show Parts is on the Linkage tab and on the View tab. It is one command
    // in two views, not two commands: they cannot disagree about whether it is
    // ticked, because they are the same QAction.
    CommandSpec spec = named(QStringLiteral("linkage.showParts"));
    spec.ribbon = { slot(QStringLiteral("linkage"), QStringLiteral("Show"), 10),
                    slot(QStringLiteral("view"), QStringLiteral("Show"), 60) };
    QAction* action = commands.add(std::move(spec));

    QCOMPARE(commands.all().size(), 1);
    QCOMPARE(commands.placements().size(), std::size_t(2));
    for (const RegisteredCommand& placed : commands.placements())
        QCOMPARE(placed.action, action);
}

void TestCommands::aCommandThatSaidNothingAboutTheRibbonIsNotOnIt()
{
    QObject owner;
    CommandRegistry commands(&owner);

    // The File menu's commands, which are not a tab's worth.
    commands.add(named(QStringLiteral("project.quit")));
    QCOMPARE(commands.all().size(), 1);
    QVERIFY(commands.placements().empty());
}

void TestCommands::groupsFollowTheOrderOfTheirEarliestCommand()
{
    QObject owner;
    CommandRegistry commands(&owner);

    // Registered out of order, and by two different features: what decides
    // where they land is the order they asked for, never who registered first.
    CommandSpec second = named(QStringLiteral("a.second"), QStringLiteral("Second"));
    second.ribbon = { slot(QStringLiteral("view"), QStringLiteral("Later"), 20) };
    commands.add(std::move(second));

    CommandSpec first = named(QStringLiteral("a.first"), QStringLiteral("First"));
    first.ribbon = { slot(QStringLiteral("view"), QStringLiteral("Earlier"), 10) };
    commands.add(std::move(first));

    Ribbon ribbon;
    buildRibbonFrom(&ribbon, commands);

    // One page was asked for, so one page is there.
    QCOMPARE(ribbon.pageKeys(), QStringList{ QStringLiteral("view") });
}

void TestCommands::aPageNoFeaturePutAnythingOnIsNotDrawn()
{
    QObject owner;
    CommandRegistry commands(&owner);

    CommandSpec spec = named(QStringLiteral("a.one"));
    spec.ribbon = { slot(QStringLiteral("help"), QStringLiteral("Program"), 10) };
    commands.add(std::move(spec));

    Ribbon ribbon;
    buildRibbonFrom(&ribbon, commands);

    // An empty tab is a question the user cannot answer.
    QCOMPARE(ribbon.pageKeys(), QStringList{ QStringLiteral("help") });
}

QTEST_MAIN(TestCommands)
#include "test_commands.moc"
