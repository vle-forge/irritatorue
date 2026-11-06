// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorObserverComponent.h"
#include "IrritatorSession.h"
#include "IrritatorSubsystem.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#include <array>
#include <chrono>
#include <limits>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS

namespace {

constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

// The fixture: a constant 1 integrated from 0 to 10 (X(t) = t), observed as
// "x/value". A cross of the threshold 5 on X sends -1 on its port "down" (1)
// then 1 on its port "up" (0) to a counter "hits": x, cross and hits are the
// outputs. The project names the component directory "fixture", unknown to
// the plug-in: the component is found by its sub directory.
FString FixtureDirectory()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("IrritatorUE"));
    return Plugin ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources/Tests")) : FString();
}

FString FixtureProject()
{
    return FixtureDirectory() / TEXT("irtue-test/integrator.pirt");
}

std::string ToUtf8(const FString& Str)
{
    const FTCHARToUTF8 Conv(*Str);
    return std::string(reinterpret_cast<const char*>(Conv.Get()), static_cast<size_t>(Conv.Length()));
}

bool LoadFixture(FAutomationTestBase& Test, irtue::session& Session)
{
    Test.TestTrue(TEXT("component directory"),
                  Session.add_component_dir("tests", ToUtf8(FixtureDirectory())));

    const bool bOk = Session.load(ToUtf8(FixtureProject()));
    if (not bOk)
        Test.AddError(FString::Printf(TEXT("load: %s"), UTF8_TO_TCHAR(std::string(Session.last_error()).c_str())));

    return bOk;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSessionLoadTest, "Irritator.Session.Load", TestFlags)

bool FIrritatorSessionLoadTest::RunTest(const FString& Parameters)
{
    {
        irtue::session Session;
        if (not LoadFixture(*this, Session))
            return false;

        TestTrue(TEXT("loaded"), Session.state() == irtue::session_state::loaded);
        TestEqual(TEXT("begin"), Session.begin_time(), 0.0);
        TestEqual(TEXT("end"), Session.end_time(), 10.0);
        TestEqual(TEXT("observers"), Session.observer_count(), 1);
        TestEqual(TEXT("observer index"), Session.find_observer("x/value"), 0);
        TestEqual(TEXT("unknown observer"), Session.find_observer("y"), -1);
        TestEqual(TEXT("inputs"), Session.input_count(), 1);
        TestEqual(TEXT("input index"), Session.find_input("one"), 0);
        TestEqual(TEXT("unknown input"), Session.find_input("x"), -1);
    }

    {
        irtue::session Session;
        TestFalse(TEXT("missing file"), Session.load(ToUtf8(FixtureDirectory() / TEXT("missing.pirt"))));
        TestFalse(TEXT("error message"), Session.last_error().empty());
        TestTrue(TEXT("empty"), Session.state() == irtue::session_state::empty);
    }

    {
        // Without the component directory, the error names the component.
        irtue::session Session;
        TestFalse(TEXT("missing component"), Session.load(ToUtf8(FixtureProject())));
        TestTrue(TEXT("component in the error"),
                 Session.last_error().find("integrator.irt") != std::string_view::npos);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSessionRunTest, "Irritator.Session.Run", TestFlags)

bool FIrritatorSessionRunTest::RunTest(const FString& Parameters)
{
    using namespace std::chrono_literals;

    irtue::session Session;
    if (not LoadFixture(*this, Session))
        return false;

    for (int i = 1; i <= 10; ++i)
        TestTrue(TEXT("advance"), Session.advance_until(0.5 * i, 1s));

    TestEqual(TEXT("time at 5"), Session.current_time(), 5.0);
    TestEqual(TEXT("x at 5"), Session.observer_value(0), 5.0, 0.02);
    TestTrue(TEXT("running"), Session.state() == irtue::session_state::running);

    TestTrue(TEXT("run to end"), Session.run_to_end());
    TestTrue(TEXT("finished"), Session.state() == irtue::session_state::finished);
    TestEqual(TEXT("time at end"), Session.current_time(), 10.0);
    TestEqual(TEXT("x at end"), Session.observer_value(0), 10.0, 0.02);

    TestTrue(TEXT("restart"), Session.restart());
    TestTrue(TEXT("loaded again"), Session.state() == irtue::session_state::loaded);
    TestEqual(TEXT("time after restart"), Session.current_time(), 0.0);

    // A stop requested before the run stops it at once.
    Session.request_stop();
    TestTrue(TEXT("stopped run"), Session.run_to_end());
    TestEqual(TEXT("time after stop"), Session.current_time(), 0.0);

    TestTrue(TEXT("run after stop"), Session.run_to_end());
    TestTrue(TEXT("finished after stop"), Session.state() == irtue::session_state::finished);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSessionInputTest, "Irritator.Session.Input", TestFlags)

bool FIrritatorSessionInputTest::RunTest(const FString& Parameters)
{
    using namespace std::chrono_literals;

    irtue::session Session;
    if (not LoadFixture(*this, Session))
        return false;

    const int One = Session.find_input("one");

    // X' = 1 to the date 5, then X' = 2: X(10) = 5 + 2 * 5.
    TestTrue(TEXT("advance to 5"), Session.advance_until(5.0, 1s));
    TestTrue(TEXT("send"), Session.send_value(One, 2.0));
    TestTrue(TEXT("advance to 10"), Session.advance_until(10.0, 1s));
    TestEqual(TEXT("x with the new slope"), Session.observer_value(0), 15.0, 0.05);
    TestFalse(TEXT("send after the end"), Session.send_value(One, 1.0));

    // A value sent before a run is applied at its start.
    TestTrue(TEXT("restart"), Session.restart());
    TestTrue(TEXT("send before the run"), Session.send_value(One, 0.5));
    TestTrue(TEXT("run"), Session.run_to_end());
    TestEqual(TEXT("x with a slope of 0.5"), Session.observer_value(0), 5.0, 0.05);

    TestFalse(TEXT("unknown input"), Session.send_value(-1, 1.0));

    // A value that the kernel refuses is reported, the simulation goes on.
    TestTrue(TEXT("restart again"), Session.restart());
    TestTrue(TEXT("send NaN"), Session.send_value(One, std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("NaN refused"), Session.advance_until(1.0, 1s));
    TestFalse(TEXT("NaN error"), Session.last_error().empty());
    TestTrue(TEXT("goes on"), Session.state() == irtue::session_state::running);
    TestTrue(TEXT("advance after the error"), Session.advance_until(2.0, 1s));
    TestEqual(TEXT("x with the first slope"), Session.observer_value(0), 2.0, 0.05);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSessionJournalTest, "Irritator.Session.Journal", TestFlags)

bool FIrritatorSessionJournalTest::RunTest(const FString& Parameters)
{
    using namespace std::chrono_literals;

    irtue::session Session;
    if (not LoadFixture(*this, Session))
        return false;

    const int One = Session.find_input("one");

    // X' = 1 to the date 5, then X' = 2: X(10) = 15.
    TestTrue(TEXT("advance to 5"), Session.advance_until(5.0, 1s));
    TestTrue(TEXT("send"), Session.send_value(One, 2.0));
    TestTrue(TEXT("advance to 10"), Session.advance_until(10.0, 1s));
    TestEqual(TEXT("game"), Session.observer_value(0), 15.0, 0.05);

    // The replay sends 2 again at the date 5, not before.
    TestTrue(TEXT("replay"), Session.replay());
    TestTrue(TEXT("loaded"), Session.state() == irtue::session_state::loaded);
    TestEqual(TEXT("time of the replay"), Session.current_time(), 0.0);

    bool bInTime = true;
    for (int i = 1; i <= 20; ++i) {
        TestTrue(TEXT("advance the replay"), Session.advance_until(0.5 * i, 1s));
        bInTime = bInTime and Session.current_time() <= 0.5 * i + 1e-9;
    }
    TestTrue(TEXT("the clock follows the targets"), bInTime);
    TestEqual(TEXT("replayed"), Session.observer_value(0), 15.0, 0.05);

    // The journal in a file, replayed by another session.
    const FString Journal = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("irtue-journal-test.irj"));
    TestTrue(TEXT("save"), Session.save_journal(ToUtf8(Journal)));

    {
        irtue::session Other;
        if (LoadFixture(*this, Other)) {
            TestTrue(TEXT("load the journal"), Other.load_journal(ToUtf8(Journal)));
            TestTrue(TEXT("advance the other"), Other.advance_until(10.0, 1s));
            TestEqual(TEXT("replayed from the file"), Other.observer_value(0), 15.0, 0.05);
        }

        TestFalse(TEXT("missing journal"), Other.load_journal(ToUtf8(Journal + TEXT(".missing"))));
    }

    // A new game forgets the journal.
    TestTrue(TEXT("restart"), Session.restart());
    TestTrue(TEXT("advance the new game"), Session.advance_until(10.0, 1s));
    TestEqual(TEXT("new game"), Session.observer_value(0), 10.0, 0.05);

    IFileManager::Get().Delete(*Journal);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSessionOutputTest, "Irritator.Session.Output", TestFlags)

bool FIrritatorSessionOutputTest::RunTest(const FString& Parameters)
{
    using namespace std::chrono_literals;

    irtue::session Session;
    if (not LoadFixture(*this, Session))
        return false;

    TestEqual(TEXT("outputs"), Session.output_count(), 3);
    const int X     = Session.find_output("x");
    const int Cross = Session.find_output("cross");
    const int Hits  = Session.find_output("hits");
    TestTrue(TEXT("output names"), X >= 0 and Cross >= 0 and Hits >= 0);
    TestEqual(TEXT("unknown output"), Session.find_output("one"), -1);
    TestFalse(TEXT("watch an unknown output"), Session.watch_output(99, true));

    std::array<irtue::output_event, 16> Events{};

    // Only the watched outputs: the 1000 quanta of x are not sent.
    TestTrue(TEXT("watch cross"), Session.watch_output(Cross, true));
    TestTrue(TEXT("watch hits"), Session.watch_output(Hits, true));
    TestTrue(TEXT("advance to 10"), Session.advance_until(10.0, 1s));

    const auto N = Session.take_events(Events);
    if (not TestEqual(TEXT("events"), static_cast<int>(N), 3))
        return false;

    TestEqual(TEXT("down: output"), Events[0].output, Cross);
    TestEqual(TEXT("down: port"), Events[0].port, 1);
    TestEqual(TEXT("down: value"), Events[0].value, -1.0);
    TestEqual(TEXT("down: time"), Events[0].time, 0.0, 0.02);

    TestEqual(TEXT("up: output"), Events[1].output, Cross);
    TestEqual(TEXT("up: port"), Events[1].port, 0);
    TestEqual(TEXT("up: value"), Events[1].value, 1.0);
    TestEqual(TEXT("up: time"), Events[1].time, 5.0, 0.02);

    TestEqual(TEXT("hits: output"), Events[2].output, Hits);
    TestEqual(TEXT("hits: port"), Events[2].port, -1);
    TestEqual(TEXT("hits: value"), Events[2].value, 1.0);
    TestEqual(TEXT("hits: time"), Events[2].time, Events[1].time);

    TestEqual(TEXT("taken"), static_cast<int>(Session.take_events(Events)), 0);

    // Beyond the capacity the oldest events are dropped.
    TestTrue(TEXT("restart"), Session.restart());
    TestTrue(TEXT("unwatch cross"), Session.watch_output(Cross, false));
    TestTrue(TEXT("unwatch hits"), Session.watch_output(Hits, false));
    TestTrue(TEXT("watch x"), Session.watch_output(X, true));
    Session.set_event_capacity(4);
    TestTrue(TEXT("advance x to 10"), Session.advance_until(10.0, 1s));

    TestEqual(TEXT("events of x"), static_cast<int>(Session.take_events(Events)), 4);
    TestTrue(TEXT("dropped"), Session.dropped_events() > 0);
    TestTrue(TEXT("the last events"), Events[0].time > 9.9 and Events[3].output == X);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIrritatorSubsystemTest, "Irritator.Subsystem", TestFlags)

bool FIrritatorSubsystemTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper Wrapper;
    if (not Wrapper.CreateTestWorld(EWorldType::Game)) {
        AddError(TEXT("no test world"));
        return false;
    }

    UIrritatorSubsystem* Sub = Wrapper.GetTestWorld()->GetSubsystem<UIrritatorSubsystem>();
    if (not TestNotNull(TEXT("subsystem"), Sub))
        return false;

    TestTrue(TEXT("component directory"), Sub->AddComponentDirectory(FixtureDirectory()));
    if (not TestTrue(TEXT("load"), Sub->LoadProject(FixtureProject())))
        return false;

    TestTrue(TEXT("observer names"), Sub->GetObserverNames() == TArray<FString>{ TEXT("x/value") });
    TestEqual(TEXT("output names"), Sub->GetOutputNames().Num(), 3);

    // Outside an actor: registered by hand.
    UIrritatorObserverComponent* Cross = NewObject<UIrritatorObserverComponent>();
    UIrritatorObserverComponent* Nope  = NewObject<UIrritatorObserverComponent>();
    Cross->OutputName = TEXT("cross");
    Nope->OutputName  = TEXT("nope");
    Sub->RegisterObserver(Cross);
    Sub->RegisterObserver(Nope);
    TestTrue(TEXT("connected"), Cross->IsConnected());
    TestFalse(TEXT("unknown output"), Nope->IsConnected());

    // The world is not ticked: the frames are simulated by hand.
    Sub->TimeScale = 2.0;
    Sub->Play();
    for (int i = 0; i < 10; ++i)
        Sub->Tick(0.1f);

    TestEqual(TEXT("time after 1 s at scale 2"), Sub->GetSimulationTime(), 2.0, 1e-6);
    TestEqual(TEXT("x after 1 s"), Sub->GetObserverValue(TEXT("x/value")), 2.0, 0.02);

    Sub->Pause();
    Sub->Tick(0.1f);
    TestEqual(TEXT("paused"), Sub->GetSimulationTime(), 2.0, 1e-6);

    TestTrue(TEXT("input names"), Sub->GetInputNames() == TArray<FString>{ TEXT("one") });
    TestFalse(TEXT("unknown input"), Sub->SendValue(TEXT("nope"), 1.0));

    // X' = 3 from the date 2: X(4) = 2 + 3 * 2.
    TestTrue(TEXT("send value"), Sub->SendValue(TEXT("one"), 3.0));
    Sub->Play();
    for (int i = 0; i < 10; ++i)
        Sub->Tick(0.1f);
    Sub->Pause();
    TestEqual(TEXT("time after 1 more s"), Sub->GetSimulationTime(), 4.0, 1e-6);
    TestEqual(TEXT("x with the new slope"), Sub->GetObserverValue(TEXT("x/value")), 8.0, 0.05);

    // X = 2 + 3 (t - 2) crosses 5 at t = 3: down at 0, then up.
    TestEqual(TEXT("cross events"), Cross->GetEventCount(), 2);
    TestEqual(TEXT("cross up"), Cross->GetLastValue(), 1.0);
    TestEqual(TEXT("cross time"), Cross->GetLastEventTime(), 3.0, 0.02);

    TestTrue(TEXT("run to end"), Sub->RunToEnd());

    for (int i = 0; i < 1000 and Sub->IsRunningInBackground(); ++i)
        FPlatformProcess::Sleep(0.01f);

    TestFalse(TEXT("run finished"), Sub->IsRunningInBackground());
    TestTrue(TEXT("finished"), Sub->GetState() == EIrritatorState::Finished);
    TestEqual(TEXT("x at end"), Sub->GetObserverValue(TEXT("x/value")), 8.0 + 3.0 * 6.0, 0.05);

    // The notification of the end of the run is queued in the game thread.
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    TestEqual(TEXT("no more cross events"), Cross->GetEventCount(), 2);

    TestTrue(TEXT("restart"), Sub->Restart());
    TestEqual(TEXT("time after restart"), Sub->GetSimulationTime(), 0.0);
    TestEqual(TEXT("events reset"), Cross->GetEventCount(), 0);

    Sub->UnregisterObserver(Cross);
    Sub->UnregisterObserver(Nope);
    TestFalse(TEXT("unregistered"), Cross->IsConnected());

    return true;
}

#endif
