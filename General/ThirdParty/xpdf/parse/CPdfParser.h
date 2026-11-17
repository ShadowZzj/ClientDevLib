#pragma once

#include <mutex>
#include "CParser.h"

class PDFDoc;

class CPdfParser :
	public CParser
{
private:
	PDFDoc* m_doc;
    std::mutex m_mtx;
    std::string m_str;

public:

    CPdfParser(void);
    ~CPdfParser(void);

private:
    static void outputToStream(void* stream, const char* text, int len);

public:
	
	//从文件打开
    virtual bool Open(const wchar_t* FilePath);

    virtual bool IsEncrypted();

	//关闭，释放资源
    virtual void Close();

	//是否有效
    virtual bool IsValid();
		
	//获取内容文本
	virtual bool GetText(std::wstring& text);
};

