#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Word to PDF converter using Aspose.Words
"""
import sys
import os
import aspose.words as aw


def convert_word_to_pdf(input_path, output_path):
    """
    Convert Word document to PDF using Aspose.Words
    
    Args:
        input_path: Path to input Word document
        output_path: Path to output PDF file
        
    Returns:
        0 on success, 1 on error
    """
    try:
        # Load Word document
        doc = aw.Document(input_path)
        
        # Save as PDF
        doc.save(output_path, aw.SaveFormat.PDF)
        
        print(f"Successfully converted: {input_path} -> {output_path}", file=sys.stderr)
        return 0
        
    except Exception as e:
        print(f"Error converting document: {e}", file=sys.stderr)
        return 1


def main():
    if len(sys.argv) < 3:
        print("Usage: word_to_pdf.py <input_word_file> <output_pdf_file>", file=sys.stderr)
        return 1
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    
    if not os.path.exists(input_path):
        print(f"Error: Input file not found: {input_path}", file=sys.stderr)
        return 1
    
    return convert_word_to_pdf(input_path, output_path)


if __name__ == "__main__":
    sys.exit(main())
