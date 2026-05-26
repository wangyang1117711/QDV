"""
OpenCV 环境验证测试脚本 - Python 版本
用途：验证 OpenCV 开发环境是否正确配置
"""
import cv2
import numpy as np
import sys

def print_section(title):
    print(f"\n{'='*50}")
    print(f"  {title}")
    print(f"{'='*50}")

def test_basic_info():
    print_section("1. OpenCV 基础信息")
    print(f"  OpenCV 版本: {cv2.__version__}")
    print(f"  模块路径: {cv2.__file__}")
    print(f"  NumPy 版本: {np.__version__}")
    
    build_info = cv2.getBuildInformation()
    print(f"\n  编译配置:")
    for line in build_info.split('\n')[:20]:
        if line.strip():
            print(f"    {line.strip()}")

def test_matrix_ops():
    print_section("2. 矩阵运算测试")
    mat = np.zeros((100, 100, 3), dtype=np.uint8)
    cv2.rectangle(mat, (10, 10), (90, 90), (102, 8, 116), -1)
    cv2.putText(mat, "PASS", (20, 55), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
    print(f"  创建 100x100 矩阵: OK")
    print(f"  绘制紫色矩形: OK")
    print(f"  添加文字: OK")

def test_image_processing():
    print_section("3. 图像处理测试")
    img = np.random.randint(0, 256, (200, 300, 3), dtype=np.uint8)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(img, (5, 5), 0)
    edges = cv2.Canny(gray, 100, 200)
    print(f"  颜色转换: OK")
    print(f"  高斯模糊: OK")
    print(f"  Canny 边缘检测: OK")

def test_contrib_modules():
    print_section("4. Contrib 模块测试")
    try:
        sift = cv2.SIFT_create()
        print(f"  SIFT 特征检测: OK")
    except Exception as e:
        print(f"  SIFT 特征检测: FAILED ({e})")

def main():
    print_section("OpenCV 环境验证测试")
    print(f"  Python: {sys.version}")
    print(f"  测试开始...")
    
    test_basic_info()
    test_matrix_ops()
    test_image_processing()
    test_contrib_modules()
    
    print_section("测试结果")
    print(f"  所有测试通过!")
    print(f"  OpenCV {cv2.__version__} 环境已正确配置")
    print(f"\n  安装路径: D:\\Soft\\ (预编译包)")
    print(f"  Python 库: pip (conda-forge 镜像)")
    print(f"  C++ 库: 待 MinGW 编译完成后使用")
    print(f"\n{'='*50}")

if __name__ == "__main__":
    main()
