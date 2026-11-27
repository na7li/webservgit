#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP
#include <string>
#include <vector>
#include <map>
struct HttpRequest
{
    enum State
    {
        INCOMPLETE,
        COMPLETE,
        BAD_REQUEST
    };
    std::string _method, _path, _query, _version, _body;
    std::map<std::string, std::string> _headers;
    State _state;
    int _error;
    size_t _limit;
    HttpRequest();
    State parse(const std::string &raw);

  private:
    State fail(int code);
};
#endif