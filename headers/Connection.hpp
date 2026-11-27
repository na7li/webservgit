#ifndef CONNECTION_HPP
#define CONNECTION_HPP
#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
#include "Cgi.hpp"
#include <ctime>
class Connection
{
    int _fd;
    std::string _incoming, _outgoing;
    size_t _sent;
    bool _responseReady;
    HttpRequest _request;
    ServerConfig _config;
    std::vector<ServerConfig> _configs;
    Cgi *_cgi;
    std::time_t _activity, _created;
    std::string _remote;
    Connection(const Connection &);
    Connection &operator=(const Connection &);
    void prepareResponse();
    void prepareError(int code);

  public:
    Connection(int fd, const std::vector<ServerConfig> &configs, const std::string &remote);
    ~Connection();
    bool hasDataToSend() const;
    bool wantsRead() const;
    bool receive();
    bool sendData();
    bool tick();
};
#endif
