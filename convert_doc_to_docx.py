#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convert .doc files to .docx using Aspose.Words
"""
import sys
import os
import glob
import aspose.words as aw


def load_license(license_path):
    """Load Aspose.Words license"""
    if license_path and os.path.exists(license_path):
        try:
            lic = aw.License()
            lic.set_license(license_path)
            print(f"✓ License loaded: {license_path}")
            return True
        except Exception as e:
            print(f"✗ Failed to load license: {e}")
    return False


def convert_doc_to_docx(input_path, output_path=None):
    """
    Convert .doc to .docx
    
    Args:
        input_path: Path to input .doc file
        output_path: Optional output .docx path (default: same name with .docx extension)
        
    Returns:
        True if successful, False otherwise
    """
    try:
        # Generate output path if not provided
        if not output_path:
            base_name = os.path.splitext(input_path)[0]
            output_path = base_name + ".docx"
        
        # Load document
        doc = aw.Document(input_path)
        
        # Save as .docx (OOXML format)
        doc.save(output_path, aw.SaveFormat.DOCX)
        
        input_size = os.path.getsize(input_path)
        output_size = os.path.getsize(output_path)
        print(f"✓ Converted: {input_path} ({input_size:,} bytes) -> {output_path} ({output_size:,} bytes)")
        return True
        
    except Exception as e:
        print(f"✗ Failed to convert {input_path}: {e}")
        return False


def main():
    print("=" * 70)
    print("Aspose.Words .doc to .docx Converter")
    print("=" * 70)
    
    # Load license
    license_file = "Aspose.Words/python专用whl包/1.lic"
    if os.path.exists(license_file):
        load_license(license_file)
    else:
        print("Warning: License file not found, running in evaluation mode")
    
    print()
    
    # Parse arguments
    if len(sys.argv) < 2:
        print("Usage:")
        print("  Single file:   python convert_doc_to_docx.py <input.doc> [output.docx]")
        print("  Multiple files: python convert_doc_to_docx.py <pattern>")
        print()
        print("Examples:")
        print('  python convert_doc_to_docx.py document.doc')
        print('  python convert_doc_to_docx.py document.doc output.docx')
        print('  python convert_doc_to_docx.py "*.doc"')
        print('  python convert_doc_to_docx.py "folder/*.doc"')
        return 1
    
    input_arg = sys.argv[1]
    
    # Check if it's a pattern (contains * or ?)
    if '*' in input_arg or '?' in input_arg:
        # Batch conversion
        files = glob.glob(input_arg)
        if not files:
            print(f"No files found matching pattern: {input_arg}")
            return 1
        
        print(f"Found {len(files)} file(s) to convert")
        print("-" * 70)
        
        success_count = 0
        fail_count = 0
        
        for file in files:
            if file.lower().endswith('.doc') and not file.lower().endswith('.docx'):
                if convert_doc_to_docx(file):
                    success_count += 1
                else:
                    fail_count += 1
            else:
                print(f"⊘ Skipped: {file} (not a .doc file)")
        
        print("-" * 70)
        print(f"Summary: {success_count} succeeded, {fail_count} failed")
        
    else:
        # Single file conversion
        input_file = input_arg
        output_file = sys.argv[2] if len(sys.argv) > 2 else None
        
        if not os.path.exists(input_file):
            print(f"Error: File not found: {input_file}")
            return 1
        
        if not input_file.lower().endswith('.doc'):
            print(f"Warning: Input file doesn't have .doc extension: {input_file}")
        
        success = convert_doc_to_docx(input_file, output_file)
        return 0 if success else 1
    
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
