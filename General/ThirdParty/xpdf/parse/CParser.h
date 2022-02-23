#pragma once

#include <string>

//�ĵ�����������
class CParser
{
public:
	CParser(){}
	virtual ~CParser(void)
	{
	}

	//��������ö��
	enum class EncodingType
	{
		etAnsi,
		etUnicode,
		etUtf8,
		etUnicodeBE
	};

public:
	
	//���ļ���
	virtual bool Open(const wchar_t* FilePath) = 0;

	//���ڴ��д�
	virtual bool Open(const char* hMemory, int size){return false;}

	//�رգ��ͷ���Դ
	virtual void Close() = 0;

	//�Ƿ���Ч
	virtual bool IsValid() = 0;
		
	//��ȡ�����ı�
	virtual bool GetPropText(std::wstring& proptext){return false;}

	//��ȡ�����ı�
	virtual bool GetText(std::wstring& text){return false;}

	//��ȡǶ���ĵ������ı�
	virtual bool GetEmbedText(std::wstring& text){return false;}
	
	//�ĵ��Ƿ��Ѽ���
	virtual bool IsEncrypted(){return false;}


};

