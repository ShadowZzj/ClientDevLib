#include <iostream>
#include <pybind11/embed.h> // ����Ƕ��Python
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // STL������Python���͵��Զ�ת��

#include <vector>
#include <spdlog/spdlog.h>
#include <General/util/File/File.h>
#include <Windows.h>
namespace py = pybind11;
#include <regex>
// ����byte��unsigned char�ı���
using byte = unsigned char;

void CallPackageFilter(void *buffer, size_t len)
{
    static bool init = false;
    
    static py::scoped_interpreter guard{};
    try
    {
        std::string module_name             = "testing";
        boost::filesystem::path currentPath = zzj::GetDynamicLibPath(CallPackageFilter);
        auto module_path                    = currentPath / "packetfilter.py";
        py::module importlib = py::module_::import("importlib.util");
        py::object spec       = importlib.attr("spec_from_file_location")(module_name, module_path.string());
        py::object module     = importlib.attr("module_from_spec")(spec);
        spec.attr("loader").attr("exec_module")(module);

        std::vector<BYTE> packet((BYTE *)buffer, (BYTE *)buffer + len);
        module.attr("PacketSniffer")(packet);
    }
    catch (py::error_already_set &e)
    {
       
        std::string exception = e.what();
        if (exception.find("ModuleNotFoundError") != std::string::npos)
        {
            std::regex pattern("No module named '(.+)'");
            std::smatch match;
            if (std::regex_search(exception, match, pattern))
            {
                spdlog::error("ModuleNotFoundError: {0}", match[1].str());
                try
                {
                    boost::filesystem::path pythonPath = zzj::GetDynamicLibPath(CallPackageFilter);
                    pythonPath /= "pythonlib\\python.exe";
                    std::string installCmd = pythonPath.string() + " -m pip install " + match[1].str();
                    py::module subprocess = py::module_::import("subprocess");
                    subprocess.attr("run")(installCmd, py::arg("shell") = false);
                }
                catch (const py::error_already_set &e)
                {
                    spdlog::error("Python error: {0}", e.what());
                }
            }
        }
        else
            spdlog::error("Python error: {0}", exception);
    }
    catch (const std::exception& e)
    {
		spdlog::error("C++ error: {0}", e.what());
	}
    catch (...)
    {
		spdlog::error("Unknown error");
	}
}

int main()
{

    std::vector<byte> packet = {0x01, 0x02, 0x03, 0x04}; // ʾ�����ݰ�
    static auto init         = []() {
        boost::filesystem::path currentPath = zzj::GetDynamicLibPath(CallPackageFilter);
        currentPath /= "pythonlib";
        //Py_SetPath(currentPath.wstring().c_str());
        Py_SetPythonHome(currentPath.wstring().c_str());
        return 0;
    }();
    try
    {
        while (true)
        {
            CallPackageFilter(packet.data(), packet.size());
            Sleep(2000);
        }
    }
    catch (py::error_already_set &e)
    {
        std::cerr << "Python error during reload: " << e.what() << std::endl;
    }

    return 0;
}
