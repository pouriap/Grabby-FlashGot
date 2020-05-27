/// \file UTF8.CPP Basic UTF-8 Conversion functions

/*
  (c) Mircea Neacsu 2014-2019. Licensed under MIT License.
  See README file for full license terms.
*/

#include "stdafx.h"
#include <windows.h>
#include <sys/stat.h>
#include "utf8.h"
#include <vector>
#include <assert.h>

using namespace std;
namespace utf8 {

/*!
  Conversion from wide character to UTF-8

  \param  s input string
  \return UTF-8 character string
*/
std::string narrow (const wchar_t* s)
{
  int nsz;
  if (!s || !(nsz = WideCharToMultiByte (CP_UTF8, 0, s, -1, 0, 0, 0, 0)))
    return string ();

  string out (nsz, 0);
  WideCharToMultiByte (CP_UTF8, 0, s, -1, &out[0], nsz, 0, 0);
  out.resize (nsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from wide character to UTF-8

  \param  s input string
  \return UTF-8 character string
*/
std::string narrow (const std::wstring& s)
{
  int nsz = WideCharToMultiByte (CP_UTF8, 0, s.c_str(), -1, 0, 0, 0, 0);
  if (!nsz)
    return string ();

  string out (nsz, 0);
  WideCharToMultiByte (CP_UTF8, 0, s.c_str (), -1, &out[0], nsz, 0, 0);
  out.resize (nsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from UTF-8 to wide character

  \param  s input string
  \return wide character string
*/
std::wstring widen (const char* s)
{
  int wsz;
  if (!s || !(wsz = MultiByteToWideChar (CP_UTF8, 0, s, -1, 0, 0)))
    return wstring ();

  wstring out (wsz, 0);
  MultiByteToWideChar (CP_UTF8, 0, s, -1, &out[0], wsz);
  out.resize (wsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from UTF-8 to wide character

  \param  s input string
  \return wide character string
*/
std::wstring widen (const std::string& s)
{
  int wsz = MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, 0, 0);
  if (!wsz)
    return wstring ();

  wstring out (wsz, 0);
  MultiByteToWideChar (CP_UTF8, 0, s.c_str (), -1, &out[0], wsz);
  out.resize (wsz - 1); //output is null-terminated
  return out;
}

} //end namespace
