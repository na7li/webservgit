#include "../headers/HttpRequest.hpp"
#include <sstream>
#include <cctype>
#include <limits>

HttpRequest::HttpRequest() : _state(INCOMPLETE), _error(400), _limit(1048576) {}
HttpRequest::State HttpRequest::fail(int code)
{
    _error = code;
    return (_state = BAD_REQUEST);
}
static std::string lower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = std::tolower(static_cast<unsigned char>(s[i]));
    return s;
}
static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static bool number(const std::string &s, size_t &n, unsigned base)
{
    if (s.empty())
        return false;
    n = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = s[i];
        unsigned d = c >= '0' && c <= '9'   ? c - '0'
                     : c >= 'a' && c <= 'f' ? c - 'a' + 10
                     : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                            : 99;
        if (d >= base || n > (std::numeric_limits<size_t>::max() - d) / base)
            return false;
        n = n * base + d;
    }
    return true;
}
HttpRequest::State HttpRequest::parse(const std::string &raw)
{
    size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos)
        return raw.size() > 16384 ? fail(431) : INCOMPLETE;
    if (end > 16384)
        return fail(431);
    size_t lineEnd = raw.find("\r\n");
    std::istringstream first(raw.substr(0, lineEnd));
    std::string target, extra;
    if (!(first >> _method >> target >> _version) || first >> extra)
        return fail(400);
    if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
        return fail(505);
    if (target.empty() || target[0] != '/' || target.find('#') != std::string::npos)
        return fail(400);
    if (target.size() > 8192)
        return fail(414);
    for (size_t i = 0; i < target.size(); ++i)
        if (static_cast<unsigned char>(target[i]) <= 32 || target[i] == 127)
            return fail(400);
    for (size_t i = 0; i < _method.size(); ++i)
        if (!std::isupper(static_cast<unsigned char>(_method[i])))
            return fail(400);
    size_t q = target.find('?');
    _path = target.substr(0, q);
    _query = q == std::string::npos ? "" : target.substr(q + 1);
    _headers.clear();
    size_t pos = lineEnd + 2;
    while (pos < end)
    {
        size_t next = raw.find("\r\n", pos), colon = raw.find(':', pos);
        if (colon == std::string::npos || colon >= next || colon == pos)
            return fail(400);
        std::string name = lower(raw.substr(pos, colon - pos));
        for (size_t i = 0; i < name.size(); ++i)
            if (!std::isalnum(static_cast<unsigned char>(name[i])) &&
                std::string("!#$%&'*+-.^_`|~").find(name[i]) == std::string::npos)
                return fail(400);
        std::string value = trim(raw.substr(colon + 1, next - colon - 1));
        for (size_t i = 0; i < value.size(); ++i)
            if ((static_cast<unsigned char>(value[i]) < 32 && value[i] != '\t') || value[i] == 127)
                return fail(400);
        if (_headers.count(name))
            return fail(400);
        _headers[name] = value;
        pos = next + 2;
    }
    if (_version == "HTTP/1.1" && (!_headers.count("host") || _headers["host"].empty()))
        return fail(400);
    if (_headers.count("expect"))
        return fail(417);
    _body.clear();
    pos = end + 4;
    if (_headers.count("transfer-encoding"))
    {
        if (_headers.count("content-length"))
            return fail(400);
        if (lower(_headers["transfer-encoding"]) != "chunked")
            return fail(501);
        while (true)
        {
            size_t next = raw.find("\r\n", pos);
            if (next == std::string::npos)
                return raw.size() - pos > 1024 ? fail(400) : INCOMPLETE;
            std::string chunk = raw.substr(pos, next - pos);
            size_t semicolon = chunk.find(';'), len;
            if (!number(chunk.substr(0, semicolon), len, 16))
                return fail(400);
            pos = next + 2;
            if (len == 0)
            {
                if (raw.size() - pos < 2)
                    return INCOMPLETE;
                if (raw.compare(pos, 2, "\r\n") == 0)
                    return (_state = COMPLETE);
                size_t trailers = raw.find("\r\n\r\n", pos);
                if (trailers == std::string::npos)
                    return raw.size() - pos > 16384 ? fail(431) : INCOMPLETE;
                if (trailers - pos > 16384)
                    return fail(431);
                while (pos < trailers)
                {
                    size_t next = raw.find("\r\n", pos), colon = raw.find(':', pos);
                    if (colon == std::string::npos || colon >= next || colon == pos)
                        return fail(400);
                    std::string name = lower(raw.substr(pos, colon - pos));
                    if (name == "content-length" || name == "transfer-encoding" || name == "host")
                        return fail(400);
                    pos = next + 2;
                }
                return (_state = COMPLETE);
            }
            if (len > _limit - _body.size())
                return fail(413);
            if (raw.size() - pos < len || raw.size() - pos - len < 2)
                return INCOMPLETE;
            if (raw.compare(pos + len, 2, "\r\n") != 0)
                return fail(400);
            _body.append(raw, pos, len);
            pos += len + 2;
        }
    }
    size_t len = 0;
    if (_headers.count("content-length") && !number(_headers["content-length"], len, 10))
        return fail(400);
    if (len > _limit)
        return fail(413);
    if (raw.size() - pos < len)
        return INCOMPLETE;
    _body = raw.substr(pos, len);
    return (_state = COMPLETE);
}
