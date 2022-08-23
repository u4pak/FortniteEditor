// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Items/FortItemDefinition.h"
#include "Sound/SoundBase.h"
#include "Engine/SkeletalMesh.h"
#include "FortWorldItemDefinition.generated.h"

/**
 * 
 */
UCLASS()
class FORTNITEGAME_API UFortWorldItemDefinition : public UFortItemDefinition
{
	GENERATED_BODY()
	
public:

	/** Preferred quickbar slot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		int                                                PreferredQuickbarSlot;


	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                     bSupportsQuickbarFocus;                               // 0x0304(0x0001) (Edit, BlueprintVisible, BlueprintReadOnly)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                                    bSupportsQuickbarFocusForGamepadOnly;                 // 0x0304(0x0001) (Edit, BlueprintVisible, BlueprintReadOnly)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                                   bShouldActivateWhenFocused;                           // 0x0304(0x0001) (Edit, BlueprintVisible, BlueprintReadOnly)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                                  bForceFocusWhenAdded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool bShowDirectionalArrowWhenFarOff = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                  bItemCanBeStolen = true;

	/** Generally gets rid of the durability in this item, removes the durability bar in the quickbar slot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		bool                                                  bItemHasDurability = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		USkeletalMesh*                                   PickupSkeletalMesh;


	/** Sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		USoundBase*                                   PickupSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		USoundBase*                                   DropSound;
};
