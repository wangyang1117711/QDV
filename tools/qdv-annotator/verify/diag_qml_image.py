# -*- coding: utf-8 -*-
"""临时诊断：QML Image 组件对含中文路径 PNG 的加载状态（不创建 Window，避免 offscreen 挂起）"""
import sys, os
os.environ["QT_QPA_PLATFORM"] = "offscreen"
from PySide6.QtCore import QUrl, QTimer, QCoreApplication, QObject
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent

app = QGuiApplication(sys.argv)
engine = QQmlEngine()

QML = '''
import QtQuick 2.15
import QtQml 2.15
Item {
    id: h
    property int events: 0
    property string src: ""
    property string report: ""
    Image {
        id: img
        cache: false
        asynchronous: false
    }
    function go(u) {
        img.source = u
        report += "SOURCE=" + img.source + "\\n"
        // 轮询状态直到 Ready/Error
        var tryCheck = function() {
            ++h.events
            var st = img.status
            if (st === Image.Ready) report += "STATUS=READY " + img.sourceSize.width + "x" + img.sourceSize.height
            else if (st === Image.Error) report += "STATUS=ERROR"
            else report += "STATUS=LOADING(" + st + ")"
            if (h.events > 50) { report += "\\nTIMEOUT=GIVING_UP" ; Qt.quit() }
            if (st === Image.Ready || st === Image.Error) Qt.quit()
            else timer.restart()
        }
        var timer = Qt.createQmlObject(
            'import QtQuick 2.15; Timer { interval: 100; repeat: false }',
            h, "timer")
        timer.triggered.connect(tryCheck)
        timer.start()
    }
}
'''

comp = QQmlComponent(engine)
comp.setData(QML.encode("utf-8"), QUrl())
obj = comp.create()
if not obj:
    print("CREATE_FAIL", comp.errorString().encode("unicode_escape").decode())
    sys.exit(1)

path = r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\images\训练图\Image_20250206104629474.png"
fwd = path.replace("\\", "/")
url = "file:///" + fwd
obj.go(url)

QTimer.singleShot(8000, QCoreApplication.quit)
app.exec()
r = obj.property("report")
print(r)
print("DONE")