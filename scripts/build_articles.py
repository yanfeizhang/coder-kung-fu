#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

import requests
from bs4 import BeautifulSoup
from opencc import OpenCC

LINK_RE = re.compile(r"\[([^\]]+)\]\((https?://[^)\s]+)\)")
SECTION_RE = re.compile(r"^#{1,6}\s*([一二三四五六七八九十]+)、")
IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png", ".gif", ".svg", ".webp")
ARTICLE_HOSTS = {"mp.weixin.qq.com", "zhuanlan.zhihu.com"}
ALLOWED_SECTIONS = set("一二三四五六七八九")
MIRROR_CACHE_PATH = Path(".article-cache/author_mirrors.json")
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"
    )
}
MIRROR_HOSTS = {
    "51cto.com",
    "blog.csdn.net",
    "cloud.tencent.com",
    "cnblogs.com",
    "developer.baidu.com",
    "jishuzhan.net",
    "juejin.cn",
    "kfngxl.cn",
    "oschina.net",
    "segmentfault.com",
    "zhuanlan.zhihu.com",
}
AUTHOR_MIRRORS: dict[str, str] | None = None
STATIC_MIRRORS = {
    "nvidia通用计算首代架构tesla与cuda10剖析":
        "https://www.cnblogs.com/wujianming-110117/p/19034994",
    "磁盘分区也是隐含了技术技巧的":
        "https://developer.cloud.tencent.com/article/1964451",
    "我们怎么解决机械硬盘既慢又容易坏的问题":
        "https://www.cnblogs.com/kfngxl/p/13924083.html",
}
TITLE_SIMPLIFIER = OpenCC("t2s")


def extract_links(readme_text: str) -> list[tuple[str, str]]:
    links: list[tuple[str, str]] = []
    seen: set[str] = set()
    current_section: str | None = None
    for line in readme_text.splitlines():
        section_match = SECTION_RE.match(line.strip())
        if section_match:
            current_section = section_match.group(1)
        if current_section not in ALLOWED_SECTIONS:
            continue
        for title, url in LINK_RE.findall(line):
            url = url.strip()
            if url.lower().endswith(IMAGE_SUFFIXES):
                continue
            hostname = (urlparse(url).hostname or "").lower()
            if hostname not in ARTICLE_HOSTS:
                continue
            if url in seen:
                continue
            seen.add(url)
            links.append((title.strip(), url))
    return links


def extract_markdown(node: BeautifulSoup) -> str:
    for tag in node.select("script,style,noscript"):
        tag.decompose()

    def render_inline(item: object) -> str:
        if not getattr(item, "name", None):
            return str(item).replace("\u00a0", " ").strip()
        element = item
        if element.name == "br":
            return "\n"
        text = "".join(render_inline(child) for child in element.children)
        text = re.sub(r"[ \t]+", " ", text).strip()
        if element.name == "a" and element.get("href") and text:
            return f"[{text}]({element['href']})"
        if element.name == "code" and text:
            return f"`{text}`"
        return text

    blocks: list[str] = []
    block_tags = {"h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "pre", "blockquote"}
    for element in node.find_all(block_tags):
        if element.find_parent(block_tags):
            continue
        if element.name.startswith("h"):
            text = render_inline(element)
            if text:
                level = min(int(element.name[1]) + 1, 6)
                blocks.append(f"{'#' * level} {text}")
        elif element.name == "li":
            text = render_inline(element)
            if text:
                blocks.append(f"- {text}")
        elif element.name == "pre":
            text = element.get_text("\n", strip=False).strip("\n")
            if text:
                blocks.append(f"```\n{text}\n```")
        elif element.name == "blockquote":
            text = render_inline(element)
            if text:
                blocks.append("\n".join(f"> {line}" for line in text.splitlines()))
        else:
            text = render_inline(element)
            if text:
                blocks.append(text)

    if not blocks:
        return node.get_text("\n", strip=True)
    markdown = "\n\n".join(block.rstrip() for block in blocks).strip()
    return "\n".join(line.rstrip() for line in markdown.splitlines())


def fetch_article(url: str) -> tuple[str, str]:
    resp = requests.get(url, headers=HEADERS, timeout=30)
    resp.raise_for_status()
    soup = BeautifulSoup(resp.text, "html.parser")

    title = ""
    content = ""

    hostname = (urlparse(url).hostname or "").lower()

    if hostname == "mp.weixin.qq.com":
        title_node = soup.select_one("#activity-name")
        content_node = soup.select_one("#js_content")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    elif hostname.endswith("cnblogs.com"):
        title_node = soup.select_one(".postTitle") or soup.select_one("h1")
        content_node = soup.select_one("#cnblogs_post_body")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    elif hostname == "cloud.tencent.com":
        title_node = soup.select_one("h1")
        content_node = (
            soup.select_one(".article-detail")
            or soup.select_one(".rich-text")
            or soup.select_one("article")
        )
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    elif hostname == "blog.csdn.net":
        title_node = soup.select_one("h1")
        content_node = soup.select_one("#content_views") or soup.select_one("article")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    elif hostname == "kfngxl.cn":
        title_node = soup.select_one(".article-title") or soup.select_one("h1")
        content_node = soup.select_one("article.article-content")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    elif hostname == "zhuanlan.zhihu.com":
        title_node = soup.select_one("h1")
        content_node = soup.select_one("article") or soup.select_one("div.Post-RichTextContainer")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(content_node) if content_node else ""
    else:
        title_node = soup.select_one("h1") or soup.select_one("title")
        body = soup.select_one("article") or soup.select_one("main") or soup.select_one("body")
        title = title_node.get_text(" ", strip=True) if title_node else ""
        content = extract_markdown(body) if body else ""

    if not title:
        title = url
    if not content:
        raise RuntimeError("No article content extracted")

    return title, content


def is_mirror_host(url: str) -> bool:
    hostname = (urlparse(url).hostname or "").lower()
    return any(hostname == host or hostname.endswith(f".{host}") for host in MIRROR_HOSTS)


def normalize_title(title: str) -> str:
    simplified = TITLE_SIMPLIFIER.convert(title)
    return re.sub(r"[^0-9a-z\u4e00-\u9fff]+", "", simplified.lower())


def titles_match(expected: str, actual: str) -> bool:
    expected_normalized = normalize_title(expected)
    actual_normalized = normalize_title(actual)
    return (
        bool(expected_normalized)
        and bool(actual_normalized)
        and (expected_normalized in actual_normalized or actual_normalized in expected_normalized)
    )


def load_author_mirrors() -> dict[str, str]:
    global AUTHOR_MIRRORS
    if AUTHOR_MIRRORS is not None:
        return AUTHOR_MIRRORS

    cache_path = MIRROR_CACHE_PATH
    if cache_path.exists():
        AUTHOR_MIRRORS = json.loads(cache_path.read_text(encoding="utf-8"))
        return AUTHOR_MIRRORS

    mirrors: dict[str, str] = {}
    for page in range(1, 21):
        soup = None
        for _ in range(3):
            try:
                response = requests.get(
                    f"http://kfngxl.cn/index.php/page/{page}/",
                    headers=HEADERS,
                    timeout=(5, 15),
                )
                response.raise_for_status()
                soup = BeautifulSoup(response.text, "html.parser")
                break
            except requests.RequestException:
                continue
        if soup is None:
            continue
        links = soup.select("article.excerpt a[href*='/archives/']")
        if not links:
            break
        for link in links:
            title = link.get_text(" ", strip=True)
            if title:
                mirrors[normalize_title(title)] = link["href"]
    AUTHOR_MIRRORS = mirrors
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(mirrors, ensure_ascii=False, indent=2), encoding="utf-8")
    return mirrors


def search_mirrors(title: str, source_url: str) -> list[str]:
    search_title = re.sub(r"^[《<]|[》>]$", "", title).strip(" 🔥")
    response = requests.get(
        "https://lite.duckduckgo.com/lite/",
        params={"q": search_title},
        headers=HEADERS,
        timeout=(5, 15),
    )
    response.raise_for_status()
    soup = BeautifulSoup(response.text, "html.parser")
    mirrors: list[str] = []
    for result in soup.select("a.result-link, a.result__a"):
        href = result.get("href", "")
        target = parse_qs(urlparse(href).query).get("uddg", [""])[0]
        target = unquote(target)
        if target and target != source_url and is_mirror_host(target):
            mirrors.append(target)
    return mirrors


def fetch_with_mirror(title: str, source_url: str, search: bool = True) -> tuple[str, str, str]:
    mirror_errors: list[str] = []
    author_url = STATIC_MIRRORS.get(normalize_title(title)) or load_author_mirrors().get(
        normalize_title(title)
    )
    mirror_urls = [author_url] if author_url else []
    if not author_url and search:
        mirror_urls.extend(search_mirrors(title, source_url))
    for mirror_url in dict.fromkeys(mirror_urls):
        if not mirror_url:
            continue
        for _ in range(3 if urlparse(mirror_url).hostname == "kfngxl.cn" else 1):
            try:
                mirror_title, content = fetch_article(mirror_url)
                if not titles_match(title, mirror_title):
                    raise RuntimeError(
                        f"mirror title mismatch: expected {title!r}, got {mirror_title!r}"
                    )
                return mirror_title, content, mirror_url
            except Exception as exc:  # noqa: BLE001
                mirror_errors.append(f"{mirror_url}: {exc}")
    details = "; ".join(mirror_errors[:3])
    if details:
        raise RuntimeError(f"source blocked; mirrors failed: {details}")
    raise RuntimeError("source blocked; no usable public mirror found")


def to_filename(index: int, title: str) -> str:
    slug = re.sub(r"[^\w\u4e00-\u9fff-]+", "-", title.strip()).strip("-").lower()
    slug = slug[:40] if slug else "article"
    return f"{index:03d}-{slug}.md"


def write_markdown(path: Path, title_zh_tw: str, source_title: str, source_url: str, content: str) -> None:
    payload = (
        "---\n"
        "layout: default\n"
        f"title: {json.dumps(title_zh_tw, ensure_ascii=False)}\n"
        "---\n\n"
        f"# {title_zh_tw}\n\n"
        f"> 原文標題：{source_title}\n\n"
        f"> 原文連結：{source_url}\n\n"
        "---\n\n"
        f"{content.strip()}\n"
    )
    path.write_text(payload, encoding="utf-8")


def write_indexes(output_dir: Path, generated: list[dict], failures: list[dict]) -> None:
    md_lines = [
        "---",
        "layout: default",
        'title: "文章列表"',
        "---",
        "",
        "# 文章列表（繁體中文 / 台灣用語）",
        "",
    ]
    for item in generated:
        md_lines.append(f"- [{item['title']}](./{item['file']})")
    (output_dir / "index.md").write_text("\n".join(md_lines) + "\n", encoding="utf-8")
    (output_dir / "failures.json").write_text(
        json.dumps(failures, ensure_ascii=False, indent=2), encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Download and convert README article links to zh-TW.")
    parser.add_argument("--readme", default="README.md")
    parser.add_argument("--output-dir", default="articles")
    parser.add_argument("--limit", type=int, default=0, help="Limit number of links (0 means all)")
    parser.add_argument(
        "--no-mirror-search",
        action="store_true",
        help="Do not search public mirrors when the original page cannot be fetched",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove existing generated pages before building",
    )
    args = parser.parse_args()

    readme_path = Path(args.readme)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for pattern in ("*.md", "*.html", "failures.json"):
            for path in output_dir.glob(pattern):
                path.unlink()

    links = extract_links(readme_path.read_text(encoding="utf-8"))
    if args.limit > 0:
        links = links[: args.limit]

    converter = OpenCC("s2twp")
    generated: list[dict] = []
    failures: list[dict] = []

    for index, (readme_title, url) in enumerate(links, start=1):
        article_url = url
        try:
            title, content = fetch_article(url)
        except Exception as original_exc:  # noqa: BLE001
            mirror_exc: Exception | None = None
            try:
                title, content, article_url = fetch_with_mirror(
                    readme_title, url, search=not args.no_mirror_search
                )
                print(f"[MIRROR] {url} <- {article_url}")
            except Exception as exc:  # noqa: BLE001
                mirror_exc = exc
            if mirror_exc is not None:
                error_text = str(original_exc)
                if mirror_exc is not None:
                    error_text = f"{error_text}; {mirror_exc}"
                failures.append({"url": url, "error": error_text})
                print(f"[FAIL] {url} -> {error_text}")
                title_tw = converter.convert(readme_title or url)
                filename = to_filename(index, title_tw)
                placeholder = (
                    "無法自動下載原文內容。可能原因包含來源網站防爬限制、地區網路限制或暫時性連線失敗。"
                    "\n\n請稍後重試，或手動補上翻譯內容。"
                )
                write_markdown(output_dir / filename, title_tw, readme_title, url, placeholder)
                generated.append(
                    {"file": filename, "title": title_tw, "url": url}
                )
                continue

        try:
            title_tw = converter.convert(title)
            content_tw = converter.convert(content)
            md_filename = to_filename(index, title_tw)
            write_markdown(output_dir / md_filename, title_tw, title, article_url, content_tw)
            generated.append(
                {"file": md_filename, "title": title_tw, "url": url}
            )
            if article_url == url:
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
            write_markdown(output_dir / filename, title_tw, readme_title, url, placeholder)
            generated.append(
                {"file": filename, "title": title_tw, "url": url}
            )

    write_indexes(output_dir, generated, failures)
    print(f"Generated: {len(generated)}, Failed: {len(failures)}")
    return 0 if generated else 1


if __name__ == "__main__":
    raise SystemExit(main())
