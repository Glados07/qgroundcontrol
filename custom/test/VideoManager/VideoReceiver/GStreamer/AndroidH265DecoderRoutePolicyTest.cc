/****************************************************************************
 *
 * Android H.265 hardware route policy tests.
 *
 ****************************************************************************/

#include "AndroidH265DecoderRoutePolicy.h"

#include <QtTest/QTest>

class AndroidH265DecoderRoutePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void adaptersPrecedeDirectFactories();
    void everyRouteIsSelectedOnce();
    void emptyRouteListIsImmediatelyExhausted();
    void factoryIdentityRepairsAStaleIndex();
    void receiverStatesRemainIndependent();
};

void AndroidH265DecoderRoutePolicyTest::adaptersPrecedeDirectFactories()
{
    const QStringList factories =
        AndroidH265DecoderRoutePolicy::orderedRetryFactories(
            {QStringLiteral("adapter-alt1"),
             QString(),
             QStringLiteral("adapter-alt2"),
             QStringLiteral("adapter-alt1")},
            {QStringLiteral("direct-a"),
             QStringLiteral("adapter-alt2"),
             QStringLiteral("direct-b"),
             QStringLiteral("direct-a")});

    QCOMPARE(factories,
             QStringList({QStringLiteral("adapter-alt1"),
                          QStringLiteral("adapter-alt2"),
                          QStringLiteral("direct-a"),
                          QStringLiteral("direct-b")}));
}

void AndroidH265DecoderRoutePolicyTest::everyRouteIsSelectedOnce()
{
    const QStringList factories = {
        QStringLiteral("adapter-alt1"),
        QStringLiteral("direct-a"),
        QStringLiteral("direct-b"),
    };

    QString previousFactory;
    int previousIndex = -1;
    for (int expectedIndex = 0;
         expectedIndex < factories.size();
         ++expectedIndex) {
        const auto selection = AndroidH265DecoderRoutePolicy::nextRoute(
            factories, previousFactory, previousIndex);
        QVERIFY(!selection.exhausted);
        QCOMPARE(selection.candidateIndex, expectedIndex);
        QCOMPARE(selection.factoryName, factories.at(expectedIndex));
        previousFactory = selection.factoryName;
        previousIndex = selection.candidateIndex;
    }

    const auto finalSelection = AndroidH265DecoderRoutePolicy::nextRoute(
        factories, previousFactory, previousIndex);
    QVERIFY(finalSelection.exhausted);
    QVERIFY(finalSelection.factoryName.isEmpty());
    QCOMPARE(finalSelection.candidateIndex, factories.size());

    const auto stableFinalSelection =
        AndroidH265DecoderRoutePolicy::nextRoute(
            factories,
            QString(),
            finalSelection.candidateIndex);
    QVERIFY(stableFinalSelection.exhausted);
    QVERIFY(stableFinalSelection.factoryName.isEmpty());
    QCOMPARE(stableFinalSelection.candidateIndex, factories.size());
}

void AndroidH265DecoderRoutePolicyTest::emptyRouteListIsImmediatelyExhausted()
{
    const auto selection = AndroidH265DecoderRoutePolicy::nextRoute(
        {}, QString(), -1);
    QVERIFY(selection.exhausted);
    QVERIFY(selection.factoryName.isEmpty());
    QCOMPARE(selection.candidateIndex, 0);
}

void AndroidH265DecoderRoutePolicyTest::factoryIdentityRepairsAStaleIndex()
{
    const QStringList factories = {
        QStringLiteral("adapter-alt1"),
        QStringLiteral("adapter-alt2"),
        QStringLiteral("direct-a"),
    };

    const auto selection = AndroidH265DecoderRoutePolicy::nextRoute(
        factories, QStringLiteral("adapter-alt2"), -1);
    QVERIFY(!selection.exhausted);
    QCOMPARE(selection.factoryName, QStringLiteral("direct-a"));
    QCOMPARE(selection.candidateIndex, 2);
}

void AndroidH265DecoderRoutePolicyTest::receiverStatesRemainIndependent()
{
    const QStringList factories = {
        QStringLiteral("adapter-alt1"),
        QStringLiteral("direct-a"),
    };

    const auto mt11First = AndroidH265DecoderRoutePolicy::nextRoute(
        factories, QString(), -1);
    const auto mt11Second = AndroidH265DecoderRoutePolicy::nextRoute(
        factories, mt11First.factoryName, mt11First.candidateIndex);
    const auto a8First = AndroidH265DecoderRoutePolicy::nextRoute(
        factories, QString(), -1);

    QCOMPARE(mt11Second.factoryName, QStringLiteral("direct-a"));
    QCOMPARE(mt11Second.candidateIndex, 1);
    QCOMPARE(a8First.factoryName, QStringLiteral("adapter-alt1"));
    QCOMPARE(a8First.candidateIndex, 0);
}

QTEST_APPLESS_MAIN(AndroidH265DecoderRoutePolicyTest)

#include "AndroidH265DecoderRoutePolicyTest.moc"
