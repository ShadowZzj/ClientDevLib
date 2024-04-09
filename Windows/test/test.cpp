#include <pybind11/pybind11.h>
#include <pybind11/embed.h> // 用于嵌入Python
#include <pybind11/stl.h> // STL容器到Python类型的自动转换
#include <iostream>
#include <vector>
#include <General/util/File/File.h>
#include <Windows.h>
namespace py = pybind11;

// 假设byte是unsigned char的别名
using byte = unsigned char;

void call_packet_filter(std::vector<byte>& packet) {
    try {
        // 尝试导入模块
        std::string module_name = "testing";

        std::string module_path = zzj::GetExecutablePath() + "\\test.py";
        // 使用importlib导入模块
        py::module_ importlib = py::module_::import("importlib.util");
        py::object spec = importlib.attr("spec_from_file_location")(module_name, module_path);
        py::object module = importlib.attr("module_from_spec")(spec);
        spec.attr("loader").attr("exec_module")(module);

        auto res = module.attr("PacketSniffer")(packet).cast<int>();
        printf("res: %d", res);

    }
    catch (py::error_already_set& e) {
        std::cerr << "Python error: " << e.what() << std::endl;
    }
}

int main() {
    py::scoped_interpreter guard{}; // 启动Python解释器

    std::vector<byte> packet = { 0x01, 0x02, 0x03, 0x04 }; // 示例数据包

    try {
        while (true)
        {
            call_packet_filter(packet);
            Sleep(2000);
        }
    }
    catch (py::error_already_set& e) {
        std::cerr << "Python error during reload: " << e.what() << std::endl;
    }

    return 0;
}