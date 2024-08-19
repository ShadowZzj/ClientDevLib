import os
import sys
currentFilePath = os.path.abspath(__file__)
llvmDir = os.path.join(currentFilePath,"../General/ThirdParty/ollvm-17.0.6/win")
llvmDir = os.path.normpath(llvmDir)
buildProps = r"""
<Project>
  <PropertyGroup>
    <LLVMInstallDir>{llvm_install_dir}</LLVMInstallDir>
    <LLVMToolsVersion>17.0.6</LLVMToolsVersion>
  </PropertyGroup>
</Project>
"""

buildProps = buildProps.format(llvm_install_dir=llvmDir)
targetPath = sys.argv[1]
targetFileName = os.path.join(targetPath,"Directory.Build.props")

with open(targetFileName, "w") as f:
    f.write(buildProps)

print("Generated Directory.Build.props at: " + targetFileName)


