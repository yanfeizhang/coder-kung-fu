#!/usr/bin/env python3
import argparse
import html
import json
import re
from pathlib import Path
from typing import Iterable

import requests
from bs4 import BeautifulSoup
from opencc import OpenCC

LINK_RE = re.compile(r"\[([^\]]+)\]\((https?://[^)\s]+)\)")
IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png", ".gif", ".svg", ".webp")
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"
    )
}


def extract_links(readme_text: str) -> list[tuple[str, str]]:
    links: list[tuple[str, str]] = []
    seen: set[str] = set()
    for title, url in LINK_RE.findall(readme_text):
        url = url.strip()
        if url.lower().endswith(IMAGE_SUFFIXES):
            continue
        if url in seen:
            continue
        seen.add(url)
        links.append((title.strip(), url))
    return links


def extract_text(node: BeautifulSoup) -> str:
    for tag in node.select("script,style,noscript"):
        tag.decompose()
    blocks: Iterable[str] = (
        item.get_text(" ", strip=True) for item in node.find_all(["h1", "h2", "h3", "p", "li"])
    )
    text = "\n\n".join(x for x in blocks if x)
    return text or node.get_text("\n", strip=True)


def fetch_article(url: str) -> tuple[str, str]:
    resp = requests.get(url, headers=HEADERS, timeout=30)
    resp.raise_for_status()
    soup = BeautifulSoup(resp.text, "html.parser")

    title = ""
    content = ""

    if "mp.weixin.qq.com" in url:
        title_node = soup.select_one("#activity-name")
        content_node = soup.select_one("#js_content")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_text(content_node) if content_node else ""
    elif "zhuanlan.zhihu.com" in url:
        title_node = soup.select_one("h1")
        content_node = soup.select_one("article") or soup.select_one("div.Post-RichTextContainer")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_text(content_node) if content_node else ""
    else:
        title_node = soup.select_one("h1") or soup.select_one("title")
        body = soup.select_one("article") or soup.select_one("main") or soup.select_one("body")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_text(body) if body else ""

    if not title:
        title = url
    if not content:
        raise RuntimeError("No article content extracted")

    return title, content


def to_filename(index: int, title: str) -> str:
    slug = re.sub(r"[^\w\u4e00-\u9fff-]+", "-", title.strip()).strip("-").lower()
    slug = slug[:40] if slug else "article"
    return f"{index:03d}-{slug}.md"


def write_markdown(path: Path, title_zh_tw: str, source_title: str, source_url: str, content: str) -> None:
    payload = (
        f"# {title_zh_tw}\n\n"
        f"> 原文標題：{source_title}\n\n"
        f"> 原文連結：{source_url}\n\n"
        "---\n\n"
        f"{content.strip()}\n"
    )
    path.write_text(payload, encoding="utf-8")


def write_html(path: Path, title_zh_tw: str, source_title: str, source_url: str, content: str) -> None:
    escaped_content = "<br><br>".join(html.escape(part) for part in content.strip().split("\n\n") if part)
    payload = (
        "<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{html.escape(title_zh_tw)}</title>"
        "</head><body>"
        f"<h1>{html.escape(title_zh_tw)}</h1>"
        f"<p><strong>原文標題：</strong>{html.escape(source_title)}</p>"
        f"<p><strong>原文連結：</strong><a href='{html.escape(source_url)}'>{html.escape(source_url)}</a></p>"
        "<hr>"
        f"<p>{escaped_content}</p>"
        "</body></html>"
    )
    path.write_text(payload, encoding="utf-8")


def write_indexes(output_dir: Path, generated: list[dict], failures: list[dict]) -> None:
    md_lines = ["# 文章列表（繁體中文 / 台灣用語）", ""]
    html_items: list[str] = []
    for item in generated:
        md_lines.append(f"- [{item['title']}](./{item['html_file']})")
        html_items.append(
            f'<li><a href="./{html.escape(item["html_file"])}">{html.escape(item["title"])}</a></li>'
        )
    if failures:
        md_lines.extend(["", "## 抓取失敗", ""])
        for item in failures:
            md_lines.append(f"- {item['url']}：{item['error']}")

    (output_dir / "index.md").write_text("\n".join(md_lines) + "\n", encoding="utf-8")

    html_doc = (
        "<!doctype html><html><head><meta charset='utf-8'><title>文章列表</title></head>"
        "<body><h1>文章列表（繁體中文 / 台灣用語）</h1><ul>"
        + "".join(html_items)
        + "</ul></body></html>"
    )
    (output_dir / "index.html").write_text(html_doc, encoding="utf-8")
    (output_dir / "failures.json").write_text(
        json.dumps(failures, ensure_ascii=False, indent=2), encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Download and convert README article links to zh-TW.")
    parser.add_argument("--readme", default="README.md")
    parser.add_argument("--output-dir", default="articles")
    parser.add_argument("--limit", type=int, default=0, help="Limit number of links (0 means all)")
    args = parser.parse_args()

    readme_path = Path(args.readme)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    links = extract_links(readme_path.read_text(encoding="utf-8"))
    if args.limit > 0:
        links = links[: args.limit]

    converter = OpenCC("s2twp")
    generated: list[dict] = []
    failures: list[dict] = []

    for index, (readme_title, url) in enumerate(links, start=1):
        try:
            title, content = fetch_article(url)
            title_tw = converter.convert(title)
            content_tw = converter.convert(content)
            md_filename = to_filename(index, title_tw)
            html_filename = md_filename.replace(".md", ".html")
            write_markdown(output_dir / md_filename, title_tw, title, url, content_tw)
            write_html(output_dir / html_filename, title_tw, title, url, content_tw)
            generated.append(
                {"file": md_filename, "html_file": html_filename, "title": title_tw, "url": url}
            )
            print(f"[OK] {url}")
        except Exception as exc:  # noqa: BLE001
            error_text = str(exc)
            failures.append({"url": url, "error": error_text})
            print(f"[FAIL] {url} -> {error_text}")
            title_tw = converter.convert(readme_title or url)
            filename = to_filename(index, title_tw)
            placeholder = (
                "無法自動下載原文內容。可能原因包含來源網站防爬限制、地區網路限制或暫時性連線失敗。"
                "\n\n請稍後重試，或手動補上翻譯內容。"
            )
            html_filename = filename.replace(".md", ".html")
            write_markdown(output_dir / filename, title_tw, readme_title, url, placeholder)
            write_html(output_dir / html_filename, title_tw, readme_title, url, placeholder)
            generated.append(
                {"file": filename, "html_file": html_filename, "title": title_tw, "url": url}
            )

    write_indexes(output_dir, generated, failures)
    print(f"Generated: {len(generated)}, Failed: {len(failures)}")
    return 0 if generated else 1


if __name__ == "__main__":
    raise SystemExit(main())
