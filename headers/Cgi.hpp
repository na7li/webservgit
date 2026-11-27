#ifndef CGI_HPP
#define CGI_HPP
#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
#include <sys/types.h>
#include <ctime>
class Cgi
{
    pid_t _pid;
    std::string _input, _output;
    std::time_t _started;
    Cgi(const Cgi &);
    Cgi &operator=(const Cgi &);

  public:
    static void reap();
    Cgi();
    ~Cgi();
    void start(const HttpRequest &r, const ServerConfig &config, const std::string &script,
               const std::string &interpreter, const std::string &remote);
    bool finish(std::string &response);
};
#endif
