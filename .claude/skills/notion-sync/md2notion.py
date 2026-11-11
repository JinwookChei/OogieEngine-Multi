"""Docs/Study의 Markdown 한 파일을 Notion-flavored Markdown으로 바꾼다.

사용법: python -I md2notion.py <입력.md> <출력.md>
출력 파일 첫 줄 = 원문 H1 제목(이모지 포함), 둘째 줄부터 = Notion에 넣을 본문.

변환 규칙
- 첫 '# ' 제목은 본문에서 빼고 첫 줄로 돌려준다 (Notion 페이지 제목 + 아이콘으로 쓴다)
- '>' 인용 묶음 -> <callout>. 첫 글자 이모지로 아이콘/색을 정한다 (없으면 📌 gray)
- 콜아웃 안의 줄은 각각 별도 블록 (목록/표 줄은 묶어서 유지)
- 언어 없는 ``` -> ```plain text (안 그러면 Notion이 javascript로 잡는다)
- 인라인 코드 밖의 '<태그' 는 '\\<' 로 이스케이프
- 코드 블록 밖에서만 빈 줄 3개 이상을 2개로 줄인다
"""
import re
import sys

COLORS = {
    "💡": "yellow_bg", "📝": "gray_bg", "⚠️": "red_bg", "🔧": "blue_bg",
    "🧩": "purple_bg", "💬": "green_bg", "✅": "green_bg", "🔺": "orange_bg",
    "❌": "red_bg", "📌": "gray_bg",
}


def esc(line):
    parts = line.split("`")
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r"<(?=[A-Za-z/!])", r"\\<", parts[i])
    return "`".join(parts)


def callout(lines):
    body = [(l[1:].lstrip(" ") if l.startswith(">") else l).rstrip() for l in lines]
    icon, color = "📌", "gray_bg"
    first = next((i for i, l in enumerate(body) if l.strip()), None)
    if first is not None:
        for e, c in COLORS.items():
            if body[first].startswith(e):
                icon, color = e, c
                body[first] = body[first][len(e):].lstrip()
                break

    blocks, cur, kind = [], [], None
    for l in body:
        if not l.strip():
            if cur:
                blocks.append(cur)
            cur, kind = [], None
            continue
        k = "list" if re.match(r"^\s*(-|\d+\.)\s", l) else "table" if l.lstrip().startswith("|") else "p"
        if k == "p" or k != kind:
            if cur:
                blocks.append(cur)
            cur, kind = [l], k
        else:
            cur.append(l)
    if cur:
        blocks.append(cur)

    texts = ["\n".join(esc(x) for x in b) for b in blocks]
    # 콜아웃 제목(첫 블록)은 문단이어야 한다. 목록/표로 시작하면 빈 제목을 둔다
    if not texts or re.match(r"^\s*(-|\d+\.)\s|^\s*\|", texts[0]):
        texts.insert(0, "<p />")
    return f'<callout icon="{icon}" color="{color}">\n' + "\n\n".join(texts) + "\n</callout>"


def convert(src_lines):
    out, title, in_code, i = [], None, False, 0
    while i < len(src_lines):
        l = src_lines[i]
        if l.startswith("```"):
            if not in_code and l.strip() == "```":
                l = "```plain text"
            in_code = not in_code
            out.append(("code", l)); i += 1; continue
        if in_code:
            out.append(("code", l)); i += 1; continue
        if title is None and l.startswith("# "):
            title = l[2:].strip(); i += 1
            continue
        if l.startswith(">"):
            grp = []
            while i < len(src_lines) and src_lines[i].startswith(">"):
                grp.append(src_lines[i]); i += 1
            out += [("text", ""), ("text", callout(grp)), ("text", "")]
            continue
        out.append(("text", esc(l))); i += 1

    # 코드 블록 밖의 연속 빈 줄만 하나로 줄인다
    result, blank = [], 0
    for kind, l in out:
        if kind == "text" and not l.strip():
            blank += 1
            if blank > 1:
                continue
        else:
            blank = 0
        result.append(l)
    return title or "", "\n".join(result).strip() + "\n"


if __name__ == "__main__":
    src = open(sys.argv[1], encoding="utf-8-sig").read().splitlines()
    title, body = convert(src)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(title + "\n" + body)
