// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FortniteGame.h"
#include "FortAccountItemDefinition.h"
#include "AthenaSeasonItemDefinition.generated.h"

class UMaterialInterace;
class UFortItemDefinition;
//class UFortGiftBoxItemDefinition;

// ----------
// Structures
// ----------

USTRUCT(BlueprintType)
struct FAthenaSeasonBannerLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	class UTexture2D*                   SurroundImage;                                 
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	class UMaterialInterface*           BannerMaterial;                                
};

USTRUCT(BlueprintType)
struct FAthenaSeasonBannerLevelSchedule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<struct FAthenaSeasonBannerLevel>            Levels; 
};

USTRUCT(BlueprintType)
struct FFortGiftBoxFortmatData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString                                     StringAssetType;             
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString                                     StringData;                  
};

USTRUCT(BlueprintType)
struct FChallengeGiftBoxData
{
	GENERATED_BODY()

	//UPROPERTY(EditAnywhere)
//	class UFortGiftBoxItemDefinition   GiftBoxToUse;      
	UPROPERTY(EditAnywhere)
	TArray<struct FFortGiftBoxFortmatData>             GiftBoxFormatData;    
};

USTRUCT(BlueprintType)
struct FAthenaRewardItemReference
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	class UFortItemDefinition*          ItemDefinition;                       
	
	UPROPERTY(EditAnywhere)
	FString                                     TemplateId;                  
	
	UPROPERTY(EditAnywhere)
	int                                                Quantity;             
	
	/* Missed property offset. */
	//uint8                                               Missed0[0x4];      
	
	UPROPERTY(EditAnywhere)
	struct FChallengeGiftBoxData                       RewardGiftBox;        
	
	UPROPERTY(EditAnywhere)
	bool                                               IsChaseReward;        
	
	UPROPERTY(EditAnywhere)
	EAthenaRewardItemType                              RewardType;           
	
	/* Missed property offset. */
	//uint8                                              Missed1[0x6];       
};

USTRUCT(BlueprintType)
struct FAthenaRewardScheduleLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<struct FAthenaRewardItemReference>          Rewards; 
};

USTRUCT(BlueprintType)
struct FAthenaSeasonalXPCurveEntry : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int                                                Level;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int                                                XpToNextLevel;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int                                                XpTotal;

	/* Missed offset */
	//uint8                                              UnknownData00[0x4];                              
};

USTRUCT(BlueprintType)
struct FAthenaRewardSchedule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
		TArray<struct FAthenaRewardScheduleLevel>          Levels;
};

/**
 * 
 */
UCLASS()
class FORTNITEGAME_API UAthenaSeasonItemDefinition : public UFortAccountItemDefinition
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere)
	int                                                SeasonNumber;  

	UPROPERTY(EditAnywhere)
	int                                                NumSeasonLevels;  

	UPROPERTY(EditAnywhere)
	int                                                NumBookLevels;    

	UPROPERTY(EditAnywhere)
	EAthenaSeasonShopVisibility                        SeasonShopVisibility;    

	UPROPERTY(EditAnywhere)
	EAthenaChallengeTabVisibility                      ChallengesVisibility;               

	// Missed Property
//	uint8                                              UnknownData00[0x2];       

	// Seasson XP Curve
	UPROPERTY(EditAnywhere)
	class UDataTable*                                  SeasonXpCurve;                                        
	UPROPERTY(EditAnywhere)
	class UDataTable*                                  BookXpCurve;                                          

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString                                     SeasonStorefront;                                     
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString                                     BattlePassLevelOfferID;                               
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	struct FPrimaryAssetId                             FreeTokenItemPrimaryAssetId;                          
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<int>                                        FreeLevelsThatNavigateToBattlePass;                   
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<int>                                        FreeLevelsThatAutoOpenTheAboutScreen;                 
	
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardSchedule                       SeasonXpScheduleFree;                                 
		
	UPROPERTY(EditAnywhere)
	struct FGameplayTag                                FreeSeasonItemContentTag;                             
		
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardSchedule                       BookXpScheduleFree;                                   
		
	UPROPERTY(EditAnywhere)
	struct FGameplayTag                                BattlePassFreeItemContentTag;                         
		
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardSchedule                       BookXpSchedulePaid;                                   
		
	UPROPERTY(EditAnywhere)
	struct FGameplayTag                                BattlePassPaidItemContentTag;                         
		
	UPROPERTY(EditAnywhere)
	struct FAthenaSeasonBannerLevelSchedule            SeasonBannerSchedule;                                 
		
	//UPROPERTY(EditAnywhere)
	//class UFortChallengeBundleScheduleDefinition*      ChallengeSchedulePaid;                                
		
	//UPROPERTY(EditAnywhere)
//	TArray<class UFortChallengeBundleScheduleDefinition*> ChallengeSchedulesAlwaysShown;                     
		
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardScheduleLevel                  SeasonGrantsToEveryone;                               
		
	UPROPERTY(EditAnywhere)
	struct FGameplayTag                                SeasonGrantsToEveryoneItemContentTag;                 
		
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardScheduleLevel                  SeasonFirstWinRewards;                                
		
	UPROPERTY(EditAnywhere)
	struct FGameplayTag                                SeasonFirstWinItemContentTag;                         
		
	UPROPERTY(EditAnywhere)
	struct FAthenaRewardScheduleLevel                  BattleStarSubstitutionReward;                         
		
	UPROPERTY(EditAnywhere)
	TArray<class UFortItemDefinition*>  ExpiringRewardTypes;                                  
		
	UPROPERTY(EditAnywhere)
	TArray<class UFortItemDefinition*>  TokensToRemoveAtSeasonEnd;                            
		
	// Missed property offset.
	//uint8                                              MissedProperty[0x8];          
};
