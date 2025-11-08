#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import json
import time
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin, urlparse

# ----------------------------
# CONFIG
# ----------------------------
BASE_URL = "https://wiki.osdev.org"
START_PAGES = [
    "/Main_Page",
    "/Category:Kernel_Development",
    "/Category:x86-64",
    "/Category:Boot_Sequence",
    "/Category:Memory_Management",
    "/Category:Interrupts",
    "/Category:Hardware",
]

OUTPUT_FILE = "osdev_dataset.jsonl"
REQUEST_DELAY = 0.5  # секунды между запросами
MAX_PAGES = 200      # ограничение для демонстрации

# ----------------------------
# UTILS
# ----------------------------
def sanitize_text(text):
    return re.sub(r'\s+', ' ', text).strip()

def extract_code_blocks(soup):
    """Извлекает <pre> и <code>, сохраняя язык если есть."""
    blocks = []
    for pre in soup.find_all('pre'):
        code = pre.get_text()
        if len(code) > 20 and ('{' in code or 'asm' in code.lower() or '__attribute__' in code):
            blocks.append(code)
    for code_tag in soup.find_all('code'):
        parent = code_tag.find_parent('pre')
        if parent:  # уже обработано выше
            continue
        code = code_tag.get_text()
        if len(code) > 50 and ('void' in code or 'uint' in code or 'asm' in code.lower()):
            blocks.append(code)
    return blocks

def clean_code(code):
    # Удаляем лишние комментарии вида [1], [edit], etc.
    code = re.sub(r'\[\d+\]', '', code)
    code = re.sub(r'//.*?\[.*?\].*?\n', '\n', code)
    return code.strip()

# ----------------------------
# MAIN SCRAPER
# ----------------------------
def scrape_osdev_dataset():
    visited = set()
    queue = [urljoin(BASE_URL, p) for p in START_PAGES]
    records = []
    session = requests.Session()
    session.headers.update({
        'User-Agent': 'OSDev-Dataset-Builder (for educational OS research)'
    })

    while queue and len(records) < MAX_PAGES:
        url = queue.pop(0)
        if url in visited:
            continue
        visited.add(url)

        print(f"[{len(records)}] Fetching: {url}")
        try:
            resp = session.get(url, timeout=10)
            resp.raise_for_status()
            soup = BeautifulSoup(resp.text, 'html.parser')

            # Удаляем служебные блоки
            for elem in soup.select('.mw-editsection, .thumb, .navbox, .infobox, .printfooter'):
                elem.decompose()

            # Получаем заголовок
            title_elem = soup.find('h1', class_='firstHeading')
            title = sanitize_text(title_elem.get_text()) if title_elem else "Unknown"

            # Основной контент
            content = soup.find('div', {'id': 'mw-content-text'})
            if not content:
                continue

            # Извлекаем код
            code_blocks = extract_code_blocks(content)
            if not code_blocks:
                continue

            # Формируем контент-описание
            paragraphs = []
            for p in content.find_all('p'):
                txt = sanitize_text(p.get_text())
                if len(txt) > 40:
                    paragraphs.append(txt)
                if len(' '.join(paragraphs)) > 500:
                    break

            context = ' '.join(paragraphs[:3]) if paragraphs else f"Implementation details for {title}."

            # Формируем промпт
            prompt = f"Explain how to implement {title} when writing an operating system in C for x86_64. Provide a complete, working code example without standard library dependencies."

            # Собираем код
            full_code = "\n\n".join([clean_code(cb) for cb in code_blocks if len(cb) > 30])
            if not full_code or len(full_code) < 100:
                continue

            answer = f"Here is a correct implementation for {title} in C (x86_64 bare-metal):\n\n```c\n{full_code}\n```"

            records.append({
                "messages": [
                    {"role": "user", "content": prompt},
                    {"role": "assistant", "content": answer}
                ]
            })

            # Ищем внутренние ссылки для обхода
            if len(records) < MAX_PAGES:
                for a in content.find_all('a', href=True):
                    href = a['href']
                    if href.startswith('/'):
                        full_href = urljoin(BASE_URL, href)
                        if full_href not in visited and 'action=edit' not in full_href:
                            queue.append(full_href)

            time.sleep(REQUEST_DELAY)

        except Exception as e:
            print(f"  → Skip {url}: {e}")
            continue

    # Сохранение
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        for rec in records:
            f.write(json.dumps(rec, ensure_ascii=False) + '\n')

    print(f"\n✅ Dataset saved to {OUTPUT_FILE}")
    print(f"📊 Total records: {len(records)}")

if __name__ == "__main__":
    scrape_osdev_dataset()