// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorSubsystem.h"

#include "IrritatorObserverComponent.h"

#include "Async/Async.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include <span>
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

FString JournalPath(const FString& FilePath)
{
    return FPaths::IsRelative(FilePath)
             ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Irritator") / FilePath)
             : FilePath;
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

    if (bPlaying and not IsRunningInBackground()) {
        const auto State = Session.state();
        if (State != irtue::session_state::loaded and State != irtue::session_state::running) {
            bPlaying = false;
        } else {
            // An absolute target: a frame without enough budget is caught up
            // by the next ones instead of slowing down the simulation for
            // ever.
            PlayTarget += static_cast<double>(DeltaTime) * TimeScale;

            const auto Budget = std::chrono::microseconds(static_cast<int64>(FrameBudgetMs * 1000.0f));
            if (not Session.advance_until(PlayTarget, Budget)) {
                bPlaying = false;
                UE_LOG(LogIrritator, Warning, TEXT("Simulation step failed: %s"), *GetLastError());
            }
        }
    }

    // Also during RunToEnd(): the background thread fills the events.
    DispatchEvents();
}

void UIrritatorSubsystem::RegisterObserver(UIrritatorObserverComponent* Observer)
{
    if (Observer) {
        Observers.AddUnique(Observer);
        ConnectObservers();
    }
}

void UIrritatorSubsystem::UnregisterObserver(UIrritatorObserverComponent* Observer)
{
    if (Observers.Remove(Observer) > 0) {
        Observer->OutputIndex = -1;
        ConnectObservers();
    }
}

void UIrritatorSubsystem::ConnectObservers()
{
    Observers.RemoveAll([](const auto& Weak) { return not Weak.IsValid(); });

    const int Count = Session.output_count();
    ObserversByOutput.Reset();
    ObserversByOutput.SetNum(Count);

    for (const auto& Weak : Observers) {
        UIrritatorObserverComponent* Observer = Weak.Get();
        const FUtf8View              Name(Observer->OutputName);

        Observer->OutputIndex = Session.find_output(Name.View());
        if (Observer->OutputIndex >= 0)
            ObserversByOutput[Observer->OutputIndex].Add(Weak);
        else if (Count > 0)
            UE_LOG(LogIrritator, Warning, TEXT("Observer of %s: unknown output '%s'"),
                   *GetNameSafe(Observer->GetOwner()), *Observer->OutputName);
    }

    for (int i = 0; i < Count; ++i)
        (void)Session.watch_output(i, not ObserversByOutput[i].IsEmpty());
}

void UIrritatorSubsystem::ResetObservers()
{
    ++RunGeneration;

    for (const auto& Weak : Observers)
        if (UIrritatorObserverComponent* Observer = Weak.Get())
            Observer->ResetEvents();
}

void UIrritatorSubsystem::DispatchEvents()
{
    if (EventBuffer.Num() == 0)
        EventBuffer.SetNumUninitialized(1024);

    const uint64 Generation = RunGeneration;

    for (;;) {
        const std::size_t Count = Session.take_events(
          std::span<irtue::output_event>(EventBuffer.GetData(), static_cast<std::size_t>(EventBuffer.Num())));

        for (std::size_t i = 0; i < Count; ++i) {
            const irtue::output_event& Event = EventBuffer[static_cast<int32>(i)];

            // A listener can load or restart the project, or add and remove
            // components: the lists are read again for each event.
            if (Generation != RunGeneration)
                return;

            if (not ObserversByOutput.IsValidIndex(Event.output))
                continue;

            const auto& List = ObserversByOutput[Event.output];
            for (int32 j = 0; j < List.Num(); ++j)
                if (UIrritatorObserverComponent* Observer = List[j].Get())
                    Observer->Receive(Event.time, Event.port, Event.value);
        }

        if (Count < static_cast<std::size_t>(EventBuffer.Num()))
            return;
    }
}

TArray<FString> UIrritatorSubsystem::GetOutputNames() const
{
    TArray<FString> Names;

    const int Count = Session.output_count();
    Names.Reserve(Count);
    for (int i = 0; i < Count; ++i)
        Names.Add(ToFString(Session.output_name(i)));

    return Names;
}

int64 UIrritatorSubsystem::GetDroppedEventCount() const
{
    return static_cast<int64>(Session.dropped_events());
}

void UIrritatorSubsystem::SetEventCapacity(int32 Capacity)
{
    Session.set_event_capacity(static_cast<std::size_t>(FMath::Max(Capacity, 1)));
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

    ResetObservers();

    if (not Session.load(Path.View())) {
        ConnectObservers();
        UE_LOG(LogIrritator, Warning, TEXT("LoadProject(%s) failed: %s"), *Full, *GetLastError());
        return false;
    }

    ConnectObservers();
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

    ResetObservers();

    if (not Session.restart()) {
        bPlaying = false;
        UE_LOG(LogIrritator, Warning, TEXT("Restart failed: %s"), *GetLastError());
        return false;
    }

    PlayTarget = Session.current_time();
    return true;
}

bool UIrritatorSubsystem::Replay()
{
    if (RefuseIfRunning(TEXT("Replay")))
        return false;

    ResetObservers();
    const bool bOk = Session.replay();
    ConnectObservers();

    if (not bOk) {
        bPlaying = false;
        UE_LOG(LogIrritator, Warning, TEXT("Replay failed: %s"), *GetLastError());
        return false;
    }

    PlayTarget = Session.current_time();
    return true;
}

bool UIrritatorSubsystem::SaveJournal(const FString& FilePath)
{
    if (RefuseIfRunning(TEXT("SaveJournal")))
        return false;

    const FString Full = JournalPath(FilePath);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full), true);

    const FUtf8View Path(Full);
    if (not Session.save_journal(Path.View())) {
        UE_LOG(LogIrritator, Warning, TEXT("SaveJournal(%s) failed: %s"), *Full, *GetLastError());
        return false;
    }

    return true;
}

bool UIrritatorSubsystem::LoadJournal(const FString& FilePath)
{
    if (RefuseIfRunning(TEXT("LoadJournal")))
        return false;

    const FString   Full = JournalPath(FilePath);
    const FUtf8View Path(Full);

    ResetObservers();
    const bool bOk = Session.load_journal(Path.View());
    ConnectObservers();

    if (not bOk) {
        bPlaying = false;
        UE_LOG(LogIrritator, Warning, TEXT("LoadJournal(%s) failed: %s"), *Full, *GetLastError());
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

                // The last events before the end.
                Self->DispatchEvents();
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

TArray<FString> UIrritatorSubsystem::GetInputNames() const
{
    TArray<FString> Names;

    const int Count = Session.input_count();
    Names.Reserve(Count);
    for (int i = 0; i < Count; ++i)
        Names.Add(ToFString(Session.input_name(i)));

    return Names;
}

bool UIrritatorSubsystem::SendValue(const FString& Name, double Value)
{
    const FUtf8View View(Name);
    const int       Index = Session.find_input(View.View());

    if (Index < 0) {
        UE_LOG(LogIrritator, Warning, TEXT("SendValue: unknown input '%s'"), *Name);
        return false;
    }

    if (not Session.send_value(Index, Value)) {
        UE_LOG(LogIrritator, Warning, TEXT("SendValue(%s) refused: the simulation is not running or too many values wait"), *Name);
        return false;
    }

    return true;
}

FString UIrritatorSubsystem::GetLastError() const
{
    // The background thread writes the error.
    if (IsRunningInBackground())
        return FString();

    return ToFString(Session.last_error());
}
