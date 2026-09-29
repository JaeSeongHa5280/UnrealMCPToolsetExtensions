#include "HJSWidgetAnimToolset.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "MovieScene.h"
#include "MovieSceneDirectorBlueprintUtils.h"
#include "MovieSceneEventUtils.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneByteSection.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneEnumSection.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Tracks/MovieSceneEnumTrack.h"
#include "Sections/MovieSceneEventTriggerSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Styling/SlateColor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogHJSWidgetAnim, Log, All);

namespace
{
	const FName RenderTransformName(TEXT("RenderTransform"));

	/** SetTransformChannel 이 받는 채널 이름 — 순서는 에디터 Transform 트랙과 같다 */
	const TCHAR* const TransformChannelNames[] = { TEXT("TranslationX"), TEXT("TranslationY"), TEXT("Angle"), TEXT("ScaleX"), TEXT("ScaleY"), TEXT("ShearX"), TEXT("ShearY") };

	FMovieSceneFloatChannel* TransformChannel(UMovieScene2DTransformSection& Section, int32 Index)
	{
		switch (Index)
		{
		case 0:		return &Section.Translation[0];
		case 1:		return &Section.Translation[1];
		case 2:		return &Section.Rotation;
		case 3:		return &Section.Scale[0];
		case 4:		return &Section.Scale[1];
		case 5:		return &Section.Shear[0];
		case 6:		return &Section.Shear[1];
		default:	return nullptr;
		}
	}

	FString Fail(const FString& Reason)
	{
		UE_LOG(LogHJSWidgetAnim, Warning, TEXT("%s"), *Reason);
		return TEXT("실패: ") + Reason;
	}

	UWidgetAnimation* FindAnimation(const UWidgetBlueprint& WidgetBlueprint, FName AnimationName)
	{
		for (UWidgetAnimation* Animation : WidgetBlueprint.Animations)
		{
			if (Animation && Animation->GetFName() == AnimationName)
			{
				return Animation;
			}
		}
		return nullptr;
	}

	/** 슬롯이 아니라 위젯 자체에 붙은 바인딩 — 에디터가 위젯 트랙을 추가할 때 만드는 모양(UWidgetAnimation::BindPossessableObject) */
	FGuid FindWidgetBinding(const UWidgetAnimation& Animation, FName WidgetName)
	{
		for (const FWidgetAnimationBinding& Binding : Animation.AnimationBindings)
		{
			if (Binding.WidgetName == WidgetName && Binding.SlotWidgetName.IsNone() && !Binding.bIsRootWidget)
			{
				return Binding.AnimationGuid;
			}
		}
		return FGuid();
	}

	template <typename TrackType>
	TrackType* FindBoundTrack(const UMovieScene& MovieScene, const FGuid& BindingGuid, FName PropertyName)
	{
		if (const FMovieSceneBinding* Binding = MovieScene.FindBinding(BindingGuid))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				TrackType* Typed = Cast<TrackType>(Track);
				if (Typed && Typed->GetPropertyName() == PropertyName)
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}

	/** 트랙의 첫 섹션 — 없으면 만들고, 범위는 전체(에디터에서 키를 찍을 때와 같다) */
	template <typename SectionType>
	SectionType* FindOrAddSection(UMovieSceneTrack& Track)
	{
		SectionType* Section = Track.GetAllSections().IsEmpty() ? nullptr : Cast<SectionType>(Track.GetAllSections()[0]);
		if (!Section)
		{
			Section = CastChecked<SectionType>(Track.CreateNewSection());
			Track.AddSection(*Section);
		}
		Section->Modify();
		Section->SetRange(TRange<FFrameNumber>::All());
		return Section;
	}

	const TCHAR* InterpName(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Linear:	return TEXT("linear");
		case RCIM_Constant:	return TEXT("constant");
		case RCIM_Cubic:	return TEXT("cubic");
		default:			return TEXT("?");
		}
	}

	FString CheckFloatKeys(const TArray<FHJSAnimFloatKey>& Keys)
	{
		if (Keys.IsEmpty())
		{
			return TEXT("키가 없다");
		}
		for (const FHJSAnimFloatKey& Key : Keys)
		{
			if (!FMath::IsFinite(Key.Time) || Key.Time < 0.f || !FMath::IsFinite(Key.Value))
			{
				return FString::Printf(TEXT("키 (%f, %f) — 시각은 0 이상, 값은 유한해야 한다"), Key.Time, Key.Value);
			}
		}
		return FString();
	}

	FString CheckColorKeys(const TArray<FHJSAnimColorKey>& Keys)
	{
		if (Keys.IsEmpty())
		{
			return TEXT("키가 없다");
		}
		for (const FHJSAnimColorKey& Key : Keys)
		{
			const FLinearColor& C = Key.Color;
			if (!FMath::IsFinite(Key.Time) || Key.Time < 0.f || !FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B) || !FMath::IsFinite(C.A))
			{
				return FString::Printf(TEXT("키 (%f, %s) — 시각은 0 이상, 색은 유한해야 한다"), Key.Time, *C.ToString());
			}
		}
		return FString();
	}

	/** 계단식 키(목록 값 · bool)의 시각 검사 */
	template <typename KeyType>
	FString CheckStepKeyTimes(const TArray<KeyType>& Keys)
	{
		if (Keys.IsEmpty())
		{
			return TEXT("키가 없다");
		}
		for (const KeyType& Key : Keys)
		{
			if (!FMath::IsFinite(Key.Time) || Key.Time < 0.f)
			{
				return FString::Printf(TEXT("키 시각 %f — 0 이상이어야 한다"), Key.Time);
			}
		}
		return FString();
	}

	FString EnumValueNames(const UEnum& Enum)
	{
		FString Names;
		for (int32 Index = 0; Index < Enum.NumEnums() - 1; ++Index)	// 마지막은 _MAX
		{
			Names += (Index == 0 ? TEXT("") : TEXT(" · ")) + Enum.GetNameStringByIndex(Index);
		}
		return Names;
	}

	/** 키를 넣는 함수들의 공통 검사(WBP · 클립 이름 · 위젯) — 문제가 있으면 이유, 없으면 빈 문자열. 검사를 통과하기 전에는 아무것도 고치지 않는다 */
	FString CheckTarget(const UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, UWidget*& OutWidget)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return TEXT("WidgetBlueprint 가 없다");
		}
		if (AnimationName.IsEmpty() || AnimationName.Contains(TEXT(" ")))
		{
			return FString::Printf(TEXT("클립 이름 '%s' 을 변수 이름으로 쓸 수 없다"), *AnimationName);
		}

		OutWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (!OutWidget)
		{
			return FString::Printf(TEXT("%s 에 위젯 '%s' 이 없다"), *WidgetBlueprint->GetName(), *WidgetName);
		}

		const FName AnimFName(*AnimationName);
		if (const UWidgetAnimation* Existing = FindAnimation(*WidgetBlueprint, AnimFName))
		{
			return Existing->GetMovieScene() ? FString() : FString::Printf(TEXT("클립 '%s' 에 무비신이 없다"), *AnimationName);
		}
		// 같은 이름의 다른 것이 있으면 NewObject 가 멈추거나 컴파일에서 변수 이름이 겹친다
		if (StaticFindObjectFast(nullptr, const_cast<UWidgetBlueprint*>(WidgetBlueprint), AnimFName) || WidgetBlueprint->WidgetTree->FindWidget(AnimFName))
		{
			return FString::Printf(TEXT("%s 에 이미 '%s' 라는 이름이 있다"), *WidgetBlueprint->GetName(), *AnimationName);
		}
		return FString();
	}

	struct FAnimTarget
	{
		UWidgetAnimation* Animation = nullptr;
		UMovieScene* MovieScene = nullptr;
		FGuid BindingGuid;
		bool bNewAnimation = false;
	};

	/** 클립(없으면 만든다 — 에디터 「+Animation」 과 같은 순서, AnimationTabSummoner.cpp) + 위젯 바인딩(없으면 만든다). 트랜잭션 안에서 부른다 */
	FAnimTarget FindOrAddAnimationBinding(UWidgetBlueprint& WidgetBlueprint, const FString& AnimationName, UWidget& Widget)
	{
		FAnimTarget Target;
		const FName AnimFName(*AnimationName);

		Target.Animation = FindAnimation(WidgetBlueprint, AnimFName);
		Target.bNewAnimation = Target.Animation == nullptr;
		if (Target.bNewAnimation)
		{
			Target.Animation = NewObject<UWidgetAnimation>(&WidgetBlueprint, AnimFName, RF_Transactional);
			Target.Animation->SetDisplayLabel(AnimationName);
			Target.Animation->MovieScene = NewObject<UMovieScene>(Target.Animation, AnimFName, RF_Transactional);
			Target.Animation->MovieScene->SetDisplayRate(FFrameRate(GetDefault<UUMGEditorProjectSettings>()->DefaultWidgetAnimationFrameRate, 1));

			WidgetBlueprint.Animations.Add(Target.Animation);
			WidgetBlueprint.OnVariableAdded(AnimFName);
		}
		else
		{
			Target.Animation->Modify();
			Target.Animation->GetMovieScene()->Modify();
		}
		Target.MovieScene = Target.Animation->GetMovieScene();

		// 위젯 트리의 이름으로 잇는다(런타임 위젯도 같은 이름)
		Target.BindingGuid = FindWidgetBinding(*Target.Animation, Widget.GetFName());
		if (!Target.BindingGuid.IsValid())
		{
			Target.BindingGuid = Target.MovieScene->AddPossessable(Widget.GetName(), Widget.GetClass());

			FWidgetAnimationBinding NewBinding;
			NewBinding.AnimationGuid = Target.BindingGuid;
			NewBinding.WidgetName = Widget.GetFName();
			NewBinding.bIsRootWidget = false;
			Target.Animation->AnimationBindings.Add(NewBinding);
		}
		return Target;
	}

	template <typename KeyType>
	TArray<KeyType> SortedByTime(const TArray<KeyType>& Keys)
	{
		TArray<KeyType> Sorted = Keys;
		Sorted.Sort([](const KeyType& A, const KeyType& B) { return A.Time < B.Time; });
		return Sorted;
	}

	/** 색 속성인가 — 에디터 색 트랙이 받는 세 가지 */
	bool IsColorProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && (StructProperty->Struct == TBaseStructure<FLinearColor>::Get()
			|| StructProperty->Struct == TBaseStructure<FColor>::Get()
			|| StructProperty->Struct == FSlateColor::StaticStruct());
	}

	/** 키를 갈아 끼운다 — 에디터 기본 보간(Sequencer 설정 KeyInterpolation = Auto → cubic auto) */
	void ReplaceKeys(FMovieSceneFloatChannel& Channel, const TArray<FHJSAnimFloatKey>& SortedKeys, FFrameRate TickResolution)
	{
		Channel.Reset();
		for (const FHJSAnimFloatKey& Key : SortedKeys)
		{
			Channel.AddCubicKey((Key.Time * TickResolution).RoundToFrame(), Key.Value);
		}
		Channel.AutoSetTangents();
	}

	/** 길이 — 가장 늦은 키까지 늘린다. 다른 트랙이 더 길면 줄이지 않는다. 끝 시각(초)을 돌려준다 */
	double ExtendPlaybackRange(UMovieScene& MovieScene, bool bNewAnimation, double LastKeySeconds)
	{
		const FFrameRate TickResolution = MovieScene.GetTickResolution();
		FFrameNumber EndFrame = (LastKeySeconds * TickResolution).RoundToFrame();
		const TRange<FFrameNumber> OldRange = MovieScene.GetPlaybackRange();
		if (!bNewAnimation && OldRange.HasUpperBound())
		{
			EndFrame = FMath::Max(EndFrame, OldRange.GetUpperBoundValue() - 1);
		}
		MovieScene.SetPlaybackRange(TRange<FFrameNumber>(0, EndFrame + 1));

		const double EndSeconds = TickResolution.AsSeconds(EndFrame);
		MovieScene.GetEditorData().WorkStart = 0.0;
		MovieScene.GetEditorData().WorkEnd = EndSeconds;
		return EndSeconds;
	}

	FString KeysToText(const TArray<FHJSAnimFloatKey>& SortedKeys)
	{
		FString Text;
		for (const FHJSAnimFloatKey& Key : SortedKeys)
		{
			Text += FString::Printf(TEXT("%s%.3fs=%.3f"), Text.IsEmpty() ? TEXT("") : TEXT(" · "), Key.Time, Key.Value);
		}
		return Text;
	}

	FString ChannelToText(const FMovieSceneFloatChannel& Channel, FFrameRate TickResolution)
	{
		const TMovieSceneChannelData<const FMovieSceneFloatValue> Data = Channel.GetData();
		const TArrayView<const FFrameNumber> Times = Data.GetTimes();
		const TArrayView<const FMovieSceneFloatValue> Values = Data.GetValues();

		FString Text = FString::Printf(TEXT("키 %d: "), Times.Num());
		for (int32 Index = 0; Index < Times.Num(); ++Index)
		{
			Text += FString::Printf(TEXT("%s%.3fs=%.3f(%s)"), Index == 0 ? TEXT("") : TEXT(" · "),
				TickResolution.AsSeconds(Times[Index]), Values[Index].Value, InterpName(Values[Index].InterpMode));
		}
		return Text;
	}

	// 열린 WBP 에디터(시퀀서가 이 클립을 보는 중) 밑에서 고치고 컴파일하면 엔진 ensure 가 난다(MovieSceneEntityGroupingSystem, 2026-09-29 실측).
	// 그래서 쓰기 전에 그 WBP 의 창을 닫는다. 닫았으면 요약 끝에 알린다.
	FString CloseWidgetEditors(UWidgetBlueprint& WidgetBlueprint)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!AssetEditors || AssetEditors->FindEditorsForAsset(&WidgetBlueprint).IsEmpty())
		{
			return FString();
		}
		AssetEditors->CloseAllEditorsForAsset(&WidgetBlueprint);
		return TEXT(" · 열려 있던 WBP 창을 닫음(다시 열기는 EditorAppToolset.OpenEditorForAsset)");
	}
}

FString UHJSWidgetAnimToolset::SetFloatTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimFloatKey>& Keys)
{
	UWidget* Widget = nullptr;
	FString Problem = CheckFloatKeys(Keys);
	if (Problem.IsEmpty())
	{
		Problem = CheckTarget(WidgetBlueprint, AnimationName, WidgetName, Widget);
	}
	if (!Problem.IsEmpty())
	{
		return Fail(Problem);
	}
	const FName PropertyFName(*PropertyName);
	if (!CastField<FFloatProperty>(Widget->GetClass()->FindPropertyByName(PropertyFName)))
	{
		return Fail(FString::Printf(TEXT("%s(%s) 에 float 속성 '%s' 이 없다"), *WidgetName, *Widget->GetClass()->GetName(), *PropertyName));
	}

	const TArray<FHJSAnimFloatKey> SortedKeys = SortedByTime(Keys);

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Set Float Track"));
	WidgetBlueprint->Modify();
	const FAnimTarget Target = FindOrAddAnimationBinding(*WidgetBlueprint, AnimationName, *Widget);

	UMovieSceneFloatTrack* Track = FindBoundTrack<UMovieSceneFloatTrack>(*Target.MovieScene, Target.BindingGuid, PropertyFName);
	if (!Track)
	{
		Track = Target.MovieScene->AddTrack<UMovieSceneFloatTrack>(Target.BindingGuid);
		Track->SetPropertyNameAndPath(PropertyFName, PropertyName);
	}
	Track->Modify();

	UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(*Track);
	ReplaceKeys(Section->GetChannel(), SortedKeys, Target.MovieScene->GetTickResolution());
	const double EndSeconds = ExtendPlaybackRange(*Target.MovieScene, Target.bNewAnimation, SortedKeys.Last().Time);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	return FString::Printf(TEXT("%s %s · %s.%s 키 %d개(%s) · 길이 %.3fs — 컴파일 · 저장은 따로"),
		Target.bNewAnimation ? TEXT("새 클립") : TEXT("클립"), *AnimationName, *WidgetName, *PropertyName, SortedKeys.Num(), *KeysToText(SortedKeys), EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::SetColorTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimColorKey>& Keys)
{
	UWidget* Widget = nullptr;
	FString Problem = CheckColorKeys(Keys);
	if (Problem.IsEmpty())
	{
		Problem = CheckTarget(WidgetBlueprint, AnimationName, WidgetName, Widget);
	}
	if (!Problem.IsEmpty())
	{
		return Fail(Problem);
	}
	const FName PropertyFName(*PropertyName);
	if (!IsColorProperty(Widget->GetClass()->FindPropertyByName(PropertyFName)))
	{
		return Fail(FString::Printf(TEXT("%s(%s) 에 색 속성(FLinearColor · FSlateColor · FColor) '%s' 이 없다"), *WidgetName, *Widget->GetClass()->GetName(), *PropertyName));
	}

	const TArray<FHJSAnimColorKey> SortedKeys = SortedByTime(Keys);

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Set Color Track"));
	WidgetBlueprint->Modify();
	const FAnimTarget Target = FindOrAddAnimationBinding(*WidgetBlueprint, AnimationName, *Widget);

	// 에디터가 색 속성에 트랙을 추가할 때와 같은 트랙 — 속성 종류(FLinearColor · FSlateColor · FColor)는 실행 때 속성에서 읽는다
	UMovieSceneColorTrack* Track = FindBoundTrack<UMovieSceneColorTrack>(*Target.MovieScene, Target.BindingGuid, PropertyFName);
	if (!Track)
	{
		Track = Target.MovieScene->AddTrack<UMovieSceneColorTrack>(Target.BindingGuid);
		Track->SetPropertyNameAndPath(PropertyFName, PropertyName);
	}
	Track->Modify();

	// 채널 넷에 같은 시각으로 — 키 하나 = 색 하나
	UMovieSceneColorSection* Section = FindOrAddSection<UMovieSceneColorSection>(*Track);
	const FFrameRate TickResolution = Target.MovieScene->GetTickResolution();
	FMovieSceneFloatChannel* Channels[] = { &Section->GetRedChannel(), &Section->GetGreenChannel(), &Section->GetBlueChannel(), &Section->GetAlphaChannel() };
	for (int32 ChannelIndex = 0; ChannelIndex < UE_ARRAY_COUNT(Channels); ++ChannelIndex)
	{
		TArray<FHJSAnimFloatKey> ChannelKeys;
		for (const FHJSAnimColorKey& Key : SortedKeys)
		{
			const FLinearColor& C = Key.Color;
			const float Value = ChannelIndex == 0 ? C.R : ChannelIndex == 1 ? C.G : ChannelIndex == 2 ? C.B : C.A;
			ChannelKeys.Add({ Key.Time, Value });
		}
		ReplaceKeys(*Channels[ChannelIndex], ChannelKeys, TickResolution);
	}
	const double EndSeconds = ExtendPlaybackRange(*Target.MovieScene, Target.bNewAnimation, SortedKeys.Last().Time);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	FString KeyText;
	for (const FHJSAnimColorKey& Key : SortedKeys)
	{
		KeyText += FString::Printf(TEXT("%s%.3fs=(%.3f,%.3f,%.3f,%.3f)"), KeyText.IsEmpty() ? TEXT("") : TEXT(" · "), Key.Time, Key.Color.R, Key.Color.G, Key.Color.B, Key.Color.A);
	}
	return FString::Printf(TEXT("%s %s · %s.%s 색 키 %d개(%s) · 길이 %.3fs — 컴파일 · 저장은 따로"),
		Target.bNewAnimation ? TEXT("새 클립") : TEXT("클립"), *AnimationName, *WidgetName, *PropertyName, SortedKeys.Num(), *KeyText, EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::SetEnumTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimEnumKey>& Keys)
{
	UWidget* Widget = nullptr;
	FString Problem = CheckStepKeyTimes(Keys);
	if (Problem.IsEmpty())
	{
		Problem = CheckTarget(WidgetBlueprint, AnimationName, WidgetName, Widget);
	}
	if (!Problem.IsEmpty())
	{
		return Fail(Problem);
	}
	// enum class(FEnumProperty — Visibility 가 이것) · TEnumAsByte(FByteProperty) 둘 다 — 에디터와 같이 바이트 트랙에 enum 을 붙인다
	const FName PropertyFName(*PropertyName);
	const FProperty* Property = Widget->GetClass()->FindPropertyByName(PropertyFName);
	UEnum* Enum = nullptr;
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Enum = EnumProperty->GetEnum();
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		Enum = ByteProperty->Enum;
	}
	if (!Enum)
	{
		return Fail(FString::Printf(TEXT("%s(%s) 에 enum 속성 '%s' 이 없다"), *WidgetName, *Widget->GetClass()->GetName(), *PropertyName));
	}
	TArray<TPair<float, uint8>> SortedValues;
	for (const FHJSAnimEnumKey& Key : SortedByTime(Keys))
	{
		const int64 Value = Enum->GetValueByNameString(Key.Value);
		if (Value == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("'%s' 은 %s 의 값이 아니다 — %s"), *Key.Value, *Enum->GetName(), *EnumValueNames(*Enum)));
		}
		SortedValues.Emplace(Key.Time, static_cast<uint8>(Value));
	}

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Set Enum Track"));
	WidgetBlueprint->Modify();
	const FAnimTarget Target = FindOrAddAnimationBinding(*WidgetBlueprint, AnimationName, *Widget);

	// UMovieSceneEnumTrack 은 쓰지 않는다 — 에디터에 그 트랙의 편집기가 없어 WBP 를 열면 ensure 뒤 크래시(2026-09-29 실측).
	// 이 도구의 옛 버전이 만든 것이 있으면 지우고 바이트 트랙으로 바꾼다
	if (UMovieSceneEnumTrack* OldTrack = FindBoundTrack<UMovieSceneEnumTrack>(*Target.MovieScene, Target.BindingGuid, PropertyFName))
	{
		Target.MovieScene->RemoveTrack(*OldTrack);
	}

	// 에디터가 enum 속성에 트랙을 추가할 때와 같은 트랙(FBytePropertyTrackEditor) — 값 이름을 보이려면 트랙이 enum 을 안다
	UMovieSceneByteTrack* Track = FindBoundTrack<UMovieSceneByteTrack>(*Target.MovieScene, Target.BindingGuid, PropertyFName);
	if (!Track)
	{
		Track = Target.MovieScene->AddTrack<UMovieSceneByteTrack>(Target.BindingGuid);
		Track->SetPropertyNameAndPath(PropertyFName, PropertyName);
	}
	Track->Modify();
	Track->SetEnum(Enum);

	UMovieSceneByteSection* Section = FindOrAddSection<UMovieSceneByteSection>(*Track);
	const FFrameRate TickResolution = Target.MovieScene->GetTickResolution();
	Section->ByteCurve.Reset();
	TMovieSceneChannelData<uint8> Data = Section->ByteCurve.GetData();
	FString KeyText;
	for (const TPair<float, uint8>& Key : SortedValues)
	{
		Data.AddKey((Key.Key * TickResolution).RoundToFrame(), Key.Value);
		KeyText += FString::Printf(TEXT("%s%.3fs=%s"), KeyText.IsEmpty() ? TEXT("") : TEXT(" · "), Key.Key, *Enum->GetNameStringByValue(Key.Value));
	}
	const double EndSeconds = ExtendPlaybackRange(*Target.MovieScene, Target.bNewAnimation, SortedValues.Last().Key);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	return FString::Printf(TEXT("%s %s · %s.%s 키 %d개(%s) · 길이 %.3fs — 컴파일 · 저장은 따로"),
		Target.bNewAnimation ? TEXT("새 클립") : TEXT("클립"), *AnimationName, *WidgetName, *PropertyName, SortedValues.Num(), *KeyText, EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::SetBoolTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimBoolKey>& Keys)
{
	UWidget* Widget = nullptr;
	FString Problem = CheckStepKeyTimes(Keys);
	if (Problem.IsEmpty())
	{
		Problem = CheckTarget(WidgetBlueprint, AnimationName, WidgetName, Widget);
	}
	if (!Problem.IsEmpty())
	{
		return Fail(Problem);
	}
	const FName PropertyFName(*PropertyName);
	if (!CastField<FBoolProperty>(Widget->GetClass()->FindPropertyByName(PropertyFName)))
	{
		return Fail(FString::Printf(TEXT("%s(%s) 에 bool 속성 '%s' 이 없다"), *WidgetName, *Widget->GetClass()->GetName(), *PropertyName));
	}

	const TArray<FHJSAnimBoolKey> SortedKeys = SortedByTime(Keys);

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Set Bool Track"));
	WidgetBlueprint->Modify();
	const FAnimTarget Target = FindOrAddAnimationBinding(*WidgetBlueprint, AnimationName, *Widget);

	UMovieSceneBoolTrack* Track = FindBoundTrack<UMovieSceneBoolTrack>(*Target.MovieScene, Target.BindingGuid, PropertyFName);
	if (!Track)
	{
		Track = Target.MovieScene->AddTrack<UMovieSceneBoolTrack>(Target.BindingGuid);
		Track->SetPropertyNameAndPath(PropertyFName, PropertyName);
	}
	Track->Modify();

	UMovieSceneBoolSection* Section = FindOrAddSection<UMovieSceneBoolSection>(*Track);
	const FFrameRate TickResolution = Target.MovieScene->GetTickResolution();
	Section->GetChannel().Reset();
	TMovieSceneChannelData<bool> Data = Section->GetChannel().GetData();
	FString KeyText;
	for (const FHJSAnimBoolKey& Key : SortedKeys)
	{
		Data.AddKey((Key.Time * TickResolution).RoundToFrame(), Key.bValue);
		KeyText += FString::Printf(TEXT("%s%.3fs=%s"), KeyText.IsEmpty() ? TEXT("") : TEXT(" · "), Key.Time, Key.bValue ? TEXT("true") : TEXT("false"));
	}
	const double EndSeconds = ExtendPlaybackRange(*Target.MovieScene, Target.bNewAnimation, SortedKeys.Last().Time);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	return FString::Printf(TEXT("%s %s · %s.%s 키 %d개(%s) · 길이 %.3fs — 컴파일 · 저장은 따로"),
		Target.bNewAnimation ? TEXT("새 클립") : TEXT("클립"), *AnimationName, *WidgetName, *PropertyName, SortedKeys.Num(), *KeyText, EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::SetTransformChannel(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& Channel, const TArray<FHJSAnimFloatKey>& Keys)
{
	UWidget* Widget = nullptr;
	FString Problem = CheckFloatKeys(Keys);
	if (Problem.IsEmpty())
	{
		Problem = CheckTarget(WidgetBlueprint, AnimationName, WidgetName, Widget);
	}
	if (!Problem.IsEmpty())
	{
		return Fail(Problem);
	}
	int32 ChannelIndex = INDEX_NONE;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TransformChannelNames); ++Index)
	{
		if (Channel == TransformChannelNames[Index])
		{
			ChannelIndex = Index;
		}
	}
	if (ChannelIndex == INDEX_NONE)
	{
		return Fail(FString::Printf(TEXT("채널 '%s' 이 없다 — TranslationX · TranslationY · Angle · ScaleX · ScaleY · ShearX · ShearY"), *Channel));
	}

	const TArray<FHJSAnimFloatKey> SortedKeys = SortedByTime(Keys);

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Set Transform Channel"));
	WidgetBlueprint->Modify();
	const FAnimTarget Target = FindOrAddAnimationBinding(*WidgetBlueprint, AnimationName, *Widget);

	// 에디터 「+Track → Transform」 과 같은 트랙 — 속성은 RenderTransform
	UMovieScene2DTransformTrack* Track = FindBoundTrack<UMovieScene2DTransformTrack>(*Target.MovieScene, Target.BindingGuid, RenderTransformName);
	if (!Track)
	{
		Track = Target.MovieScene->AddTrack<UMovieScene2DTransformTrack>(Target.BindingGuid);
		Track->SetPropertyNameAndPath(RenderTransformName, RenderTransformName.ToString());
	}
	Track->Modify();

	UMovieScene2DTransformSection* Section = FindOrAddSection<UMovieScene2DTransformSection>(*Track);
	ReplaceKeys(*TransformChannel(*Section, ChannelIndex), SortedKeys, Target.MovieScene->GetTickResolution());
	const double EndSeconds = ExtendPlaybackRange(*Target.MovieScene, Target.bNewAnimation, SortedKeys.Last().Time);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	return FString::Printf(TEXT("%s %s · %s.RenderTransform.%s 키 %d개(%s) · 길이 %.3fs — 컴파일 · 저장은 따로"),
		Target.bNewAnimation ? TEXT("새 클립") : TEXT("클립"), *AnimationName, *WidgetName, *Channel, SortedKeys.Num(), *KeysToText(SortedKeys), EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::AddEventKey(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, float Time, const FString& EventName)
{
	if (!WidgetBlueprint)
	{
		return Fail(TEXT("WidgetBlueprint 가 없다"));
	}
	UWidgetAnimation* Animation = FindAnimation(*WidgetBlueprint, FName(*AnimationName));
	if (!Animation || !Animation->GetMovieScene())
	{
		return Fail(FString::Printf(TEXT("클립 '%s' 이 없다 — SetFloatTrack · SetTransformChannel 로 먼저 만든다"), *AnimationName));
	}
	if (!FMath::IsFinite(Time) || Time < 0.f)
	{
		return Fail(FString::Printf(TEXT("시각 %f — 0 이상이어야 한다"), Time));
	}
	if (FKismetNameValidator(WidgetBlueprint).IsValid(EventName) != EValidatorResult::Ok)
	{
		return Fail(FString::Printf(TEXT("이벤트 이름 '%s' 을 쓸 수 없다(비었거나 · 이미 있거나 · 쓸 수 없는 글자)"), *EventName));
	}

	const FString ClosedNote = CloseWidgetEditors(*WidgetBlueprint);
	const FScopedTransaction Transaction(INVTEXT("HJS: Add Event Key"));
	WidgetBlueprint->Modify();
	Animation->Modify();
	UMovieScene* MovieScene = Animation->GetMovieScene();
	MovieScene->Modify();

	// 클립 전체에 걸린 이벤트 트랙 하나(에디터 「+Track → Event」) — 키가 WBP 자신의 이벤트를 부른다
	UMovieSceneEventTrack* Track = Cast<UMovieSceneEventTrack>(MovieScene->FindTrack(UMovieSceneEventTrack::StaticClass()));
	if (!Track)
	{
		Track = CastChecked<UMovieSceneEventTrack>(MovieScene->AddTrack(UMovieSceneEventTrack::StaticClass()));
	}
	Track->Modify();
	UMovieSceneEventTriggerSection* Section = FindOrAddSection<UMovieSceneEventTriggerSection>(*Track);

	TMovieSceneChannelData<FMovieSceneEvent> Data = Section->EventChannel.GetData();
	const int32 KeyIndex = Data.AddKey((Time * MovieScene->GetTickResolution()).RoundToFrame(), FMovieSceneEvent());
	FMovieSceneEvent* Event = &Data.GetValues()[KeyIndex];

	// 엔진 FMovieSceneEventUtils::BindNewUserFacingEvent 와 같은 순서 — 노드 이름만 우리가 정한다
	FMovieSceneEventUtils::BindEventSectionToBlueprint(Section, WidgetBlueprint);
	FMovieSceneDirectorBlueprintEndpointDefinition Definition = FMovieSceneEventUtils::GenerateEventDefinition(Track);
	Definition.EndpointName = EventName;
	UK2Node_CustomEvent* EventNode = FMovieSceneDirectorBlueprintUtils::CreateEventEndpoint(WidgetBlueprint, Definition);
	if (!EventNode)
	{
		return Fail(TEXT("커스텀 이벤트 노드를 못 만들었다 — 키는 들어갔다, 에디터에서 Ctrl+Z"));
	}
	FMovieSceneEventUtils::SetEndpoint(Event, Section, EventNode, FMovieSceneDirectorBlueprintUtils::FindCallTargetPin(EventNode, Definition.PossibleCallTargetClass));

	const double EndSeconds = ExtendPlaybackRange(*MovieScene, false, Time);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	return FString::Printf(TEXT("클립 %s · 이벤트 키 %.3fs → 커스텀 이벤트 '%s'(그래프 %s) · 길이 %.3fs — 뒤에 붙일 노드는 BlueprintTools 로, 컴파일 · 저장은 따로"),
		*AnimationName, Time, *EventNode->CustomFunctionName.ToString(), *EventNode->GetGraph()->GetName(), EndSeconds) + ClosedNote;
}

FString UHJSWidgetAnimToolset::DescribeAnimations(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint)
	{
		return Fail(TEXT("WidgetBlueprint 가 없다"));
	}

	FString Out = FString::Printf(TEXT("# %s — 애니메이션 %d개\n"), *WidgetBlueprint->GetName(), WidgetBlueprint->Animations.Num());

	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		UMovieScene* MovieScene = Animation ? Animation->GetMovieScene() : nullptr;
		if (!MovieScene)
		{
			continue;
		}

		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
		const double Start = Range.HasLowerBound() ? TickResolution.AsSeconds(Range.GetLowerBoundValue()) : 0.0;
		const double End = Range.HasUpperBound() ? TickResolution.AsSeconds(Range.GetUpperBoundValue() - 1) : Start;
		Out += FString::Printf(TEXT("%s  길이 %.3fs (%.3f ~ %.3f) · 표시 %.0ffps\n"),
			*Animation->GetName(), End - Start, Start, End, MovieScene->GetDisplayRate().AsDecimal());

		// 클립 전체에 걸린 트랙 — 이벤트 키와 그 키가 부르는 이벤트 노드
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(Track);
			if (!EventTrack)
			{
				Out += FString::Printf(TEXT("  (클립 트랙) %s\n"), *Track->GetClass()->GetName());
				continue;
			}
			for (UMovieSceneSection* Section : EventTrack->GetAllSections())
			{
				UMovieSceneEventTriggerSection* TriggerSection = Cast<UMovieSceneEventTriggerSection>(Section);
				if (!TriggerSection)
				{
					continue;
				}
				TMovieSceneChannelData<FMovieSceneEvent> Data = TriggerSection->EventChannel.GetData();
				const TArrayView<const FFrameNumber> Times = Data.GetTimes();
				const TArrayView<FMovieSceneEvent> Events = Data.GetValues();

				FString KeyText;
				for (int32 Index = 0; Index < Times.Num(); ++Index)
				{
					const UK2Node_CustomEvent* Endpoint = Cast<UK2Node_CustomEvent>(FMovieSceneEventUtils::FindEndpoint(&Events[Index], TriggerSection, WidgetBlueprint));
					KeyText += FString::Printf(TEXT("%s%.3fs→%s"), Index == 0 ? TEXT("") : TEXT(" · "),
						TickResolution.AsSeconds(Times[Index]), Endpoint ? *Endpoint->CustomFunctionName.ToString() : TEXT("(연결 없음)"));
				}
				Out += FString::Printf(TEXT("  (이벤트) 키 %d: %s\n"), Times.Num(), *KeyText);
			}
		}

		for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
		{
			FString Target = Binding.bIsRootWidget ? TEXT("(위젯 자신)") : Binding.WidgetName.ToString();
			if (!Binding.SlotWidgetName.IsNone())
			{
				Target += FString::Printf(TEXT(" 의 슬롯 %s"), *Binding.SlotWidgetName.ToString());
			}
			Out += FString::Printf(TEXT("  %s\n"), *Target);

			const FMovieSceneBinding* SceneBinding = MovieScene->FindBinding(Binding.AnimationGuid);
			if (!SceneBinding || SceneBinding->GetTracks().IsEmpty())
			{
				Out += TEXT("    (트랙 없음)\n");
				continue;
			}

			for (UMovieSceneTrack* Track : SceneBinding->GetTracks())
			{
				if (const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track))
				{
					for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
					{
						if (const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
						{
							Out += FString::Printf(TEXT("    %s  float  %s\n"), *FloatTrack->GetPropertyName().ToString(), *ChannelToText(FloatSection->GetChannel(), TickResolution));
						}
					}
				}
				else if (Cast<UMovieSceneEnumTrack>(Track))
				{
					// 에디터에 편집기가 없는 트랙 — 이 WBP 를 에디터에서 열면 크래시한다. SetEnumTrack 을 다시 부르면 바이트 트랙으로 바뀐다
					Out += FString::Printf(TEXT("    %s  !! EnumTrack — 에디터가 못 여는 트랙, SetEnumTrack 으로 다시 넣을 것\n"), *CastChecked<UMovieScenePropertyTrack>(Track)->GetPropertyName().ToString());
				}
				else if (const UMovieSceneByteTrack* ByteTrack = Cast<UMovieSceneByteTrack>(Track))
				{
					const UEnum* Enum = ByteTrack->GetEnum();
					for (UMovieSceneSection* Section : ByteTrack->GetAllSections())
					{
						const UMovieSceneByteSection* ByteSection = Cast<UMovieSceneByteSection>(Section);
						if (!ByteSection)
						{
							continue;
						}
						const TMovieSceneChannelData<const uint8> Data = ByteSection->ByteCurve.GetData();
						FString KeyText;
						for (int32 Index = 0; Index < Data.GetTimes().Num(); ++Index)
						{
							const uint8 Value = Data.GetValues()[Index];
							KeyText += FString::Printf(TEXT("%s%.3fs=%s"), Index == 0 ? TEXT("") : TEXT(" · "), TickResolution.AsSeconds(Data.GetTimes()[Index]),
								Enum ? *Enum->GetNameStringByValue(Value) : *FString::FromInt(Value));
						}
						Out += FString::Printf(TEXT("    %s  %s  키 %d: %s\n"), *ByteTrack->GetPropertyName().ToString(), Enum ? TEXT("enum") : TEXT("byte"), Data.GetTimes().Num(), *KeyText);
					}
				}
				else if (const UMovieSceneBoolTrack* BoolTrack = Cast<UMovieSceneBoolTrack>(Track))
				{
					for (UMovieSceneSection* Section : BoolTrack->GetAllSections())
					{
						const UMovieSceneBoolSection* BoolSection = Cast<UMovieSceneBoolSection>(Section);
						if (!BoolSection)
						{
							continue;
						}
						const TMovieSceneChannelData<const bool> Data = BoolSection->GetChannel().GetData();
						FString KeyText;
						for (int32 Index = 0; Index < Data.GetTimes().Num(); ++Index)
						{
							KeyText += FString::Printf(TEXT("%s%.3fs=%s"), Index == 0 ? TEXT("") : TEXT(" · "), TickResolution.AsSeconds(Data.GetTimes()[Index]),
								Data.GetValues()[Index] ? TEXT("true") : TEXT("false"));
						}
						Out += FString::Printf(TEXT("    %s  bool  키 %d: %s\n"), *BoolTrack->GetPropertyName().ToString(), Data.GetTimes().Num(), *KeyText);
					}
				}
				else if (const UMovieSceneColorTrack* ColorTrack = Cast<UMovieSceneColorTrack>(Track))
				{
					for (UMovieSceneSection* Section : ColorTrack->GetAllSections())
					{
						UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section);
						if (!ColorSection)
						{
							continue;
						}
						const TCHAR* ChannelNames[] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
						const FMovieSceneFloatChannel* Channels[] = { &ColorSection->GetRedChannel(), &ColorSection->GetGreenChannel(), &ColorSection->GetBlueChannel(), &ColorSection->GetAlphaChannel() };
						for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
						{
							Out += FString::Printf(TEXT("    %s.%s  color  %s\n"), *ColorTrack->GetPropertyName().ToString(), ChannelNames[Index], *ChannelToText(*Channels[Index], TickResolution));
						}
					}
				}
				else if (const UMovieScene2DTransformTrack* TransformTrack = Cast<UMovieScene2DTransformTrack>(Track))
				{
					for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
					{
						UMovieScene2DTransformSection* TransformSection = Cast<UMovieScene2DTransformSection>(Section);
						if (!TransformSection)
						{
							continue;
						}
						// 키가 있는 채널만 — 나머지는 기본값
						for (int32 Index = 0; Index < UE_ARRAY_COUNT(TransformChannelNames); ++Index)
						{
							const FMovieSceneFloatChannel* Channel = TransformChannel(*TransformSection, Index);
							if (Channel->GetNumKeys() > 0)
							{
								Out += FString::Printf(TEXT("    RenderTransform.%s  %s\n"), TransformChannelNames[Index], *ChannelToText(*Channel, TickResolution));
							}
						}
					}
				}
				else
				{
					const UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
					Out += FString::Printf(TEXT("    %s  %s (키는 아직 이 도구가 안 읽는다)\n"),
						PropertyTrack ? *PropertyTrack->GetPropertyName().ToString() : TEXT("-"), *Track->GetClass()->GetName());
				}
			}
		}
	}
	return Out;
}
