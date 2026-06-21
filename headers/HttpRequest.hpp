#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <vector>
# include <map>

struct HttpRequest
{
	enum State
	{
		INCOMPLETE,		// incomplet request mazal mawslatch
		COMPLETE,		// full request received
		BAD_REQUEST		// responds 400 and closes
	};

	std::string							_method;	// GET, POST DELETE,....
	std::string							_path;		// "/images/cat.png"
	std::string							_query;		// part after '?' in the URL
	std::string							_version;	// "HTTP/1.1"
	std::map<std::string, std::string>	_headers;	// keys are lower-cased: {"host": "...", "content-length": "..."}
	std::string							_body;		// for POST
	State								_state;

	HttpRequest();

	State	parse(const std::string &raw);

	private:
		bool	parseRequestLine(const std::string &line);
		bool	parseHeaders(const std::vector<std::string> &lines);
		State	checkBody(const std::string &rest);
};

#endif