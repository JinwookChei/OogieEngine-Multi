---
name: study-diagram
description: 학습 개념을 동적으로 시각화하는 단일 HTML 다이어그램(Docs/Diagrams/*.html)을 만들거나 갱신한다. 개념 설명이나 Docs/Study 학습 정리를 작성할 때, 글과 정적인 표만으로는 이해가 어려운 "시간에 따라 바뀌는 상태"(메모리 블록의 일생, 풀 Push/Pop, 락 상태 전이, 스레드 실행 순서 겹침, 패킷 버퍼 조립, 참조 카운트 변화)나 "입력에 따라 바뀌는 결과"(요청 크기 → 담당 풀)가 있으면 Claude가 스스로 이 스킬을 불러 쓴다. 사용자가 "그림으로", "시각화", "다이어그램", "동적으로 보여줘"를 요청할 때도 쓴다.
---

# study-diagram — 학습용 인터랙티브 다이어그램

`Docs/Diagrams/Phase1-3_Memory.html`처럼 **브라우저에서 바로 열리는 HTML 파일 하나**로 개념을 움직이는 그림으로 보여 준다. 학습 정리 문서(`Docs/Study`)를 보완하는 자료이며, `notion-sync` 스킬이 Notion 페이지에 embed로 올린다.

## 1. 언제 만드나 (Claude가 스스로 판단)

만든다 — 아래 중 하나라도 해당하면:
- **시간 순서**가 핵심인 개념: 블록/객체의 일생, Push/Pop, 상태 전이(Lock의 Read/Write 비트), 참조 카운트 증감
- **여러 스레드가 겹치는 순서**: 데이터 레이스, CAS 실패와 재시도, ABA, 데드락 사이클
- **입력에 따라 결과가 바뀌는** 계산: 크기 → 풀 선택, 올림 공식, 오프셋 계산
- **구조가 커서 한 장의 표로 안 보이는** 것: 메모리 배치(헤더/데이터/여분), 소유 관계 트리, 호출 경로 분기(Debug/Release)

만들지 않는다:
- 표 한 개, 코드 블록 한 개로 충분히 설명되는 개념 (규칙 목록, 용어 정의)
- 사용자가 아직 작성하지 않은 코드의 **정답 구현**을 드러내는 그림 (학습 진행 방식: 코드를 직접 주지 않는다). 개념과 동작 원리만 그린다
- 아직 답하지 않은 확인 질문의 답을 그대로 보여 주는 그림 → 만들 거라면 사용자에게 미리 알린다

판단이 애매하면 만들기 전에 한 줄로 묻는다: "이 개념은 다이어그램으로 보면 쉬울 것 같은데 만들까요?"

## 2. 파일 규칙

| 항목 | 규칙 |
|---|---|
| 위치/이름 | `Docs/Diagrams/Phase<번호>_<주제>.html` (예: `Phase1-3_Memory.html`, `Phase1-2_Lock.html`, `Phase1-4_IOCP.html`). 같은 Phase에 주제가 여러 개면 주제별로 나눈다 |
| 인코딩/줄바꿈 | UTF-8 (BOM 없음), CRLF. 쓰고 나서 바이트로 확인 |
| 구성 | **파일 하나에 CSS·JS를 모두 인라인**. 외부 스크립트/라이브러리 없음 (vanilla JS). 폰트만 Google Fonts `<link>` 허용 (오프라인이면 시스템 폰트로 대체되도록 fallback 지정) |
| 언어 | 본문 한국어. 코드 이름(`MemoryPool`, `Pop`)은 원문 그대로 |
| 시작점 | `template.html`(이 스킬 폴더)을 복사해서 시작한다. 색 토큰, 다크 모드, 레이아웃, 단계 재생기(stepper)가 들어 있다 |
| 기존 파일 | 같은 주제 파일이 있으면 새로 만들지 않고 **섹션을 추가/수정**한다 |

## 3. 디자인 규칙 (`Phase1-3_Memory.html`과 같은 모양)

- **색은 의미로만** 쓴다. `template.html`의 토큰을 그대로 쓴다:
  - `--header` 청록: 헤더·메타데이터·풀 안 상태 / `--data` 파랑: 사용자 데이터·사용 중 / `--free` 회색 빗금: 빈칸·낭비 / `--accent` 주황: 포인터·현재 선택·강조 / `--danger` 빨강: 크래시·오류·Debug 경로
- 라이트/다크 모드 모두 지원 (`prefers-color-scheme` + `data-theme`). 배경은 `body`에 명시
- 세로 한 줄 흐름: `header`(eyebrow + h1 + 한 줄 요약) → `section` 여러 개(번호 붙인 h2 + 짧은 설명 + `.panel`)
- 폭 920px 최대, 좁은 화면(720px 이하)에서 2열을 1열로. 넓은 그림은 `.scroll`로 가로 스크롤
- 진행 상태 칩(`.chip.done/.now/.next`)은 **현재 코드 상태와 일치**해야 한다. Step이 끝나면 갱신한다
- 숫자·주소는 `--f-mono` 글꼴, `font-variant-numeric: tabular-nums`

## 4. 상호작용 패턴 (하나 이상 넣는다)

| 패턴 | 언제 | 구현 |
|---|---|---|
| **단계 재생기** (◀ 이전 / ▶ 다음 / ⏯ 자동 재생 / ⟲ 처음) | 시간 순서 (Push/Pop, 스레드 교차, 상태 전이) | `template.html`의 `Stepper`: 프레임 배열 `{ caption, render(state) }`를 넘긴다. 현재 단계 번호/전체를 표시 |
| **입력 → 결과** (숫자 입력 + 슬라이더 + 프리셋 버튼) | 계산 (크기 → 풀, 올림, 오프셋) | `input` 이벤트에서 `render(v)` 한 번 호출. 입력값은 범위로 잘라 쓴다 |
| **토글** (Debug/Release, 스레드 A/B 보기) | 같은 그림의 두 경우 비교 | 버튼 그룹 + `aria-pressed` |
| **호버/클릭 상세** | 구조 그림의 칸마다 설명 | `title` 또는 아래 설명 패널 갱신 |

- 키보드로도 조작 가능해야 한다 (`button`, `input` 사용, `:focus-visible` 스타일 유지)
- 애니메이션은 짧게(150~250ms), `prefers-reduced-motion`이면 끈다
- 자동 재생은 기본으로 꺼 둔다 (사용자가 직접 넘기며 보게)

## 5. 내용 정확성 (가장 중요)

- 그림의 값은 **실제 코드와 같아야** 한다: 이름, 크기, 순서, 분기 조건. 그리기 전에 해당 소스 파일을 읽는다.
- **크기·정렬·오프셋은 추측하지 않는다.** 테스트 출력, `STATIC_ASSERT`, 표준 라이브러리 헤더로 확인한 값만 쓴다 (`sizeof(MemoryHeader) == 16`은 테스트 출력으로 확인한 값). 확인 못 한 값은 쓰지 않거나 "예시"라고 표시한다.
- JS 안의 계산(풀 테이블, 올림 공식)은 C++ 코드와 **같은 식**으로 쓴다. 계산 결과를 C++ 테스트 기댓값과 몇 개 대조한다.
- 코드나 주석에 강의 비교 문구를 넣지 않는다. 강의 코드 원문을 그림에 옮기지 않는다.
- 사용자의 MEMO 내용을 그림에 쓸 때는 바꾸지 않고 그대로 인용한다.

## 6. 만든 뒤 확인 (반드시)

1. **구조 검사**: Python으로 태그 짝, `getElementById`로 찾는 id가 모두 존재하는지 확인
   ```bash
   python -I - "<파일>" <<'EOF'
   import re, sys
   s = open(sys.argv[1], encoding="utf-8").read()
   ids = set(re.findall(r'id="([^"]+)"', s))
   used = set(re.findall(r'getElementById\(\s*[\'"]([^\'"]+)', s))
   print("missing ids:", used - ids or "none")
   for t in ("section", "div", "script", "style"):
       print(t, s.count("<" + t), s.count("</" + t + ">"))
   EOF
   ```
2. **렌더링 확인**: headless Edge로 라이트 모드 스크린샷을 찍어 `Read`로 직접 본다. 넓은 화면(1000px)과 좁은 화면(500px) 두 장. (headless Edge는 창 폭이 약 500px보다 작아지지 않는다. 400으로 찍으면 레이아웃은 500 기준인데 그림만 잘려서 오른쪽이 넘친 것처럼 보인다)
   ```bash
   E="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
   "$E" --headless=new --disable-gpu --window-size=1000,2400 --screenshot="<스크래치패드>/d_wide.png" "file:///C:/Project/OogieEngine-Multi/Docs/Diagrams/<파일>.html"
   "$E" --headless=new --disable-gpu --window-size=500,2400 --screenshot="<스크래치패드>/d_narrow.png" "file:///C:/Project/OogieEngine-Multi/Docs/Diagrams/<파일>.html"
   ```
   보는 것: 글자가 세로로 한 글자씩 쪼개지지 않는지(폭 0인 요소), 칸이 겹치거나 잘리지 않는지, 색 의미가 맞는지, 첫 화면 상태가 의도대로인지
   - ⚠️ 실제로 겪은 함정 (`Phase1-3_Memory.html`, 2026-11-12 수정)
     - `width:0%`인 칸에 글자를 넣으면 한 글자씩 세로로 쪼개진다 → 띠 위 라벨은 `position:absolute` + `left:N%` + `white-space:nowrap`
     - 글과 `<code>`가 섞인 `li`를 `display:grid`로 만들면 글 조각과 `<code>`가 각각 칸으로 흩어진다 → 번호는 `::before`를 절대 위치로, `li`는 일반 블록으로
     - `overflow-x:auto`만 주면 세로도 auto가 되어 작은 넘침에 세로 스크롤바가 생긴다 → `overflow-y:hidden`을 함께 준다
   - 다크 모드는 `--force-dark-mode` 옵션을 붙여 한 장 더 찍어 본다
   - 스크린샷은 초기 상태만 보인다. 단계 재생기의 각 프레임은 URL 해시(`#step=3`)로 시작 단계를 지정할 수 있게 만들고, 중요한 프레임 1~2개를 그 방식으로 찍어 확인한다
3. 문제가 있으면 고치고 다시 찍는다. 확인하지 못한 부분은 보고할 때 그대로 말한다.

## 7. 연결

- 학습 정리 문서의 해당 Step(또는 Phase 맨 위)에 한 줄을 넣는다:
  `> 🗺️ 인터랙티브 다이어그램: Docs/Diagrams/<파일>.html — <무엇을 볼 수 있는지 한 줄>`
- Notion에는 Phase가 끝날 때 `notion-sync` 스킬이 해당 Phase 페이지에 embed로 올린다 (이미 올린 다이어그램을 고쳤다면 그때 다시 올린다).
- 다이어그램을 만들거나 고친 것은 사용자에게 알린다: 파일 경로, 볼 수 있는 내용, 브라우저에서 여는 방법(탐색기에서 더블클릭).
- 커밋은 사용자가 요청할 때만 한다 (`docs: ...`).
