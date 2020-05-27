/*!
  \file UTF8.H UTF-8 Conversion functions
  (c) Mircea Neacsu 2014-2019

*/
#pragma once
#include <string>
#include <vector>

namespace utf8 {

std::string narrow (const wchar_t* s);
std::string narrow (const std::wstring& s);
std::wstring widen (const char* s);
std::wstring widen (const std::string& s);

}