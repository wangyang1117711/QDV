# -*- coding: utf-8 -*-
"""临时诊断：直接验证含中文路径的 PNG 能否被 Qt QImage 解码（不经 QML）"""
import sys, os
os.environ["QT_QPA_PLATFORM"] = "offscreen"
from PySide6.QtGui import QGuiApplication, QImage

app = QGuiApplication(sys.argv)

paths = [
    r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\images\训练图\Image_20250206104629474.png",
    r"C:\Users\wangy\Downloads\rect2_qdv_project (4).qdvann",
]
p = paths[0]
img = QImage(p)
print("QImage.load result:", not img.isNull(), "size:", img.width(), "x", img.height())
print("path exists:", os.path.exists(p))
print("DONE")