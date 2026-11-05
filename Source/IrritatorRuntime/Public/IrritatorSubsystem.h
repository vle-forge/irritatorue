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
 * The components of the projects are searched in Content/Irritator of the
 * project, in the directories added by AddComponentDirectory(), then in the
 * directories of the installation of irritator.
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

    UFUNCTION(BlueprintPure, Category = "Irritator")
    FString GetLastError() const;

    UPROPERTY(BlueprintAssignable, Category = "Irritator")
    FIrritatorRunFinished OnSimulationFinished;

    /// Units of simulated time by second of game.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Irritator")
    double TimeScale = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Irritator")
    float FrameBudgetMs = 2.0f;

private:
    bool RefuseIfRunning(const TCHAR* Command) const;

    irtue::session    Session; // by value: the session's memory is only touched inside IrritatorCore
    TFuture<void>     RunFuture;
    std::atomic<bool> bBackgroundRun{ false };
    double            PlayTarget = 0.0;
    bool              bPlaying   = false;
};
