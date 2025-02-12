﻿#include "SpudRuntimeStoredActorComponent.h"

#include "SpudSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(SpudRuntimeStoredActorComponent, All, All);

USpudRuntimeStoredActorComponent::USpudRuntimeStoredActorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    bAutoActivate = true;
}

void USpudRuntimeStoredActorComponent::BeginPlay()
{
    Super::BeginPlay();

    const auto SpudSubsystem = GetSpudSubsystem(GetWorld());
    SpudSubsystem->OnLevelStore.AddDynamic(this, &ThisClass::OnLevelStore);
    SpudSubsystem->PostUnloadStreamingLevel.AddDynamic(this, &ThisClass::OnPostUnloadCell);

    if (bCanCrossCell)
    {
        SetComponentTickEnabled(true);
    }
    else
    {
        UpdateCurrentCell();
    }
}

void USpudRuntimeStoredActorComponent::UpdateCurrentCell()
{
    const UWorldPartitionRuntimeCell* OutOverlappedCell = nullptr;
    GetCurrentOverlappedCell(OutOverlappedCell);

    if (OutOverlappedCell)
    {
        CurrentCellName = OutOverlappedCell->GetName();
    }
}

// ReSharper disable once CppMemberFunctionMayBeConst
void USpudRuntimeStoredActorComponent::OnLevelStore(const FString& LevelName)
{
    if (CurrentCellName.IsEmpty() || !IsActive())
    {
        return;
    }

    if (CurrentCellName == LevelName)
    {
        // Always store when cell unloaded
        const auto Actor = GetOwner();
        UE_LOG(SpudRuntimeStoredActorComponent, Log, TEXT("Storing actor in cell: %s"), *CurrentCellName);
        GetSpudSubsystem(GetWorld())->StoreActorByCell(Actor, CurrentCellName);
        // If this is pawn, we also store its controller (AI)
        if (auto Pawn = Cast<APawn>(GetOwner()))
        {
            if (auto Controller = Pawn->GetController())
            {
                UE_LOG(SpudRuntimeStoredActorComponent, Log, TEXT("Storing actor's controller in cell: %s"), *CurrentCellName);
                GetSpudSubsystem(GetWorld())->StoreActorByCell(Controller, CurrentCellName);
            }
        }
    }
}

// ReSharper disable once CppMemberFunctionMayBeConst
void USpudRuntimeStoredActorComponent::OnPostUnloadCell(const FName& LevelName)
{
    if (CurrentCellName.IsEmpty() || !IsActive())
    {
        return;
    }

    if (CurrentCellName == LevelName)
    {
        // If this is pawn, we also destroy its controller (AI)
        if (auto Pawn = Cast<APawn>(GetOwner()))
        {
            if (auto Controller = Pawn->GetController())
            {
                UE_LOG(SpudRuntimeStoredActorComponent, Log, TEXT("Destroying actor's controller in cell: %s"), *CurrentCellName);
                Controller->Destroy();
            }
        }
        
        UE_LOG(SpudRuntimeStoredActorComponent, Log, TEXT("Destroying actor in cell: %s"), *CurrentCellName);
        GetOwner()->Destroy();
    }
}

void USpudRuntimeStoredActorComponent::GetCurrentOverlappedCell(
    const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const
{
    const auto WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!WorldPartitionSubsystem)
    {
        return;
    }

    const auto OwnerLocation = GetOwner()->GetActorLocation();
    auto SmallestCellVolume = 0.f;

    const auto ForEachCellFunction = [this, &CurrentOverlappedCell, &OwnerLocation, &SmallestCellVolume](const UWorldPartitionRuntimeCell* Cell) -> bool
    {
        // for simplicity, assuming actor bounds are small enough that only a single cell needs to be considered
        if (const auto CellBounds = Cell->GetCellBounds(); CellBounds.IsInsideXY(OwnerLocation))
        {
            // use the smallest cell
            if (const auto Volume = CellBounds.GetVolume(); !CurrentOverlappedCell || Volume < SmallestCellVolume)
            {
                // we dont need this log
                //UE_LOG(SpudRuntimeStoredActorComponent, Log, TEXT("GetCurrentOverlappedCell: found cell %s"), *Cell->GetName());

                SmallestCellVolume = Volume;
                CurrentOverlappedCell = Cell;
            }
        }
        return true;
    };

    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    auto ForEachWpFunction = [ForEachCellFunction](UWorldPartition* WorldPartition) -> bool
    {
        if (WorldPartition)
        {
            WorldPartition->RuntimeHash->ForEachStreamingCells(ForEachCellFunction);
        }

        return true;
    };

    WorldPartitionSubsystem->ForEachWorldPartition(ForEachWpFunction);
}

void USpudRuntimeStoredActorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                     FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bCanCrossCell)
    {
        UpdateCurrentCell();
    }
}

void USpudRuntimeStoredActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    const auto SpudSubsystem = GetSpudSubsystem(GetWorld());
    SpudSubsystem->OnLevelStore.RemoveDynamic(this, &ThisClass::OnLevelStore);
    SpudSubsystem->PostUnloadStreamingLevel.RemoveDynamic(this, &ThisClass::OnPostUnloadCell);

    Super::EndPlay(EndPlayReason);
}
