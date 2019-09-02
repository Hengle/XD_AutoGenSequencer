﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSection.h"
#include "MovieSceneEvalTemplate.h"
#include "DialogueSentenceSection.generated.h"

class USoundBase;
class UDialogueSentence;
class UAudioComponent;

/**
 * 
 */
UCLASS()
class XD_AUTOGENSEQUENCER_API UDialogueSentenceSection : public UMovieSceneSection
{
	GENERATED_BODY()
public:
	//~ UMovieSceneSection interface
	TOptional<TRange<FFrameNumber> > GetAutoSizeRange() const override;
	void TrimSection(FQualifiedFrameTime TrimTime, bool bTrimLeft) override;
	UMovieSceneSection* SplitSection(FQualifiedFrameTime SplitTime) override;
	TOptional<FFrameTime> GetOffsetTime() const override;
	FMovieSceneEvalTemplatePtr GenerateTemplate() const override;

public:
	/** The offset into the beginning of the audio clip */
	UPROPERTY(EditAnywhere, Category = Dialogue)
	FFrameNumber StartFrameOffset;

	UPROPERTY(EditAnywhere, Category = Dialogue)
	UDialogueSentence* DialogueSentence;

	UPROPERTY(EditAnywhere, Category = Dialogue)
	TArray<FMovieSceneObjectBindingID> Targets;

	USoundBase* GetDefualtSentenceSound() const;
private:
	static FFrameNumber GetStartOffsetAtTrimTime(FQualifiedFrameTime TrimTime, FFrameNumber StartOffset, FFrameNumber StartFrame);
};

USTRUCT()
struct XD_AUTOGENSEQUENCER_API FDialogueSentenceSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FDialogueSentenceSectionTemplate() = default;
	FDialogueSentenceSectionTemplate(const UDialogueSentenceSection& Section)
		:SentenceSection(&Section)
	{}

	UPROPERTY()
	const UDialogueSentenceSection* SentenceSection;

private:
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
	virtual void SetupOverrides() override { EnableOverrides(RequiresTearDownFlag); }
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
};

struct XD_AUTOGENSEQUENCER_API FDialogueSentenceSectionExecutionToken : IMovieSceneExecutionToken
{
	FDialogueSentenceSectionExecutionToken(const UDialogueSentenceSection* InSentenceSection);

	void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override;

	void EnsureSentenceIsPlaying(USoundBase* SentenceSound, UAudioComponent& AudioComponent, FPersistentEvaluationData& PersistentData, const FMovieSceneContext& Context, bool bAllowSpatialization, IMovieScenePlayer& Player) const;

	const UDialogueSentenceSection* SentenceSection;
	FObjectKey SectionKey;
	TMap<AActor*, USoundBase*> SentenceSoundMap;
};
