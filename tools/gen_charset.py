#!/usr/bin/env python3
"""生成 fonts/charset.txt：ASCII + Latin-1/拉丁扩展 A + 希腊/西里尔 + 常用符号
+ GB2312 全字库（沿用旧字集）+ 日文假名全块 + main/*.c、main/i18n.h 里出现的全部字符。
用法：python3 tools/gen_charset.py && pyftsubset <AlibabaPuHuiTi-3-55-Regular.ttf> \
  --text-file=fonts/charset.txt --output-file=fonts/puhui_subset.ttf \
  --layout-features='' --no-hinting --desubroutinize --drop-tables+=GSUB,GPOS,DSIG
"""
import glob, os, re
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
chars = set()
old = os.path.join(root, 'fonts', 'charset.txt')
if os.path.exists(old):
    chars.update(open(old, encoding='utf-8').read())
for lo, hi in [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F),        # ASCII, Latin-1, Latin Ext-A
               (0x370, 0x3FF), (0x400, 0x45F),                      # Greek, Cyrillic
               (0x2010, 0x2027), (0x2030, 0x203B), (0x2190, 0x2199),# 标点/箭头
               (0x3040, 0x309F), (0x30A0, 0x30FF), (0x3000, 0x303F),# 平假名/片假名/CJK 标点
               (0xFF01, 0xFF5E)]:                                    # 全角 ASCII
    chars.update(chr(c) for c in range(lo, hi + 1))
for f in glob.glob(os.path.join(root, 'main', '*.c')) + glob.glob(os.path.join(root, 'main', '*.h')):
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', open(f, encoding='utf-8').read()):
        chars.update(m.group(1))
chars.add('□'); chars.add('°'); chars.add('·'); chars.add('→'); chars.add('…')
chars = {c for c in chars if ord(c) >= 0x20 and c not in '\r\n\t'}
open(old, 'w', encoding='utf-8').write(''.join(sorted(chars)))
print(f'{len(chars)} chars → {old}')
