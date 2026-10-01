// Copyright (c) 2026 INRAE Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "IrritatorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogIrritator, Log, All);

void UIrritatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogIrritator, Log, TEXT("Irritator subsystem created (world: %s)"), *GetNameSafe(GetWorld()));
}

void UIrritatorSubsystem::Deinitialize()
{
    UE_LOG(LogIrritator, Log, TEXT("Irritator subsystem destroyed"));
    Super::Deinitialize();
}

bool UIrritatorSubsystem::LoadModel(const FString& FilePath)
{
    const FTCHARToUTF8 Utf8(*FilePath);
    const bool bOk = Session.load(std::string_view(
        reinterpret_cast<const char*>(Utf8.Get()), static_cast<size_t>(Utf8.Length())));

    if (!bOk) {
        UE_LOG(LogIrritator, Warning, TEXT("LoadModel failed: %s"), *GetLastError());
    }
    
    return bOk;
}

bool UIrritatorSubsystem::IsModelLoaded() const
{
    return Session.is_loaded();
}

FString UIrritatorSubsystem::GetLastError() const
{
    const std::string_view Msg = Session.last_error();
    const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Msg.data()), static_cast<int32>(Msg.size()));
    return FString(Conv.Length(), Conv.Get());
}