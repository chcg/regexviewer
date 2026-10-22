#pragma once
#include <string>

std::wstring widen(const std::string& s);
std::string  narrow(const std::wstring& w);
bool         write_file(const std::wstring& path, const std::string& bytes);
std::string  read_file(const std::wstring& path);
