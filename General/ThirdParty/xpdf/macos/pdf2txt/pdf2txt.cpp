//
//  pdf2txt.cpp
//  pdf2txt
//
//  Created by bixiangyang on 2021/11/26.
//

#include <iostream>
#include "pdf2txt.hpp"
#include "pdf2txtPriv.hpp"

void pdf2txt::HelloWorld(const char * s)
{
    pdf2txtPriv *theObj = new pdf2txtPriv;
    theObj->HelloWorldPriv(s);
    delete theObj;
};

void pdf2txtPriv::HelloWorldPriv(const char * s) 
{
    std::cout << s << std::endl;
};

