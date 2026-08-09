// ExampleCodeGenerator.cpp - 生成 C++/Python 调用示例代码

#include "Export/ExampleCodeGenerator.h"
#include "Export/ExportConfig.h"

#include <QTextStream>

QString ExampleCodeGenerator::generateCppMain(const ExportConfig& config)
{
    QString code;
    QTextStream s(&code);

    s << QStringLiteral("// 算子流程调用示例（C++）\n");
    s << QStringLiteral("// 编译: g++ -o main main.cpp -L./bin -lQDVPipeline\n");
    s << QStringLiteral("// 运行: ./main\n\n");
    s << QStringLiteral("#include <windows.h>\n");
    s << QStringLiteral("#include <stdio.h>\n");
    s << QStringLiteral("#include <string>\n\n");
    s << QStringLiteral("// 加载 DLL\n");
    s << QStringLiteral("typedef void* (*CreateFn)();\n");
    s << QStringLiteral("typedef int  (*LoadFn)(void*, const char*);\n");
    s << QStringLiteral("typedef int  (*SetImgFn)(void*, const char*);\n");
    s << QStringLiteral("typedef int  (*RunFn)(void*);\n");
    s << QStringLiteral("typedef const char* (*GetJsonFn)(void*);\n");
    s << QStringLiteral("typedef void (*DestroyFn)(void*);\n");
    s << QStringLiteral("typedef const char* (*GetErrFn)(void*);\n\n");
    s << QStringLiteral("int main() {\n");
    s << QStringLiteral("    HMODULE h = LoadLibraryA(\"bin/QDVPipeline.dll\");\n");
    s << QStringLiteral("    if (!h) { printf(\"DLL加载失败\\n\"); return 1; }\n\n");
    s << QStringLiteral("    auto create  = (CreateFn)GetProcAddress(h, \"QDVP_Pipeline_Create\");\n");
    s << QStringLiteral("    auto load    = (LoadFn)GetProcAddress(h, \"QDVP_Pipeline_Load\");\n");
    s << QStringLiteral("    auto setImg  = (SetImgFn)GetProcAddress(h, \"QDVP_Pipeline_SetInputImage\");\n");
    s << QStringLiteral("    auto run     = (RunFn)GetProcAddress(h, \"QDVP_Pipeline_Run\");\n");
    s << QStringLiteral("    auto getJson = (GetJsonFn)GetProcAddress(h, \"QDVP_Pipeline_GetResultJson\");\n");
    s << QStringLiteral("    auto destroy = (DestroyFn)GetProcAddress(h, \"QDVP_Pipeline_Destroy\");\n");
    s << QStringLiteral("    auto getErr  = (GetErrFn)GetProcAddress(h, \"QDVP_GetLastError\");\n\n");
    s << QStringLiteral("    void* p = create();\n");
    s << QStringLiteral("    int r = load(p, \"scheme/pipeline.json\");\n");
    s << QStringLiteral("    if (r != 0) { printf(\"加载失败: %s\\n\", getErr(p)); destroy(p); return 2; }\n\n");
    s << QStringLiteral("    r = setImg(p, \"test.png\");\n");
    s << QStringLiteral("    if (r != 0) { printf(\"图像失败: %s\\n\", getErr(p)); destroy(p); return 3; }\n\n");
    s << QStringLiteral("    r = run(p);\n");
    s << QStringLiteral("    if (r != 0) { printf(\"执行失败: %s\\n\", getErr(p)); destroy(p); return 4; }\n\n");
    s << QStringLiteral("    const char* json = getJson(p);\n");
    s << QStringLiteral("    printf(\"结果: %s\\n\", json);\n\n");
    s << QStringLiteral("    destroy(p);\n");
    s << QStringLiteral("    FreeLibrary(h);\n");
    s << QStringLiteral("    return 0;\n");
    s << QStringLiteral("}\n");
    s.flush();
    return code;
}

QString ExampleCodeGenerator::generateCppCMake(const ExportConfig& config)
{
    QString code;
    QTextStream s(&code);
    s << QStringLiteral("cmake_minimum_required(VERSION 3.16)\n");
    s << QStringLiteral("project(") << config.interfaceName << QStringLiteral("Example LANGUAGES CXX)\n");
    s << QStringLiteral("set(CMAKE_CXX_STANDARD 17)\n");
    s << QStringLiteral("add_executable(main main.cpp)\n");
    s << QStringLiteral("# 链接 QDVPipeline DLL 的导入库\n");
    s << QStringLiteral("target_link_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../bin)\n");
    s << QStringLiteral("target_link_libraries(main QDVPipeline)\n");
    s.flush();
    return code;
}

QString ExampleCodeGenerator::generatePythonMain(const ExportConfig& config)
{
    QString code;
    QTextStream s(&code);
    s << QStringLiteral("# 算子流程调用示例（Python）\n");
    s << QStringLiteral("# 运行: python main.py\n\n");
    s << QStringLiteral("import os, sys\n");
    s << QStringLiteral("sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'bin'))\n");
    s << QStringLiteral("from qdv_pipeline import ") << config.interfaceName << QStringLiteral("\n\n");
    s << QStringLiteral("def main():\n");
    s << QStringLiteral("    with ") << config.interfaceName << QStringLiteral("() as p:\n");
    s << QStringLiteral("        if p.load(os.path.join('..', '..', 'scheme', 'pipeline.json')) != 0:\n");
    s << QStringLiteral("            print('加载失败:', p.last_error()); return 1\n");
    s << QStringLiteral("        if p.set_input_image('test.png') != 0:\n");
    s << QStringLiteral("            print('图像失败:', p.last_error()); return 2\n");
    s << QStringLiteral("        if p.run() != 0:\n");
    s << QStringLiteral("            print('执行失败:', p.last_error()); return 3\n");
    s << QStringLiteral("        result = p.get_result_json()\n");
    s << QStringLiteral("        print('结果:', result)\n");
    s << QStringLiteral("    return 0\n\n");
    s << QStringLiteral("if __name__ == '__main__':\n");
    s << QStringLiteral("    sys.exit(main())\n");
    s.flush();
    return code;
}

QString ExampleCodeGenerator::generatePythonWrapper(const ExportConfig& config)
{
    QString code;
    QTextStream s(&code);

    s << QStringLiteral("\"\"\"QDV 算子流程 Python wrapper（ctypes 加载 QDVPipeline.dll）\n");
    s << QStringLiteral("\n");
    s << QStringLiteral("用法:\n");
    s << QStringLiteral("    from qdv_pipeline import ") << config.interfaceName << QStringLiteral("\n");
    s << QStringLiteral("    with ") << config.interfaceName << QStringLiteral("() as p:\n");
    s << QStringLiteral("        p.load('scheme/pipeline.json')\n");
    s << QStringLiteral("        p.set_input_image('test.png')\n");
    s << QStringLiteral("        p.run()\n");
    s << QStringLiteral("        result = p.get_result_json()\n");
    s << QStringLiteral("\"\"\"\n\n");
    s << QStringLiteral("import ctypes\n");
    s << QStringLiteral("import os\n");
    s << QStringLiteral("import json\n\n");

    s << QStringLiteral("class ") << config.interfaceName << QStringLiteral(":\n");
    s << QStringLiteral("    def __init__(self, runtime_dir=None):\n");
    s << QStringLiteral("        \"\"\"初始化，定位 QDVPipeline.dll。\n");
    s << QStringLiteral("        :param runtime_dir: DLL 所在目录，默认为与本文件同级的运行时目录\n");
    s << QStringLiteral("        \"\"\"\n");
    s << QStringLiteral("        self._dll = self._load_dll(runtime_dir)\n");
    s << QStringLiteral("        self._handle = None\n\n");

    s << QStringLiteral("    def _load_dll(self, runtime_dir):\n");
    s << QStringLiteral("        dll_name = 'QDVPipeline.dll'\n");
    s << QStringLiteral("        # 搜索路径：1) 指定目录 2) 同级目录 3) 同级 qdv_runtime/\n");
    s << QStringLiteral("        candidates = []\n");
    s << QStringLiteral("        if runtime_dir:\n");
    s << QStringLiteral("            candidates.append(os.path.join(runtime_dir, dll_name))\n");
    s << QStringLiteral("        candidates.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), dll_name))\n");
    s << QStringLiteral("        candidates.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'qdv_runtime', dll_name))\n");
    s << QStringLiteral("        for path in candidates:\n");
    s << QStringLiteral("            if os.path.exists(path):\n");
    s << QStringLiteral("                return ctypes.CDLL(path)\n");
    s << QStringLiteral("        raise FileNotFoundError(f'找不到 {dll_name}，搜索路径: {candidates}')\n\n");

    s << QStringLiteral("    def load(self, scheme_path):\n");
    s << QStringLiteral("        \"\"\"加载方案文件。返回 0=成功，非0=失败。\"\"\"\n");
    s << QStringLiteral("        self._handle = self._dll.QDVP_Pipeline_Create()\n");
    s << QStringLiteral("        if not self._handle:\n");
    s << QStringLiteral("            return 99\n");
    s << QStringLiteral("        return self._dll.QDVP_Pipeline_Load(self._handle, scheme_path.encode('utf-8'))\n\n");

    s << QStringLiteral("    def set_input_image(self, image_path):\n");
    s << QStringLiteral("        \"\"\"设置输入图像。返回 0=成功。\"\"\"\n");
    s << QStringLiteral("        if not self._handle: return 1\n");
    s << QStringLiteral("        return self._dll.QDVP_Pipeline_SetInputImage(self._handle, image_path.encode('utf-8'))\n\n");

    s << QStringLiteral("    def run(self):\n");
    s << QStringLiteral("        \"\"\"执行流程。返回 0=成功。\"\"\"\n");
    s << QStringLiteral("        if not self._handle: return 1\n");
    s << QStringLiteral("        return self._dll.QDVP_Pipeline_Run(self._handle)\n\n");

    s << QStringLiteral("    def get_result_json(self):\n");
    s << QStringLiteral("        \"\"\"获取 JSON 结果，解析为 dict。\"\"\"\n");
    s << QStringLiteral("        if not self._handle: return {}\n");
    s << QStringLiteral("        raw = self._dll.QDVP_Pipeline_GetResultJson(self._handle)\n");
    s << QStringLiteral("        text = ctypes.cast(raw, ctypes.c_char_p).value.decode('utf-8')\n");
    s << QStringLiteral("        try:\n");
    s << QStringLiteral("            return json.loads(text)\n");
    s << QStringLiteral("        except Exception:\n");
    s << QStringLiteral("            return text\n\n");

    s << QStringLiteral("    def get_result(self, name):\n");
    s << QStringLiteral("        \"\"\"获取指定算子结果。\"\"\"\n");
    s << QStringLiteral("        if not self._handle: return {}\n");
    s << QStringLiteral("        raw = self._dll.QDVP_Pipeline_GetResult(self._handle, name.encode('utf-8'))\n");
    s << QStringLiteral("        text = ctypes.cast(raw, ctypes.c_char_p).value.decode('utf-8')\n");
    s << QStringLiteral("        try:\n");
    s << QStringLiteral("            return json.loads(text)\n");
    s << QStringLiteral("        except Exception:\n");
    s << QStringLiteral("            return text\n\n");

    s << QStringLiteral("    def save_output_image(self, name, save_path):\n");
    s << QStringLiteral("        \"\"\"保存指定算子输出图。\"\"\"\n");
    s << QStringLiteral("        if not self._handle: return 1\n");
    s << QStringLiteral("        return self._dll.QDVP_Pipeline_GetOutputImage(self._handle, name.encode('utf-8'), save_path.encode('utf-8'))\n\n");

    s << QStringLiteral("    def version(self):\n");
    s << QStringLiteral("        raw = self._dll.QDVP_GetVersion()\n");
    s << QStringLiteral("        return ctypes.cast(raw, ctypes.c_char_p).value.decode('utf-8')\n\n");

    s << QStringLiteral("    def last_error(self):\n");
    s << QStringLiteral("        if not self._handle: return ''\n");
    s << QStringLiteral("        raw = self._dll.QDVP_GetLastError(self._handle)\n");
    s << QStringLiteral("        return ctypes.cast(raw, ctypes.c_char_p).value.decode('utf-8')\n\n");

    s << QStringLiteral("    def close(self):\n");
    s << QStringLiteral("        if self._handle:\n");
    s << QStringLiteral("            self._dll.QDVP_Pipeline_Destroy(self._handle)\n");
    s << QStringLiteral("            self._handle = None\n\n");

    s << QStringLiteral("    def __enter__(self): return self\n");
    s << QStringLiteral("    def __exit__(self, *a): self.close()\n");
    s.flush();
    return code;
}
