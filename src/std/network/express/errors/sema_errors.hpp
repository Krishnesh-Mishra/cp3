// =============================================================================
// src/std/network/express/errors/sema_errors.hpp - see sema_errors.cpp.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3::runtime::network::express::errors::sema {

extern const char* const kHttpError;
extern const char* const kNextError;

std::vector<std::string> httpErrorFields();
std::vector<std::string> sendFileOptionNames();
std::vector<std::string> staticOptionNames();
std::vector<std::string> dotfilesValues();
bool isSendFileOption(const std::string& name);
bool isStaticOption(const std::string& name);
bool isDotfilesValue(const std::string& v);
bool checkErrorHandlerArity(std::size_t params, std::string& diag);
bool checkNextArity(std::size_t args, std::string& diag);
bool checkSendFileArity(std::size_t args, std::string& diag);

}  // namespace cp3::runtime::network::express::errors::sema
