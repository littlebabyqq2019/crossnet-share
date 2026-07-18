# 创建一个简单的蓝色图标给服务器，绿色图标给客户端
import struct

def create_simple_ico(filename, r, g, b):
    """创建一个简单的 32x32 单色图标"""
    size = 32
    
    # ICO 文件头
    header = struct.pack('<HHH', 0, 1, 1)  # Reserved, Type(1=ICO), Count
    
    # 图像目录条目
    dir_entry = struct.pack('<BBBBHHII', 
        size, size,  # Width, Height
        0,           # ColorCount (0 for >8bpp)
        0,           # Reserved
        1,           # ColorPlanes
        32,          # BitsPerPixel
        0,           # ImageSize (填充后)
        22           # ImageOffset (6+16=22)
    )
    
    # BMP 信息头
    bmp_header = struct.pack('<IIIHHIIIIII',
        40,          # HeaderSize
        size,        # Width
        size * 2,    # Height (包含 AND mask)
        1,           # Planes
        32,          # BitCount
        0,           # Compression
        size * size * 4,  # ImageSize
        0, 0, 0, 0   # 其他字段
    )
    
    # 创建像素数据 (BGRA 格式，从下到上)
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            # 创建圆形图标
            dx = x - size/2
            dy = y - size/2
            dist = (dx*dx + dy*dy) ** 0.5
            
            if dist < size/2 - 2:  # 圆形区域
                pixels.extend([b, g, r, 255])  # BGRA
            else:
                pixels.extend([0, 0, 0, 0])  # 透明
    
    # AND mask (全部为 0，使用 alpha 通道)
    and_mask = bytes(size * 4)
    
    # 组合所有部分
    image_data = bmp_header + pixels + and_mask
    
    # 更新图像大小
    dir_entry = dir_entry[:8] + struct.pack('<I', len(image_data)) + dir_entry[12:]
    
    # 写入文件
    with open(filename, 'wb') as f:
        f.write(header + dir_entry + image_data)

# 创建服务器图标 (蓝色)
create_simple_ico('server_icon.ico', 37, 99, 235)

# 创建客户端图标 (绿色)
create_simple_ico('client_icon.ico', 34, 197, 94)

print("Icons created successfully!")
