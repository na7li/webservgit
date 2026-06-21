#include "../headers/Connection.hpp"

#include <sys/socket.h>
#include <sstream>

Connection::Connection(int fd) : _fd(fd), _responseReady(false)
{
}

Connection::~Connection()
{
}

bool Connection::hasDataToSend() const
{
	return _responseReady;
}

bool Connection::receive()
{
	char	buffer[4096];
	ssize_t	bytes = recv(_fd, buffer, sizeof(buffer), 0);

	if (bytes <= 0)
		return false;
	_incoming.append(buffer, bytes);

	HttpRequest::State	state = _request.parse(_incoming);
	if (state == HttpRequest::COMPLETE)
		prepareResponse();
	else if (state == HttpRequest::BAD_REQUEST)
		prepareError(400, "Bad Request");
	return true;
}

bool Connection::sendData()
{
	ssize_t	bytes = send(_fd, _outgoing.c_str(), _outgoing.size(), 0);

	if (bytes <= 0)
		return false;
	_outgoing.erase(0, bytes);
	return !_outgoing.empty();
}

// Step 6 replaces this with Response::build().
void Connection::prepareResponse()
{
	std::ostringstream	body;
	body << "<h1>Hello from webserv</h1>\n"
		 << "<p>Method: " << _request._method << "</p>\n"
		 << "<p>Path: " << _request._path << "</p>\n"
		 << "<p>Query: " << _request._query << "</p>\n";

	std::string			content = body.str();
	std::ostringstream	response;
	response << "HTTP/1.1 200 OK\r\n"
			 << "Content-Type: text/html\r\n"
			 << "Content-Length: " << content.size() << "\r\n"
			 << "Connection: close\r\n"
			 << "\r\n"
			 << content;
	_outgoing = response.str();
	_responseReady = true;
}

void Connection::prepareError(int code, const std::string &reason)
{
	std::ostringstream	body;
	body << "<h1>" << code << " " << reason << "</h1>\n";

	std::string			content = body.str();
	std::ostringstream	response;
	response << "HTTP/1.1 " << code << " " << reason << "\r\n"
			 << "Content-Type: text/html\r\n"
			 << "Content-Length: " << content.size() << "\r\n"
			 << "Connection: close\r\n"
			 << "\r\n"
			 << content;
	_outgoing = response.str();
	_responseReady = true;
}
