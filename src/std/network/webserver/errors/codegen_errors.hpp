// =============================================================================
// src/std/network/webserver/errors/codegen_errors.hpp - see codegen_errors.cpp.
// =============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cp3::runtime::network::webserver::errors::codegen {

std::string sendFileOptionsText(
    const std::vector<std::pair<std::string, std::string>>& fields);
std::string staticOptionsText(
    const std::vector<std::pair<std::string, std::string>>& fields);
std::string sendFileCallText(const std::string& res, const std::string& req,
                             const std::string& path, const std::string& options);
std::string staticCallText(const std::string& dir, const std::string& options);
std::string errorHandlerAdaptText(const std::string& handler);
std::string nextCallText(const std::string& next, const std::string& arg);
std::string httpErrorText(int status, const std::string& message);

}  // namespace cp3::runtime::network::webserver::errors::codegen
