import urllib.request
import json
import os
import concurrent.futures
import time

EMOJILIB_URL = "https://raw.githubusercontent.com/muan/emojilib/main/dist/emoji-en-US.json"
TWEMOJI_BASE = "https://cdnjs.cloudflare.com/ajax/libs/twemoji/14.0.2/72x72/{}.png"

HEADER_FILE = "emoji_data.hpp"

def get_codepoint(char):
    return "-".join(f"{ord(c):x}" for c in char)

def fetch_emoji_bytes(char, tags):
    codepoint = get_codepoint(char)
    possible_ids = [codepoint]
    # Twemoji sometimes removes fe0f
    if "fe0f" in codepoint:
        possible_ids.append(codepoint.replace("-fe0f", "").replace("fe0f-", ""))
    
    for eid in possible_ids:
        url = TWEMOJI_BASE.format(eid)
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req) as response:
                img_data = response.read()
            return {"char": char, "tags": tags, "data": img_data, "eid": eid}
        except urllib.error.HTTPError as e:
            if e.code == 404:
                continue
        except Exception as e:
            pass
            
    return None

def main():
    print("Fetching emoji data...")
    req = urllib.request.Request(EMOJILIB_URL, headers={'User-Agent': 'Mozilla/5.0'})
    response = urllib.request.urlopen(req).read().decode('utf-8')
    data = json.loads(response)
    
    emojis_to_fetch = list(data.items())
    print(f"Loaded {len(emojis_to_fetch)} emojis. Downloading images...")
    
    valid_emojis = []
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=30) as executor:
        futures = {executor.submit(fetch_emoji_bytes, char, tags): char for char, tags in emojis_to_fetch}
        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            result = future.result()
            if result:
                valid_emojis.append(result)
                if result["char"] == "😂":
                    with open("icon.png", "wb") as icon_file:
                        icon_file.write(result["data"])
            if i % 100 == 0:
                print(f"Processed {i}/{len(emojis_to_fetch)}")
                
    print(f"Downloaded {len(valid_emojis)} valid emojis.")
    
    # Generate C++ Header
    with open(HEADER_FILE, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <vector>\n")
        f.write("#include <string>\n\n")
        
        # Write individual array variables first
        for i, e in enumerate(valid_emojis):
            f.write(f"const unsigned char data_{i}[] = {{")
            hex_data = [f"0x{b:02x}" for b in e["data"]]
            f.write(", ".join(hex_data))
            f.write("};\n")
            
        f.write("\nstruct EmojiData {\n")
        f.write("    const char* char_str;\n")
        f.write("    const char* tags;\n")
        f.write("    const unsigned char* image_data;\n")
        f.write("    size_t image_size;\n")
        f.write("};\n\n")
        
        f.write("const std::vector<EmojiData> ALL_EMOJIS = {\n")
        for i, e in enumerate(valid_emojis):
            # Join tags with spaces and escape quotes
            tags_str = " ".join(e["tags"]).replace('\\', '\\\\').replace('"', '\\"')
            char_str = e["char"].replace('\\', '\\\\').replace('"', '\\"')
            size = len(e["data"])
            f.write(f'    {{"{char_str}", "{tags_str}", data_{i}, {size}}},\n')
        f.write("};\n")

    print(f"Generated {HEADER_FILE}")

if __name__ == "__main__":
    main()
