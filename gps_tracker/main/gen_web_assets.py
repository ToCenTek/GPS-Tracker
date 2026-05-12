#!/usr/bin/env python3
"""将index.html转换为嵌入式C头文件"""
import sys

def escape_for_c(s):
    result = []
    for c in s:
        if c == '\\':
            result.append('\\\\')
        elif c == '"':
            result.append('\\"')
        elif c == '\n':
            result.append('\\n"\n"')
        elif c == '\r':
            pass
        elif ord(c) < 32:
            result.append(f'\\x{ord(c):02x}')
        else:
            result.append(c)
    return ''.join(result)

def main():
    if len(sys.argv) < 2:
        print("Usage: gen_web_assets.py <html_file>")
        return
    with open(sys.argv[1], 'r') as f:
        html = f.read()
    escaped = escape_for_c(html)
    print('#ifndef WEB_ASSETS_H')
    print('#define WEB_ASSETS_H')
    print()
    print('#include <stddef.h>')
    print()
    print('static const char web_assets_html[] =')
    print(f'"{escaped}";')
    print()
    print(f'static const size_t web_assets_html_len = {len(html)};')
    print()
    print('#endif')

if __name__ == '__main__':
    main()
