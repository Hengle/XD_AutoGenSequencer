﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSection.h"
#include "MovieSceneEvalTemplate.h"
#include "DialogueSentenceSection.generated.h"

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
	UPROPERTY(EditAnywhere, Category="Audio")
	FFrameNumber StartFrameOffset;

private:
	static FFrameNumber GetStartOffsetAtTrimTime(FQualifiedFrameTime TrimTime, FFrameNumber StartOffset, FFrameNumber StartFrame);
};

USTRUCT()
struct FDialogueSentenceSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
public:
	FDialogueSentenceSectionTemplate() = default;
	FDialogueSentenceSectionTemplate(const UDialogueSentenceSection& Section)
		:AudioSection(&Section)
	{}

	UPROPERTY()
	const UDialogueSentenceSection* AudioSection;

private:

	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
	virtual void SetupOverrides() override { EnableOverrides(RequiresTearDownFlag); }
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
};