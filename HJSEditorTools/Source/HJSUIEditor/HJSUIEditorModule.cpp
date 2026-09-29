// 공용(HJS) 에디터 모듈 — 이 모듈의 MCP 툴셋을 목록 없이 등록하고, 사용법 경로를 툴셋 설명에 붙인다.
// 새 툴셋 = 클래스(또는 hjs_toolset 의 .py) + 플러그인 Docs/<클래스 이름>.md 한 장. 등록 · 경로는 여기가 한다(README 「도구 추가」).
// MCP 는 툴셋 목록을 그때그때 읽으므로 Claude 쪽 재등록은 없다.

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/Toolset.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogHJSEditorTools, Log, All);

namespace HJSEditorTools
{
	/** 이 모듈이 든 플러그인의 Docs/<클래스 이름>.md 절대 경로. 플러그인을 못 찾으면 빈 문자열 */
	static FString UsageDocPath(const UClass* Class)
	{
		static const FString DocsDir = []
		{
			// 플러그인 이름을 적지 않는다 — 모듈을 다른 이름의 플러그인으로 옮겨도 그 플러그인의 Docs/ 를 찾는다(UBT 가 정의)
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UTF8_TO_TCHAR(UE_PLUGIN_NAME));
			return Plugin ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Docs")) : FString();
		}();
		return DocsDir.IsEmpty() ? FString() : (DocsDir / Class->GetName()) + TEXT(".md");
	}

	/**
	 * 등록된 툴셋 중 Docs/<클래스 이름>.md 가 있는 것은 설명(툴팁) 끝에 그 경로를 붙인다 — list_toolsets 한 줄에 보인다.
	 * C++ · Python 툴셋을 같이 처리한다(Python 툴셋은 등록 시점이 달라 레지스트리가 바뀔 때마다 부른다). 이미 붙었으면 건너뛴다.
	 */
	static void AttachUsageDocs()
	{
		const auto Subsystem = UToolsetRegistrySubsystem::Get();
		if (!Subsystem.HasValue())
		{
			return;
		}
		Subsystem.GetValue()->ToolsetRegistry.ForEachToolset([](const FString&, const UE::ToolsetRegistry::FToolset& Toolset)
		{
			UClass* const Class = Toolset.GetToolsetClass();
			const FString DocPath = Class ? UsageDocPath(Class) : FString();
			if (DocPath.IsEmpty() || !FPaths::FileExists(DocPath))
			{
				return;
			}
			const FString ToolTip = Class->GetMetaData(TEXT("ToolTip"));
			if (!ToolTip.Contains(DocPath))
			{
				Class->SetMetaData(TEXT("ToolTip"), *FString::Printf(TEXT("%s 사용법(먼저 읽는다): %s"), *ToolTip, *DocPath));
			}
		});
	}
}

class FHJSUIEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FHJSUIEditorModule::RegisterToolsets);
		FCoreDelegates::OnPreExit.AddRaw(this, &FHJSUIEditorModule::UnregisterToolsets);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.RemoveAll(this);
		FCoreDelegates::OnPreExit.RemoveAll(this);
	}

private:
	/** 이 모듈 안의 툴셋 클래스 전부 — 등록 목록을 따로 두지 않는다 */
	static TArray<UClass*> FindModuleToolsets()
	{
		const FString ModulePackage = FString(TEXT("/Script/")) + UTF8_TO_TCHAR(UE_MODULE_NAME);
		TArray<UClass*> Result;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* const Class = *It;
			if (Class->IsChildOf(UToolsetDefinition::StaticClass())
				&& !Class->HasAnyClassFlags(CLASS_Abstract)
				&& Class->GetOutermost()->GetName() == ModulePackage)
			{
				Result.Add(Class);
			}
		}
		return Result;
	}

	void RegisterToolsets()
	{
		for (UClass* const Class : FindModuleToolsets())
		{
			const FString DocPath = HJSEditorTools::UsageDocPath(Class);
			if (DocPath.IsEmpty() || !FPaths::FileExists(DocPath))
			{
				UE_LOG(LogHJSEditorTools, Warning, TEXT("툴셋 %s 의 사용법 문서가 없다: %s — README 「도구 추가」"), *Class->GetName(), *DocPath);
			}
			UToolsetRegistry::RegisterToolsetClass(Class);
			RegisteredClasses.Add(Class);
		}

		if (const auto Subsystem = UToolsetRegistrySubsystem::Get(); Subsystem.HasValue())
		{
			RegistryChangedHandle = Subsystem.GetValue()->ToolsetRegistry.OnToolsetRegistered().AddStatic(&HJSEditorTools::AttachUsageDocs);
		}
		HJSEditorTools::AttachUsageDocs();
	}

	void UnregisterToolsets()
	{
		if (const auto Subsystem = UToolsetRegistrySubsystem::Get(); Subsystem.HasValue())
		{
			Subsystem.GetValue()->ToolsetRegistry.OnToolsetRegistered().Remove(RegistryChangedHandle);
		}
		for (const TWeakObjectPtr<UClass>& Class : RegisteredClasses)
		{
			if (Class.IsValid())
			{
				UToolsetRegistry::UnregisterToolsetClass(Class.Get());
			}
		}
		RegisteredClasses.Reset();
	}

	TArray<TWeakObjectPtr<UClass>> RegisteredClasses;
	FDelegateHandle RegistryChangedHandle;
};

IMPLEMENT_MODULE(FHJSUIEditorModule, HJSUIEditor)
