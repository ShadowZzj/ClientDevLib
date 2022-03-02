//
//  catdoc.cpp
//  catdoc
//
//  Created by bixiangyang on 2022/2/28.
//

#include <iostream>
#include "catdoc.hpp"
#include "catdocPriv.hpp"

void catdoc::HelloWorld(const char * s)
{
    catdocPriv *theObj = new catdocPriv;
    theObj->HelloWorldPriv(s);
    delete theObj;
};

void catdocPriv::HelloWorldPriv(const char * s) 
{
    std::cout << s << std::endl;
};

