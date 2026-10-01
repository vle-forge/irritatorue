#include "IrritatorBlueprintLibrary.h"
#include "IrritatorCore.h"

FString UIrritatorBlueprintLibrary::GetIrritatorVersion()
{
    const irtue::version v = irtue::get_version();
    return FString::Printf(TEXT("%d.%d.%d"), v.major, v.minor, v.patch);
}
