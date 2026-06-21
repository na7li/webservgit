#include "../headers/HttpRequest.hpp"

#include <sstream>
#include <cctype>

HttpRequest::HttpRequest() : _state(INCOMPLETE)
{
}

static std::string toLower(const std::string &text)
{
	std::string	result = text;

	for (size_t i = 0; i < result.size(); ++i)
		result[i] = std::tolower(static_cast<unsigned char>(result[i]));
	return result;
}

static std::string trim(const std::string &text)
{
	std::string::size_type	start = text.find_first_not_of(" \t");
	std::string::size_type	end = text.find_last_not_of(" \t");

	if (start == std::string::npos)
		return "";
	return text.substr(start, end - start + 1);
}

static std::vector<std::string> splitLines(const std::string &head)
{
	std::vector<std::string>	lines;
	std::string::size_type		start = 0;
	std::string::size_type		pos;

	while ((pos = head.find("\r\n", start)) != std::string::npos)
	{
		lines.push_back(head.substr(start, pos - start));
		start = pos + 2;
	}
	lines.push_back(head.substr(start));
	return lines;
}

HttpRequest::State HttpRequest::parse(const std::string &raw)
{
	std::string::size_type	headerEnd = raw.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return INCOMPLETE;

	std::vector<std::string>	lines = splitLines(raw.substr(0, headerEnd));
	if (!parseRequestLine(lines[0]))
		return (_state = BAD_REQUEST);
	if (!parseHeaders(lines))
		return (_state = BAD_REQUEST);

	return checkBody(raw.substr(headerEnd + 4));
}

bool HttpRequest::parseRequestLine(const std::string &line)
{
	std::istringstream	stream(line);
	std::string			target;
	std::string			extra;

	if (!(stream >> _method >> target >> _version))
		return false;
	if (stream >> extra)
		return false;
	if (_version.compare(0, 5, "HTTP/") != 0)
		return false;

	std::string::size_type	mark = target.find('?');
	if (mark == std::string::npos)
		_path = target;
	else
	{
		_path = target.substr(0, mark);
		_query = target.substr(mark + 1);
	}
	return true;
}

bool HttpRequest::parseHeaders(const std::vector<std::string> &lines)
{
	for (size_t i = 1; i < lines.size(); ++i)
	{
		std::string::size_type	colon = lines[i].find(':');
		if (colon == std::string::npos)
			return false;

		std::string	name = toLower(trim(lines[i].substr(0, colon)));
		std::string	value = trim(lines[i].substr(colon + 1));
		_headers[name] = value;
	}
	return true;
}

HttpRequest::State HttpRequest::checkBody(const std::string &rest)
{
	std::map<std::string, std::string>::iterator	it = _headers.find("content-length");
	if (it == _headers.end())
		return (_state = COMPLETE);

	std::istringstream	stream(it->second);
	size_t				expected;
	if (!(stream >> expected))
		return (_state = BAD_REQUEST);

	if (rest.size() < expected)
		return INCOMPLETE;

	_body = rest.substr(0, expected);
	return (_state = COMPLETE);
}
