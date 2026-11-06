// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Subsystems/WorldSubsystem.h"
#include "IrritatorSession.h"

#include <atomic>

#include "IrritatorSubsystem.generated.h" // always last

UENUM(BlueprintType)
enum class EIrritatorState : uint8
{
    Empty,
    Loaded,
    Running,
    Finished,
    Error
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIrritatorRunFinished, bool, bSuccess);

class UIrritatorObserverComponent;

/**
 * The simulation of an irritator project in a world.
 *
 * Two ways to run it:
 * - Play(): each frame simulates DeltaTime * TimeScale units of time, within
 *   FrameBudgetMs milliseconds. If a frame is too short, the simulation is late
 *   and catches up in the next frames.
 * - RunToEnd(): the simulation runs to its end date in a background thread,
 *   then OnSimulationFinished is broadcast in the game thread. During the run
 *   only the getters work.
 *
 * The game acts on the simulation with SendValue(): a constant of the
 * project, marked "configurable" in irritator, sends a new value. The
 * simulation acts on the game with the events of its outputs, the models
 * marked "observable" in irritator: an UIrritatorObserverComponent receives
 * the events of one output in the tick of the world.
 *
 * The components of the projects are searched in Content/Irritator of the
 * project, in the directories added by AddComponentDirectory(), then in the
 * directories of the installation of irritator. Content/Irritator is staged
 * as is (outside the pak files) in the packaged game.
 */
UCLASS()
class IRRITATORRUNTIME_API UIrritatorSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /// Used by the next LoadProject().
    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool AddComponentDirectory(const FString& Directory);

    /// A relative path is read from Content/Irritator.
    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool LoadProject(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    void Play();

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    void Pause();

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool Restart();

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool RunToEnd();

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    void StopRun();

    UFUNCTION(BlueprintPure, Category = "Irritator")
    bool IsPlaying() const { return bPlaying; }

    UFUNCTION(BlueprintPure, Category = "Irritator")
    bool IsRunningInBackground() const;

    UFUNCTION(BlueprintPure, Category = "Irritator")
    EIrritatorState GetState() const;

    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetSimulationTime() const;

    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetBeginTime() const;

    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetEndTime() const;

    /// The names are "<observer>/<model>", as in the project.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    TArray<FString> GetObserverNames() const;

    /// The value of the last observation, 0 if the name is unknown.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetObserverValue(const FString& Name) const;

    /// The constants "configurable" in the project, named as in irritator.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    TArray<FString> GetInputNames() const;

    /**
     * The constant Name sends Value at the current date of the simulation: in
     * the next frame of Play(), or soon during RunToEnd().
     *
     * @return false if the input is unknown, if the simulation is not loaded
     * or finished, or if too many values wait.
     */
    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool SendValue(const FString& Name, double Value);

    /// The models "observable" in the project, named as in irritator.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    TArray<FString> GetOutputNames() const;

    /// The events dropped because too many waited (see SetEventCapacity()).
    UFUNCTION(BlueprintPure, Category = "Irritator")
    int64 GetDroppedEventCount() const;

    /// The events waiting for the next tick, 65536 by default: beyond, the
    /// oldest are dropped. Only a RunToEnd() can produce so many.
    UFUNCTION(BlueprintCallable, Category = "Irritator")
    void SetEventCapacity(int32 Capacity);

    UFUNCTION(BlueprintPure, Category = "Irritator")
    FString GetLastError() const;

    /// Called by the components: the name is resolved by each load.
    void RegisterObserver(UIrritatorObserverComponent* Observer);
    void UnregisterObserver(UIrritatorObserverComponent* Observer);

    /// Broadcasts the events waiting to their components. Called by Tick().
    void DispatchEvents();

    UPROPERTY(BlueprintAssignable, Category = "Irritator")
    FIrritatorRunFinished OnSimulationFinished;

    /// Units of simulated time by second of game.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Irritator")
    double TimeScale = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Irritator")
    float FrameBudgetMs = 2.0f;

private:
    bool RefuseIfRunning(const TCHAR* Command) const;

    /// Resolves the names of the components and watches their outputs.
    void ConnectObservers();
    void ResetObservers();

    irtue::session    Session; // by value: the session's memory is only touched inside IrritatorCore
    TFuture<void>     RunFuture;
    std::atomic<bool> bBackgroundRun{ false };
    double            PlayTarget = 0.0;
    bool              bPlaying   = false;

    TArray<TWeakObjectPtr<UIrritatorObserverComponent>>         Observers;
    TArray<TArray<TWeakObjectPtr<UIrritatorObserverComponent>>> ObserversByOutput;
    TArray<irtue::output_event>                                 EventBuffer;

    /// Grows at each load and restart: the events taken before belong to
    /// the previous run.
    uint64 RunGeneration = 0;
};
