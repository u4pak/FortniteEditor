// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "CustomCharacterPartData.generated.h"

UCLASS(BlueprintType)
class UCustomAccessoryAttachmentData : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	struct FVector                                     MaleRelativeScale;       
	
	UPROPERTY(EditAnywhere)
	struct FVector                                     FemaleRelativeScale;     
	
	UPROPERTY(EditAnywhere)
	struct FVector                                     SmallMaleRelativeScale;  
	
	UPROPERTY(EditAnywhere)
	struct FVector                                     SmallFemaleRelativeScale;
	
	UPROPERTY(EditAnywhere)
	struct FVector                                     LargeMaleRelativeScale;  
	
	UPROPERTY(EditAnywhere)
	struct FVector                                     LargeFemaleRelativeScale;
};

UCLASS(DefaultToInstanced, EditInlineNew)
class FORTNITEGAME_API UCustomCharacterPartData : public UObject
{
	GENERATED_BODY()
	
};

UCLASS(BlueprintType)
class FORTNITEGAME_API UCustomCharacterAccessoryData : public UCustomCharacterPartData
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	class UClass*                       AnimClass;

	UPROPERTY(EditAnywhere)
	class UCustomAccessoryAttachmentData*              AttachmentOverrideData;

	UPROPERTY(EditAnywhere)
	FName                                       AttachSocketName = "AttachSocketName";
};

UCLASS(BlueprintType)
class FORTNITEGAME_API UCustomCharacterHatData : public UCustomCharacterAccessoryData
{
	GENERATED_BODY()
};

UCLASS(BlueprintType)
class FORTNITEGAME_API UCustomCharacterBodyPartData : public UCustomCharacterPartData
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	class UClass*                       AnimClass;         
	UPROPERTY(EditAnywhere)
	class UClass*                       FrontEndAnimClass;
	UPROPERTY(EditAnywhere)
	class UClass*                       MannequinAnimClass;
/*	UPROPERTY(EditAnywhere)
	TSoftObjectPtr<class UCustomAccessoryColorSwatch>  AccessoryColors;*/
};

UCLASS(BlueprintType)
class FORTNITEGAME_API UCustomCharacterHeadData : public UCustomCharacterBodyPartData
{
	GENERATED_BODY()
};
