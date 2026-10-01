#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IrritatorBlueprintLibrary.generated.h"   // toujours en dernier

UCLASS()
class IRRITATORRUNTIME_API UIrritatorBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Irritator")
    static FString GetIrritatorVersion();
};
