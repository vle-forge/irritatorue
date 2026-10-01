// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IrritatorSession.h"
#include "IrritatorSubsystem.generated.h" // always last

UCLASS()
class IRRITATORRUNTIME_API UIrritatorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Irritator")
    bool LoadModel(const FString& FilePath);

    UFUNCTION(BlueprintPure, Category = "Irritator")
    bool IsModelLoaded() const;

    UFUNCTION(BlueprintPure, Category = "Irritator")
    FString GetLastError() const;

private:
    irtue::session Session; // by value: the session's memory is only touched inside IrritatorCore
};