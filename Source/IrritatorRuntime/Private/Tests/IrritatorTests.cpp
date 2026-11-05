// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorSession.h"
#include "IrritatorSubsystem.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#include <chrono>
#include <limits>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS

namespace {

constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

// The fixture: a constant 1 integrated from 0 to 10 (X(t) = t), observed as
// "x/value". The project names the component directory "fixture", unknown
// to the plug-in: the component is found by its sub directory.
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

    TestTrue(TEXT("run to end"), Sub->RunToEnd());

    for (int i = 0; i < 1000 and Sub->IsRunningInBackground(); ++i)
        FPlatformProcess::Sleep(0.01f);

    TestFalse(TEXT("run finished"), Sub->IsRunningInBackground());
    TestTrue(TEXT("finished"), Sub->GetState() == EIrritatorState::Finished);
    TestEqual(TEXT("x at end"), Sub->GetObserverValue(TEXT("x/value")), 8.0 + 3.0 * 6.0, 0.05);

    TestTrue(TEXT("restart"), Sub->Restart());
    TestEqual(TEXT("time after restart"), Sub->GetSimulationTime(), 0.0);

    // The notification of the end of the run is queued in the game thread.
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

    return true;
}

#endif
