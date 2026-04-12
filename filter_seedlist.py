#!/usr/bin/env python3

from concurrent.futures import ThreadPoolExecutor, as_completed
import os
import threading
from urllib.parse import urlparse
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

# Define allowed TLDs (single and two-part)
allowed_single_tlds = {'com', 'org', 'net', 'edu', 'gov', 'ca', 'uk', 'au'}
allowed_two_part_tlds = {'co.uk', 'com.au'}

# Define forbidden strings
forbidden_strings = {'cdn', 'dns'}

input_file = 'src/crawler/seedList.txt'
output_file = 'src/crawler/seedList.txt'
removed_file = 'src/crawler/removed.txt'

kept_links = []
removed_links = []

max_workers = min(32, (os.cpu_count() or 1) * 5)

thread_local = threading.local()


def get_session():
    if not hasattr(thread_local, 'session'):
        session = requests.Session()
        retry_strategy = Retry(total=3, backoff_factor=0.5, status_forcelist=[429, 500, 502, 503, 504])
        adapter = HTTPAdapter(max_retries=retry_strategy)
        session.mount("http://", adapter)
        session.mount("https://", adapter)
        thread_local.session = session
    return thread_local.session


def is_dead_link(response):
    # Treat hard 404-style failures and common soft-404 pages as dead links.
    if response.status_code in {404, 410}:
        return True

    content_type = response.headers.get('Content-Type', '').lower()
    if 'text/html' in content_type:
        body = response.text.lower()
        if any(marker in body for marker in ('page not found', "can't be found", 'not found')):
            return True

    return False


def is_valid_tld(link):
    # Extract domain and TLD
    parsed_url = urlparse(link)
    domain = parsed_url.netloc.lower()

    # Extract TLD
    parts = domain.split('.')
    tld_valid = False

    # Check two-part TLD first (e.g., co.uk, com.au)
    if len(parts) >= 2:
        two_part_tld = '.'.join(parts[-2:])
        if two_part_tld in allowed_two_part_tlds:
            tld_valid = True

    # Check single-part TLD (e.g., com, org, edu)
    if not tld_valid and len(parts) >= 1:
        single_part_tld = parts[-1]
        if single_part_tld in allowed_single_tlds:
            tld_valid = True

    return tld_valid


def process_link(i, raw_link):
    link = raw_link.strip()
    if not link:
        return i, None, None, None

    # Check for forbidden strings
    if any(forbidden in link.lower() for forbidden in forbidden_strings):
        return i, 'removed', link, f"[{i+1}] Removed (forbidden string): {link}"

    if not is_valid_tld(link):
        return i, 'removed', link, f"[{i+1}] Removed (invalid TLD): {link}"

    # Try to fetch the page and identify dead links (page not found/unreachable).
    try:
        response = get_session().get(link, timeout=10, allow_redirects=True)

        if is_dead_link(response):
            return i, 'removed', link, f"[{i+1}] Removed (dead link - HTTP {response.status_code}): {link}"
        return i, 'kept', link, f"[{i+1}] Kept: {link}"

    except requests.exceptions.RequestException as e:
        # Unreachable URLs map to browser-level "can't be found" behavior.
        return i, 'removed', link, f"[{i+1}] Removed (dead link - connection error: {type(e).__name__}): {link}"

# Read the file
with open(input_file, 'r') as f:
    links = f.readlines()

print(f"Processing {len(links)} links with {max_workers} workers...")

results = {}
with ThreadPoolExecutor(max_workers=max_workers) as executor:
    futures = [executor.submit(process_link, i, link) for i, link in enumerate(links)]

    for future in as_completed(futures):
        i, status, link, message = future.result()
        if message:
            print(message)
        if status and link:
            results[i] = (status, link)

for i in sorted(results):
    status, link = results[i]
    if status == 'kept':
        kept_links.append(link)
    else:
        removed_links.append(link)

removed_count = len(removed_links)

# Write back the filtered links
with open(output_file, 'w') as f:
    for link in kept_links:
        f.write(link + '\n')

# Write removed links
with open(removed_file, 'w') as f:
    for link in removed_links:
        f.write(link + '\n')

print(f"\nRemoved {removed_count} links")
print(f"Kept {len(kept_links)} links")
print(f"Removed links written to {removed_file}")
