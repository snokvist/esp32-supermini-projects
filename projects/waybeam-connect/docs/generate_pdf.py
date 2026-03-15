#!/usr/bin/env python3
"""Generate a styled PDF from USERGUIDE.md using markdown + weasyprint."""

import os
import markdown
from weasyprint import HTML

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MD_PATH = os.path.join(SCRIPT_DIR, "USERGUIDE.md")
PDF_PATH = os.path.join(SCRIPT_DIR, "USERGUIDE.pdf")

CSS = """
@page {
    size: A4;
    margin: 20mm 18mm 20mm 18mm;
    @bottom-center {
        content: "waybeam-connect — User Guide";
        font-size: 8pt;
        color: #999;
        font-family: 'Inter', 'Helvetica Neue', Arial, sans-serif;
    }
    @bottom-right {
        content: "Page " counter(page) " of " counter(pages);
        font-size: 8pt;
        color: #999;
        font-family: 'Inter', 'Helvetica Neue', Arial, sans-serif;
    }
}

@page :first {
    @bottom-center { content: none; }
    @bottom-right { content: none; }
}

body {
    font-family: 'Inter', 'Helvetica Neue', Arial, sans-serif;
    font-size: 10pt;
    line-height: 1.55;
    color: #1a1a1a;
    max-width: 100%;
}

h1 {
    font-size: 22pt;
    font-weight: 700;
    color: #0d47a1;
    border-bottom: 3px solid #0d47a1;
    padding-bottom: 8px;
    margin-top: 0;
    margin-bottom: 6px;
}

h1 + p {
    font-size: 10pt;
    color: #555;
    font-style: italic;
    margin-top: 0;
}

h2 {
    font-size: 15pt;
    font-weight: 700;
    color: #1565c0;
    border-bottom: 1.5px solid #bbdefb;
    padding-bottom: 4px;
    margin-top: 28px;
    margin-bottom: 10px;
    page-break-after: avoid;
}

h3 {
    font-size: 12pt;
    font-weight: 600;
    color: #1e88e5;
    margin-top: 18px;
    margin-bottom: 6px;
    page-break-after: avoid;
}

h4 {
    font-size: 10.5pt;
    font-weight: 600;
    color: #333;
    margin-top: 12px;
    margin-bottom: 4px;
}

p {
    margin: 6px 0;
}

ul, ol {
    margin: 4px 0;
    padding-left: 22px;
}

li {
    margin: 2px 0;
}

strong {
    font-weight: 600;
    color: #0d47a1;
}

code {
    font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', 'Consolas', monospace;
    font-size: 8.5pt;
    background: #f0f4f8;
    padding: 1px 4px;
    border-radius: 3px;
    color: #c62828;
}

pre {
    font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', 'Consolas', monospace;
    font-size: 7.5pt;
    line-height: 1.4;
    background: #f5f7fa;
    border: 1px solid #dde3ea;
    border-left: 3px solid #1565c0;
    border-radius: 4px;
    padding: 10px 12px;
    overflow-x: auto;
    page-break-inside: avoid;
    white-space: pre;
}

pre code {
    background: none;
    padding: 0;
    border-radius: 0;
    font-size: inherit;
    color: #1a1a1a;
}

table {
    border-collapse: collapse;
    width: 100%;
    margin: 10px 0;
    font-size: 9pt;
    page-break-inside: avoid;
}

th {
    background: #e3f2fd;
    color: #0d47a1;
    font-weight: 600;
    text-align: left;
    padding: 6px 8px;
    border: 1px solid #bbdefb;
}

td {
    padding: 5px 8px;
    border: 1px solid #dde3ea;
    vertical-align: top;
}

tr:nth-child(even) td {
    background: #fafbfc;
}

blockquote {
    margin: 10px 0;
    padding: 8px 14px;
    border-left: 4px solid #ffa726;
    background: #fff8e1;
    color: #5d4037;
    font-size: 9.5pt;
    page-break-inside: avoid;
}

blockquote strong {
    color: #e65100;
}

hr {
    border: none;
    border-top: 1px solid #dde3ea;
    margin: 20px 0;
}

/* Title page spacing */
h1:first-of-type {
    margin-top: 60px;
    font-size: 26pt;
    text-align: center;
    border-bottom: none;
    padding-bottom: 0;
}

h1:first-of-type + p {
    text-align: center;
    font-size: 11pt;
    margin-bottom: 30px;
}

/* Keep diagrams together */
h3 + pre, h3 + p + pre {
    page-break-before: avoid;
}
"""

def main():
    with open(MD_PATH, "r") as f:
        md_text = f.read()

    html_body = markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "toc"],
        output_format="html5",
    )

    html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<style>{CSS}</style>
</head>
<body>
{html_body}
</body>
</html>"""

    HTML(string=html_doc).write_pdf(PDF_PATH)
    size_kb = os.path.getsize(PDF_PATH) / 1024
    print(f"Generated {PDF_PATH} ({size_kb:.0f} KB)")

if __name__ == "__main__":
    main()
