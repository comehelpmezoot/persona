#!/usr/bin/env python3
"""Bracket-balance check for the C++ sources using a real string/comment-aware scanner."""
import sys, glob, os
def scan(src):
    depth = {'{': 0, '(': 0, '[': 0}; pairs = {'}': '{', ')': '(', ']': '['}
    i, n, line = 0, len(src), 1
    stack = []
    while i < n:
        c = src[i]
        if c == '\n': line += 1
        if src.startswith('//', i):
            while i < n and src[i] != '\n': i += 1
            continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2); j = n if j < 0 else j + 2
            line += src.count('\n', i, j); i = j; continue
        if c == '"' or c == "'":
            q = c; i += 1
            while i < n and src[i] != q:
                if src[i] == '\\': i += 1
                if i < n and src[i] == '\n': line += 1
                i += 1
            i += 1; continue
        if c in '{([': stack.append((c, line))
        elif c in '})]':
            if not stack or stack[-1][0] != pairs[c]: return f'unexpected {c} at line {line}'
            stack.pop()
        i += 1
    return f'unclosed {stack[-1][0]} opened at line {stack[-1][1]}' if stack else None
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bad = 0
for f in sorted(glob.glob(os.path.join(root, 'src', '*'))):
    if f.endswith(('.cpp', '.h')):
        r = scan(open(f, encoding='utf-8').read())
        print(('BAD ' if r else 'ok  ') + os.path.basename(f) + (' - ' + r if r else ''))
        bad += bool(r)
sys.exit(1 if bad else 0)
