#include "../headers/ConfigParser.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

ConfigParser::ConfigParser() : _index(0)
{
}

ConfigParser::~ConfigParser()
{
}

std::vector<ServerConfig> ConfigParser::parse(const std::string &path)
{
	tokenize(readFile(path));
	_index = 0;

	std::vector<ServerConfig> servers;
	while (!atEnd())
	{
		expect("server");
		servers.push_back(parseServer());
	}
	if (servers.empty())
		throw std::runtime_error("config: no 'server' block found");
	validateServers(servers);
	return servers;
}

// --- reading & tokenizing ----------------------------

std::string ConfigParser::readFile(const std::string &path) const
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		throw std::runtime_error("config: cannot open file '" + path + "'");

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

std::string ConfigParser::stripComments(const std::string &content) const
{
	std::stringstream	in(content);
	std::string			line;
	std::string			out;

	while (std::getline(in, line))
	{
		std::string::size_type hash = line.find('#');
		if (hash != std::string::npos)
			line = line.substr(0, hash);
		out += line + "\n";
	}
	return out;
}

std::string ConfigParser::padSpecials(const std::string &content) const
{
	std::string out;

	for (std::string::size_type i = 0; i < content.size(); ++i)
	{
		char c = content[i];
		if (c == '{' || c == '}' || c == ';')
			out += std::string(" ") + c + " ";
		else
			out += c;
	}
	return out;
}

void ConfigParser::tokenize(const std::string &content)
{
	std::stringstream	stream(padSpecials(stripComments(content)));
	std::string			word;

	_tokens.clear();
	while (stream >> word)
		_tokens.push_back(word);
}

// --- token cursor -----------------------------------------------------------

bool ConfigParser::atEnd() const
{
	return _index >= _tokens.size();
}

std::string ConfigParser::peek() const
{
	if (atEnd())
		return "";
	return _tokens[_index];
}

std::string ConfigParser::advance()
{
	if (atEnd())
		throw std::runtime_error("config: unexpected end of file");
	return _tokens[_index++];
}

void ConfigParser::expect(const std::string &token)
{
	if (peek() != token)
		throw std::runtime_error("config: expected '" + token + "' but got '" + peek() + "'");
	_index++;
}

// --- server block --------------------------------------

ServerConfig ConfigParser::parseServer()
{
	ServerConfig			server;
	std::set<std::string>	seen;

	expect("{");
	while (!atEnd() && peek() != "}")
		parseServerDirective(server, seen);
	expect("}");
	if (seen.find("listen") == seen.end())
		throw std::runtime_error("config: server block is missing a 'listen' directive");
	return server;
}

void ConfigParser::parseServerDirective(ServerConfig &server, std::set<std::string> &seen)
{
	std::string key = advance();

	if (key == "location")
	{
		server._locations.push_back(parseLocation());
		return;
	}

	std::vector<std::string> args = collectArgs();
	if (key == "listen")
	{
		markSeen(seen, key);
		parseListen(server, args);
	}
	else if (key == "server_name")
	{
		markSeen(seen, key);
		requireArgs(args, 1, key);
		server._serverNames = args;
	}
	else if (key == "error_page")
		parseErrorPage(server, args);
	else if (key == "client_max_body_size")
	{
		markSeen(seen, key);
		requireExactArgs(args, 1, key);
		server._clientMaxBodySize = toSize(args[0]);
	}
	else
		throw std::runtime_error("config: unknown server directive '" + key + "'");
}

// --- location block ----------------
Location ConfigParser::parseLocation()
{
	Location				location;
	std::set<std::string>	seen;

	location._path = advance();
	expect("{");
	while (!atEnd() && peek() != "}")
		parseLocationDirective(location, seen);
	expect("}");
	if (location._root.empty() && location._redirectCode == 0)
		throw std::runtime_error("config: location '" + location._path
			+ "' needs a 'root' (or a 'return')");
	return location;
}

void ConfigParser::parseLocationDirective(Location &location, std::set<std::string> &seen)
{
	std::string					key = advance();
	std::vector<std::string>	args = collectArgs();

	if (key == "root")
	{
		markSeen(seen, key);
		requireExactArgs(args, 1, key);
		location._root = args[0];
	}
	else if (key == "index")
	{
		markSeen(seen, key);
		requireExactArgs(args, 1, key);
		location._index = args[0];
	}
	else if (key == "autoindex")
	{
		markSeen(seen, key);
		requireExactArgs(args, 1, key);
		if (args[0] != "on" && args[0] != "off")
			throw std::runtime_error("config: autoindex must be 'on' or 'off', got '" + args[0] + "'");
		location._autoindex = (args[0] == "on");
	}
	else if (key == "allow_methods")
	{
		markSeen(seen, key);
		requireArgs(args, 1, key);
		location._allowedMethods = args;
	}
	else if (key == "return")
	{
		markSeen(seen, key);
		requireExactArgs(args, 2, key);
		location._redirectCode = toInt(args[0]);
		if (location._redirectCode < 300 || location._redirectCode > 399)
			throw std::runtime_error("config: return code must be 3xx, got '" + args[0] + "'");
		location._redirectUrl = args[1];
	}
	else if (key == "upload_store")
	{
		markSeen(seen, key);
		requireExactArgs(args, 1, key);
		location._uploadPath = args[0];
	}
	else if (key == "cgi")
	{
		markSeen(seen, key);
		requireExactArgs(args, 2, key);
		location._cgiExtension = args[0];
		location._cgiPath = args[1];
	}
	else
		throw std::runtime_error("config: unknown location directive '" + key + "'");
}

// --- argument helpers -------------------

std::vector<std::string> ConfigParser::collectArgs()
{
	std::vector<std::string> args;

	while (!atEnd() && peek() != ";" && peek() != "{" && peek() != "}")
		args.push_back(advance());
	expect(";");
	return args;
}

void ConfigParser::parseListen(ServerConfig &server, const std::vector<std::string> &args)
{
	requireExactArgs(args, 1, "listen");

	std::string					value = args[0];
	std::string::size_type		colon = value.find(':');
	if (colon == std::string::npos)
	{
		server._host = "0.0.0.0";
		server._port = toPort(value);
	}
	else
	{
		server._host = value.substr(0, colon);
		server._port = toPort(value.substr(colon + 1));
	}
}

void ConfigParser::parseErrorPage(ServerConfig &server, const std::vector<std::string> &args)
{
	requireArgs(args, 2, "error_page");

	std::string page = args.back();
	for (size_t i = 0; i + 1 < args.size(); ++i)
		server._errorPages[toInt(args[i])] = page;
}

// --- validation & conversion -----------------

void ConfigParser::requireArgs(const std::vector<std::string> &args, size_t n, const std::string &directive) const
{
	if (args.size() < n)
		throw std::runtime_error("config: directive '" + directive + "' has too few arguments");
}

void ConfigParser::requireExactArgs(const std::vector<std::string> &args, size_t n, const std::string &directive) const
{
	if (args.size() != n)
		throw std::runtime_error("config: directive '" + directive + "' expects exactly "
			+ sizeToStr(n) + " argument(s)");
}

void ConfigParser::markSeen(std::set<std::string> &seen, const std::string &key) const
{
	if (!seen.insert(key).second)
		throw std::runtime_error("config: directive '" + key + "' is set more than once");
}

void ConfigParser::validateServers(const std::vector<ServerConfig> &servers) const
{
	for (size_t i = 0; i < servers.size(); ++i)
		for (size_t j = i + 1; j < servers.size(); ++j)
			if (sameEndpoint(servers[i], servers[j]) && shareName(servers[i], servers[j]))
				throw std::runtime_error("config: two server blocks share the same listen and server_name");
}

bool ConfigParser::sameEndpoint(const ServerConfig &a, const ServerConfig &b) const
{
	return a._host == b._host && a._port == b._port;
}

bool ConfigParser::shareName(const ServerConfig &a, const ServerConfig &b) const
{
	if (a._serverNames.empty() || b._serverNames.empty())
		return a._serverNames.empty() && b._serverNames.empty();

	for (size_t i = 0; i < a._serverNames.size(); ++i)
		for (size_t j = 0; j < b._serverNames.size(); ++j)
			if (a._serverNames[i] == b._serverNames[j])
				return true;
	return false;
}

int ConfigParser::toInt(const std::string &value) const
{
	std::stringstream	stream(value);
	int					result;
	char				leftover;

	if (!(stream >> result) || (stream >> leftover))
		throw std::runtime_error("config: invalid number '" + value + "'");
	return result;
}

int ConfigParser::toPort(const std::string &value) const
{
	int port = toInt(value);

	if (port < 0 || port > 65535)
		throw std::runtime_error("config: port out of range 0-65535: '" + value + "'");
	return port;
}

size_t ConfigParser::toSize(const std::string &value) const
{
	if (value.empty())
		throw std::runtime_error("config: empty size value");

	size_t		multiplier = 1;
	std::string	number = value;
	char		suffix = value[value.size() - 1];
	if (!std::isdigit(static_cast<unsigned char>(suffix)))
	{
		if (suffix == 'K' || suffix == 'k')
			multiplier = 1024;
		else if (suffix == 'M' || suffix == 'm')
			multiplier = 1024 * 1024;
		else if (suffix == 'G' || suffix == 'g')
			multiplier = 1024 * 1024 * 1024;
		else
			throw std::runtime_error("config: invalid size suffix in '" + value + "'");
		number = value.substr(0, value.size() - 1);
	}

	std::stringstream	stream(number);
	size_t				result;
	char				leftover;
	if (!(stream >> result) || (stream >> leftover))
		throw std::runtime_error("config: invalid size '" + value + "'");
	return result * multiplier;
}

std::string ConfigParser::sizeToStr(size_t n) const
{
	std::stringstream stream;

	stream << n;
	return stream.str();
}
