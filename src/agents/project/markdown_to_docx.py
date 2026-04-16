# -*- coding: utf-8 -*-
"""
markdown_to_docx.py
~~~~~~~~~~~~~~~~~~~
将 Markdown 文本转换为格式化的 .docx 文件。

使用 pandoc 作为转换引擎，并通过自定义 reference.docx 模板控制样式。
pandoc 需安装在系统或 conda 环境中。
"""

import os
import subprocess
import tempfile
from pathlib import Path
from typing import Optional

from src.logging.logger import get_logger

logger = get_logger("markdown_to_docx")

_HERE = Path(__file__).resolve().parent
_REFERENCE_DOCX = _HERE / "reference.docx"


def _build_reference_docx() -> None:
    """
    生成 pandoc reference.docx 样式模板。
    仅在模板文件不存在时运行一次。
    """
    try:
        from docx import Document
        from docx.enum.text import WD_ALIGN_PARAGRAPH
        from docx.oxml import OxmlElement
        from docx.oxml.ns import qn
        from docx.shared import Cm, Pt, RGBColor
    except ImportError:
        logger.warning("python-docx not available, skipping reference.docx creation")
        return

    doc = Document()

    # ── 页面：A4，2.5cm 页边距 ──
    section = doc.sections[0]
    section.page_width = Cm(21)
    section.page_height = Cm(29.7)
    section.left_margin = section.right_margin = Cm(2.5)
    section.top_margin = section.bottom_margin = Cm(2.5)

    _FONT_CN = "宋体"
    _FONT_EN = "Times New Roman"
    _FONT_CODE = "Courier New"

    def _set_font(style, name_en: str, name_cn: str, size_pt: float,
                  color: Optional[RGBColor] = None, bold: bool = False):
        font = style.font
        font.name = name_en
        font.size = Pt(size_pt)
        font.bold = bold
        if color:
            font.color.rgb = color
        # East-Asia font via XML
        rpr = style.element.get_or_add_rPr()
        rFonts = OxmlElement("w:rFonts")
        rFonts.set(qn("w:eastAsia"), name_cn)
        rpr.insert(0, rFonts)

    def _set_spacing(style, before: float = 0, after: float = 6, line: Optional[float] = None):
        pf = style.paragraph_format
        pf.space_before = Pt(before)
        pf.space_after = Pt(after)
        if line is not None:
            pf.line_spacing = Pt(line)

    # Normal
    normal = doc.styles["Normal"]
    _set_font(normal, _FONT_EN, _FONT_CN, 12, RGBColor(0x26, 0x26, 0x26))
    _set_spacing(normal, after=6, line=18)

    # Heading 1
    h1 = doc.styles["Heading 1"]
    _set_font(h1, _FONT_EN, _FONT_CN, 16, RGBColor(0x1F, 0x39, 0x64), bold=True)
    _set_spacing(h1, before=12, after=4)
    # Bottom border
    pPr = h1.element.get_or_add_pPr()
    pBdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "6")
    bottom.set(qn("w:space"), "1")
    bottom.set(qn("w:color"), "1F3964")
    pBdr.append(bottom)
    pPr.append(pBdr)

    # Heading 2
    h2 = doc.styles["Heading 2"]
    _set_font(h2, _FONT_EN, _FONT_CN, 14, RGBColor(0x2E, 0x54, 0x96), bold=True)
    _set_spacing(h2, before=8, after=4)

    # Heading 3
    h3 = doc.styles["Heading 3"]
    _set_font(h3, _FONT_EN, _FONT_CN, 12, RGBColor(0x1F, 0x74, 0x8D), bold=True)
    _set_spacing(h3, before=6, after=3)

    # Code block style (Verbatim Char → used by pandoc for inline code)
    if "Verbatim Char" not in [s.name for s in doc.styles]:
        vc = doc.styles.add_style("Verbatim Char", 2)  # 2 = character style
    else:
        vc = doc.styles["Verbatim Char"]
    vc.font.name = _FONT_CODE
    vc.font.size = Pt(10)

    # Source Code (used by pandoc for code blocks)
    if "Source Code" not in [s.name for s in doc.styles]:
        sc = doc.styles.add_style("Source Code", 1)  # 1 = paragraph style
    else:
        sc = doc.styles["Source Code"]
    _set_font(sc, _FONT_CODE, _FONT_CODE, 10, RGBColor(0x2D, 0x2D, 0x2D))
    _set_spacing(sc, before=4, after=4)
    sc.paragraph_format.left_indent = Cm(0.5)
    # Gray shading
    pPr2 = sc.element.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), "F2F2F2")
    pPr2.append(shd)

    doc.save(str(_REFERENCE_DOCX))
    logger.info(f"Created reference.docx at {_REFERENCE_DOCX}")


def _get_pandoc_path() -> Optional[str]:
    """查找 pandoc 可执行文件路径。"""
    # 1. 当前 Python 环境同目录
    py_bin = Path(os.sys.executable).parent
    candidate = py_bin / "pandoc"
    if candidate.exists():
        return str(candidate)

    # 2. PATH
    try:
        result = subprocess.run(["which", "pandoc"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass

    # 3. pypandoc 内置路径
    try:
        import pypandoc
        path = pypandoc.get_pandoc_path()
        if path:
            return path
    except Exception:
        pass

    return None


def convert_markdown_to_docx(markdown_text: str, output_path: "str | Path") -> Path:
    """
    将 Markdown 字符串转换为格式化的 .docx 文件。

    使用 pandoc 作为转换引擎。首次调用会自动生成 reference.docx 样式模板。

    Args:
        markdown_text: 输入 Markdown 文本
        output_path:   输出 .docx 文件路径

    Returns:
        保存后的 Path 对象

    Raises:
        RuntimeError: pandoc 不可用时抛出
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 确保 reference.docx 存在
    if not _REFERENCE_DOCX.exists():
        _build_reference_docx()

    pandoc = _get_pandoc_path()
    if not pandoc:
        raise RuntimeError(
            "pandoc not found. Install via: conda install -c conda-forge pandoc"
        )

    # 将 Markdown 写入临时文件（避免 shell 注入 + 支持大文档）
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".md", encoding="utf-8", delete=False
    ) as tmp:
        tmp.write(markdown_text)
        tmp_path = tmp.name

    try:
        cmd = [
            pandoc,
            tmp_path,
            "-f", "markdown+smart",
            "-t", "docx",
            "-o", str(output_path),
            "--toc-depth=3",
        ]
        if _REFERENCE_DOCX.exists():
            cmd += ["--reference-doc", str(_REFERENCE_DOCX)]

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            logger.error(f"pandoc error: {result.stderr}")
            raise RuntimeError(f"pandoc conversion failed: {result.stderr}")

        logger.info(f"docx saved to {output_path} ({output_path.stat().st_size} bytes)")
        return output_path
    finally:
        os.unlink(tmp_path)
