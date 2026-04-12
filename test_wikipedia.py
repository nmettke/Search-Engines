import requests

session = requests.Session()
session.headers.update({
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
})

# Test Wikipedia
test_urls = [
    "https://en.wikipedia.org/wiki/Main_Page",
    "https://en.wikipedia.org/wiki/Ann_Arbor,_Michigan"
]

for url in test_urls:
    try:
        response = session.get(url, timeout=10)
        print(f"URL: {url}")
        print(f"Status Code: {response.status_code}")
        print(f"Content Length: {len(response.content)}")
        print(f"Content Type: {response.headers.get('content-type')}")
        print()
    except requests.exceptions.RequestException as e:
        print(f"URL: {url}")
        print(f"Error: {type(e).__name__}: {e}")
        print()
