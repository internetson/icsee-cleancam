with open('extracted/web-x/js/main.js', 'r', encoding='utf-8', errors='ignore') as f:
    js = f.read()

import re
print("Length of main.js:", len(js))

# Search for endpoints or paths
paths = re.findall(r'["\'](/[^"\']+)["\']', js)
print("Paths found in main.js:", set(paths))

# Search for POST or GET requests
requests = re.findall(r'type:\s*["\'](POST|GET)["\']', js)
print("Request types:", set(requests))

# Print first 2000 characters of main.js
print("\n--- Start of main.js ---")
print(js[:1500])
