// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "IrritatorObserverComponent.generated.h" // always last

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FIrritatorOutputEvent, double, Time, int32, Port, double, Value);

/**
 * Receives the events of an output of the simulation of the world: a model
 * "observable" in irritator, named by OutputName.
 *
 * An event is a message of the model on its output port Port (a threshold
 * crossed, a generator that emits...) or, for a model without output port (a
 * counter), its new value with Port = -1. The events of a frame are broadcast
 * in the next tick of the world, in the order of the simulation, with their
 * date of simulation.
 *
 * Only the outputs with a component are watched: the others cost nothing to
 * the simulation. The values of the continuous models (an integrator) are
 * read with UIrritatorSubsystem::GetObserverValue() instead.
 */
UCLASS(ClassGroup = (Irritator), meta = (BlueprintSpawnableComponent))
class IRRITATORRUNTIME_API UIrritatorObserverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UIrritatorObserverComponent();

    /// Read by BeginPlay(): change it with SetOutputName() during the game.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Irritator")
    FString OutputName;

    UPROPERTY(BlueprintAssignable, Category = "Irritator")
    FIrritatorOutputEvent OnEvent;

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    void SetOutputName(const FString& Name);

    /// False if no output of the loaded project has this name.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    bool IsConnected() const { return OutputIndex >= 0; }

    /// The value of the last event, 0 before the first one.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetLastValue() const { return LastValue; }

    UFUNCTION(BlueprintPure, Category = "Irritator")
    double GetLastEventTime() const { return LastTime; }

    /// The events received since the load or the restart of the project.
    UFUNCTION(BlueprintPure, Category = "Irritator")
    int32 GetEventCount() const { return EventCount; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class UIrritatorSubsystem;

    void Receive(double Time, int32 Port, double Value);
    void ResetEvents();

    int32  OutputIndex = -1; ///< Set by the subsystem.
    int32  EventCount  = 0;
    double LastValue   = 0.0;
    double LastTime    = 0.0;
};
