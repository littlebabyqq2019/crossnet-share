#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Extract text from PDF files for indexing
"""
import sys
import os

def extract_text_pypdf2(pdf_path):
    """Extract text using PyPDF2"""
    try:
        import PyPDF2
        
        with open(pdf_path, 'rb') as file:
            reader = PyPDF2.PdfReader(file)
            text_parts = []
            
            # 限制页数，避免处理超大PDF
            max_pages = min(len(reader.pages), 100)
            
            for i in range(max_pages):
                try:
                    page = reader.pages[i]
                    text_parts.append(page.extract_text())
                except Exception as e:
                    # 单页提取失败，继续下一页
                    print(f"Warning: Failed to extract page {i}: {e}", file=sys.stderr)
                    continue
            
            return '\n'.join(text_parts)
    except Exception as e:
        raise Exception(f"PyPDF2 extraction failed: {e}")


def extract_text_pdfplumber(pdf_path):
    """Extract text using pdfplumber (更准确)"""
    try:
        import pdfplumber
        
        text_parts = []
        with pdfplumber.open(pdf_path) as pdf:
            # 限制页数
            max_pages = min(len(pdf.pages), 100)
            
            for i in range(max_pages):
                try:
                    page = pdf.pages[i]
                    text = page.extract_text()
                    if text:
                        text_parts.append(text)
                except Exception as e:
                    print(f"Warning: Failed to extract page {i}: {e}", file=sys.stderr)
                    continue
        
        return '\n'.join(text_parts)
    except Exception as e:
        raise Exception(f"pdfplumber extraction failed: {e}")


def extract_text(pdf_path):
    """
    Extract text from PDF file
    Tries multiple methods in order of preference
    """
    if not os.path.exists(pdf_path):
        print(f"Error: File not found: {pdf_path}", file=sys.stderr)
        return ""
    
    # 检查文件大小
    file_size_mb = os.path.getsize(pdf_path) / (1024 * 1024)
    if file_size_mb > 50:
        print(f"Warning: Large PDF file ({file_size_mb:.1f} MB), may take long time", file=sys.stderr)
    
    # 尝试多种提取方法
    methods = [
        ("pdfplumber", extract_text_pdfplumber),
        ("PyPDF2", extract_text_pypdf2),
    ]
    
    for method_name, method_func in methods:
        try:
            print(f"Trying {method_name}...", file=sys.stderr)
            text = method_func(pdf_path)
            
            if text and len(text.strip()) > 10:
                print(f"Success with {method_name}, extracted {len(text)} characters", file=sys.stderr)
                return text
            else:
                print(f"{method_name} extracted no meaningful text", file=sys.stderr)
        except Exception as e:
            print(f"{method_name} failed: {e}", file=sys.stderr)
            continue
    
    # 所有方法都失败
    print("Error: All extraction methods failed", file=sys.stderr)
    return ""


def main():
    if len(sys.argv) < 2:
        print("Usage: extract_pdf_text.py <pdf_file>", file=sys.stderr)
        return 1
    
    pdf_path = sys.argv[1]
    
    try:
        text = extract_text(pdf_path)
        
        # 输出到stdout，强制使用 UTF-8 编码
        if text:
            # 在 Windows 上需要确保输出是 UTF-8
            # 使用 buffer.write 避免编码问题
            sys.stdout.buffer.write(text.encode('utf-8'))
            return 0
        else:
            return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
