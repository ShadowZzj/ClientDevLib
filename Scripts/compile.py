import os
import sys
import platform
import subprocess
import shlex
import io
import threading
from termcolor import colored
from concurrent.futures import ThreadPoolExecutor

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
platformDir = ""
platformProjectSuffix = ""
compilerPath = ""
compileArg = ""


def GetProjectFileIndir(path):
    file_list = os.listdir(path)
    for file in file_list:
        cur_path = os.path.join(path, file)
        fileName = os.path.basename(cur_path)
        if fileName.endswith(platformProjectSuffix):
            return cur_path
    return ""


def GetProjectFileList(path):

    ret = []
    file_list = os.listdir(path)

    for file in file_list:

        cur_path = os.path.join(path, file)

        if os.path.isdir(cur_path):
            fileName = os.path.basename(cur_path)
            projectFileName = GetProjectFileIndir(cur_path)
            if projectFileName != "":
                ret.append(projectFileName)
            retArr = GetProjectFileList(cur_path)
            for item in retArr:
                ret.append(item)

    return ret


lock = threading.Lock()

'"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe" "C:\\Users\\zhuzhengjia\\Desktop\\red-pass\\redpass-daemon\\ClientDevLib\\Windows\\build\\x64\\ClientDevLib.sln"'
def Compile(projectFile, res):
    cmd = compilerPath + ' ' + compileArg + '"'+projectFile+'"'
    cmd = shlex.split(cmd)
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    stdout, stderr = p.communicate()

    lock.acquire()
    print(stdout.decode('gb2312'))
    print("---------------------------------------------")
    res[projectFile] = p.returncode
    lock.release()


def GetWinCompilerPath():
    cmd = '"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\\**\\Bin\\MSBuild.exe"'
    cmd = shlex.split(cmd)
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    stdout, stderr = p.communicate()
    stdout = stdout.decode('utf-8')
    stdout = stdout[0:-2]
    stdout = '"' + stdout + '"'
    return stdout

def main():
    curDir = os.path.split(os.path.realpath(__file__))[0]
    macPackageDir = os.path.join(curDir, "../MacOS/build/x64/Release/bin")
    runPlatform = platform.platform()
    if len(sys.argv) == 2 and runPlatform.find("macOS") != -1:
        if(sys.argv[1] == "-removesignatures"):
            # iterate files in current directory, remove signatures in all files
            for root, dirs, files in os.walk(macPackageDir):
                for file in files:
                    filePath = os.path.join(root, file)
                    os.system("codesign --remove-signature " + filePath)
            print("Removing signature files done!")
        return

    global platformDir
    global platformProjectSuffix
    global compilerPath
    global compileArg

    if runPlatform.find("Windows") != -1:
        platformDir = "Windows"
        platformProjectSuffix = ".sln"
        compilerPath = GetWinCompilerPath()
        compileArg = ""
    elif runPlatform.find("macOS") != -1:
        platformDir = "MacOS"
        platformProjectSuffix = ".xcodeproj"
        compilerPath = "xcodebuild"
        compileArg = " -project "
    else:
        print("Unsupport platform")
        return

    projectSearchDir = os.path.dirname(curDir)
    projectSearchDir = os.path.join(projectSearchDir, platformDir)
    projectFileList = GetProjectFileList(projectSearchDir)
    result = {}

    threadPool = ThreadPoolExecutor(max_workers=4, thread_name_prefix="test_")
    for projectFile in projectFileList:
        uture = threadPool.submit(Compile, projectFile, result)

    threadPool.shutdown(wait=True)

    isMakePkg = True
    for key in result.keys():
        color = ""
        output = key + "     "
        if result[key] == 0:
            output = output + "Success"
            color = "green"
        else:
            output = output + "Fail"
            color = "red"
            isMakePkg = False
        print(colored(output, color))

main()
