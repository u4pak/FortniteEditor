// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FortHeroSpecialization.generated.h"

class UCustomCharacterPart;

UCLASS()
class FORTNITEGAME_API UFortHeroSpecialization : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere)
	TArray<TSoftObjectPtr<class UCustomCharacterPart>> CharacterParts;
};
