# Unreal MCP Toolset Extensions

언리얼 5.8 에디터에 들어 있는 **에픽 MCP 서버**에 붙여 쓰는 **커스텀 툴셋 모음**입니다.
에픽 기본 툴셋에 없는 작업을 AI(Claude Code 같은 MCP 클라이언트)가 에디터에서 직접 할 수 있게 해 줍니다. 사람이 누르는 UI 는 없습니다 — AI 에게 말로 시키면 AI 가 이 도구를 불러 씁니다.

코드 · 클래스 이름 앞의 `HJS` 는 만든 사람의 접두어입니다.

---

## 1. 들어 있는 툴셋

| 툴셋 (AI 가 부르는 이름) | 하는 일 | 설명 |
|---|---|---|
| `HJSUIEditor.HJSWidgetAnimToolset` | 위젯 블루프린트(WBP)의 **애니메이션 클립**을 만들고 읽는다 | 4절 |

툴셋은 앞으로 늘어납니다. 버전 확인과 새 버전 받는 법은 6절.

### AI 가 읽는 사용법

툴셋마다 AI 용 사용법(흐름 · 요령 · 함정)이 `HJSEditorTools/Docs/<툴셋 클래스 이름>.md` 에 있습니다. 도구가 등록될 때 그 파일의 **절대 경로가 툴셋 설명 끝에 자동으로 붙어서**, AI 는 도구 목록을 볼 때 경로를 알고 필요한 절만 읽습니다. 사람이 따로 알려 줄 것은 없습니다.
AI 가 도구 목록에서 받는 것은 툴셋당 한 줄이라, 켜 둬도 평소 대화 비용은 거의 늘지 않습니다(에픽 MCP 기본 설정 기준).

---

## 2. 요구 환경

| 항목 | 조건 |
|---|---|
| 언리얼 | **5.8.x.** 5.8 미만은 안 됩니다 — 이 도구가 올라타는 에픽 MCP 서버 · 툴셋 등록 장치가 5.8 에서 처음 들어왔습니다. 5.9 이후는 확인하지 않았습니다(5절) |
| 프로젝트 | **C++ 프로젝트.** 소스로 드리므로 내 PC 에서 컴파일됩니다. 블루프린트 전용 프로젝트면 에디터에서 C++ 클래스를 하나 추가해 C++ 프로젝트로 바꿉니다 |
| 컴파일러 | UE 5.8 C++ 를 빌드할 수 있는 Visual Studio |
| OS | Windows (확인한 곳은 Windows 뿐) |
| AI | MCP 클라이언트 — Streamable HTTP 로 `http://127.0.0.1:8000/mcp` 에 붙을 수 있는 것. 확인한 것은 **Claude Code** |

**확인한 환경**(2026-09-30): 에픽 런처판 **UE 5.8.1 의 빈 C++ 프로젝트**에 3절대로 설치 → 빌드 → 위젯 애니 기능 7개 호출 → 컴파일 · 저장 → 만든 WBP 를 에디터에서 열고 재생까지. 소스 빌드 UE 5.8.2 에서도 쓰고 있습니다.

---

## 3. 설치

### 3-1. 받기 · 폴더 복사

이 페이지 위쪽의 초록색 **Code** 버튼 → **Download ZIP** 으로 받습니다(GitHub 가 저장소 파일을 압축해 내려 주는 기본 기능입니다 — git 을 쓰면 `git clone`). 압축을 풀고 안의 `HJSEditorTools` 폴더를 프로젝트의 `Plugins/` 아래에 넣습니다(없으면 만듭니다).

```
<내 프로젝트>/
  <내 프로젝트>.uproject
  Plugins/
    HJSEditorTools/
      HJSEditorTools.uplugin
      Source/  Docs/
```

### 3-2. `.uproject` 에 플러그인 켜기

`.uproject` 를 메모장으로 열어 `"Plugins"` 배열에 두 줄을 더합니다(배열이 없으면 만듭니다).

```json
"Plugins": [
  { "Name": "ModelContextProtocol", "Enabled": true, "TargetAllowList": [ "Editor" ] },
  { "Name": "HJSEditorTools",       "Enabled": true, "TargetAllowList": [ "Editor" ] }
]
```

- `ModelContextProtocol` = 에픽 MCP 서버. `HJSEditorTools` 를 켜면 필요한 에픽 플러그인(`ToolsetRegistry` · `UMGToolSet` · `EditorToolset` · `PythonScriptPlugin`)은 자동으로 같이 켜집니다.
- `"Editor"` 로 제한해 두면 게임 빌드(패키징)에는 들어가지 않습니다. 에디터 전용 도구입니다.
- 다른 에픽 툴셋(`Engine/Plugins/Experimental/Toolsets/`)이 필요하면 같은 모양으로 한 줄씩 더합니다. `AllToolsets` 는 수십 개가 한꺼번에 켜져 권하지 않습니다.

### 3-3. MCP 서버 자동 시작

`Config/DefaultEditorPerProjectUserSettings.ini` 에 넣습니다(파일이 없으면 만듭니다).

```ini
[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
bAutoStartServer=True
ServerPortNumber=8000
```

- 이미 떠 있는 에디터에서는 콘솔 명령 `ModelContextProtocol.StartServer 8000` 으로도 켤 수 있습니다.
- 에디터 하나만 이 포트를 쓸 수 있습니다. 여러 프로젝트를 동시에 띄우면 포트를 다르게 줍니다.

### 3-4. 빌드

에디터를 끈 상태에서 `.uproject` 를 더블클릭 → "모듈을 다시 빌드할까요?" → **예.**
또는 Visual Studio 에서 `<내 프로젝트>Editor` 를 빌드하거나, 명령줄로:

```
"<엔진 폴더>\Engine\Build\BatchFiles\Build.bat" <내 프로젝트>Editor Win64 Development -Project="<.uproject 절대 경로>" -WaitMutex
```

새 모듈이라 에디터를 켠 채 라이브 코딩으로는 안 붙습니다 — 에디터를 끄고 빌드합니다.

### 3-5. AI 연결

에디터가 뜨면 로그에 `Starting MCP server on port 8000` 이 나옵니다. MCP 클라이언트에 이 주소를 등록합니다.

```
http://127.0.0.1:8000/mcp
```

Claude Code 라면 한 번만:

```
claude mcp add --transport http unreal http://127.0.0.1:8000/mcp
```

- 서버는 **내 PC(localhost)에서만** 받습니다.
- 에디터가 **다 뜬 뒤에** 연결합니다. 이미 연결이 실패로 남았으면 클라이언트에서 다시 연결합니다(Claude Code: `/mcp` → Reconnect).

### 3-6. 확인

AI 에게 "언리얼 MCP 툴셋 목록 보여 줘" 라고 합니다. 1절 표의 툴셋이 목록에 있고 설명 끝에 사용법 경로가 붙어 있으면 끝입니다. 예:

```
HJSUIEditor.HJSWidgetAnimToolset: 위젯 블루프린트의 애니메이션 클립(float · 트랜스폼 · 색 · 목록 값 · bool · 이벤트 키)을 만들고 읽는다. 컴파일 · 저장은 하지 않는다. 사용법(먼저 읽는다): <내 프로젝트>/Plugins/HJSEditorTools/Docs/HJSWidgetAnimToolset.md
```

경로가 안 붙어 있으면 에디터 로그에서 `LogHJSEditorTools` 경고를 봅니다(`Docs/` 폴더를 빠뜨린 경우).

---

## 4. 툴셋: 위젯 애니메이션 — `HJSUIEditor.HJSWidgetAnimToolset`

에픽 기본 도구(`UMGToolSet`)는 위젯 **트리**(버튼 · 이미지 배치)만 만들고 **애니메이션 클립은 못 만듭니다.** 이 툴셋이 그 빈자리를 채웁니다.

예를 들어 AI 에게 이렇게 말하면 됩니다.

> WBP_Popup 에 `Anim_Open` 클립을 만들어 줘. Root 가 0.3초 동안 투명 → 불투명으로 나타나면서 크기가 0.8 → 1.0 으로 커지고, 0.3초에 효과음이 나게.

AI 는 이 도구로 클립 · 트랙 · 키 · 이벤트를 만들고, 에픽 도구로 컴파일 · 저장합니다. 결과는 에디터의 애니메이션 타임라인에서 사람이 만든 것과 똑같이 보이고 고칠 수 있습니다.

### 4-1. 되는 것

| 기능 | 함수 | 예 |
|---|---|---|
| float 속성 — 페이드 등 | `SetFloatTrack` | `RenderOpacity` 0 → 1 |
| 렌더 트랜스폼 — 이동 · 회전 · 크기 · 기울임 | `SetTransformChannel` | `ScaleX` 0.8 → 1.0, `Angle` 0 → 360 |
| 색 | `SetColorTrack` | Border 의 `BrushColor`, Image 의 `ColorAndOpacity` |
| 목록 값(enum) — 계단식으로 딱 바뀜 | `SetEnumTrack` | 끝나면 `Visibility` = `Collapsed` |
| 켜고 끄기(bool) — 계단식 | `SetBoolTrack` | 나타나는 동안 버튼 `bIsEnabled` = false |
| **이벤트 키 · 노티파이** — 애니의 특정 시각에 함수 부르기 | `AddEventKey` | 0.3초에 효과음 재생 / C++ 함수 호출 |
| **텍스트 스냅샷** — 클립 전체를 글로 뜨기 | `DescribeAnimations` | 리뷰 · 기록용(`.uasset` 은 git diff 에 안 보이므로) |

- 쓰기 함수는 **클립이 없으면 만들고**, 같은 트랙이 있으면 키를 **전부 갈아 끼웁니다.** 실패하면 아무것도 바꾸지 않습니다. 에디터에서 Ctrl+Z 가 됩니다.
- 그 WBP 의 에디터 창이 열려 있으면 쓰기 전에 **스스로 닫습니다**(열린 타임라인 밑에서 고치면 엔진이 멈추는 경우가 있어서).

### 4-2. 안 되는 것

| 안 되는 것 | 대신 |
|---|---|
| 컴파일 · 저장 | 에픽 도구 `UMGToolSet.CompileWidgetBlueprint` · `AssetTools.save_assets` (같이 켜짐) |
| 위젯 트리 만들기 | 에픽 `UMGToolSet` |
| 곡선 종류 고르기 | 전부 에디터 기본값(cubic auto) |
| 슬롯 트랙(캔버스 위치 · 여백) | 없음 |
| WBP 안에 넣은 다른 WBP 의 **내부 부품** | 통째로는 움직일 수 있다. 내부 부품은 UMG 제한으로 부모 클립이 못 잡는다 → 자식 WBP 에 클립을 따로 만든다 |

### 4-3. 노티파이 — 애니 도중에 함수 부르기

언리얼 애니메이션의 **이벤트 트랙** 기능 그대로입니다. 블루프린트 노드로 나오는 함수(`BlueprintCallable`)면 무엇이든 애니의 한 시각에 부를 수 있습니다.
`AddEventKey` 가 이벤트 키와 커스텀 이벤트 노드를 만들고, 그 뒤에 무엇을 부를지는 AI 가 에픽 `BlueprintTools` 로 노드를 이어 붙입니다.

- **(a) 내 C++ 함수를 부른다** — 처리를 C++ 에 두고 싶을 때. **그 WBP 의 부모 C++ 클래스**(`UUserWidget` 을 상속한 내 클래스)에 함수 하나만 있으면 됩니다:
  ```cpp
  UFUNCTION(BlueprintCallable)
  void OnAnimNotify(FName Tag);   // 태그로 여러 시각을 구분해 받는다
  ```
- **(b) 블루프린트 노드를 부른다** — WBP 안에서 끝낼 때. 예: `Play Sound 2D`(소리 에셋을 핀 값으로).

### 4-4. 쓰는 흐름

1. **어떤 클립을 만들지 말로 설명합니다.** 위젯 이름(트리 안의 이름) · 클립 이름 · 시각 · 값을 알려 주면 정확합니다.
2. AI 가 `Docs/HJSWidgetAnimToolset.md` 를 읽고 → 도구로 트랙 · 키를 넣고 → 컴파일 → 저장합니다.
3. 에디터에서 그 WBP 를 열어 타임라인으로 재생해 봅니다. 고칠 점을 다시 말로 합니다.
4. 게임에서 재생은 C++ 에서:
   ```cpp
   UPROPERTY(Transient, meta = (BindWidgetAnim))
   TObjectPtr<UWidgetAnimation> Anim_Open;   // 변수 이름 = 클립 이름

   PlayAnimation(Anim_Open);
   ```
   `BindWidgetAnim`(필수)이면 클립이 없을 때 WBP 컴파일이 실패합니다 — 이름이 어긋난 걸 컴파일러가 잡아 주는 것이니 정상입니다. 클립을 만들기 전에는 `BindWidgetAnimOptional` 로 둡니다.
5. 기록을 남기고 싶으면 "이 WBP 애니 스냅샷 떠 줘" → `DescribeAnimations` 결과를 텍스트 파일로 저장하게 합니다.

### 4-5. 이 툴셋의 주의

| 주의 | 내용 |
|---|---|
| **한국어 에디터에서 `write_graph_dsl` 쓰지 말 것** | 에픽 `BlueprintTools.write_graph_dsl` 은 연결 안 된 노드를 지우는데, 한국어 에디터에서는 판정이 틀려 **이벤트 노드가 지워집니다** → 노티파이가 끊깁니다. 노드는 `create_node` · `connect_pins` 로 하나씩 잇게 합니다(사용법 md 에 적혀 있어 AI 가 따릅니다) |
| 노드 이름도 현지화됨 | 한국어 에디터에서는 노드 type_id 가 `오디오\|PlaySound2D` 처럼 나옵니다. AI 가 `get_node_infos` 로 실제 이름을 본 뒤 씁니다 |
| 코드가 같은 속성을 바꾸는 위젯 | 게임 코드가 실행 중에 같은 속성(글자 색 · Visibility 등)을 바꾸면 애니와 부딪칩니다 → 다른 속성이나 다른 위젯을 대상으로 합니다 |
| 색 값 | 선형 RGBA(0~1)입니다. 에디터 색 선택기의 hex 는 sRGB 라 변환이 필요합니다 — AI 에게 hex 로 말해도 됩니다 |
| 확인 범위 | 노티파이는 **에디터 Play 에서만** 확인했습니다. 독립 실행(`-game`) · 패키징 빌드는 확인하지 않았습니다 |

---

## 5. 공통 주의

| 주의 | 내용 |
|---|---|
| **Experimental API** | 에픽이 실험 단계로 표시한 기능(`ToolsetRegistry` · `ModelContextProtocol`)에 기댑니다. 엔진을 올리면 먼저 빌드되는지 · 툴셋 목록에 뜨는지부터 봅니다. 5.9 이후에서 빌드가 깨지면 그 API 이름이 바뀐 것일 수 있습니다 |
| **AI 가 에셋을 직접 고칩니다** | **버전 관리(git 등)나 백업을 해 둔 프로젝트에서** 쓰세요. 처음에는 시험용 폴더(예: `Content/Developers/<내 이름>/`)에 복사본을 만들어 시켜 보고, 마음에 들면 정식 위치로 옮기는 방식이 안전합니다 |
| 설정이 가리키는 에셋은 이동이 조용히 취소됨 | ini · 설정이 가리키는 에셋을 MCP 로 옮기면 에디터의 확인 창이 떠서 **자동 취소**됩니다(결과 `false`). 설정을 비우고 옮긴 뒤 다시 채웁니다 |
| 저장 | 도구는 저장하지 않습니다. 저장은 AI 가 `AssetTools.save_assets` 로 합니다 |
| 도구 메시지 언어 | 도구가 돌려주는 요약 · 오류 문구와 AI 용 사용법은 한국어입니다 |

---

## 6. 버전 · 새 버전 받기

**지금 이 페이지의 버전: 0.1**

- **내가 가진 버전**은 내 프로젝트의 `Plugins/HJSEditorTools/HJSEditorTools.uplugin` 안 `"VersionName"` 입니다. 위 버전보다 낮으면 새 버전이 나온 것입니다.
- **새 버전 받기**: 이 페이지에서 다시 **Code → Download ZIP**(또는 `git pull`) → `HJSEditorTools` 폴더를 통째로 갈아 끼우기 → 에디터를 끄고 다시 빌드.

### 변경 기록

| 버전 | 날짜 | 바뀐 것 |
|---|---|---|
| 0.1 | 2026-09-30 | 첫 공개 — 위젯 애니메이션 툴셋 |

---

## 7. 라이선스

[MIT License](LICENSE). 보증은 없습니다 — 쓰기 전에 프로젝트를 백업하세요.
