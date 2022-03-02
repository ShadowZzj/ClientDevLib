//
//  catppt.cpp
//  catppt
//
//  Created by bixiangyang on 2022/3/1.
//

#include <iostream>
#include "catppt.hpp"
#include "catpptPriv.hpp"

void catppt::HelloWorld(const char * s)
{
    catpptPriv *theObj = new catpptPriv;
    theObj->HelloWorldPriv(s);
    delete theObj;
};

void catpptPriv::HelloWorldPriv(const char * s) 
{
    std::cout << s << std::endl;
};

