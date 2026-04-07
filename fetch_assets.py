import urllib.request
import json
import os
import concurrent.futures
import time

# iamcal/emoji-datasource provides sorting and category data
EMOJI_DATA_URL = "https://raw.githubusercontent.com/iamcal/emoji-data/master/emoji.json"
# Twemoji CDN still used for consistent 72x72 PNGs
TWEMOJI_BASE = "https://cdnjs.cloudflare.com/ajax/libs/twemoji/14.0.2/72x72/{}.png"

HEADER_FILE = "emoji_data.hpp"

def unified_to_char(unified):
    """Convert hex string (e.g. '1F600-1F601') to UTF-8 characters."""
    return "".join(chr(int(x, 16)) for x in unified.split("-"))

def fetch_image_from_twemoji(unified):
    # Twemoji uses lowercase hex IDs
    codepoint = unified.lower()
    possible_ids = [codepoint]
    # Handle Twemoji's inconsistent use of 'fe0f'
    if "fe0f" in codepoint:
        possible_ids.append(codepoint.replace("-fe0f", "").replace("fe0f-", ""))
    
    for eid in possible_ids:
        url = TWEMOJI_BASE.format(eid)
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req) as response:
                return response.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                continue
        except Exception as e:
            pass
    return None

def fetch_emoji_bytes(emoji_item):
    unified = emoji_item['unified']
    name = emoji_item.get('name', 'UNKNOWN')
    short_names = " ".join(emoji_item.get('short_names', []))
    category = emoji_item.get('category', '')
    subcategory = emoji_item.get('subcategory', '')
    
    tags = f"{short_names} {category} {subcategory}".lower()
    char = unified_to_char(unified)

    # Fetch base emoji
    base_result = fetch_image_from_twemoji(unified)
    if not base_result:
        return None

    emoji_info = {
        "char": char,
        "name": name,
        "tags": tags,
        "data": base_result,
        "sort": emoji_item.get('sort_order', 9999),
        "skin_variations": [None] * 5,
        "skin_chars": [None] * 5,
    }

    # Fitzpatrick modifiers
    SKIN_TONES = ["1F3FB", "1F3FC", "1F3FD", "1F3FE", "1F3FF"]
    
    variations = emoji_item.get('skin_variations', {})
    for i, tone in enumerate(SKIN_TONES):
        if tone in variations:
            v_unified = variations[tone]['unified']
            v_data = fetch_image_from_twemoji(v_unified)
            if v_data:
                emoji_info["skin_variations"][i] = v_data
                emoji_info["skin_chars"][i] = unified_to_char(v_unified)

    return emoji_info

def main():
    print("Fetching emoji data source...")
    req = urllib.request.Request(EMOJI_DATA_URL, headers={'User-Agent': 'Mozilla/5.0'})
    response = urllib.request.urlopen(req).read().decode('utf-8')
    data = json.loads(response)
    
    # Sort by standard sort order field
    data = sorted(data, key=lambda x: x.get('sort_order', 9999))
    
    print(f"Loaded {len(data)} emojis. Downloading images...")
    
    valid_emojis = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=30) as executor:
        futures = {executor.submit(fetch_emoji_bytes, item): item for item in data}
        for i, future in enumerate(concurrent.futures.as_completed(futures)):
            result = future.result()
            if result:
                valid_emojis.append(result)
            if i % 100 == 0:
                print(f"Processed {i}/{len(data)}")

    # Final sort after multi-threaded download
    valid_emojis.sort(key=lambda x: x["sort"])
    
    count = len(valid_emojis)
    print(f"Successfully downloaded {count} emojis.")
    
    # Generate C++ Header
    with open(HEADER_FILE, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <array>\n")
        f.write("#include <cstddef>\n\n")
        
        f.write("struct EmojiData {\n")
        f.write("    const char* char_str;\n")
        f.write("    const char* tags;\n")
        f.write("    const char* name;\n")
        f.write("    const unsigned char* image_data;\n")
        f.write("    size_t image_size;\n")
        f.write("    const char* skin_variations[5];\n")
        f.write("    const unsigned char* skin_images[5];\n")
        f.write("    size_t skin_sizes[5];\n")
        f.write("};\n\n")

        # Write individual binary arrays for images
        for i, e in enumerate(valid_emojis):
            f.write(f"const unsigned char data_{i}_base[] = {{")
            f.write(", ".join(f"0x{b:02x}" for b in e["data"]))
            f.write("};\n")
            for v_idx, v_data in enumerate(e["skin_variations"]):
                if v_data:
                    f.write(f"const unsigned char data_{i}_v{v_idx}[] = {{")
                    f.write(", ".join(f"0x{b:02x}" for b in v_data))
                    f.write("};\n")
            
        f.write(f"\nconstexpr std::array<EmojiData, {count}> ALL_EMOJIS = {{{{\n")
        for i, e in enumerate(valid_emojis):
            name_str = e["name"].replace('\\', '\\\\').replace('"', '\\"')
            tags_str = e["tags"].replace('\\', '\\\\').replace('"', '\\"')
            char_str = e["char"].replace('\\', '\\\\').replace('"', '\\"')
            size = len(e["data"])
            
            # Skin Tone Variations arrays
            v_chars_list = []
            for v in e["skin_chars"]:
                if v:
                    # Escape backslashes and double quotes in emoji chars
                    v_esc = v.replace('\\', '\\\\').replace('"', '\\"')
                    v_chars_list.append(f'"{v_esc}"')
                else:
                    v_chars_list.append("nullptr")
            v_chars = "{" + ", ".join(v_chars_list) + "}"
            
            v_images = "{" + ", ".join(f"data_{i}_v{vi}" if vd else "nullptr" for vi, vd in enumerate(e["skin_variations"])) + "}"
            v_sizes = "{" + ", ".join(str(len(vd)) if vd else "0" for vd in e["skin_variations"]) + "}"

            f.write(f'    {{"{char_str}", "{tags_str}", "{name_str}", data_{i}_base, {size}, {v_chars}, {v_images}, {v_sizes}}},\n')
        f.write("}};\n")

    print(f"Optimized {HEADER_FILE} with constexpr std::array.")

if __name__ == "__main__":
    main()
