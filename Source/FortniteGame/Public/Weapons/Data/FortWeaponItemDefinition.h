// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Items/FortWorldItemDefinition.h"
#include "FortWeaponItemDefinition.generated.h"

/**
 * 
 */
UCLASS()
class FORTNITEGAME_API UFortWeaponItemDefinition : public UFortWorldItemDefinition
{
	GENERATED_BODY()
	
public:
	/* TODO: Categorize and improve the class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* WeaponActorClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		USkeletalMesh* WeaponMeshOverride;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		struct FDataTableRowHandle WeaponStatHandle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* BaseAlteration;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* BaseCosmeticAlteration;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* PrimaryFireAbility;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* SecondaryFireAbility;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UClass* ReloadAbility;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UFortItemDefinition* AmmoData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		EFortWeaponTriggerType TriggerType;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool bValidForLastEquipped;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		uint8 DisplayTier;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		struct FDataTableCategoryHandle LootLevelData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UTexture2D* ReticleImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UTexture2D* HitNotifyImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UTexture2D* MuzzleBlockedImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UTexture2D* ReticleCenterImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		UTexture2D* ReticleCenterPerfectAimImage;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool bReticleCornerOutsideSpreadRadius;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		TArray<float> ReticleCornerAngles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		TArray<FName> ActualAnalyticFNames;
};
