//========================================================================
//
// pdftotext.cc
//
// Copyright 1997-2013 Glyph & Cog, LLC
//
//========================================================================



#include <aconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#ifdef DEBUG_FP_LINUX
#  include <fenv.h>
#  include <fpu_control.h>
#endif
#include "gmem.h"
#include "gmempp.h"
#include "parseargs.h"
#include "GString.h"
#include "GList.h"
#include "GlobalParams.h"
#include "Object.h"
#include "Stream.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "PDFDoc.h"
#include "TextOutputDev.h"
#include "CharTypes.h"
#include "UnicodeMap.h"
#include "TextString.h"
#include "Error.h"
#include "config.h"

#include "CStrCvt.h"
#include <iostream>
using namespace std;

#include "CPdfParser.h"

static int firstPage = 1;
static int lastPage = 0;
static GBool physLayout = gFalse;
static GBool simpleLayout = gFalse;
static GBool simple2Layout = gFalse;
static GBool tableLayout = gFalse;
static GBool linePrinter = gFalse;
static GBool rawOrder = gFalse;
static double fixedPitch = 0;
static double fixedLineSpacing = 0;
static GBool clipText = gFalse;
static GBool discardDiag = gFalse;
static char textEncName[128] = "";
static char textEOL[16] = "";
static GBool noPageBreaks = gFalse;
static GBool insertBOM = gFalse;
static double marginLeft = 0;
static double marginRight = 0;
static double marginTop = 0;
static double marginBottom = 0;
static char ownerPassword[33] = "\001";
static char userPassword[33] = "\001";
static GBool quiet = gFalse;
static char cfgFileName[256] = "";
static GBool listEncodings = gFalse;
static GBool printVersion = gFalse;
static GBool printHelp = gFalse;


#include <locale>
#include <codecvt>
#pragma warning(disable:4996)

#ifdef COMPILE_WIN
#include <Windows.h>
#endif

string str;

static void outputToString(void* stream, const char* text, int len)
{
    string* str = (string*)stream;    
    for (int i = 0; i < len; i++)
        *str += text[i];
}

bool ExtractText(char* lpszFileName, std::wstring& content, bool* isEncrypt)
{
    PDFDoc* doc;
    char* fileName;
    GString* ownerPW, * userPW;
    TextOutputControl textOutControl;
    TextOutputDev* textOut;
    UnicodeMap* uMap;
    GBool ok;
    char* p;
    int exitCode;

#ifdef DEBUG_FP_LINUX
    // enable exceptions on floating point div-by-zero
    feenableexcept(FE_DIVBYZERO);
    // force 64-bit rounding: this avoids changes in output when minor
    // code changes result in spills of x87 registers; it also avoids
    // differences in output with valgrind's 64-bit floating point
    // emulation (yes, this is a kludge; but it's pretty much
    // unavoidable given the x87 instruction set; see gcc bug 323 for
    // more info)
    fpu_control_t cw;
    _FPU_GETCW(cw);
    cw = (fpu_control_t)((cw & ~_FPU_EXTENDED) | _FPU_DOUBLE);
    _FPU_SETCW(cw);
#endif

    exitCode = 99;

    fileName = lpszFileName;

    // read config file
    globalParams = new GlobalParams(cfgFileName);
    globalParams->setTextEncoding("UTF-8");


    if (textEOL[0]) {
        if (!globalParams->setTextEOL(textEOL)) {
            fprintf(stderr, "Bad '-eol' value on command line\n");
        }
    }

    // get mapping to output encoding
    if (!(uMap = globalParams->getTextEncoding())) {
        error(errConfig, -1, "Couldn't get text encoding");
        goto err1;
    }

    // open PDF file
    doc = new PDFDoc(fileName, nullptr, nullptr);
    
    if (!doc->isOk()) {
        exitCode = 1;
        goto err2;
    }

    // check for copy permission
    if (!doc->okToCopy()) {
        error(errNotAllowed, -1,
            "Copying of text from this document is not allowed.");
        exitCode = 3;
        goto err2;
    }


    // get page range
    if (firstPage < 1) {
        firstPage = 1;
    }
    if (lastPage < 1 || lastPage > doc->getNumPages()) {
        lastPage = doc->getNumPages();
    }

    // write text file
    if (tableLayout) {
        textOutControl.mode = textOutTableLayout;
        textOutControl.fixedPitch = fixedPitch;
    }
    else if (physLayout) {
        textOutControl.mode = textOutPhysLayout;
        textOutControl.fixedPitch = fixedPitch;
    }
    else if (simpleLayout) {
        textOutControl.mode = textOutSimpleLayout;
    }
    else if (simple2Layout) {
        textOutControl.mode = textOutSimple2Layout;
    }
    else if (linePrinter) {
        textOutControl.mode = textOutLinePrinter;
        textOutControl.fixedPitch = fixedPitch;
        textOutControl.fixedLineSpacing = fixedLineSpacing;
    }
    else if (rawOrder) {
        textOutControl.mode = textOutRawOrder;
    }
    else {
        textOutControl.mode = textOutReadingOrder;
    }
    textOutControl.clipText = clipText;
    textOutControl.discardDiagonalText = discardDiag;
    textOutControl.insertBOM = insertBOM;
    textOutControl.marginLeft = marginLeft;
    textOutControl.marginRight = marginRight;
    textOutControl.marginTop = marginTop;
    textOutControl.marginBottom = marginBottom;
    //textOut = new TextOutputDev(textFileName->getCString(), &textOutControl,
    //    gFalse, gTrue);

    str.reserve(1024);
    textOut = new TextOutputDev(outputToString, &str, &textOutControl);
    if (textOut->isOk()) {
        doc->displayPages(textOut, firstPage, lastPage, 72, 72, 0,
            gFalse, gTrue, gFalse);
    }
    else {
        delete textOut;
        exitCode = 2;
        goto err3;
    }
    delete textOut;

    exitCode = 0;

    // clean up
err3:
err2:
    delete doc;
    uMap->decRefCnt();
err1:
    delete globalParams;
err0:

    // check for memory leaks
    Object::memCheck(stderr);
    gMemReport(stderr);

    std::wstring wstr = utf8_to_wstring(str);
    wcout << wstr.c_str() << endl;

    return !wstr.empty();
}

int main(int argc, char *argv[])
{
    for (int i = 0; i < 1000; i++)
    {
        cout << i << endl;
        str.clear();
        bool isEncrypt = false;
        std::wstring content; 
        ExtractText(argv[1], content, &isEncrypt);
        Sleep(100);
    }
    return 0;
}
