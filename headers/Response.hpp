#ifndef RESPONSE_HPP
#define RESPONSE_HPP
#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
class Response
{
  public:
    static std::string build(const HttpRequest &request, const ServerConfig &config);
    static std::string error(int code, const ServerConfig &config);
    static std::string pack(int code, const std::string &body, const std::string &headers = "");
    static std::string reason(int code);
    static bool cgiTarget(const HttpRequest &request, const ServerConfig &config,
                          std::string &script, std::string &interpreter);
    static std::string cgiResponse(const std::string &output);
};
#endif