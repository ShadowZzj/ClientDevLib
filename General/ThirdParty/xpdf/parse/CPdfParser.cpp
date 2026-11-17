#include <aconf.h>
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
#include <mutex>

#include "CPdfParser.h"


    CPdfParser::CPdfParser(void) : m_doc(NULL)
    {
    }

    CPdfParser::~CPdfParser(void)
    {
        Close();
    }

void CPdfParser::outputToStream(void* stream, const char* text, int len)
    {
        CPdfParser* pThis = (CPdfParser*)stream;
        std::string& str = pThis->m_str;
        for (int i = 0; i < len; i++)
            str += text[i];
    }

    //从文件打开
    bool CPdfParser::Open(const wchar_t* FilePath)
    {
#ifdef _WIN32
        std::string fileName = wstring2string(FilePath);
#else
        std::string fileName = wstring_to_utf8(FilePath);
#endif
        m_doc = new PDFDoc((char*)fileName.c_str(), nullptr, nullptr);
        if (m_doc == nullptr)
            return false;
        return m_doc->isOk() && m_doc->okToCopy();
    }

    bool CPdfParser::IsEncrypted()
    {
        if (!m_doc || m_doc->isOk() == false)
            return false;

        return m_doc->isEncrypted();
    }

    //关闭，释放资源
    void CPdfParser::Close()
    {
        if (m_doc)
        {
            delete m_doc;
            m_doc = NULL;
        }
    }

    //是否有效
    bool CPdfParser::IsValid()
    {
        return m_doc != NULL;
    }

    //获取内容文本
    bool CPdfParser::GetText(std::wstring& text)
    {
        bool bResult = false;
        char cfgFileName[256] = "";

        if (!IsValid())
            return false;

        TextOutputControl textOutControl;
        TextOutputDev* textOut = nullptr;

        int firstPage = 1;
        int lastPage = 0;


        do
        {
            // read config file
            if (globalParams == nullptr)
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                if (globalParams == nullptr)
                {
                    globalParams = new GlobalParams(cfgFileName);
                    if (globalParams == nullptr)
                        break;
                    globalParams->setTextEncoding("UTF-8");
                }
            }

            // get page range
            firstPage = 1;
            lastPage = m_doc->getNumPages();

            textOutControl.mode = textOutReadingOrder;

            m_str.clear();
            m_str.reserve(2048);
            textOut = new TextOutputDev(outputToStream, this, &textOutControl);
            if (textOut && textOut->isOk())
            {
                m_doc->displayPages(textOut, firstPage, lastPage, 72, 72, 0,
                    gFalse, gTrue, gFalse);
            }
        } while (false);

        if (textOut)
            delete textOut;

        text = utf8_to_wstring(m_str);
        return !text.empty();
    }

