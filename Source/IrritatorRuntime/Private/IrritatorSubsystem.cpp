// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorSubsystem.h"

#include "Async/Async.h"
#include "Misc/Paths.h"

#include <string_view>

DEFINE_LOG_CATEGORY_STATIC(LogIrritator, Log, All);

namespace {

// Keeps the converter alive while the view is used.
struct FUtf8View
{
    explicit FUtf8View(const FString& Str)
      : Conv(*Str)
    {}

    std::string_view View() const
    {
        return std::string_view(reinterpret_cast<const char*>(Conv.Get()),
                                static_cast<size_t>(Conv.Length()));
    }

    FTCHARToUTF8 Conv;
};

FString ToFString(const std::string_view Str)
{
    const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Str.data()),
                            static_cast<int32>(Str.size()));
    return FString(Conv.Length(), Conv.Get());
}

FString ContentDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Irritator"));
}

} // namespace

void UIrritatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Added even if it does not exist yet: the directories are checked by
    // each load.
    (void)AddComponentDirectory(ContentDirectory());
}

void UIrritatorSubsystem::Deinitialize()
{
    bPlaying = false;

    if (RunFuture.IsValid()) {
        Session.request_stop();
        RunFuture.Wait();
    }

    Super::Deinitialize();
}

TStatId UIrritatorSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UIrritatorSubsystem, STATGROUP_Tickables);
}

void UIrritatorSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (not bPlaying or IsRunningInBackground())
        return;

    const auto State = Session.state();
    if (State != irtue::session_state::loaded and State != irtue::session_state::running) {
        bPlaying = false;
        return;
    }

    // An absolute target: a frame without enough budget is caught up by the
    // next ones instead of slowing down the simulation for ever.
    PlayTarget += static_cast<double>(DeltaTime) * TimeScale;

    const auto Budget = std::chrono::microseconds(static_cast<int64>(FrameBudgetMs * 1000.0f));
    if (not Session.advance_until(PlayTarget, Budget)) {
        bPlaying = false;
        UE_LOG(LogIrritator, Warning, TEXT("Simulation step failed: %s"), *GetLastError());
    }
}

bool UIrritatorSubsystem::RefuseIfRunning(const TCHAR* Command) const
{
    if (IsRunningInBackground()) {
        UE_LOG(LogIrritator, Warning, TEXT("%s refused: the simulation runs in background"), Command);
        return true;
    }

    return false;
}

bool UIrritatorSubsystem::AddComponentDirectory(const FString& Directory)
{
    if (RefuseIfRunning(TEXT("AddComponentDirectory")))
        return false;

    const FUtf8View Path(FPaths::ConvertRelativePathToFull(Directory));
    return Session.add_component_dir("Unreal project", Path.View());
}

bool UIrritatorSubsystem::LoadProject(const FString& FilePath)
{
    if (RefuseIfRunning(TEXT("LoadProject")))
        return false;

    bPlaying = false;

    const FString Full = FPaths::IsRelative(FilePath) ? ContentDirectory() / FilePath : FilePath;
    const FUtf8View Path(Full);

    if (not Session.load(Path.View())) {
        UE_LOG(LogIrritator, Warning, TEXT("LoadProject(%s) failed: %s"), *Full, *GetLastError());
        return false;
    }

    PlayTarget = Session.current_time();
    return true;
}

void UIrritatorSubsystem::Play()
{
    if (RefuseIfRunning(TEXT("Play")))
        return;

    PlayTarget = Session.current_time();
    bPlaying   = true;
}

void UIrritatorSubsystem::Pause()
{
    bPlaying = false;
}

bool UIrritatorSubsystem::Restart()
{
    if (RefuseIfRunning(TEXT("Restart")))
        return false;

    if (not Session.restart()) {
        bPlaying = false;
        UE_LOG(LogIrritator, Warning, TEXT("Restart failed: %s"), *GetLastError());
        return false;
    }

    PlayTarget = Session.current_time();
    return true;
}

bool UIrritatorSubsystem::RunToEnd()
{
    if (RefuseIfRunning(TEXT("RunToEnd")))
        return false;

    if (Session.state() == irtue::session_state::empty) {
        UE_LOG(LogIrritator, Warning, TEXT("RunToEnd refused: no project loaded"));
        return false;
    }

    bPlaying = false;
    bBackgroundRun = true;

    // `this` outlives the thread: Deinitialize() waits for it.
    TWeakObjectPtr<UIrritatorSubsystem> Weak(this);
    RunFuture = Async(EAsyncExecution::Thread, [this, Weak]() {
        const bool    bOk   = Session.run_to_end();
        const FString Error = bOk ? FString() : ToFString(Session.last_error());

        // Lowered before the broadcast: a listener can restart the simulation.
        bBackgroundRun = false;

        AsyncTask(ENamedThreads::GameThread, [Weak, bOk, Error]() {
            if (UIrritatorSubsystem* Self = Weak.Get()) {
                if (not bOk)
                    UE_LOG(LogIrritator, Warning, TEXT("RunToEnd failed: %s"), *Error);
                Self->OnSimulationFinished.Broadcast(bOk);
            }
        });
    });

    return true;
}

void UIrritatorSubsystem::StopRun()
{
    if (IsRunningInBackground())
        Session.request_stop();
}

bool UIrritatorSubsystem::IsRunningInBackground() const
{
    return bBackgroundRun.load();
}

EIrritatorState UIrritatorSubsystem::GetState() const
{
    switch (Session.state()) {
    case irtue::session_state::empty:
        return EIrritatorState::Empty;
    case irtue::session_state::loaded:
        return EIrritatorState::Loaded;
    case irtue::session_state::running:
        return EIrritatorState::Running;
    case irtue::session_state::finished:
        return EIrritatorState::Finished;
    case irtue::session_state::error:
        return EIrritatorState::Error;
    }

    return EIrritatorState::Error;
}

double UIrritatorSubsystem::GetSimulationTime() const
{
    return Session.current_time();
}

double UIrritatorSubsystem::GetBeginTime() const
{
    return Session.begin_time();
}

double UIrritatorSubsystem::GetEndTime() const
{
    return Session.end_time();
}

TArray<FString> UIrritatorSubsystem::GetObserverNames() const
{
    TArray<FString> Names;

    const int Count = Session.observer_count();
    Names.Reserve(Count);
    for (int i = 0; i < Count; ++i)
        Names.Add(ToFString(Session.observer_name(i)));

    return Names;
}

double UIrritatorSubsystem::GetObserverValue(const FString& Name) const
{
    const FUtf8View View(Name);
    return Session.observer_value(Session.find_observer(View.View()));
}

FString UIrritatorSubsystem::GetLastError() const
{
    // The background thread writes the error.
    if (IsRunningInBackground())
        return FString();

    return ToFString(Session.last_error());
}
