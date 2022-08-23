// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Heroes/FortWorkerType.h"
#include "FortHeroType.generated.h"

/**
 * 
 */
UCLASS()
class FORTNITEGAME_API UFortHeroType : public UFortWorkerType
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere)
	uint8                                              bForceShowHeadAccessory : 1;
	UPROPERTY(EditAnywhere)
	uint8                                              bForceShowBackpack : 1;
	UPROPERTY(EditAnywhere)
	TArray<TSoftObjectPtr<class UFortHeroSpecialization>> Specializations;
};
