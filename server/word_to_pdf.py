#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Word to PDF converter using Aspose.Words
"""
import sys
import os
import aspose.words as aw


def load_license(license_path):
    """
    Load Aspose.Words license
    
    Args:
        license_path: Path to license file
        
    Returns:
        True if license loaded successfully, False otherwise
    """
    if not license_path or not os.path.exists(license_path):
        print(f"License file not found: {license_path}", file=sys.stderr)
        return False
    
    try:
        lic = aw.License()
        lic.set_license(license_path)
        print(f"Aspose.Words License set successfully from: {license_path}", file=sys.stderr)
        return True
    except Exception as e:
        print(f"Error setting Aspose.Words License: {e}", file=sys.stderr)
        return False


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
        print("Usage: word_to_pdf.py <input_word_file> <output_pdf_file> [license_file]", file=sys.stderr)
        return 1
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    license_path = sys.argv[3] if len(sys.argv) > 3 else None
    
    if not os.path.exists(input_path):
        print(f"Error: Input file not found: {input_path}", file=sys.stderr)
        return 1
    
    # Load license if provided
    if license_path:
        load_license(license_path)
    else:
        print("Warning: No license file provided, running in evaluation mode", file=sys.stderr)
    
    return convert_word_to_pdf(input_path, output_path)


if __name__ == "__main__":
    sys.exit(main())
