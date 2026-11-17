#pragma once

#include <string>

//文档解析器基类
class CParser
{
public:
	CParser(){}
	virtual ~CParser(void)
	{
	}

	//编码类型枚举
	enum class EncodingType
	{
		etAnsi,
		etUnicode,
		etUtf8,
		etUnicodeBE
	};

public:
	
	//从文件打开
	virtual bool Open(const wchar_t* FilePath) = 0;

	//从内存中打开
	virtual bool Open(const char* hMemory, int size){return false;}

	//关闭，释放资源
	virtual void Close() = 0;

	//是否有效
	virtual bool IsValid() = 0;
		
	//获取属性文本
	virtual bool GetPropText(std::wstring& proptext){return false;}

	//获取内容文本
	virtual bool GetText(std::wstring& text){return false;}

	//获取嵌入文档内容文本
	virtual bool GetEmbedText(std::wstring& text){return false;}
	
	//文档是否已加密
	virtual bool IsEncrypted(){return false;}


};

