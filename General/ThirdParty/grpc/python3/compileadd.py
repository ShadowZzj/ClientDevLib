import os
import os.path
import sys
import re
import shutil
import time
import datetime
import string

def getLibList(path):
    libList = []
    for root, dirs, files in os.walk(path):
        for file in files:
            if file.endswith(".a") or file.endswith(".dylib"):
                libList.append(os.path.join(root, file))
    return libList


def createlipo(armLibList):

    for lib in armLibList:
        outputpath = lib.replace('arm', 'universal')
        x64path = lib.replace('arm', 'x64')
        cmd = "lipo -create -output " + outputpath
        cmd += " " + lib
        cmd += " " + x64path

        print(cmd)
        os.system(cmd)


def main():
    armdir = '/Users/vbb/kepm_client_pc/ClientDevLib/General/ThirdParty/grpc/mac/arm'
    armLibList = getLibList(armdir)
    createlipo(armLibList)

main()



