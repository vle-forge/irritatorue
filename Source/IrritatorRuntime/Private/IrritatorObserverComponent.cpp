// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorObserverComponent.h"

#include "IrritatorSubsystem.h"

#include "Engine/World.h"

UIrritatorObserverComponent::UIrritatorObserverComponent()
{
    // The subsystem pushes the events: nothing to do by frame.
    PrimaryComponentTick.bCanEverTick = false;
}

void UIrritatorObserverComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UWorld* World = GetWorld())
        if (UIrritatorSubsystem* Sub = World->GetSubsystem<UIrritatorSubsystem>())
            Sub->RegisterObserver(this);
}

void UIrritatorObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
        if (UIrritatorSubsystem* Sub = World->GetSubsystem<UIrritatorSubsystem>())
            Sub->UnregisterObserver(this);

    Super::EndPlay(EndPlayReason);
}

void UIrritatorObserverComponent::SetOutputName(const FString& Name)
{
    if (Name == OutputName)
        return;

    OutputName = Name;

    if (HasBegunPlay())
        if (UWorld* World = GetWorld())
            if (UIrritatorSubsystem* Sub = World->GetSubsystem<UIrritatorSubsystem>())
                Sub->RegisterObserver(this); // registered again by name
}

void UIrritatorObserverComponent::Receive(double Time, int32 Port, double Value)
{
    ++EventCount;
    LastValue = Value;
    LastTime  = Time;

    OnEvent.Broadcast(Time, Port, Value);
}

void UIrritatorObserverComponent::ResetEvents()
{
    EventCount = 0;
    LastValue  = 0.0;
    LastTime   = 0.0;
}
