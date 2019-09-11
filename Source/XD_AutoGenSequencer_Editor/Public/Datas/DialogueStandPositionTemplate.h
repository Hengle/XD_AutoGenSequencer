﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Factories/Factory.h"
#include "DialogueStandPositionTemplate.generated.h"

class ACharacter;
class UChildActorComponent;

USTRUCT()
struct FDialogueStandPosition
{
	GENERATED_BODY()
public:
	FDialogueStandPosition() = default;
	FDialogueStandPosition(const FName& StandName, const TSubclassOf<ACharacter>& PreviewCharacter, const FTransform& StandPosition)
		:StandPosition(StandPosition), StandName(StandName), PreviewCharacter(PreviewCharacter)
	{}

	UPROPERTY(EditAnywhere, meta = (MakeEditWidget = true))
	FTransform StandPosition;

	UPROPERTY(EditAnywhere)
	FName StandName;

	UPROPERTY(EditAnywhere)
	TSubclassOf<ACharacter> PreviewCharacter;

	UPROPERTY(Transient)
	UChildActorComponent* PreviewCharacterInstance = nullptr;
};

UCLASS(Transient, abstract, Blueprintable, hidedropdown, hidecategories = (Input, Movement, Collision, Rendering, Replication, Actor, LOD, Cooking), showCategories = ("Utilities|Transformation"))
class XD_AUTOGENSEQUENCER_EDITOR_API ADialogueStandPositionTemplate : public AInfo
{
	GENERATED_BODY()
public:
	ADialogueStandPositionTemplate();

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "站位模板")
	TSubclassOf<ACharacter> PreviewCharacter;

	UPROPERTY(EditAnywhere, Category = "站位模板")
	TArray<FDialogueStandPosition> StandPositions;

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	void OnConstruction(const FTransform& Transform) override;

	UChildActorComponent* CreateChildActorComponent();

	void CreateAllTemplatePreviewCharacter();

	uint8 bSpawnedPreviewCharacter : 1;

	void ClearInvalidPreviewCharacter();

	void ApplyStandPositionsToDefault();

	UPROPERTY(Transient)
	TArray<UChildActorComponent*> PreviewCharacters;

	DECLARE_DELEGATE(FOnInstanceChanged);
	FOnInstanceChanged OnInstanceChanged;

	bool IsInEditorMode() const;
#endif
};

UCLASS()
class XD_AUTOGENSEQUENCER_EDITOR_API UDialogueStandPositionTemplateFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDialogueStandPositionTemplateFactory();

	UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;

	FText GetDisplayName() const override;
	uint32 GetMenuCategories() const override;
};
