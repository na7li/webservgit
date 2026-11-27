#include "../headers/Connection.hpp"
#include "../headers/Response.hpp"
#include <sys/socket.h>

Connection::Connection(int fd, const std::vector<ServerConfig> &configs, const std::string &remote)
    : _fd(fd), _sent(0), _responseReady(false), _config(configs[0]), _configs(configs), _cgi(NULL),
      _activity(std::time(NULL)), _created(_activity), _remote(remote)
{
    _request._limit = _config._clientMaxBodySize;
}
Connection::~Connection()
{
    delete _cgi;
}
bool Connection::hasDataToSend() const
{
    return _responseReady;
}
bool Connection::wantsRead() const
{
    return !_responseReady && !_cgi;
}
bool Connection::receive()
{
    char buffer[8192];
    ssize_t bytes = recv(_fd, buffer, sizeof(buffer), 0);
    if (bytes <= 0)
        return false;
    _activity = std::time(NULL);
    _incoming.append(buffer, bytes);
    // Select the virtual host as soon as headers are available, before body limits apply.
    HttpRequest::State state = _request.parse(_incoming);
    if (_incoming.find("\r\n\r\n") != std::string::npos && _request._headers.count("host"))
    {
        std::string host = _request._headers["host"];
        host = host.substr(0, host.find(':'));
        for (size_t i = 0; i < _configs.size(); ++i)
            for (size_t j = 0; j < _configs[i]._serverNames.size(); ++j)
                if (_configs[i]._serverNames[j] == host)
                    _config = _configs[i];
        _request._limit = _config._clientMaxBodySize;
        state = _request.parse(_incoming);
    }
    if (state == HttpRequest::COMPLETE)
        prepareResponse();
    else if (state == HttpRequest::BAD_REQUEST)
        prepareError(_request._error);
    if (_incoming.size() > _request._limit && _incoming.size() - _request._limit > 65536 &&
        !_responseReady && !_cgi)
        prepareError(413);
    return true;
}
bool Connection::sendData()
{
    ssize_t bytes = send(_fd, _outgoing.data() + _sent, _outgoing.size() - _sent, 0);
    if (bytes <= 0)
        return false;
    _activity = std::time(NULL);
    _sent += bytes;
    return _sent < _outgoing.size();
}
void Connection::prepareError(int code)
{
    _outgoing = Response::error(code, _config);
    _responseReady = true;
    _incoming.clear();
}
void Connection::prepareResponse()
{
    try
    {
        std::string script, interpreter;
        if (Response::cgiTarget(_request, _config, script, interpreter))
        {
            _cgi = new Cgi;
            _cgi->start(_request, _config, script, interpreter, _remote);
        }
        else
        {
            _outgoing = Response::build(_request, _config);
            _responseReady = true;
        }
    }
    catch (int code)
    {
        delete _cgi;
        _cgi = NULL;
        // Build produces route-specific Allow headers for rejected methods.
        if (code == 405)
        {
            _outgoing = Response::build(_request, _config);
            _responseReady = true;
        }
        else
            prepareError(code);
    }
    _incoming.clear();
}
bool Connection::tick()
{
    if (_cgi)
    {
        try
        {
            if (_cgi->finish(_outgoing))
            {
                delete _cgi;
                _cgi = NULL;
                _responseReady = true;
                _activity = std::time(NULL);
            }
        }
        catch (int code)
        {
            delete _cgi;
            _cgi = NULL;
            prepareError(code);
        }
    }
    if (std::time(NULL) - _activity >= 10 ||
        (!_responseReady && !_cgi && std::time(NULL) - _created >= 30))
    {
        if (_responseReady)
            return false;
        prepareError(408);
        _activity = std::time(NULL);
    }
    return true;
}
