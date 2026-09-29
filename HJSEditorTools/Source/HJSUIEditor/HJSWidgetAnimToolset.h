// 공용(HJS) — AI 가 MCP 로 부르는 위젯 애니메이션 도구. 엔진 내장 툴셋에는 애니 클립을 만드는 기능이 없다.
// 에디터의 「+Animation」 버튼 · 키 찍기 · 이벤트 트랙 「새 엔드포인트」와 같은 순서로 만든다.
// 클래스 주석은 list_toolsets 의 한 줄 설명이 된다 — 한 줄로 둔다. 공통 동작 · 요령 · 함정은 Docs/HJSWidgetAnimToolset.md(모듈이 설명 끝에 경로를 붙인다).
// 지금 되는 것: float · 렌더 트랜스폼 채널 · 색 · 목록 값(Visibility 등) · bool · 이벤트 키. 슬롯(위치 · 여백) 트랙은 필요해질 때.
// 안에 든 WBP(예: 버튼 WBP 인스턴스)는 부모 트리에서 위젯 하나라 통째로 움직일 수 있다. 그 내부 부품은 부모 클립이 못 잡는다 — 자식 WBP 에 클립을 따로 만든다.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "HJSWidgetAnimToolset.generated.h"

class UWidgetBlueprint;

/** 키 하나 — 시각(초)과 값 */
USTRUCT(BlueprintType)
struct FHJSAnimFloatKey
{
	GENERATED_BODY()

	/** 클립 시작부터의 시각(초) */
	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	float Time = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	float Value = 0.f;
};

/** 색 키 하나 — 시각(초)과 색(선형 RGBA, 0~1. 에디터 색 선택기의 hex 는 sRGB 라 변환해서 넣는다) */
USTRUCT(BlueprintType)
struct FHJSAnimColorKey
{
	GENERATED_BODY()

	/** 클립 시작부터의 시각(초) */
	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	float Time = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	FLinearColor Color = FLinearColor::White;
};

/** 목록 값 키 — 시각(초)과 값 이름(예: Visibility 면 Visible · Collapsed · Hidden · HitTestInvisible · SelfHitTestInvisible). 키 시각에 딱 바뀐다(계단식) */
USTRUCT(BlueprintType)
struct FHJSAnimEnumKey
{
	GENERATED_BODY()

	/** 클립 시작부터의 시각(초) */
	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	float Time = 0.f;

	/** 값 이름 — "Collapsed" 또는 "ESlateVisibility::Collapsed" */
	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	FString Value;
};

/** 켜고 끄는 키 — 시각(초)과 값. 키 시각에 딱 바뀐다(계단식) */
USTRUCT(BlueprintType)
struct FHJSAnimBoolKey
{
	GENERATED_BODY()

	/** 클립 시작부터의 시각(초) */
	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	float Time = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "HJS|WidgetAnim")
	bool bValue = false;
};

/** 위젯 블루프린트의 애니메이션 클립(float · 트랜스폼 · 색 · 목록 값 · bool · 이벤트 키)을 만들고 읽는다. 컴파일 · 저장은 하지 않는다. */
UCLASS(BlueprintType)
class UHJSWidgetAnimToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * 위젯의 float 속성 하나(예: RenderOpacity)에 키를 넣는다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름 — C++ 의 BindWidgetAnim 변수 이름과 같아야 한다(예: Anim_FadeIn).
	 * @param WidgetName 움직일 위젯의 이름 — 위젯 트리 안의 이름(예: Root).
	 * @param PropertyName 그 위젯의 float 속성 C++ 이름(예: RenderOpacity).
	 * @param Keys 시각(초) · 값 목록. 순서는 상관없다.
	 * @return 한 줄 요약. 실패하면 "실패:" 로 시작하고 아무것도 바꾸지 않는다.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString SetFloatTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimFloatKey>& Keys);

	/**
	 * 위젯의 색 속성 하나(FLinearColor · FSlateColor · FColor — 예: Border 의 BrushColor, Image 의 ColorAndOpacity)에 키를 넣는다. R · G · B · A 네 채널을 같이 쓴다.
	 * 코드가 실행 중에 같은 속성을 바꾸는 위젯(예: 승패 글자 색)이면 애니가 그 값을 덮는다 — 다른 속성을 고른다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름.
	 * @param WidgetName 움직일 위젯의 이름 — 위젯 트리 안의 이름.
	 * @param PropertyName 그 위젯의 색 속성 C++ 이름(예: BrushColor).
	 * @param Keys 시각(초) · 색(선형 RGBA) 목록. 순서는 상관없다.
	 * @return 한 줄 요약. 실패하면 "실패:" 로 시작하고 아무것도 바꾸지 않는다.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString SetColorTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimColorKey>& Keys);

	/**
	 * 위젯의 목록 값(enum) 속성 하나에 키를 넣는다 — 대표는 Visibility(보이기 · 클릭 · 레이아웃 자리). 값은 키 시각에 딱 바뀐다.
	 * 불투명도 0 과 달리 Collapsed 는 자리까지 없애고, Hidden · HitTestInvisible 은 클릭을 막거나 통과시킨다.
	 * 코드가 실행 중에 같은 위젯의 Visibility 를 바꾸면(예: 나타나는 동안 버튼 막기) 애니와 부딪친다 — 다른 위젯을 고른다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름.
	 * @param WidgetName 대상 위젯의 이름 — 위젯 트리 안의 이름(캔버스 패널 · 안에 든 WBP 인스턴스도 된다).
	 * @param PropertyName enum 속성 C++ 이름(예: Visibility).
	 * @param Keys 시각(초) · 값 이름 목록. 순서는 상관없다.
	 * @return 한 줄 요약. 실패하면 "실패:" 로 시작하고 아무것도 바꾸지 않는다(틀린 값 이름이면 쓸 수 있는 이름을 알려 준다).
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString SetEnumTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimEnumKey>& Keys);

	/**
	 * 위젯의 bool 속성 하나(예: bIsEnabled — 눌림 가능 여부)에 키를 넣는다. 값은 키 시각에 딱 바뀐다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름.
	 * @param WidgetName 대상 위젯의 이름 — 위젯 트리 안의 이름.
	 * @param PropertyName bool 속성 C++ 이름(예: bIsEnabled).
	 * @param Keys 시각(초) · 값 목록. 순서는 상관없다.
	 * @return 한 줄 요약. 실패하면 "실패:" 로 시작하고 아무것도 바꾸지 않는다.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString SetBoolTrack(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& PropertyName, const TArray<FHJSAnimBoolKey>& Keys);

	/**
	 * 위젯의 렌더 트랜스폼(에디터의 Transform 트랙) 채널 하나에 키를 넣는다. 키가 없는 채널은 기본값(이동 0 · 회전 0 · 크기 1 · 기울임 0)이다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름.
	 * @param WidgetName 움직일 위젯의 이름 — 위젯 트리 안의 이름.
	 * @param Channel TranslationX · TranslationY(픽셀) · Angle(도) · ScaleX · ScaleY · ShearX · ShearY 중 하나.
	 * @param Keys 시각(초) · 값 목록. 순서는 상관없다.
	 * @return 한 줄 요약. 실패하면 "실패:" 로 시작하고 아무것도 바꾸지 않는다.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString SetTransformChannel(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, const FString& WidgetName, const FString& Channel, const TArray<FHJSAnimFloatKey>& Keys);

	/**
	 * 클립의 이벤트 트랙에 키를 하나 넣고, 그 키가 부를 커스텀 이벤트 노드를 WBP 이벤트 그래프에 만든다(시퀀서의 「새 엔드포인트」와 같다).
	 * 이벤트 뒤에 붙일 노드는 editor_toolset BlueprintTools 로 만든다(create_node · connect_pins). 클립은 먼저 있어야 한다.
	 * @param WidgetBlueprint 대상 WBP.
	 * @param AnimationName 클립 이름.
	 * @param Time 키 시각(초).
	 * @param EventName 커스텀 이벤트 이름 — WBP 안에서 겹치지 않아야 한다(예: Notify_A).
	 * @return 한 줄 요약(만든 노드 이름 · 그래프). 실패하면 "실패:" 로 시작한다.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString AddEventKey(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName, float Time, const FString& EventName);

	/**
	 * WBP 의 애니메이션 클립을 텍스트로 뜬다 — 클립 · 길이 · 대상 위젯 · 트랙 · 채널 · 키 · 이벤트 키.
	 * UMGToolSet.GetWidgetDescription 은 위젯 트리만 보여 주므로, 스냅샷에 이 결과를 같이 남긴다(프로젝트 규약의 스냅샷 규칙).
	 * @param WidgetBlueprint 대상 WBP.
	 */
	UFUNCTION(meta = (AICallable), Category = "HJS|WidgetAnim")
	static FString DescribeAnimations(UWidgetBlueprint* WidgetBlueprint);
};
