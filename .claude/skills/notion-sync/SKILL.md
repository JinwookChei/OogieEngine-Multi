---
name: notion-sync
description: Docs/Study 학습 정리와 Docs/Diagrams 다이어그램(HTML)을 Notion "[ 프로젝트 정리 ] → OogieEngine Server" 페이지에 Notion MCP로 올리거나 갱신한다. 파트(Plan.md의 Phase, 예: Phase 1-3)가 끝나 사용자가 Notion 업로드에 동의했을 때, 또는 사용자가 "노션에 올려줘", /notion-sync 를 요청할 때 사용한다.
---

# notion-sync — 학습 정리를 Notion에 올리기

저장소의 학습 정리(`Docs/Study/*.md`)와 다이어그램(`Docs/Diagrams/*.html`)을 Notion에 옮긴다.
**저장소 문서가 원본**이다. Notion은 사본이므로 내용 수정은 항상 저장소 문서를 먼저 고치고 그다음 Notion에 반영한다.

## 0. 대상 페이지 (Notion)

| 페이지 | 아이콘 | ID | 원본 |
|---|---|---|---|
| `[ 프로젝트 정리 ]` (상위) | — | `4156412b-0acd-8055-ac90-c7a3ddc96f3a` | — |
| **OogieEngine Server** (메인) | 📚 | `4156412b-0acd-805b-8d62-ccfb7bb7c918` | `Docs/Study/README.md` |
| Phase 0. 환경 준비 | 🛠️ | `4156412b-0acd-81d1-91bd-dc9bb2f0356f` | `Phase0_Setup.md` |
| Phase 1-1. ServerCore 기반 | 🧱 | `4156412b-0acd-8159-ae6e-e422244bd955` | `Phase1-1_Foundation.md` |
| Phase 1-2. 동기화 | 🔒 | `4156412b-0acd-81cd-b08f-f440516a0a93` | `Phase1-2_Synchronization.md` |
| Phase 1-3. 메모리 | 🧠 | `4156412b-0acd-8117-a60e-ec1679c15947` | `Phase1-3_Memory.md` + `Docs/Diagrams/Phase1-3_Memory.html`, `Phase1-3_MemoryPool.html` |

- 새 Phase 페이지를 만들면 **이 표에 행을 추가**한다 (ID는 create-pages 결과에서).
- ID가 맞지 않으면 `notion-search`로 "OogieEngine Server"를 찾아 다시 확인하고 표를 고친다.

## 1. 준비

1. Notion 도구 스키마를 불러온다: `ToolSearch("select:mcp__claude_ai_Notion__notion-fetch,mcp__claude_ai_Notion__notion-create-pages,mcp__claude_ai_Notion__notion-update-page,mcp__claude_ai_Notion__notion-create-file-upload")`
2. Markdown 규칙이 기억나지 않으면 `notion-fetch`로 `notion://docs/notion-flavored-markdown-spec`을 읽는다.
3. 이번에 올릴 범위를 정한다: 끝난 Phase 파일, README(목차/전체 그림이 바뀌었으면), 해당 Phase의 다이어그램.
4. 저장소 문서가 최신인지 확인한다 (Plan.md와 Step 표, 확인 질문 답이 반영됐는지). 빠진 게 있으면 **먼저 저장소 문서를 고친다**.

## 2. 변환

```bash
S=<스크래치패드 경로>; mkdir -p "$S/notion"
for f in Docs/Study/*.md; do python -I .claude/skills/notion-sync/md2notion.py "$f" "$S/notion/$(basename "$f")"; done
```
- 출력 첫 줄 = 원문 H1(예: `🧠 Phase 1-3. 메모리`). 앞의 이모지는 **페이지 아이콘**, 나머지는 **제목**으로 쓰고, 본문에는 넣지 않는다.
- 스크립트 규칙: `>` 인용 → `<callout>`(💡 yellow, 📝 gray, ⚠️ red, 🔧 blue, 🧩 purple, 💬/✅ green, 🔺 orange, 그 외 📌 gray), 언어 없는 코드 블록 → `plain text`, 인라인 코드 밖 `<태그` 이스케이프.
- Windows 콘솔에서 Python 출력이 깨지면 `PYTHONIOENCODING=utf-8`을 붙인다.

## 3. 페이지 만들기 / 갱신

### 새 Phase 페이지
- `notion-create-pages`, parent = 메인 페이지 ID, `icon` = 이모지, `allow_async: false`.
- 본문이 **약 40KB를 넘으면 나눈다**: 앞부분(Step 몇 개)으로 페이지를 만들고, 나머지는 `notion-update-page`의 `insert_content` + `position: {"type":"end"}`로 Step 단위로 덧붙인다.
- 만든 뒤 위 0번 표에 ID를 추가한다.

### 기존 Phase 페이지 갱신
- 바뀐 부분이 Step 몇 개면 `update_content`(작은 old_str → new_str) 또는 끝에 `insert_content`.
- 전체를 다시 쓰려면 `replace_content`. 이때 페이지에 있는 **HTML embed 태그(`<embed source="notion-file-block://..."/>`)와 하위 페이지 태그(`<page url=...>`)를 fetch한 그대로 포함**해야 지워지지 않는다. 지워질 블록이 있다는 오류가 나면 사용자에게 묻고, 임의로 `allow_deleting_content: true`를 쓰지 않는다.

### 메인 페이지 (README)
- 목차 표의 문서 칸은 `.md` 링크 대신 하위 페이지 mention: `<mention url="https://app.notion.com/p/<ID 대시 없이>">제목</mention>`.
- 목차 표 아래에 하위 페이지 블록 `<page url="...">제목</page>`을 모두 둔다 (`replace_content`할 때 빠뜨리면 하위 페이지가 지워진다).
- README 마지막의 "Notion으로 가져오기" 안내 줄은 넣지 않는다.

## 4. 다이어그램 (HTML) 올리기

1. HTML 파일 내용을 끝까지 읽고 현재 코드와 맞는지 본다 (진행 상태 표시, 함수 동작 설명). 다르면 **저장소 파일을 먼저 고친다**.
2. `notion-create-file-upload`(filename = 파일 이름) → 받은 `upload_url`로 업로드:
   ```bash
   curl -s -X POST "<upload_url>" -H "authorization: <upload_headers.authorization 값>" -F "file=@Docs/Diagrams/<파일>.html;type=text/html"
   ```
   응답의 `status`가 `uploaded`인지 확인한다. 업로드 URL은 약 1시간 뒤 만료되므로 바로 배치한다.
3. 해당 Phase 페이지의 Step 표 바로 아래, 첫 Step 앞에 섹션으로 넣는다:
   ```
   ## 🗺️ <주제> 한눈에 보기 (인터랙티브)

   <무엇을 그린 그림인지 한 줄>. 원본: `Docs/Diagrams/<파일>.html`

   <embed source="file-upload://<file_upload_id>" />
   ```
   (HTML은 반드시 `<embed>`로 넣는다. 코드 블록이나 `<file>`로 넣지 않는다.)

⚠️ **이미 있는 embed는 `update_content`로 바꿀 수 없다.** fetch에 보이는 `notion-file-block://...` 태그를 old_str로 넣어도 매칭되지 않는다 (2026-11-11 확인).
→ 새 파일을 업로드해 설명 문단 바로 아래에 새 embed를 넣고, 사용자에게 **옛 embed 블록을 직접 지워 달라고** 알린다. 어느 쪽이 옛것인지(위치, 눈에 띄는 차이)를 구체적으로 말한다.

## 5. 확인

- 긴 페이지는 fetch 결과가 파일로 저장된다. Python으로 `json.loads(...)['text']`를 읽어 확인한다:
  - `## ` 제목 목록이 원문 Step과 같은지
  - `<callout` 개수가 변환 결과와 같은지
  - 마지막 부분이 원문 끝과 같은지 (잘림 없음)
- 경고가 있으면 사용자에게 그대로 알린다.

## 6. 마무리

1. Plan.md "📍 현재 진행 상황"에 Notion 동기화 상태를 한 줄로 적는다 (예: `Notion: Phase 1-3까지 반영 (2026-11-11)`).
2. 사용자에게 보고한다: 만든/갱신한 페이지, 다이어그램 위치, 직접 해야 할 일(옛 embed 삭제 등), 원문에서 발견한 오래된 내용.
3. 저장소 파일을 고쳤으면 커밋은 사용자가 요청할 때만 한다 (`docs: ...`).

## 규칙

- 강의 코드 원문(`MMOServer_example/`)은 Notion에도 올리지 않는다. 학습 문서에 있는 사용자 코드만 올린다.
- 사용자의 MEMO 내용(📝 내 메모)은 문서에 있는 그대로 옮긴다. 고쳐 쓰지 않는다.
- 미답변 확인 질문은 `(미답변)` 그대로 둔다. Notion에 올리면서 답을 채우지 않는다.
- Notion 페이지를 지우거나 옮기는 일은 사용자가 요청할 때만 한다.
