// Fill out your copyright notice in the Description page of Project Settings.


#include "DialogueSentenceTrack.h"
#include "DialogueSentenceSection.h"
#include "MovieSceneCompilerRules.h"

#define LOCTEXT_NAMESPACE "DialogueSentenceTrack"

UDialogueSentenceTrack::UDialogueSentenceTrack(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SetDisplayName(LOCTEXT("DialogueSentenceTrackName", "Dialogue Sentence Track"));
#endif
}

bool UDialogueSentenceTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UDialogueSentenceSection::StaticClass();
}

void UDialogueSentenceTrack::RemoveAllAnimationData()
{
	SentenceSections.Empty();
}

bool UDialogueSentenceTrack::HasSection(const UMovieSceneSection& Section) const
{
	return SentenceSections.Contains(&Section);
}

void UDialogueSentenceTrack::AddSection(UMovieSceneSection& Section)
{
	SentenceSections.Add(&Section);
}

void UDialogueSentenceTrack::RemoveSection(UMovieSceneSection& Section)
{
	SentenceSections.Remove(&Section);
}

void UDialogueSentenceTrack::RemoveSectionAt(int32 SectionIndex)
{
	SentenceSections.RemoveAt(SectionIndex);
}

bool UDialogueSentenceTrack::IsEmpty() const
{
	return SentenceSections.Num() == 0;
}

const TArray<UMovieSceneSection*>& UDialogueSentenceTrack::GetAllSections() const
{
	return SentenceSections;
}

bool UDialogueSentenceTrack::SupportsMultipleRows() const
{
	return false;
}

FMovieSceneTrackRowSegmentBlenderPtr UDialogueSentenceTrack::GetRowSegmentBlender() const
{
	struct FBlender : FMovieSceneTrackRowSegmentBlender
	{
		virtual void Blend(FSegmentBlendData& BlendData) const override
		{
			// Run the default high pass filter for overlap priority
			MovieSceneSegmentCompiler::FilterOutUnderlappingSections(BlendData);
		}
	};
	return FBlender();
}

UMovieSceneSection* UDialogueSentenceTrack::CreateNewSection()
{
	return NewObject<UDialogueSentenceSection>(this, NAME_None, RF_Transactional);;
}

#undef LOCTEXT_NAMESPACE
