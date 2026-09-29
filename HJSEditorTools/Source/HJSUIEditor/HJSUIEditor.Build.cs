// 공용(HJS) 에디터 전용 모듈 — AI 가 MCP 로 부르는 UI 제작 도구. HJSEditorTools 플러그인의 UI 도메인 모듈이다(README).
// 게임 빌드에는 안 들어간다. 공용 UI 런타임(HJSUI)과는 별개 플러그인이다.

using UnrealBuildTool;

public class HJSUIEditor : ModuleRules
{
	public HJSUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// 엔진 UMGToolSet 과 같은 의존 — 툴셋 등록(ToolsetRegistry) · 위젯 블루프린트(UMGEditor · UnrealEd) · 위젯 애니(MovieScene)
		// MovieSceneTools · BlueprintGraph: 이벤트 키가 부를 커스텀 이벤트 노드(시퀀서 「새 엔드포인트」와 같은 함수)
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ToolsetRegistry",
			"Projects",		// 사용법 문서 경로 — IPluginManager 로 플러그인 폴더를 찾는다
			"UMG",
			"SlateCore",
			"UMGEditor",
			"UnrealEd",
			"MovieScene",
			"MovieSceneTracks",
			"MovieSceneTools",
			"BlueprintGraph",
		});
	}
}
