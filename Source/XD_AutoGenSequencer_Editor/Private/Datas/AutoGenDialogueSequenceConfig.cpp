﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "AutoGenDialogueSequenceConfig.h"

#include "DialogueStandPositionTemplate.h"
#include "DialogueSentence.h"
#include "AutoGenDialogueSystemData.h"
#include "PreviewDialogueSoundSequence.h"
#include "PreviewDialogueSentenceTrack.h"
#include "AutoGenDialogueSequenceConfig.h"
#include "PreviewDialogueSentenceTrack.h"
#include "PreviewDialogueSentenceSection.h"
#include "DialogueSentenceTrack.h"
#include "DialogueInterface.h"
#include "DialogueSentenceSection.h"
#include "TwoTargetCameraTrackingTrack.h"
#include "AutoGenDialogueCameraTemplate.h"
#include "TwoTargetCameraTrackingSection.h"
#include "DialogueCameraUtils.h"
#include "AutoGenDialogueAnimSet.h"
#include "AutoGenDialogueSettings.h"
#include "AutoGenDialogueCameraSet.h"
#include "GenAnimTrackUtils.h"

#include "ScopedTransaction.h"
#include "Transform.h"
#include "GameFramework/Character.h"
#include "MovieSceneCameraCutTrack.h"
#include "Engine/World.h"
#include "CinematicCamera/Public/CineCameraActor.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneFolder.h"
#include "MovieSceneCameraCutSection.h"
#include "Sound/DialogueWave.h"
#include "MovieSceneSkeletalAnimationTrack.h"
#include "CineCameraComponent.h"
#include "MovieSceneFloatTrack.h"
#include "MovieSceneFloatSection.h"
#include "Animation/AnimSequence.h"
#include "MovieSceneSkeletalAnimationSection.h"
#include "MovieSceneSpawnTrack.h"
#include "MovieSceneBoolSection.h"
#include "MovieSceneActorReferenceSection.h"
#include "MovieSceneActorReferenceTrack.h"
#include "MovieSceneObjectPropertyTrack.h"
#include "MovieSceneObjectPropertySection.h"
#include "AutoGenSequence_Log.h"
#include "Engine/RectLight.h"
#include "Components/RectLightComponent.h"
#include "MovieScene3DAttachTrack.h"

#define LOCTEXT_NAMESPACE "FXD_AutoGenSequencer_EditorModule"

USoundBase* FDialogueSentenceEditData::GetDefaultDialogueSound() const
{
	return DialogueSentence->SentenceWave;
}

UAutoGenDialogueSequenceConfig::UAutoGenDialogueSequenceConfig(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer),
	bEnableMergeCamera(true),
	bEnableSplitCamera(true),
	bGenerateSupplementLightGroup(true),
	bShowGenerateLog(true)
{
	AutoGenDialogueCameraSet = GetDefault<UAutoGenDialogueSettings>()->DefaultAutoGenDialogueCameraSet.LoadSynchronous();
}

void UAutoGenDialogueSequenceConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAutoGenDialogueSequenceConfig, CameraMergeMaxTime))
	{
		if (CameraMergeMaxTime * 3.f > CameraSplitMinTime)
		{
			CameraMergeMaxTime = CameraSplitMinTime / 3.f;
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UAutoGenDialogueSequenceConfig, CameraSplitMinTime))
	{
		if (CameraMergeMaxTime * 3.f > CameraSplitMinTime)
		{
			CameraSplitMinTime = CameraMergeMaxTime * 3.f;
		}
	}
}

bool UAutoGenDialogueSequenceConfig::IsConfigValid(TArray<FText>& ErrorMessages) const
{
	bool bIsSucceed = Super::IsConfigValid(ErrorMessages);

	if (AutoGenDialogueCameraSet == nullptr)
	{
		ErrorMessages.Add(LOCTEXT("默认镜头集未配置", "默认镜头集未配置"));
		bIsSucceed &= false;
	}
	else
	{
		bIsSucceed &= AutoGenDialogueCameraSet->IsValid(ErrorMessages);
	}

	TArray<FName> ValidNameList = DialogueStation.GetCharacterNames();
	for (const FDialogueSentenceEditData& Data : DialogueSentenceEditDatas)
	{
		if (!IsDialogueSentenceEditDataValid(Data, ValidNameList))
		{
			// TODO：详细描述错误
			ErrorMessages.Add(LOCTEXT("对白中存在问题", "对白中存在问题"));
			bIsSucceed &= false;
			break;
		}
	}
	return bIsSucceed;
}

TSubclassOf<UAutoGenDialogueAnimSetBase> UAutoGenDialogueSequenceConfig::GetAnimSetType() const
{
	return UAutoGenDialogueAnimSet::StaticClass();
}

bool UAutoGenDialogueSequenceConfig::IsDialogueSentenceEditDataValid(const FDialogueSentenceEditData &Data, const TArray<FName>& ValidNameList) const
{
	if (!Data.DialogueSentence)
	{
		return false;
	}
	if (!Data.DialogueSentence->SentenceWave)
	{
		return false;
	}
	if (Data.TargetNames.Contains(Data.SpeakerName))
	{
		return false;
	}
	if (TSet<FDialogueCharacterName>(Data.TargetNames).Num() != Data.TargetNames.Num())
	{
		return false;
	}
	if (!ValidNameList.Contains(Data.SpeakerName.GetName()))
	{
		return false;
	}
	for (const FDialogueCharacterName& Name : Data.TargetNames)
	{
		if (!ValidNameList.Contains(Name.GetName()))
		{
			return false;
		}
	}
	return true;
}

namespace DialogueSequencerUtils
{
	template<typename TrackType>
	static TrackType* CreatePropertyTrack(UMovieScene& MovieScene, const FGuid& ObjectGuid, const FName& PropertyName, const FString& PropertyPath)
	{
		TrackType* Track = MovieScene.AddTrack<TrackType>(ObjectGuid);
		Track->SetPropertyNameAndPath(PropertyName, PropertyPath);
		return Track;
	}
	template<typename SectionType>
	static SectionType* CreatePropertySection(UMovieScenePropertyTrack* MovieScenePropertyTrack)
	{
		SectionType* Section = CastChecked<SectionType>(MovieScenePropertyTrack->CreateNewSection());
		MovieScenePropertyTrack->AddSection(*Section);
		Section->SetRange(TRange<FFrameNumber>::All());
		return Section;
	}

	static FGuid AddChildObject(UMovieSceneSequence& Sequence, UObject* Parent, UMovieScene& MovieScene, const FGuid& ParentGuid, UObject* ChildObject)
	{
		FGuid ChildGuid = MovieScene.AddPossessable(ChildObject->GetName(), ChildObject->GetClass());
		FMovieScenePossessable* ChildPossessable = MovieScene.FindPossessable(ChildGuid);
		ChildPossessable->SetParent(ParentGuid);
		FMovieSceneSpawnable* ParentSpawnable = MovieScene.FindSpawnable(ParentGuid);
		ParentSpawnable->AddChildPossessable(ChildGuid);
		Sequence.BindPossessableObject(ChildGuid, *ChildObject, Parent);
		return ChildGuid;
	}
}

void UAutoGenDialogueSequenceConfig::GeneratePreview() const
{
	const FScopedTransaction Transaction(LOCTEXT("生成预览序列描述", "生成预览序列"));
	PreviewDialogueSoundSequence->Modify();

	UMovieScene& MovieScene = *PreviewDialogueSoundSequence->GetMovieScene();
	for (UPreviewDialogueSentenceTrack* PreviewDialogueSentenceTrack : PreviewDialogueSoundSequence->PreviewDialogueSentenceTracks)
	{
		if (PreviewDialogueSentenceTrack)
		{
			PreviewDialogueSoundSequence->GetMovieScene()->RemoveMasterTrack(*PreviewDialogueSentenceTrack);
		}
	}
	PreviewDialogueSoundSequence->PreviewDialogueSentenceTracks.Empty();

	FFrameRate FrameRate = MovieScene.GetTickResolution();

	FFrameNumber PaddingNumber = FFrameTime(PaddingTime * FrameRate).FrameNumber;
	FFrameNumber CurFrameNumber;
	TMap<FName, UPreviewDialogueSentenceTrack*> TrackMap;
	for (const FDialogueSentenceEditData& DialogueSentenceEditData : DialogueSentenceEditDatas)
	{
		FName SpeakerName = DialogueSentenceEditData.SpeakerName.GetName();
		check(SpeakerName != NAME_None);
		UPreviewDialogueSentenceTrack* PreviewDialogueSentenceTrack = TrackMap.FindRef(SpeakerName);
		if (PreviewDialogueSentenceTrack == nullptr)
		{
			PreviewDialogueSentenceTrack = MovieScene.AddMasterTrack<UPreviewDialogueSentenceTrack>();
			PreviewDialogueSentenceTrack->SetDisplayName(FText::FromName(SpeakerName));
			PreviewDialogueSentenceTrack->SpeakerName = SpeakerName;
			PreviewDialogueSoundSequence->PreviewDialogueSentenceTracks.Add(PreviewDialogueSentenceTrack);
			TrackMap.Add(SpeakerName, PreviewDialogueSentenceTrack);
		}

		FFrameNumber Duration;
		UMovieSceneSection* Section = PreviewDialogueSentenceTrack->AddNewDialogueOnRow(DialogueSentenceEditData, CurFrameNumber, Duration);
		CurFrameNumber += Duration + PaddingNumber;
	}

	FFrameNumber EndFrameNumber = (CurFrameNumber - PaddingNumber);
	MovieScene.SetPlaybackRange(FFrameNumber(0), EndFrameNumber.Value);
	MovieScene.SetWorkingRange(0.f, EndFrameNumber / FrameRate);
	MovieScene.SetViewRange(0.f, EndFrameNumber / FrameRate);
}

void UAutoGenDialogueSequenceConfig::Generate(TSharedRef<ISequencer> SequencerRef, UWorld* World, const TMap<FName, TSoftObjectPtr<ACharacter>>& CharacterNameInstanceMap, UAutoGenDialogueSystemData& AutoGenDialogueSystemData) const
{
	const FScopedTransaction Transaction(LOCTEXT("生成对白序列描述", "生成对白序列"));
	ULevelSequence& AutoGenDialogueSequence = *AutoGenDialogueSystemData.GetOwingLevelSequence();
	AutoGenDialogueSequence.Modify();

	ISequencer& Sequencer = SequencerRef.Get();
	UMovieScene& MovieScene = *AutoGenDialogueSequence.GetMovieScene();
	const FFrameRate FrameRate = MovieScene.GetTickResolution();
	const FFrameNumber SequenceStartFrameNumber = FFrameNumber(0);
	const FFrameNumber SequenceEndFrameNumber = PreviewDialogueSoundSequence->GetMovieScene()->GetPlaybackRange().GetUpperBoundValue();
	TArray<UMovieSceneTrack*>& AutoGenTracks = AutoGenDialogueSystemData.AutoGenTracks;

	// 数据预处理
	TMap<FName, ACharacter*> NameInstanceMap;
	TMap<ACharacter*, FGenDialogueCharacterData> DialogueCharacterDataMap;
	{
		int32 Idx = 0;
		for (const TPair<FName, TSoftObjectPtr<ACharacter>>& Entry : CharacterNameInstanceMap)
		{
			FName SpeakerName = Entry.Key;
			if (ACharacter* Character = Entry.Value.Get())
			{
				check(Character->Implements<UDialogueInterface>());
				NameInstanceMap.Add(Entry.Key, Character);

				const FDialogueCharacterData* DialogueCharacterData = DialogueStation.DialogueCharacterDatas.FindByPredicate([&](const FDialogueCharacterData& E) {return E.NameOverride == SpeakerName; });
				check(DialogueCharacterData);
				FGenDialogueCharacterData& GenDialogueCharacterData = DialogueCharacterDataMap.Add(Character, *DialogueCharacterData);
				GenDialogueCharacterData.CharacterIdx = Idx;

				Idx += 1;
			}
		}
	}

	UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene.GetCameraCutTrack());
	if (CameraCutTrack == nullptr)
	{
		CameraCutTrack = (UMovieSceneCameraCutTrack*)MovieScene.AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
		CameraCutTrack->Modify();
	}

	UMovieSceneFolder* AutoGenCameraFolder;
	{
		const FName AutoGenCameraFolderName = TEXT("自动相机组");
		if (UMovieSceneFolder** P_AutoGenCameraFolder = MovieScene.GetRootFolders().FindByPredicate([&](UMovieSceneFolder* Folder) {return Folder->GetFolderName() == AutoGenCameraFolderName; }))
		{
			AutoGenCameraFolder = *P_AutoGenCameraFolder;
		}
		else
		{
			AutoGenCameraFolder = NewObject<UMovieSceneFolder>(&MovieScene, NAME_None, RF_Transactional);
			MovieScene.GetRootFolders().Add(AutoGenCameraFolder);
			AutoGenCameraFolder->SetFolderName(AutoGenCameraFolderName);
		}
	}

	UMovieSceneFolder* SupplementLightFolder = nullptr;
	if (bGenerateSupplementLightGroup)
	{
		const FName SupplementLightFolderName = TEXT("自动补光组");
		if (UMovieSceneFolder** P_SupplementLightFolder = MovieScene.GetRootFolders().FindByPredicate([&](UMovieSceneFolder* Folder) {return Folder->GetFolderName() == SupplementLightFolderName; }))
		{
			SupplementLightFolder = *P_SupplementLightFolder;
		}
		else
		{
			SupplementLightFolder = NewObject<UMovieSceneFolder>(&MovieScene, NAME_None, RF_Transactional);
			MovieScene.GetRootFolders().Add(SupplementLightFolder);
			SupplementLightFolder->SetFolderName(SupplementLightFolderName);
		}
	}

	// 清除之前的数据
	{
		for (UMovieSceneTrack* PreGenTrack : AutoGenTracks)
		{
			if (PreGenTrack)
			{
				MovieScene.RemoveTrack(*PreGenTrack);
			}
		}
		AutoGenTracks.Empty();

		// 镜头
		for (const FGuid& CameraComponentGuid : AutoGenDialogueSystemData.AutoGenCameraComponentGuids)
		{
			MovieScene.RemovePossessable(CameraComponentGuid);
		}
		AutoGenDialogueSystemData.AutoGenCameraComponentGuids.Empty();
		CameraCutTrack->RemoveAllAnimationData();
		for (const FGuid& CameraGuid : AutoGenDialogueSystemData.AutoGenCameraGuids)
		{
			// TODO：之后假如有锁定导轨的逻辑需要再处理，现在先全删了
// 			int32 Idx = CameraCutTrack->GetAllSections().IndexOfByPredicate([&](UMovieSceneSection* E) { return CastChecked<UMovieSceneCameraCutSection>(E)->GetCameraBindingID().GetGuid() == CameraGuid; });
// 			if (Idx != INDEX_NONE)
// 			{
// 				CameraCutTrack->RemoveSectionAt(Idx);
// 			}
			MovieScene.RemoveSpawnable(CameraGuid);

			AutoGenCameraFolder->RemoveChildObjectBinding(CameraGuid);
		}
		AutoGenDialogueSystemData.AutoGenCameraGuids.Empty();

		// 补光
		if (bGenerateSupplementLightGroup)
		{
			for (const FGuid& SupplementLightGuid : AutoGenDialogueSystemData.AutoGenSupplementLightGuids)
			{
				MovieScene.RemoveSpawnable(SupplementLightGuid);
				SupplementLightFolder->RemoveChildObjectBinding(SupplementLightGuid);
			}
			AutoGenDialogueSystemData.AutoGenSupplementLightGuids.Empty();
		}
	}

	// 整理数据
	TArray<FGenDialogueData> SortedDialogueDatas;
	for (UPreviewDialogueSentenceTrack* PreviewDialogueSentenceTrack : PreviewDialogueSoundSequence->PreviewDialogueSentenceTracks)
	{
		const FName& SpeakerName = PreviewDialogueSentenceTrack->SpeakerName;
		if (ACharacter* SpeakerInstance = NameInstanceMap.FindRef(SpeakerName))
		{
			for (UMovieSceneSection* Section : PreviewDialogueSentenceTrack->GetAllSections())
			{
				FGenDialogueData& Data = SortedDialogueDatas.AddDefaulted_GetRef();
				Data.PreviewDialogueSentenceSection = CastChecked<UPreviewDialogueSentenceSection>(Section);
				Data.SpeakerInstance = SpeakerInstance;
				Data.SpeakerName = SpeakerName;

				const FDialogueSentenceEditData& DialogueSentenceEditData = Data.PreviewDialogueSentenceSection->DialogueSentenceEditData;
				for (const FDialogueCharacterName& TargetName : DialogueSentenceEditData.TargetNames)
				{
					Data.Targets.Add(NameInstanceMap.FindRef(TargetName.GetName()));
				}
			}
		}
	}
	SortedDialogueDatas.Sort([&](const FGenDialogueData& LHS, const FGenDialogueData& RHS)
		{
			return LHS.PreviewDialogueSentenceSection->GetRange().GetLowerBoundValue().Value < RHS.PreviewDialogueSentenceSection->GetRange().GetLowerBoundValue().Value;
		});

	// 生成对象轨
	for (TPair<ACharacter*, FGenDialogueCharacterData>& Pair : DialogueCharacterDataMap)
	{
		ACharacter* Speaker = Pair.Key;
		FGenDialogueCharacterData& GenDialogueCharacterData = Pair.Value;
		FGuid BindingGuid = AutoGenDialogueSystemData.FindOrAddPossessable(Speaker);
		// TODO: 先用MovieSceneSequenceID::Root，以后再找MovieSceneSequenceID怎么获得
		GenDialogueCharacterData.BindingID = FMovieSceneObjectBindingID(BindingGuid, MovieSceneSequenceID::Root);
	}

	// 生成对白导轨
	TMap<ACharacter*, UDialogueSentenceTrack*> DialogueSentenceTrackMap;
	for (int32 Idx = 0; Idx < SortedDialogueDatas.Num(); ++Idx)
	{
		const FGenDialogueData& GenDialogueData = SortedDialogueDatas[Idx];
		ACharacter* Speaker = GenDialogueData.SpeakerInstance;
		const FGenDialogueCharacterData& GenDialogueCharacterData = DialogueCharacterDataMap[Speaker];
		FGuid SpeakerBindingGuid = GenDialogueCharacterData.BindingID.GetGuid();
		const UPreviewDialogueSentenceSection* PreviewDialogueSentenceSection = GenDialogueData.PreviewDialogueSentenceSection;
		const FDialogueSentenceEditData& DialogueSentenceEditData = PreviewDialogueSentenceSection->DialogueSentenceEditData;
		TRange<FFrameNumber> SectionRange = PreviewDialogueSentenceSection->GetRange();
		FFrameNumber StartFrameNumber = SectionRange.GetLowerBoundValue();
		FFrameNumber EndFrameNumber = SectionRange.GetUpperBoundValue();
		const TArray<ACharacter*>& Targets = GenDialogueData.Targets;

		{
			UDialogueSentenceTrack* DialogueSentenceTrack = DialogueSentenceTrackMap.FindRef(Speaker);
			if (!DialogueSentenceTrack)
			{
				DialogueSentenceTrack = MovieScene.AddTrack<UDialogueSentenceTrack>(SpeakerBindingGuid);
				DialogueSentenceTrackMap.Add(Speaker, DialogueSentenceTrack);
				AutoGenTracks.Add(DialogueSentenceTrack);
			}
			UDialogueSentenceSection* DialogueSentenceSection = DialogueSentenceTrack->AddNewSentenceOnRow(DialogueSentenceEditData.DialogueSentence, StartFrameNumber);
			for (ACharacter* Target : Targets)
			{
				DialogueSentenceSection->Targets.Add(DialogueCharacterDataMap[Target].BindingID);
			}
		}
	}

	// -- 动作
	TMap<ACharacter*, FAnimTrackData> AnimationTrackDataMap = EvaluateAnimations(SequenceStartFrameNumber, SequenceEndFrameNumber, FrameRate, SortedDialogueDatas, DialogueCharacterDataMap);
	// 合并相同的动画
	for (TPair<ACharacter*, FAnimTrackData>& Pair : AnimationTrackDataMap)
	{
		FAnimTrackData& AnimationTrackData = Pair.Value;
		TArray<FAnimSectionVirtualData>& AnimSectionVirtualDatas = AnimationTrackData.AnimSectionVirtualDatas;
		AnimSectionVirtualDatas.Sort([](const FAnimSectionVirtualData& LHS, const FAnimSectionVirtualData& RHS) {return LHS.AnimRange.GetLowerBoundValue() < RHS.AnimRange.GetLowerBoundValue(); });
		for (int32 Idx = 1; Idx < AnimSectionVirtualDatas.Num();)
		{
			FAnimSectionVirtualData& LeftAnimSectionVirtualData = AnimSectionVirtualDatas[Idx - 1];
			FAnimSectionVirtualData& RightAnimSectionVirtualData = AnimSectionVirtualDatas[Idx];
			if (LeftAnimSectionVirtualData.AnimSequence == RightAnimSectionVirtualData.AnimSequence)
			{
				LeftAnimSectionVirtualData.AnimRange.SetUpperBoundValue(RightAnimSectionVirtualData.AnimRange.GetUpperBoundValue());
				LeftAnimSectionVirtualData.BlendOutTime = RightAnimSectionVirtualData.BlendOutTime;
				LeftAnimSectionVirtualData.GenDialogueDatas.Append(RightAnimSectionVirtualData.GenDialogueDatas);
				AnimSectionVirtualDatas.RemoveAt(Idx);
			}
			else
			{
				++Idx;
			}
		}
	}
	// 生成动画轨道
	for (TPair<ACharacter*, FAnimTrackData>& Pair : AnimationTrackDataMap)
	{
		ACharacter* Speaker = Pair.Key;
		FAnimTrackData& AnimationTrackData = Pair.Value;
		const FGenDialogueCharacterData& DialogueCharacterData = DialogueCharacterDataMap[Speaker];
		FGuid SpeakerBindingGuid = DialogueCharacterData.BindingID.GetGuid();
		UMovieSceneSkeletalAnimationTrack* AnimTrack = MovieScene.AddTrack<UMovieSceneSkeletalAnimationTrack>(SpeakerBindingGuid);
		AutoGenTracks.Add(AnimTrack);

		for (const FAnimSectionVirtualData& AnimSectionVirtualData : AnimationTrackData.AnimSectionVirtualDatas)
		{
			FFrameNumber AnimStartFrameNumber = AnimSectionVirtualData.AnimRange.GetLowerBoundValue();
			FFrameNumber AnimEndFrameNumber = AnimSectionVirtualData.AnimRange.GetUpperBoundValue();
			UMovieSceneSkeletalAnimationSection* TalkAnimSection = Cast<UMovieSceneSkeletalAnimationSection>(AnimTrack->AddNewAnimation(AnimStartFrameNumber, AnimSectionVirtualData.AnimSequence));
			TalkAnimSection->SetRange(AnimSectionVirtualData.AnimRange);

			TalkAnimSection->Params.SlotName = DialogueCharacterData.TalkAnimSlotName;
			GenAnimTrackUtils::SetBlendInOutValue(TalkAnimSection->Params.Weight, FrameRate, AnimStartFrameNumber, AnimSectionVirtualData.BlendInTime, AnimEndFrameNumber, AnimSectionVirtualData.BlendOutTime);
		}
	}

	// LookAt
	TMap<ACharacter*, UMovieSceneActorReferenceSection*> LookAtTrackMap;
	for (const TPair<ACharacter*, FGenDialogueCharacterData>& Pair : DialogueCharacterDataMap)
	{
		ACharacter* Speaker = Pair.Key;
		FGuid SpeakerBindingGuid = Pair.Value.BindingID.GetGuid();
		const FDialogueCharacterData& DialogueCharacterData = DialogueCharacterDataMap[Speaker];

		UMovieSceneActorReferenceTrack* LookAtTargetTrack = DialogueSequencerUtils::CreatePropertyTrack<UMovieSceneActorReferenceTrack>(MovieScene, SpeakerBindingGuid, DialogueCharacterData.LookAtTargetPropertyName, DialogueCharacterData.LookAtTargetPropertyName.ToString());
		AutoGenTracks.Add(LookAtTargetTrack);
		UMovieSceneActorReferenceSection* LookAtTargetSection = DialogueSequencerUtils::CreatePropertySection<UMovieSceneActorReferenceSection>(LookAtTargetTrack);
		LookAtTrackMap.Add(Speaker, LookAtTargetSection);
	}
	for (TPair<ACharacter*, FAnimTrackData>& Pair : AnimationTrackDataMap)
	{
		ACharacter* Speaker = Pair.Key;
		FAnimTrackData& AnimationTrackData = Pair.Value;
		FGuid SpeakerBindingGuid = DialogueCharacterDataMap[Speaker].BindingID.GetGuid();

		for (const FAnimSectionVirtualData& AnimSectionVirtualData : AnimationTrackData.AnimSectionVirtualDatas)
		{
			if (AnimSectionVirtualData.IsContainSpeakingData())
			{
				for (const FGenDialogueData* GenDialogueData : AnimSectionVirtualData.GenDialogueDatas)
				{
					const FFrameNumber SentenceStartFrameNumber = GenDialogueData->PreviewDialogueSentenceSection->GetRange().GetLowerBoundValue();
					const TArray<ACharacter*>& Targets = GenDialogueData->Targets;
					for (const TPair<ACharacter*, UMovieSceneActorReferenceSection*>& Entry : LookAtTrackMap)
					{
						ACharacter* Character = Entry.Key;
						UMovieSceneActorReferenceSection* LookAtTargetSection = Entry.Value;
						TMovieSceneChannelData<FMovieSceneActorReferenceKey> LookAtTargetChannel = const_cast<FMovieSceneActorReferenceData&>(LookAtTargetSection->GetActorReferenceData()).GetData();
						if (Character == Speaker)
						{
							if (Targets.Num() > 0)
							{
								// 说话者看向第一个对话目标
								LookAtTargetChannel.UpdateOrAddKey(SentenceStartFrameNumber, FMovieSceneActorReferenceKey(DialogueCharacterDataMap[Targets[0]].BindingID));
							}
						}
						else
						{
							LookAtTargetChannel.UpdateOrAddKey(SentenceStartFrameNumber, FMovieSceneActorReferenceKey(DialogueCharacterDataMap[Speaker].BindingID));
						}
					}
				}
			}
		}
	}

	// -- 镜头
	struct FCameraCutUtils
	{
		static UMovieSceneCameraCutSection* AddCameraCut(UMovieScene& MovieScene, FGuid CameraGuid, UMovieSceneCameraCutTrack& CameraCutTrack, FFrameNumber CameraCutStartFrame, FFrameNumber CameraCutEndFrame)
		{
			UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack.CreateNewSection());
			CameraCutSection->SetRange(TRange<FFrameNumber>(CameraCutStartFrame, CameraCutEndFrame));
			CameraCutSection->SetCameraGuid(CameraGuid);
			CameraCutTrack.AddSection(*CameraCutSection);

			TMovieSceneChannelData<bool> SpawnChannel = GetSpawnChannel(MovieScene, CameraGuid);
			SpawnChannel.AddKey(CameraCutStartFrame, true);
			SpawnChannel.AddKey(CameraCutEndFrame, false);
			return CameraCutSection;
		}

		static TMovieSceneChannelData<bool> GetSpawnChannel(UMovieScene& MovieScene, FGuid CameraGuid)
		{
			UMovieSceneSpawnTrack* SpawnTrack = MovieScene.FindTrack<UMovieSceneSpawnTrack>(CameraGuid);
			UMovieSceneBoolSection* SpawnSection = Cast<UMovieSceneBoolSection>(SpawnTrack->GetAllSections()[0]);
			return SpawnSection->GetChannel().GetData();
		}
	};

	using FCameraHandle = int32;
	const FCameraHandle InvalidCameraHandle = INDEX_NONE;
	// 镜头的生成信息
	struct FCameraCutCreateData
	{
		FCameraHandle CameraHandle;
		TRange<FFrameNumber> CameraCutRange;
		// 和镜头有关系的SortedDialogueDatas下标
		TArray<int32> RelatedSentenceIdxs;
		float GetDuration(const FFrameRate& FrameRate) const
		{
			return (CameraCutRange / FrameRate).Size<double>();
		}
	};
	struct FCameraActorCreateData
	{
		FCameraHandle CameraHandle;
		int32 UseNumber;

		FVector CameraLocation;
		FRotator CameraRotation;
		ACineCameraActor* CameraParams;
		const AAutoGenDialogueCameraTemplate* CameraTemplate;
		ACharacter* Speaker;
		FString GetSpeakerName() const { return Speaker->GetActorLabel(); }
		TArray<ACharacter*> Targets;
	};
	TArray<FCameraCutCreateData> CameraCutCreateDatas;
	TArray<FCameraActorCreateData> CameraActorCreateDatas;
	FFrameNumber CurCameraCutFrame = FFrameNumber(0);

	struct FCameraGenerateUtils
	{
		static void AppendRelatedSentenceIdxs(TArray<int32>& Array, const TArray<int32>& AppendDatas)
		{
			for (int32 Data : AppendDatas)
			{
				Array.AddUnique(Data);
			}
			Array.Sort();
		}
		// 合并相同镜头
		static bool CanMergeSameCut(const FCameraCutCreateData& LHS, const FCameraCutCreateData& RHS)
		{
			return LHS.CameraHandle == RHS.CameraHandle;
		}
		static void MergeSameCut(int32 Idx, TArray<FCameraCutCreateData>& CameraCutCreateDatas, TArray<FCameraActorCreateData>& CameraActorCreateDatas)
		{
			// 左边向右边合并
			FCameraCutCreateData& CameraCutCreateData = CameraCutCreateDatas[Idx];
			FCameraCutCreateData& NextCameraCutCreateData = CameraCutCreateDatas[Idx + 1];

			check(CanMergeSameCut(CameraCutCreateData, NextCameraCutCreateData));

			FCameraActorCreateData& CameraActorCreateData = CameraActorCreateDatas[CameraCutCreateData.CameraHandle];
			CameraActorCreateData.UseNumber -= 1;

			AppendRelatedSentenceIdxs(CameraCutCreateData.RelatedSentenceIdxs, NextCameraCutCreateData.RelatedSentenceIdxs);
			CameraCutCreateData.CameraCutRange.SetUpperBoundValue(NextCameraCutCreateData.CameraCutRange.GetUpperBoundValue());
			CameraCutCreateDatas.RemoveAt(Idx + 1);
		}
		static void TryMergeAllSameCut(TArray<FCameraCutCreateData>& CameraCutCreateDatas, TArray<FCameraActorCreateData>& CameraActorCreateDatas)
		{
			for (int32 Idx = 0; Idx < CameraCutCreateDatas.Num() - 1;)
			{
				FCameraCutCreateData& CameraCutCreateData = CameraCutCreateDatas[Idx];
				FCameraCutCreateData& NextCameraCutCreateData = CameraCutCreateDatas[Idx + 1];
				if (CanMergeSameCut(CameraCutCreateData, NextCameraCutCreateData))
				{
					MergeSameCut(Idx, CameraCutCreateDatas, CameraActorCreateDatas);
				}
				else
				{
					++Idx;
				}
			}
		}

		using FCameraWeightsData = AAutoGenDialogueCameraTemplate::FCameraWeightsData;

		static FCameraHandle AddOrFindVirtualCameraData(float DialogueProgress, const UAutoGenDialogueSequenceConfig& GenConfig, ACharacter* Speaker, const TArray<ACharacter*>& Targets, TArray<FCameraActorCreateData>& CameraActorCreateDatas, const TArray<FCameraHandle>& ExcludeCamreaHandles)
		{
			TArray<const AAutoGenDialogueCameraTemplate*> ExcludeCameraTemplates;
			for (const FCameraHandle CameraHandle : ExcludeCamreaHandles)
			{
				ExcludeCameraTemplates.Add(CameraActorCreateDatas[CameraHandle].CameraTemplate);
			}

			// 遍历镜头集获取权重
			TArray<FCameraWeightsData> CameraWeightsDatas;
			for (const FAutoGenDialogueCameraConfig& AutoGenDialogueCameraConfig : GenConfig.AutoGenDialogueCameraSet->CameraTemplates)
			{
				if (ExcludeCameraTemplates.Contains(AutoGenDialogueCameraConfig.CameraTemplate.GetDefaultObject()))
				{
					continue;
				}

				AAutoGenDialogueCameraTemplate* CameraTemplate = AutoGenDialogueCameraConfig.CameraTemplate.GetDefaultObject();
				FCameraWeightsData CameraWeightsData = CameraTemplate->EvaluateCameraTemplate(Speaker, Targets, DialogueProgress);
				if (CameraWeightsData.IsValid())
				{
					CameraWeightsData.Weights *= AutoGenDialogueCameraConfig.Weights;
					CameraWeightsDatas.Add(CameraWeightsData);
				}
			}
			CameraWeightsDatas.Sort([](const FCameraWeightsData& LHS, const FCameraWeightsData& RHS) {return LHS.Weights > RHS.Weights; });

			// 现在直接选择最优的
			check(CameraWeightsDatas.Num() > 0);
			const FCameraWeightsData& SelectedData = CameraWeightsDatas[0];

			// Log
			if (GenConfig.bShowGenerateLog)
			{
				FString TargetNames;
				if (Targets.Num() > 0)
				{
					const FString Padding = TEXT("，");
					for (ACharacter* Target : Targets)
					{
						TargetNames += Target->GetActorLabel() + Padding;
					}
					TargetNames.RemoveFromEnd(Padding);
				}
				else
				{
					TargetNames = TEXT("空");
				}
				AutoGenSequence_Display_Log("    进度[%f]，说话者[%s]，对话目标[%s]选择结果：", DialogueProgress, *Speaker->GetActorLabel(), *TargetNames);
				for (const FCameraWeightsData& E : CameraWeightsDatas)
				{
					AutoGenSequence_Display_Log("      [%s] %f", *E.CameraTemplate->GetClass()->GetDisplayNameText().ToString(), E.Weights);
				}
			}

			// 若镜头存在则直接使用
			FCameraActorCreateData* ExistedCameraData = CameraActorCreateDatas.FindByPredicate([&](const FCameraActorCreateData& E)
				{
					return E.Speaker == Speaker &&
						E.Targets == Targets &&
						E.CameraLocation == SelectedData.CameraLocation &&
						E.CameraRotation == SelectedData.CameraRotation &&
						E.CameraTemplate == SelectedData.CameraTemplate;
				});

			if (ExistedCameraData)
			{
				ExistedCameraData->UseNumber += 1;
				return ExistedCameraData->CameraHandle;
			}
			else
			{
				FCameraHandle CameraHandle = CameraActorCreateDatas.AddDefaulted();
				FCameraActorCreateData& CameraActorCreateData = CameraActorCreateDatas[CameraHandle];

				CameraActorCreateData.CameraHandle = CameraHandle;
				CameraActorCreateData.Speaker = Speaker;
				CameraActorCreateData.Targets = Targets;
				CameraActorCreateData.UseNumber = 1;

				const AAutoGenDialogueCameraTemplate* CameraTemplate = SelectedData.CameraTemplate;
				CameraActorCreateData.CameraLocation = SelectedData.CameraLocation;
				CameraActorCreateData.CameraRotation = SelectedData.CameraRotation;
				CameraActorCreateData.CameraParams = CastChecked<ACineCameraActor>(CameraTemplate->CineCamera->GetChildActorTemplate());
				CameraActorCreateData.CameraTemplate = CameraTemplate;
				return CameraHandle;
			}
		}
	};

	if (bShowGenerateLog)
	{
		AutoGenSequence_Display_Log("镜头生成开始：");
		AutoGenSequence_Display_Log("  选择镜头：");
	}

	const float DialogueCount = SortedDialogueDatas.Num();
	for (int32 Idx = 0; Idx < SortedDialogueDatas.Num(); ++Idx)
	{
		FGenDialogueData& GenDialogueData = SortedDialogueDatas[Idx];
		const TArray<ACharacter*>& Targets = GenDialogueData.Targets;
		ACharacter* Speaker = GenDialogueData.SpeakerInstance;
		const FName& SpeakerName = GenDialogueData.SpeakerName;
		const UPreviewDialogueSentenceSection* PreviewDialogueSentenceSection = GenDialogueData.PreviewDialogueSentenceSection;
		TRange<FFrameNumber> SectionRange = PreviewDialogueSentenceSection->GetRange();
		FFrameNumber EndFrameNumber = SectionRange.GetUpperBoundValue();

		const float DialogueProgress = Idx / DialogueCount;

		FCameraHandle CameraHandle = InvalidCameraHandle;

		CameraHandle = FCameraGenerateUtils::AddOrFindVirtualCameraData(DialogueProgress, *this, Speaker, Targets, CameraActorCreateDatas, {});

		FCameraCutCreateData& CameraCutCreateData = CameraCutCreateDatas.AddDefaulted_GetRef();
		CameraCutCreateData.CameraHandle = CameraHandle;
		CameraCutCreateData.CameraCutRange = TRange<FFrameNumber>(CurCameraCutFrame, EndFrameNumber);
		CameraCutCreateData.RelatedSentenceIdxs.AddUnique(Idx);

		CurCameraCutFrame = EndFrameNumber;
	}

	// 太短的镜头做合并
	if (bEnableMergeCamera)
	{
		if (bShowGenerateLog)
		{
			AutoGenSequence_Display_Log("  合并长度小于[%f 秒]的镜头：", CameraMergeMaxTime);
		}

		for (int32 Idx = 0; Idx < CameraCutCreateDatas.Num(); ++Idx)
		{
			FCameraCutCreateData& CameraCutCreateData = CameraCutCreateDatas[Idx];
			float MidDuration = CameraCutCreateData.GetDuration(FrameRate);
			if (MidDuration < CameraMergeMaxTime)
			{
				// 合并方案
				// 假如合并后的镜头的长度超过了镜头分离最小长度就不合并
				// TODO：或许这种情况可以制定特殊的分离镜头策略
				const int32 MergeModeCount = 2;
				enum class ECameraMergeMode : uint8
				{
					// 1. 左侧拉长至右侧
					LeftToRight = 0,
					// 2. 右侧拉长至左侧
					RightToLeft = 1
				};

				TArray<ECameraMergeMode> InvalidModes;
				bool HasLeftCut = Idx != 0;
				bool HasRightCut = Idx != CameraCutCreateDatas.Num() - 1;

				if (!HasLeftCut)
				{
					InvalidModes.Add(ECameraMergeMode::LeftToRight);
				}
				else
				{
					FCameraCutCreateData& LeftCameraCutCreateData = CameraCutCreateDatas[Idx - 1];
					float MergedDuration = LeftCameraCutCreateData.GetDuration(FrameRate) + MidDuration;
					if (HasRightCut)
					{
						FCameraCutCreateData& RightCameraCutCreateData = CameraCutCreateDatas[Idx + 1];
						if (FCameraGenerateUtils::CanMergeSameCut(LeftCameraCutCreateData, RightCameraCutCreateData))
						{
							MergedDuration += RightCameraCutCreateData.GetDuration(FrameRate);
						}
					}
					if (MergedDuration > CameraSplitMinTime)
					{
						InvalidModes.Add(ECameraMergeMode::LeftToRight);
					}
				}
				if (!HasRightCut)
				{
					InvalidModes.Add(ECameraMergeMode::RightToLeft);
				}
				else
				{
					FCameraCutCreateData& RightCameraCutCreateData = CameraCutCreateDatas[Idx + 1];
					float MergedDuration = RightCameraCutCreateData.GetDuration(FrameRate) + MidDuration;
					if (HasLeftCut)
					{
						FCameraCutCreateData& LeftCameraCutCreateData = CameraCutCreateDatas[Idx - 1];
						if (FCameraGenerateUtils::CanMergeSameCut(LeftCameraCutCreateData, RightCameraCutCreateData))
						{
							MergedDuration += LeftCameraCutCreateData.GetDuration(FrameRate);
						}
					}
					if (MergedDuration > CameraSplitMinTime)
					{
						InvalidModes.Add(ECameraMergeMode::RightToLeft);
					}
				}

				// 假如没有可行的合并方案就不合并
				if (InvalidModes.Num() == MergeModeCount)
				{
					continue;
				}

				ECameraMergeMode CameraMergeMode;
				do
				{
					CameraMergeMode = ECameraMergeMode(FMath::RandHelper(MergeModeCount));
				} while (InvalidModes.Contains(CameraMergeMode));


				switch (CameraMergeMode)
				{
				case ECameraMergeMode::LeftToRight:
				{
					FCameraCutCreateData& LeftCameraCutCreateData = CameraCutCreateDatas[Idx - 1];
					FCameraGenerateUtils::AppendRelatedSentenceIdxs(LeftCameraCutCreateData.RelatedSentenceIdxs, CameraCutCreateData.RelatedSentenceIdxs);
					LeftCameraCutCreateData.CameraCutRange.SetUpperBoundValue(CameraCutCreateData.CameraCutRange.GetUpperBoundValue());

					if (bShowGenerateLog)
					{
						const TRange<double> CameraCutCreateDataTimeRange = CameraCutCreateData.CameraCutRange / FrameRate;
						const TRange<double> LeftCameraCutCreateDataTimeRange = LeftCameraCutCreateData.CameraCutRange / FrameRate;
						AutoGenSequence_Display_Log("    左侧镜头[%f 秒]被合并：[%f - %f] 的镜头被合并至 [%f - %f]", MidDuration, CameraCutCreateDataTimeRange.GetLowerBoundValue(), CameraCutCreateDataTimeRange.GetUpperBoundValue(),
							LeftCameraCutCreateDataTimeRange.GetLowerBoundValue(), LeftCameraCutCreateDataTimeRange.GetUpperBoundValue());
					}
				}
				break;
				case ECameraMergeMode::RightToLeft:
				{
					FCameraCutCreateData& RightCameraCutCreateData = CameraCutCreateDatas[Idx + 1];
					FCameraGenerateUtils::AppendRelatedSentenceIdxs(RightCameraCutCreateData.RelatedSentenceIdxs, CameraCutCreateData.RelatedSentenceIdxs);
					RightCameraCutCreateData.CameraCutRange.SetLowerBoundValue(CameraCutCreateData.CameraCutRange.GetLowerBoundValue());

					if (bShowGenerateLog)
					{
						const TRange<double> CameraCutCreateDataTimeRange = CameraCutCreateData.CameraCutRange / FrameRate;
						const TRange<double> RightCameraCutCreateDataTimeRange = RightCameraCutCreateData.CameraCutRange / FrameRate;
						AutoGenSequence_Display_Log("    右侧镜头[%f 秒]被合并：[%f - %f] 的镜头被合并至 [%f - %f]", MidDuration, CameraCutCreateDataTimeRange.GetLowerBoundValue(), CameraCutCreateDataTimeRange.GetUpperBoundValue(),
							RightCameraCutCreateDataTimeRange.GetLowerBoundValue(), RightCameraCutCreateDataTimeRange.GetUpperBoundValue());
					}
				}
				break;
				}

				CameraCutCreateDatas.RemoveAt(Idx);
				Idx -= 1;
			}
		}
		FCameraGenerateUtils::TryMergeAllSameCut(CameraCutCreateDatas, CameraActorCreateDatas);
	}

	// 太长的镜头拆开来
	if (bEnableSplitCamera)
	{
		if (bShowGenerateLog)
		{
			AutoGenSequence_Display_Log("  拆分长度大于[%f 秒]的镜头：", CameraSplitMinTime);
		}

		for (int32 Idx = 0; Idx < CameraCutCreateDatas.Num(); ++Idx)
		{
			FCameraCutCreateData& CameraCutCreateData = CameraCutCreateDatas[Idx];
			FCameraActorCreateData& CameraActorCreateData = CameraActorCreateDatas[CameraCutCreateData.CameraHandle];
			float CameraDuration = CameraCutCreateData.GetDuration(FrameRate);
			if (CameraDuration > CameraSplitMinTime)
			{
				FCameraCutCreateData& LeftCameraCutCreateData = CameraCutCreateData;
				TArray<int32> AllRelatedSentenceIdxs = CameraCutCreateData.RelatedSentenceIdxs;
				CameraCutCreateData.RelatedSentenceIdxs.Empty();
				FFrameNumber LeftStartFrameNumber = LeftCameraCutCreateData.CameraCutRange.GetLowerBoundValue();
				FFrameNumber LeftEndFrameNumber = LeftCameraCutCreateData.CameraCutRange.GetUpperBoundValue();

				// TODO：可以根据RelatedSentenceIdxs来选择
				FFrameNumber MinPadding = (CameraMergeMaxTime * FrameRate).GetFrame();
				FFrameNumber MidStartFrameNumber = LeftStartFrameNumber + MinPadding;
				FFrameNumber MidEndFrameNumber = MidStartFrameNumber + MinPadding;
				LeftCameraCutCreateData.CameraCutRange.SetUpperBoundValue(MidStartFrameNumber);
				FCameraCutCreateData& MidCameraCutCreateData = CameraCutCreateDatas.InsertDefaulted_GetRef(Idx + 1);
				MidCameraCutCreateData.CameraCutRange = TRange<FFrameNumber>(MidStartFrameNumber, MidEndFrameNumber);

				FFrameNumber RightStartFrameNumber = MidEndFrameNumber;
				FFrameNumber RightEndFrameNumber = LeftEndFrameNumber;
				FCameraCutCreateData& RightCameraCutCreateData = CameraCutCreateDatas.InsertDefaulted_GetRef(Idx + 2);
				RightCameraCutCreateData.CameraHandle = LeftCameraCutCreateData.CameraHandle;
				RightCameraCutCreateData.CameraCutRange = TRange<FFrameNumber>(RightStartFrameNumber, RightEndFrameNumber);

				// 分离镜头后计算各自的RelatedSentenceIdxs
				for (int32 RelatedSentenceIdx : AllRelatedSentenceIdxs)
				{
					const FGenDialogueData& DialogueData = SortedDialogueDatas[RelatedSentenceIdx];
					TRange<FFrameNumber> SentenceRange = DialogueData.PreviewDialogueSentenceSection->GetRange();

					if (LeftCameraCutCreateData.CameraCutRange.Overlaps(SentenceRange))
					{
						LeftCameraCutCreateData.RelatedSentenceIdxs.Add(RelatedSentenceIdx);
					}
					if (MidCameraCutCreateData.CameraCutRange.Overlaps(SentenceRange))
					{
						MidCameraCutCreateData.RelatedSentenceIdxs.Add(RelatedSentenceIdx);
					}
					if (RightCameraCutCreateData.CameraCutRange.Overlaps(SentenceRange))
					{
						RightCameraCutCreateData.RelatedSentenceIdxs.Add(RelatedSentenceIdx);
					}
				}

				float DialogueProgress = MidCameraCutCreateData.RelatedSentenceIdxs[MidCameraCutCreateData.RelatedSentenceIdxs.Num() / 2] / DialogueCount;
				// 保证MidCamera和前一个是不同的
				MidCameraCutCreateData.CameraHandle = FCameraGenerateUtils::AddOrFindVirtualCameraData(DialogueProgress, *this, CameraActorCreateData.Speaker, CameraActorCreateData.Targets, CameraActorCreateDatas, { LeftCameraCutCreateData.CameraHandle });
				check(MidCameraCutCreateData.CameraHandle != LeftCameraCutCreateData.CameraHandle);

				if (bShowGenerateLog)
				{
					TRange<double> LeftTimeRange = LeftCameraCutCreateData.CameraCutRange / FrameRate;
					TRange<double> MidTimeRange = MidCameraCutCreateData.CameraCutRange / FrameRate;
					TRange<double> RightTimeRange = RightCameraCutCreateData.CameraCutRange / FrameRate;
					AutoGenSequence_Display_Log("  将[%f 秒]的镜头拆分为 [%f - %f]，[%f - %f]，[%f - %f]三个镜头：", CameraDuration,
						LeftTimeRange.GetLowerBoundValue(), LeftTimeRange.GetUpperBoundValue(),
						MidTimeRange.GetLowerBoundValue(), MidTimeRange.GetUpperBoundValue(),
						RightTimeRange.GetLowerBoundValue(), RightTimeRange.GetUpperBoundValue());
				}
			}
		}
		FCameraGenerateUtils::TryMergeAllSameCut(CameraCutCreateDatas, CameraActorCreateDatas);
	}

	// TODO：镜头时长的膨胀和收缩

	// 生成摄像机导轨
	struct FSpawnedCameraData
	{
		FGuid CameraGuid;
		UMovieSceneActorReferenceSection* DepthOfFieldTargetSection;
	};

	TMap<FCameraHandle, FSpawnedCameraData> SpawnedCameraMap;
	for (const FCameraCutCreateData& CameraCutCreateData : CameraCutCreateDatas)
	{
		const FCameraActorCreateData& CameraActorCreateData = CameraActorCreateDatas[CameraCutCreateData.CameraHandle];
		const TRange<FFrameNumber>& CameraCutRange = CameraCutCreateData.CameraCutRange;

		FSpawnedCameraData& SpawnedCameraData = SpawnedCameraMap.FindOrAdd(CameraCutCreateData.CameraHandle);
		if (!SpawnedCameraData.CameraGuid.IsValid())
		{
			FGuid& CameraGuid = SpawnedCameraData.CameraGuid;

			ACharacter* Speaker = CameraActorCreateData.Speaker;
			const TArray<ACharacter*>& Targets = CameraActorCreateData.Targets;

			// TODO: 不用Spawn，增加Spawnable之后设置Template
			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags &= ~RF_Transactional;
			SpawnParams.Template = CameraActorCreateData.CameraParams;
			ACineCameraActor* AutoGenCamera = World->SpawnActor<ACineCameraActor>(CameraActorCreateData.CameraLocation, CameraActorCreateData.CameraRotation, SpawnParams);
			AutoGenCamera->SetActorLabel(FString::Printf(TEXT("%s_Camera"), *CameraActorCreateData.GetSpeakerName()));
			CameraGuid = AutoGenDialogueSystemData.CreateSpawnable(AutoGenCamera);
			World->EditorDestroyActor(AutoGenCamera, false);

			AutoGenCameraFolder->AddChildObjectBinding(CameraGuid);
			AutoGenCamera = Cast<ACineCameraActor>(MovieScene.FindSpawnable(CameraGuid)->GetObjectTemplate());
			AutoGenDialogueSystemData.AutoGenCameraGuids.Add(CameraGuid);
			{
				if (CurCameraCutFrame != SequenceStartFrameNumber)
				{
					TMovieSceneChannelData<bool> SpawnChannel = FCameraCutUtils::GetSpawnChannel(MovieScene, CameraGuid);
					SpawnChannel.AddKey(SequenceStartFrameNumber, false);
				}

				UCineCameraComponent* CineCameraComponent = AutoGenCamera->GetCineCameraComponent();
				FGuid CineCameraComponentGuid = DialogueSequencerUtils::AddChildObject(AutoGenDialogueSequence, AutoGenCamera, MovieScene, CameraGuid, CineCameraComponent);
				AutoGenDialogueSystemData.AutoGenCameraComponentGuids.Add(CineCameraComponentGuid);
				// 生成镜头的特殊轨道
				CameraActorCreateData.CameraTemplate->GenerateCameraTrackData(Speaker, Targets, MovieScene, CineCameraComponentGuid, DialogueCharacterDataMap);

				// 生成景深导轨
				// TODO：这个是否也可以移至GenerateCameraTrackData？
				{
					// 景深目标为说话者
					CineCameraComponent->FocusSettings.FocusMethod = ECameraFocusMethod::Tracking;
					// TODO: 现在用字符串记录路径，可以考虑使用FPropertyPath，或者编译期通过一种方式做检查
					UMovieSceneActorReferenceTrack* ActorToTrackTrack = DialogueSequencerUtils::CreatePropertyTrack<UMovieSceneActorReferenceTrack>(MovieScene, CineCameraComponentGuid,
						GET_MEMBER_NAME_CHECKED(FCameraTrackingFocusSettings, ActorToTrack), TEXT("FocusSettings.TrackingFocusSettings.ActorToTrack"));
					AutoGenTracks.Add(ActorToTrackTrack);
					UMovieSceneActorReferenceSection* ActorToTrackSection = DialogueSequencerUtils::CreatePropertySection<UMovieSceneActorReferenceSection>(ActorToTrackTrack);
					SpawnedCameraData.DepthOfFieldTargetSection = ActorToTrackSection;
				}
			}
		}

		FCameraCutUtils::AddCameraCut(MovieScene, SpawnedCameraData.CameraGuid, *CameraCutTrack, CameraCutRange.GetLowerBoundValue(), CameraCutRange.GetUpperBoundValue());

		// 景深
		TMovieSceneChannelData<FMovieSceneActorReferenceKey> ActorToTrackSectionChannel = const_cast<FMovieSceneActorReferenceData&>(SpawnedCameraData.DepthOfFieldTargetSection->GetActorReferenceData()).GetData();
		for (int32 RelatedSentenceIdx : CameraCutCreateData.RelatedSentenceIdxs)
		{
			FGenDialogueData& GenDialogueData = SortedDialogueDatas[RelatedSentenceIdx];
			ACharacter* Speaker = GenDialogueData.SpeakerInstance;
			FFrameNumber SentenceStartTime = GenDialogueData.PreviewDialogueSentenceSection->GetRange().GetLowerBoundValue();
			FFrameNumber DepthOfFieldTargetChangedFrameNumber = SentenceStartTime > SequenceStartFrameNumber ? SentenceStartTime : SequenceStartFrameNumber;
			ActorToTrackSectionChannel.UpdateOrAddKey(DepthOfFieldTargetChangedFrameNumber, FMovieSceneActorReferenceKey(DialogueCharacterDataMap[Speaker].BindingID));
		}

		// 补光
		if (bGenerateSupplementLightGroup)
		{
			TArray<ACharacter*> VisitedCharacters;
			for (int32 RelatedSentenceIdx : CameraCutCreateData.RelatedSentenceIdxs)
			{
				FGenDialogueData& GenDialogueData = SortedDialogueDatas[RelatedSentenceIdx];

				ACharacter* Speaker = GenDialogueData.SpeakerInstance;
				if (!VisitedCharacters.Contains(Speaker))
				{
					VisitedCharacters.Add(Speaker);
					FActorSpawnParameters SpawnParams;
					SpawnParams.ObjectFlags &= ~RF_Transactional;
					// 现在的补光是Butterfly Lighting模式
					// TODO：结合上下文丰富补光模式
					ARectLight* AutoGenSupplementLight = World->SpawnActor<ARectLight>(FVector::ForwardVector * 100.f, FRotator(0.f, 180.f, 0.f), SpawnParams);
					AutoGenSupplementLight->SetActorLabel(FString::Printf(TEXT("%s_SupplementLight"), *CameraActorCreateData.GetSpeakerName()));
					{
						AutoGenSupplementLight->SetMobility(EComponentMobility::Movable);
						URectLightComponent* RectLight = AutoGenSupplementLight->RectLightComponent;
						// TODO：根据环境控制强度，可以考虑生成器使用使用者定义的类，使用者自己类型中控制强度。
						RectLight->SetIntensityUnits(ELightUnits::Candelas);
						RectLight->SetIntensity(1.f);

						// TODO：现在先就按一直方式写，之后使用使用者定义的类，就使用那边类的配置，这边就不用了
						RectLight->SetCastShadows(false);
						// 设置光照通道，只影响角色
						RectLight->LightingChannels.bChannel0 = false;
						RectLight->LightingChannels.bChannel2 = true;
					}
					FGuid SupplementLightGuid = AutoGenDialogueSystemData.CreateSpawnable(AutoGenSupplementLight);
					World->EditorDestroyActor(AutoGenSupplementLight, false);
					SupplementLightFolder->AddChildObjectBinding(SupplementLightGuid);
					AutoGenDialogueSystemData.AutoGenSupplementLightGuids.Add(SupplementLightGuid);

					FFrameNumber SupplementLightStartTime = CameraCutCreateData.CameraCutRange.GetLowerBoundValue();
					FFrameNumber SupplementLightEndTime = CameraCutCreateData.CameraCutRange.GetUpperBoundValue();

					TMovieSceneChannelData<bool> SpawnChannel = FCameraCutUtils::GetSpawnChannel(MovieScene, SupplementLightGuid);
					SpawnChannel.AddKey(SequenceStartFrameNumber, false);
					SpawnChannel.AddKey(SupplementLightStartTime, true);
					SpawnChannel.AddKey(SupplementLightEndTime, false);

					// 吸附至角色上
					UMovieScene3DAttachTrack* MovieScene3DAttachTrack = MovieScene.AddTrack<UMovieScene3DAttachTrack>(SupplementLightGuid);
					{
						MovieScene3DAttachTrack->AddConstraint(SupplementLightStartTime, CameraCutCreateData.CameraCutRange.Size<FFrameNumber>().Value, NAME_None, Speaker->GetRootComponent()->GetFName(), DialogueCharacterDataMap[Speaker].BindingID);
					}
				}
			}
		}
	}

	// 后处理
	MovieScene.SetPlaybackRange(FFrameNumber(0), SequenceEndFrameNumber.Value);
	MovieScene.SetWorkingRange(0.f, SequenceEndFrameNumber / FrameRate);
	MovieScene.SetViewRange(0.f, SequenceEndFrameNumber / FrameRate);
}

TMap<ACharacter*, UAutoGenDialogueSequenceConfig::FAnimTrackData> UAutoGenDialogueSequenceConfig::EvaluateAnimations(
	const FFrameNumber SequenceStartFrameNumber,
	const FFrameNumber SequenceEndFrameNumber,
	const FFrameRate FrameRate,
	const TArray<FGenDialogueData>& SortedDialogueDatas,
	const TMap<ACharacter*, FGenDialogueCharacterData>& DialogueCharacterDataMap) const
{
	TMap<ACharacter*, FAnimTrackData> AnimationTrackDataMap;
	for (int32 Idx = 0; Idx < SortedDialogueDatas.Num(); ++Idx)
	{
		const FGenDialogueData& GenDialogueData = SortedDialogueDatas[Idx];
		ACharacter* Speaker = GenDialogueData.SpeakerInstance;
		const TArray<ACharacter*>& Targets = GenDialogueData.Targets;
		const FGenDialogueCharacterData& DialogueCharacterData = DialogueCharacterDataMap[Speaker];
		FGuid SpeakerBindingGuid = DialogueCharacterData.BindingID.GetGuid();
		const UPreviewDialogueSentenceSection* PreviewDialogueSentenceSection = GenDialogueData.PreviewDialogueSentenceSection;
		TRange<FFrameNumber> SectionRange = PreviewDialogueSentenceSection->GetRange();
		FFrameNumber StartFrameNumber = SectionRange.GetLowerBoundValue();
		FFrameNumber EndFrameNumber = SectionRange.GetUpperBoundValue();

		FAnimTrackData& AnimationTrackData = AnimationTrackDataMap.FindOrAdd(Speaker);

		TArray<FAnimSectionVirtualData>& AnimSectionVirtualDatas = AnimationTrackData.AnimSectionVirtualDatas;

		if (DialogueCharacterData.DialogueAnimSet)
		{
			// TODO：根据动作的相似程度确认混合时间
			const float BlendTime = 0.7f;

			FFrameNumber AnimStartFrameNumber = StartFrameNumber;
			FFrameNumber AnimEndFrameNumber = EndFrameNumber;
			// 动作应该早于说话，且晚于结束
			const float AnimStartEarlyTime = 0.5f;
			const float AnimEndDelayTime = 0.5f;

			if (AnimSectionVirtualDatas.Num() > Idx + 1)
			{
				// 防止插到前一个对话动画里
				const FAnimSectionVirtualData& PreAnimSectionVirtualData = AnimSectionVirtualDatas[Idx - 1];
				AnimStartFrameNumber = FMath::Max(PreAnimSectionVirtualData.AnimRange.GetUpperBoundValue() - GenAnimTrackUtils::SecondToFrameNumber(PreAnimSectionVirtualData.BlendOutTime, FrameRate),
					AnimStartFrameNumber - GenAnimTrackUtils::SecondToFrameNumber(AnimStartEarlyTime, FrameRate));
			}
			else
			{
				AnimStartFrameNumber = FMath::Max(SequenceStartFrameNumber, AnimStartFrameNumber - GenAnimTrackUtils::SecondToFrameNumber(AnimStartEarlyTime, FrameRate));
			}
			if (Idx == AnimSectionVirtualDatas.Num() - 1)
			{
				AnimEndFrameNumber = FMath::Min(SequenceEndFrameNumber, AnimEndFrameNumber + GenAnimTrackUtils::SecondToFrameNumber(AnimEndDelayTime, FrameRate));
			}

			// TODO：增加动作选择策略
			UAnimSequence* SelectedAnimSequence = EvaluateTalkAnimation(DialogueCharacterData, GenDialogueData);

			FAnimSectionVirtualData& AnimSectionVirtualData = AnimSectionVirtualDatas.AddDefaulted_GetRef();
			AnimSectionVirtualData.AnimSequence = SelectedAnimSequence;
			AnimSectionVirtualData.AnimRange = TRange<FFrameNumber>(AnimStartFrameNumber, AnimEndFrameNumber);
			AnimSectionVirtualData.BlendInTime = BlendTime;
			AnimSectionVirtualData.BlendOutTime = BlendTime;
			AnimSectionVirtualData.GenDialogueDatas.Add(&GenDialogueData);
		}
	}
	// 没动画的地方补上Idle动画
	for (TPair<ACharacter*, FAnimTrackData>& Pair : AnimationTrackDataMap)
	{
		ACharacter* Speaker = Pair.Key;
		FAnimTrackData& AnimTrackData = Pair.Value;
		const FGenDialogueCharacterData& DialogueCharacterData = DialogueCharacterDataMap[Speaker];

		TArray<FAnimSectionVirtualData>& AnimSectionVirtualDatas = AnimTrackData.AnimSectionVirtualDatas;

		// TODO：添加站立动画选择策略
		UAnimSequence* SelectedIdleAnim = EvaluateIdleAnimation(DialogueCharacterData);

		const float StartBlendInTime = 0.25f;
		const float EndBlendInTime = 0.25f;
		if (AnimSectionVirtualDatas.Num() > 0)
		{
			{
				const FAnimSectionVirtualData FirstVirtualData = AnimSectionVirtualDatas[0];
				if (FirstVirtualData.AnimRange.GetLowerBoundValue() > SequenceStartFrameNumber)
				{
					FAnimSectionVirtualData& IdleAnimSectionVirtualData = AnimSectionVirtualDatas.InsertDefaulted_GetRef(0);
					IdleAnimSectionVirtualData.AnimSequence = SelectedIdleAnim;
					IdleAnimSectionVirtualData.BlendInTime = StartBlendInTime;
					IdleAnimSectionVirtualData.BlendOutTime = FirstVirtualData.BlendInTime;
					IdleAnimSectionVirtualData.AnimRange = TRange<FFrameNumber>(SequenceStartFrameNumber
						, FirstVirtualData.AnimRange.GetLowerBoundValue() + GenAnimTrackUtils::SecondToFrameNumber(IdleAnimSectionVirtualData.BlendOutTime, FrameRate));
				}
			}

			const TArray<FAnimSectionVirtualData> CachedAnimSectionVirtualDatas = AnimTrackData.AnimSectionVirtualDatas;
			for (int32 Idx = 1; Idx < CachedAnimSectionVirtualDatas.Num(); ++Idx)
			{
				const FAnimSectionVirtualData& LeftVirtualData = CachedAnimSectionVirtualDatas[Idx - 1];
				const FAnimSectionVirtualData& RightVirtualData = CachedAnimSectionVirtualDatas[Idx];
				if (LeftVirtualData.AnimRange.GetUpperBoundValue() < RightVirtualData.AnimRange.GetLowerBoundValue())
				{
					FAnimSectionVirtualData& IdleAnimSectionVirtualData = AnimSectionVirtualDatas.InsertDefaulted_GetRef(0);
					IdleAnimSectionVirtualData.AnimSequence = SelectedIdleAnim;
					IdleAnimSectionVirtualData.BlendInTime = LeftVirtualData.BlendOutTime;
					IdleAnimSectionVirtualData.BlendOutTime = RightVirtualData.BlendInTime;
					IdleAnimSectionVirtualData.AnimRange = TRange<FFrameNumber>(LeftVirtualData.AnimRange.GetUpperBoundValue() - GenAnimTrackUtils::SecondToFrameNumber(IdleAnimSectionVirtualData.BlendInTime, FrameRate),
						RightVirtualData.AnimRange.GetLowerBoundValue() + GenAnimTrackUtils::SecondToFrameNumber(IdleAnimSectionVirtualData.BlendOutTime, FrameRate));
				}
			}

			{
				const FAnimSectionVirtualData LastVirtualData = AnimSectionVirtualDatas.Last();
				if (LastVirtualData.AnimRange.GetUpperBoundValue() < SequenceEndFrameNumber)
				{
					FAnimSectionVirtualData& IdleAnimSectionVirtualData = AnimSectionVirtualDatas.AddDefaulted_GetRef();
					IdleAnimSectionVirtualData.AnimSequence = SelectedIdleAnim;
					IdleAnimSectionVirtualData.BlendInTime = LastVirtualData.BlendInTime;
					IdleAnimSectionVirtualData.BlendOutTime = EndBlendInTime;
					IdleAnimSectionVirtualData.AnimRange = TRange<FFrameNumber>(LastVirtualData.AnimRange.GetUpperBoundValue() - GenAnimTrackUtils::SecondToFrameNumber(IdleAnimSectionVirtualData.BlendInTime, FrameRate),
						SequenceEndFrameNumber);
				}
			}
		}
		else
		{
			// 没动画就一直站着
			FAnimSectionVirtualData& IdleAnimSectionVirtualData = AnimSectionVirtualDatas.InsertDefaulted_GetRef(0);
			IdleAnimSectionVirtualData.AnimSequence = SelectedIdleAnim;
			IdleAnimSectionVirtualData.BlendInTime = StartBlendInTime;
			IdleAnimSectionVirtualData.BlendOutTime = EndBlendInTime;
			IdleAnimSectionVirtualData.AnimRange.SetLowerBoundValue(SequenceStartFrameNumber);
			IdleAnimSectionVirtualData.AnimRange.SetUpperBoundValue(SequenceEndFrameNumber);
		}
	}
	return AnimationTrackDataMap;
}

UAnimSequence* UAutoGenDialogueSequenceConfig::EvaluateTalkAnimation(const FGenDialogueCharacterData& GenDialogueCharacterData, const FGenDialogueData& GenDialogueData) const
{
	const TArray<UAnimSequence*>& TalkAnims = CastChecked<UAutoGenDialogueAnimSet>(GenDialogueCharacterData.DialogueAnimSet)->TalkAnims;
	UAnimSequence* SelectedTalkAnim = TalkAnims[FMath::RandHelper(TalkAnims.Num())];
	return SelectedTalkAnim;
}

UAnimSequence* UAutoGenDialogueSequenceConfig::EvaluateIdleAnimation(const FGenDialogueCharacterData& GenDialogueCharacterData) const
{
	const TArray<UAnimSequence*>& IdleAnims = CastChecked<UAutoGenDialogueAnimSet>(GenDialogueCharacterData.DialogueAnimSet)->IdleAnims;
	UAnimSequence* SelectedTalkAnim = IdleAnims[FMath::RandHelper(IdleAnims.Num())];
	return SelectedTalkAnim;
}

const FDialogueSentenceEditData& UAutoGenDialogueSequenceConfig::FGenDialogueData::GetDialogueSentenceEditData() const
{
	return PreviewDialogueSentenceSection->DialogueSentenceEditData;
}

#undef LOCTEXT_NAMESPACE
