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
	
	//���ļ���
    virtual bool Open(const wchar_t* FilePath);

    virtual bool IsEncrypted();

	//�رգ��ͷ���Դ
    virtual void Close();

	//�Ƿ���Ч
    virtual bool IsValid();
		
	//��ȡ�����ı�
	virtual bool GetText(std::wstring& text);
};

