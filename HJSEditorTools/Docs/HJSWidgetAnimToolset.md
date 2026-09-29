# HJSWidgetAnimToolset — 위젯 애니 클립 만들기 · 읽기 (Claude 용)

툴셋 이름 `HJSUIEditor.HJSWidgetAnimToolset`. 언리얼 MCP `call_tool` 로 부른다.
**이 문서는 절 단위로 읽는다.** 아래 목록에서 지금 작업에 필요한 절만 연다.

| 절 | 언제 읽나 |
|---|---|
| 1. 되는 것 · 안 되는 것 | 요청이 이 도구로 되는지 판단할 때 |
| 2. 기본 흐름 | 클립을 만들거나 고칠 때 (항상) |
| 3. C++ 와 잇기 | 코드가 재생하는 클립일 때 |
| 4. 노티파이 | 애니의 특정 시각에 함수(소리 · 처리)를 부를 때 |
| 5. 값 요령 | 색 · 회전 · 이동 · 크기 · Visibility · bool 값을 넣을 때 |
| 6. 함정 | 호출이 이유 없이 실패하거나 결과가 이상할 때 |
| 7. 확인 안 된 것 | 새 환경(독립형 · 패키징)에서 쓸 때 |

**함수 인자 · 설명은 여기 없다** — 툴셋 자체가 들고 있다. 인자 이름 하나만 모르면 **틀린 인자로 한 번 불러** 오류에 딸려 오는 입력 스키마를 본다(`describe_toolset` 은 툴셋 전체라 수천 토큰).

---

## 1. 되는 것 · 안 되는 것

| 되는 것 | 함수 |
|---|---|
| float 속성 (불투명도 등) | `SetFloatTrack` |
| 렌더 트랜스폼 채널 (이동 · 회전 · 크기 · 기울임) | `SetTransformChannel` |
| 색 속성 (BrushColor · ColorAndOpacity 등) | `SetColorTrack` |
| 목록 값 (Visibility 등 enum) — 계단식 | `SetEnumTrack` |
| 켜고 끄기 (bIsEnabled 등 bool) — 계단식 | `SetBoolTrack` |
| 이벤트 키 + 커스텀 이벤트 노드 (노티파이) | `AddEventKey` |
| 클립 전체를 텍스트로 (스냅샷) | `DescribeAnimations` |

| 안 되는 것 | 대신 |
|---|---|
| 컴파일 · 저장 | `UMGToolSet.CompileWidgetBlueprint` · `AssetTools.save_assets` |
| 곡선 종류 고르기 | 전부 cubic auto(에디터 기본값) 고정 |
| 슬롯 트랙 (위치 · 여백) | 없음 — 필요해지면 도구에 더한다 |
| **안에 든 WBP 의 내부 부품** | 부모 트리에서 안에 든 WBP 는 위젯 하나다 → **통째로는 부모 클립이 움직인다.** 내부 부품은 부모 클립이 못 잡는다(UMG 제한) → 자식 WBP 에 클립을 따로 만든다 |
| 위젯 트리 만들기 | `UMGToolSet`(엔진 내장) |

쓰기 함수 공통: 클립이 없으면 만든다 · 같은 트랙(채널)이 있으면 키를 **전부 갈아 끼운다** · 곡선은 에디터 기본값(cubic auto) · 클립 길이는 가장 늦은 키까지 늘기만 한다 · Ctrl+Z 가 된다 · 실패하면 `실패:` 로 시작하고 아무것도 안 바꾼다 · **그 WBP 의 에디터 창이 열려 있으면 쓰기 전에 스스로 닫는다**(열린 시퀀서 밑에서 고치고 컴파일하면 엔진 `ensure`) — 요약 끝에 "창을 닫음"이 붙으면 사람에게 보여 줄 때 `EditorAppToolset.OpenEditorForAsset` 으로 다시 연다.

## 2. 기본 흐름

**쓰기는 그 프로젝트의 샌드박스 규칙을 따른다**(예: `Content/Developers/<사용자>/` 에서 만들고 확인 뒤 정식 경로로 — 규칙은 그 프로젝트의 규약 문서에 있다). 정식 에셋을 직접 고치지 않는다.

1. 정식 WBP 를 샌드박스로 `duplicate` (새 WBP 면 샌드박스에 만든다)
2. `Set*Track` · `SetTransformChannel` · `AddEventKey` 호출
3. `CompileWidgetBlueprint` — C++ 가 필수 바인딩(`BindWidgetAnim`)이면 **여기서 클립 이름이 대조된다**
4. `DescribeAnimations` 결과를 트리 스냅샷 옆에 남긴다(`.uasset` 은 git diff 에 안 보인다)
5. 사용자 확인 — **Claude 가 `EditorAppToolset.OpenEditorForAsset` 으로 샌드박스 쪽을 열어 준다**(경로만 주면 같은 이름의 정식 쪽을 연다). 에디터 타임라인에서 재생해 보면 된다
6. 정식 교체 — 정식 파일을 `Saved/` 에 백업 → 참조자 확인(`get_referencers`) → 정식 삭제 → 샌드박스를 정식 경로로 `move` → 컴파일 · 저장 · `DescribeAnimations` 가 같은지 확인

Play 로 확인해야 하면 설정 CDO 가 가리키는 에셋 경로를 **그 에디터 세션 안에서만** 샌드박스로 돌린다(ini 는 안 바뀐다). 교체 전에 반드시 되돌린다 — 6 의 함정 참조.

## 3. C++ 와 잇기

- **클립 이름 = C++ `BindWidgetAnim` 변수 이름.** 재생 시점은 C++ 가 정한다(`PlayAnimation`). WBP 이벤트 그래프에 재생 로직을 두지 않는다.
- 필수 바인딩(`BindWidgetAnim`)이면 클립이 없을 때 WBP 컴파일이 실패한다 — 이름 대조를 컴파일러가 해 주는 것이라 **이게 정상 상태다.** 클립을 아직 못 만드는 동안만 `BindWidgetAnimOptional` 로 두고, 클립을 붙이면 필수로 되돌린다.
- 코드가 실행 중에 같은 속성을 바꾸는 위젯이면(승패 글자 색 · 나타나는 동안 버튼 막는 Visibility 등) 애니가 덮거나 부딪친다 → **다른 속성이나 다른 위젯**을 대상으로 한다(예: Visibility 대신 `bIsEnabled`).

## 4. 노티파이 — 애니의 특정 시각에 함수 부르기

언리얼 이벤트 트랙 기능 그대로다. **블루프린트 노드로 나오는 함수(`BlueprintCallable`)면 무엇이든** 애니의 한 시각에 부를 수 있다. 공용 코드 · 부품은 따로 필요 없다.

1. `AddEventKey(WBP, 클립, 시각, 이벤트 이름)` → 이벤트 키 + 커스텀 이벤트 노드. 엔진이 **"Sequencer Events" 그래프**를 따로 만들어 노드를 거기 둔다. 발동 시각은 이벤트 키가 가진다. 클립이 먼저 있어야 한다.
2. 이벤트 뒤에 부를 함수 노드를 에픽 `BlueprintTools` 로 **노드 · 핀 단위**로 잇는다: `create_node` → `connect_pins`(이벤트 `then` → 함수 `execute`) → 인자는 `set_pin_value`. `write_graph_dsl` 은 쓰지 않는다(6 참조).
3. 에디터 Play 로 확인한다.

부를 함수는 요청에 맞춰 고른다:

| 무엇을 부르나 | 예 |
|---|---|
| 그 위젯의 C++ 클래스에 있는 `UFUNCTION(BlueprintCallable)` — 처리를 C++ 에 둔다 | `UMyPopup::OnAnimNotify(FName Tag)` — 태그 하나로 여러 시각을 받아 C++ 델리게이트로 넘기는 모양. 로그로 발동 시각을 대조할 수 있다 |
| 엔진 · 에셋 함수 노드 — WBP 안에서 끝낸다 | `Play Sound 2D`(소리 에셋을 핀 값으로) |

어느 쪽을 쓸지는 프로젝트 규칙이 정한다 — WBP 이벤트 그래프를 비워 두는 규칙이 있는 프로젝트면 C++ 함수 쪽이다. 먼저 확인한다. C++ 함수가 아직 없으면 그 위젯 클래스에 만드는 것부터다(코드 구조는 사용자와 먼저 정한다).

## 5. 값 요령

- **색은 선형 RGBA(0~1).** 에디터 색 선택기의 hex 는 sRGB 라 변환해서 넣는다.
- **트랜스폼 채널** `TranslationX/Y`(픽셀) · `Angle`(도) · `ScaleX/Y` · `ShearX/Y`. 키가 없는 채널은 기본값(이동 0 · 회전 0 · 크기 1 · 기울임 0). 한 채널에 한 번씩 부른다(회전 + 이동 + 크기 = 5번).
- **Visibility** 값 이름: `Visible` · `Collapsed` · `Hidden` · `HitTestInvisible` · `SelfHitTestInvisible`. 불투명도 0 과 달리 `Collapsed` 는 자리까지 없앤다. 틀린 이름이면 쓸 수 있는 이름을 돌려준다.
- 계단식(enum · bool)은 키 시각에 딱 바뀐다 — 사이 값이 없다.
- 시각 · 값은 **자리값**으로 넣고 조정 대상이라고 적는다(값을 정하는 건 사용자다).

## 6. 함정

| 증상 | 원인 · 대응 |
|---|---|
| 그래프의 기존 노드가 사라짐 | `write_graph_dsl` 은 도달 못 하는 노드를 지우는데, 도달 판정이 핀 종류 문자열(`exec`)이다. **한국어 에디터는 "실행"으로 나와** 이벤트 노드가 지워진다 → 시퀀서 연결이 끊긴다. **쓰지 않는다** |
| `create_node` 가 노드를 못 찾음 | 노드 type_id 도 현지화된다(예: `오디오\|PlaySound2D`). `get_node_infos` 로 실제 이름을 본 뒤 쓴다 |
| `move` 가 이유 없이 `false` | 설정(ini · 설정 CDO)이 그 에셋을 가리키는 채 옮기면 에디터의 "INI 찾기/바꾸기" 확인 창이 뜨고 MCP 호출이라 **자동 취소**된다 → 그 설정을 비우고 옮긴 뒤 다시 채운다. 이유 없는 `false` 는 `Saved/Logs/<프로젝트>.log` 끝을 본다 |
| 사용자가 "안 바뀌었다" | 정식 쪽 WBP 창을 보고 있다 → `OpenEditorForAsset` 으로 샌드박스 쪽을 열어 준다 |
| 사용자의 "잘됨"이 무엇에 대한 것인지 모름 | 확인 대상이 둘 이상이면 **하나씩 묻고**, Play 확인은 로그 시각으로 대조한다(설정을 돌린 뒤 Play 가 실제로 있었는가) |
| `DescribeAnimations` 에 "에디터가 못 여는 트랙" | 옛 버전 도구가 목록 값을 `MovieSceneEnumTrack` 으로 만든 흔적이다 — 에디터에 그 트랙 편집기가 없어 클립을 고르면 크래시. `SetEnumTrack` 을 다시 부르면 바이트 트랙(`UMovieSceneByteTrack` + enum, 에디터가 만드는 것과 같음)으로 바꿔 넣는다 |
| 삭제가 자동 모드 분류기에 거부됨 | 우회하지 말고 사용자에게 방법을 묻는다. 삭제 전 백업 · 복구 경로를 먼저 말한다(`Content/` 가 git 에 없을 수 있다) |

## 7. 확인 안 된 것

- 노티파이(C++ 함수 · 엔진 노드 둘 다) — **에디터 Play 에서만** 확인됐다. 독립형(`-game`) · 패키징 빌드는 아직.
- 이 도구는 엔진의 **Experimental** API(`ToolsetRegistry`)에 기댄다 — 엔진을 올리면 먼저 빌드 · `list_toolsets` 부터 본다.
