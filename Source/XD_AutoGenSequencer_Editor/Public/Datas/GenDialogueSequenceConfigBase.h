﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SoftObjectPtr.h"
#include "SubclassOf.h"
#include "MovieSceneObjectBindingID.h"
#include "GenDialogueSequenceConfigBase.generated.h"

class ISequencer;
class ACharacter;
class UPreviewDialogueSoundSequence;
class UAutoGenDialogueSequence;
class ADialogueStandPositionTemplate;
class UAutoGenDialogueAnimSetBase;

/**
 *
 */
USTRUCT()
struct XD_AUTOGENSEQUENCER_EDITOR_API FDialogueCharacterData
{
	GENERATED_BODY()
public:
	FDialogueCharacterData();

	UPROPERTY(EditAnywhere)
	FName NameOverride;

	UPROPERTY(EditAnywhere)
	TSoftObjectPtr<ACharacter> InstanceOverride;

	UPROPERTY(EditAnywhere)
	TSubclassOf<ACharacter> TypeOverride;
	
	UPROPERTY(EditAnywhere)
 	FTransform PositionOverride;

	UPROPERTY(EditAnywhere)
	FName TalkAnimSlotName = TEXT("DefaultSlot");

	// 对应的角色蓝图中必须存在该命名的Character类型变量
	// TODO：做检查
	UPROPERTY(EditAnywhere)
	FName LookAtTargetPropertyName = TEXT("CineLookAtTarget");

	UPROPERTY(EditAnywhere)
	UAutoGenDialogueAnimSetBase* DialogueAnimSet;
};

// 生成期间的角色数据信息
struct XD_AUTOGENSEQUENCER_EDITOR_API FGenDialogueCharacterData : public FDialogueCharacterData
{
	FGenDialogueCharacterData(const FDialogueCharacterData& DialogueCharacterData)
		:FDialogueCharacterData(DialogueCharacterData)
	{}

	int32 CharacterIdx;
	FMovieSceneObjectBindingID BindingID;
};

USTRUCT()
struct XD_AUTOGENSEQUENCER_EDITOR_API FDialogueStationInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, meta = (DisplayName = "站位模板"))
	TSubclassOf<ADialogueStandPositionTemplate> DialogueStationTemplate;

	UPROPERTY(EditAnywhere, EditFixedSize = true, meta = (DisplayName = "对话角色"))
	TArray<FDialogueCharacterData> DialogueCharacterDatas;
	
	void SyncInstanceData(const ADialogueStandPositionTemplate* Instance);
	TArray<FName> GetCharacterNames() const;

	TArray<TSharedPtr<FString>> DialogueNameList;
	TArray<TSharedPtr<FString>>& GetDialogueNameList();
	void ReinitDialogueNameList();
};

UCLASS(abstract)
class XD_AUTOGENSEQUENCER_EDITOR_API UGenDialogueSequenceConfigBase : public UObject
{
	GENERATED_BODY()
public:
	UGenDialogueSequenceConfigBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	UPreviewDialogueSoundSequence* PreviewDialogueSoundSequence;

public:
	UPROPERTY(EditAnywhere, Category = "站位模板", meta = (ShowOnlyInnerProperties = true))
	FDialogueStationInstance DialogueStation;

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool IsConfigValid() const;
	virtual TSubclassOf<UAutoGenDialogueAnimSetBase> GetAnimSetType() const;
public:
	virtual void GeneratePreview() const {}

	virtual void Generate(TSharedRef<ISequencer> SequencerRef, UWorld* World, const TMap<FName, TSoftObjectPtr<ACharacter>>& CharacterNameInstanceMap, UAutoGenDialogueSequence& AutoGenDialogueSequence) const {}
};