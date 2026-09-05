# -*- coding: utf-8 -*-
"""临时诊断：验证 QML Image 能否加载含中文/空格的本地 PNG 路径"""
import os, sys
os.environ["QT_QPA_PLATFORM"] = "offscreen"
from PySide6.QtCore import QUrl, QTimer, QCoreApplication
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent

app = QGuiApplication(sys.argv)
engine = QQmlEngine()

qml = '''
import QtQuick 2.15
import QtQuick.Window 2.15
Window {
    visible: true; width: 400; height: 300
    Image { id: img; anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false }
    Component.onCompleted: {
        var p = "%PATH%"
        img.source = p
        console.log("set source:", img.source)
    }
    Connections {
        target: img
        function onStatusChanged() {
            console.log("IMG_STATUS", img.status,
                        img.status === Image.Ready ? "READY " + img.sourceSize.width + "x" + img.sourceSize.height
                        : img.status === Image.Error ? "ERROR source=" + img.source : "")
        }
    }
    Timer { interval: 4000; running: true; repeat: false; onTriggered: Qt.quit() }
}
'''

path = r"E:\anchor\WorkBuddy\moren\单丝计数V2.0\images\训练图\Image_20250206104629474.png"
fwd = path.replace("\\", "/")
url = "file:///" + fwd
qml = qml.replace("%PATH%", url)

comp = QQmlComponent(engine)
comp.setData(qml.encode("utf-8"), QUrl())
obj = comp.create()
if not obj:
    print("CREATE FAIL", comp.errorString())
    sys.exit(1)
app.exec()
print("DONE")
