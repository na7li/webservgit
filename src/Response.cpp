#include "../headers/Response.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

std::string Response::reason(int c)
{
    switch (c)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 411:
        return "Length Required";
    case 413:
        return "Content Too Large";
    case 414:
        return "URI Too Long";
    case 415:
        return "Unsupported Media Type";
    case 417:
        return "Expectation Failed";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    case 505:
        return "HTTP Version Not Supported";
    default:
        return "Response";
    }
}
std::string Response::pack(int code, const std::string &body, const std::string &headers)
{
    std::ostringstream out;
    out << "HTTP/1.1 " << code << " " << reason(code)
        << "\r\nServer: webserv\r\nConnection: close\r\nContent-Length: "
        << (code == 204 ? 0 : body.size()) << "\r\n"
        << headers << "\r\n";
    if (code != 204)
        out << body;
    return out.str();
}

static void safeParents(const std::string &path)
{
    for (size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/')
        {
            std::string parent = path.substr(0, i);
            int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
            if (fd < 0)
            {
                struct stat s;
                if (stat(parent.c_str(), &s) == 0)
                    throw 403;
                throw 404;
            }
            close(fd);
        }
}
static std::string readFile(const std::string &path)
{
    safeParents(path);
    struct stat s;
    if (stat(path.c_str(), &s) != 0)
        throw 404;
    if (!S_ISREG(s.st_mode))
        throw 403;
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        throw 403;
    std::string out;
    char buffer[16384];
    try
    {
        while (true)
        {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n < 0)
                throw 500;
            if (!n)
                break;
            out.append(buffer, n);
        }
    }
    catch (...)
    {
        close(fd);
        throw;
    }
    close(fd);
    return out;
}
static std::string escape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        switch (s[i])
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += s[i];
        }
    }
    return out;
}
static std::string encode(const std::string &s)
{
    const char *hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = s[i];
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static std::string decode(const std::string &path)
{
    std::string out;
    for (size_t i = 0; i < path.size(); ++i)
    {
        unsigned char c = path[i];
        if (c == '%')
        {
            if (i + 2 >= path.size() || hex(path[i + 1]) < 0 || hex(path[i + 2]) < 0)
                throw 400;
            c = hex(path[i + 1]) * 16 + hex(path[i + 2]);
            i += 2;
        }
        if (c < 32 || c == 127 || c == '\\')
            throw 400;
        out += c;
    }
    std::istringstream parts(out);
    std::string part;
    while (std::getline(parts, part, '/'))
        if (part == "..")
            throw 403;
    return out;
}
static const Location &route(const HttpRequest &r, const ServerConfig &c, std::string &path)
{
    path = decode(r._path);
    const Location *best = NULL;
    for (size_t i = 0; i < c._locations.size(); ++i)
    {
        const std::string &p = c._locations[i]._path;
        if (path.compare(0, p.size(), p) == 0 &&
            (p == "/" || p[p.size() - 1] == '/' || path.size() == p.size() ||
             path[p.size()] == '/') &&
            (!best || p.size() > best->_path.size()))
            best = &c._locations[i];
    }
    if (!best)
        throw 404;
    if (r._method != "GET" && r._method != "POST" && r._method != "DELETE")
        throw 501;
    if (!best->_allowedMethods.empty() &&
        std::find(best->_allowedMethods.begin(), best->_allowedMethods.end(), r._method) ==
            best->_allowedMethods.end())
        throw 405;
    if (r._body.size() > c._clientMaxBodySize)
        throw 413;
    return *best;
}
static std::string join(const std::string &root, const std::string &suffix)
{
    return root + (root.empty() || root[root.size() - 1] != '/' ? "/" : "") +
           (suffix.size() && suffix[0] == '/' ? suffix.substr(1) : suffix);
}
static std::string mapped(const Location &l, const std::string &path)
{
    std::string file = join(l._root, path.substr(l._path.size()));
    safeParents(file);
    return file;
}
static bool exists(const std::string &p, struct stat &s)
{
    return stat(p.c_str(), &s) == 0;
}
static std::string mime(const std::string &p)
{
    size_t dot = p.rfind('.');
    std::string ext = dot == std::string::npos ? "" : p.substr(dot);
    if (ext == ".html" || ext == ".htm")
        return "text/html; charset=utf-8";
    if (ext == ".css")
        return "text/css";
    if (ext == ".js")
        return "application/javascript";
    if (ext == ".json")
        return "application/json";
    if (ext == ".png")
        return "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".gif")
        return "image/gif";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".ico")
        return "image/x-icon";
    if (ext == ".txt" || ext == ".py")
        return "text/plain; charset=utf-8";
    if (ext == ".pdf")
        return "application/pdf";
    return "application/octet-stream";
}
std::string Response::error(int code, const ServerConfig &c)
{
    std::map<int, std::string>::const_iterator page = c._errorPages.find(code);
    if (page != c._errorPages.end())
    {
        try
        {
            HttpRequest r;
            r._method = "GET";
            r._path = page->second;
            std::string path;
            const Location &l = route(r, c, path);
            return pack(code, readFile(mapped(l, path)),
                        "Content-Type: text/html; charset=utf-8\r\n");
        }
        catch (...)
        {
        }
    }
    return pack(code,
                "<!doctype html><html><title>" + reason(code) + "</title><h1>" + reason(code) +
                    "</h1></html>\n",
                "Content-Type: text/html; charset=utf-8\r\n");
}
bool Response::cgiTarget(const HttpRequest &r, const ServerConfig &c, std::string &script,
                         std::string &interpreter)
{
    std::string path;
    const Location &l = route(r, c, path);
    if (l._redirectCode || l._cgiExtension.empty() || r._method == "DELETE")
        return false;
    size_t extension = path.find(l._cgiExtension);
    while (extension != std::string::npos && extension + l._cgiExtension.size() < path.size() &&
           path[extension + l._cgiExtension.size()] != '/')
        extension = path.find(l._cgiExtension, extension + 1);
    std::string scriptPath =
        extension == std::string::npos ? path : path.substr(0, extension + l._cgiExtension.size());
    script = mapped(l, scriptPath);
    struct stat s;
    if (exists(script, s) && S_ISDIR(s.st_mode) && !l._index.empty())
        script = join(script, l._index);
    if (script.size() < l._cgiExtension.size() ||
        script.compare(script.size() - l._cgiExtension.size(), l._cgiExtension.size(),
                       l._cgiExtension) != 0)
        return false;
    if (!exists(script, s))
        throw 404;
    if (!S_ISREG(s.st_mode) || access(script.c_str(), R_OK) != 0)
        throw 403;
    safeParents(script);
    int fd = open(script.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        throw 403;
    close(fd);
    interpreter = l._cgiPath;
    return true;
}
static std::string listing(const std::string &file, const std::string &url)
{
    int fd = open(file.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        throw 403;
    close(fd);
    DIR *dir = opendir(file.c_str());
    if (!dir)
        throw 403;
    std::vector<std::string> names;
    try
    {
        struct dirent *ent;
        while ((ent = readdir(dir)))
            if (std::string(ent->d_name) != "." && std::string(ent->d_name) != "..")
                names.push_back(ent->d_name);
    }
    catch (...)
    {
        closedir(dir);
        throw;
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    std::string body = "<!doctype html><h1>Index of " + escape(url) + "</h1><ul>";
    for (size_t i = 0; i < names.size(); ++i)
        body += "<li><a href=\"" + encode(names[i]) + "\">" + escape(names[i]) + "</a></li>";
    return body + "</ul>\n";
}
static std::string upload(const HttpRequest &r, const Location &l)
{
    if (l._uploadPath.empty())
        throw 403;
    std::string name, body = r._body;
    std::map<std::string, std::string>::const_iterator ct = r._headers.find("content-type");
    if (ct != r._headers.end() && ct->second.find("multipart/form-data") == 0)
    {
        size_t b = ct->second.find("boundary=");
        if (b == std::string::npos)
            throw 400;
        std::string boundary = ct->second.substr(b + 9);
        if (boundary.size() > 1 && boundary[0] == '"' && boundary[boundary.size() - 1] == '"')
            boundary = boundary.substr(1, boundary.size() - 2);
        if (boundary.empty() || boundary.size() > 70)
            throw 400;
        std::string delimiter = "--" + boundary;
        size_t pos = 0;
        bool found = false;
        while ((pos = body.find(delimiter, pos)) != std::string::npos)
        {
            pos += delimiter.size();
            if (body.compare(pos, 2, "--") == 0)
                break;
            if (body.compare(pos, 2, "\r\n") != 0)
                throw 400;
            size_t end = body.find("\r\n\r\n", pos);
            if (end == std::string::npos)
                throw 400;
            std::string head = body.substr(pos, end - pos);
            size_t n = head.find("filename=\"");
            size_t next = body.find("\r\n" + delimiter, end + 4);
            if (next == std::string::npos)
                throw 400;
            if (n != std::string::npos)
            {
                size_t stop = head.find('"', n + 10);
                if (stop == std::string::npos)
                    throw 400;
                name = head.substr(n + 10, stop - n - 10);
                body = body.substr(end + 4, next - end - 4);
                found = true;
                break;
            }
            pos = next + 2;
        }
        if (!found)
            throw 400;
    }
    else
    {
        std::string path = decode(r._path);
        size_t slash = path.rfind('/');
        name = path.substr(slash + 1);
        if (name.empty() || path == l._path)
            name = "upload.bin";
    }
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos)
        throw 400;
    for (size_t i = 0; i < name.size(); ++i)
        if (static_cast<unsigned char>(name[i]) < 32 || name[i] == 127)
            throw 400;
    std::string target = join(l._uploadPath, name);
    safeParents(target);
    int fd = open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        struct stat s;
        if (exists(target, s))
            throw 409;
        throw 403;
    }
    size_t pos = 0;
    while (pos < body.size())
    {
        ssize_t n = write(fd, body.data() + pos, body.size() - pos);
        if (n <= 0)
        {
            close(fd);
            std::remove(target.c_str());
            throw 500;
        }
        pos += n;
    }
    close(fd);
    std::string url = l._path + (l._path[l._path.size() - 1] == '/' ? "" : "/") + encode(name);
    return Response::pack(201, "Uploaded " + escape(name) + "\n",
                          "Content-Type: text/plain\r\nLocation: " + url + "\r\n");
}
std::string Response::build(const HttpRequest &r, const ServerConfig &c)
{
    try
    {
        std::string path;
        const Location &l = route(r, c, path);
        if (l._redirectCode)
            return pack(l._redirectCode, "", "Location: " + l._redirectUrl + "\r\n");
        if (r._method == "POST")
            return upload(r, l);
        std::string file = mapped(l, path);
        struct stat s;
        if (!exists(file, s))
            throw 404;
        if (r._method == "DELETE")
        {
            if (!S_ISREG(s.st_mode))
                throw 403;
            if (std::remove(file.c_str()) != 0)
                throw 403;
            return pack(204, "");
        }
        if (S_ISDIR(s.st_mode))
        {
            int fd = open(file.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
            if (fd < 0)
                throw 403;
            close(fd);
            if (path[path.size() - 1] != '/')
                return pack(301, "",
                            "Location: " + r._path + "/" +
                                (r._query.empty() ? "" : "?" + r._query) + "\r\n");
            std::string index = join(file, l._index);
            struct stat st;
            if (!l._index.empty() && exists(index, st) && S_ISREG(st.st_mode))
                file = index;
            else
            {
                if (!l._autoindex)
                    throw 403;
                return pack(200, listing(file, path), "Content-Type: text/html; charset=utf-8\r\n");
            }
        }
        else if (!S_ISREG(s.st_mode))
            throw 403;
        return pack(200, readFile(file), "Content-Type: " + mime(file) + "\r\n");
    }
    catch (int code)
    {
        if (code == 405)
        {
            std::string path;
            std::string allow;
            for (size_t i = 0; i < c._locations.size(); ++i)
            {
                const Location &l = c._locations[i];
                if (r._path.compare(0, l._path.size(), l._path) == 0 &&
                    (l._path == "/" || l._path[l._path.size() - 1] == '/' ||
                     r._path.size() == l._path.size() || r._path[l._path.size()] == '/') &&
                    l._path.size() >= path.size())
                {
                    path = l._path;
                    allow = "";
                    for (size_t j = 0; j < l._allowedMethods.size(); ++j)
                        allow += (j ? ", " : "") + l._allowedMethods[j];
                }
            }
            std::string response = error(405, c);
            response.insert(response.find("\r\n") + 2, "Allow: " + allow + "\r\n");
            return response;
        }
        return error(code, c);
    }
}
std::string Response::cgiResponse(const std::string &output)
{
    size_t end = output.find("\r\n\r\n"), skip = 4;
    if (end == std::string::npos)
    {
        end = output.find("\n\n");
        skip = 2;
    }
    if (end == std::string::npos || end > 16384)
        throw 502;
    std::istringstream in(output.substr(0, end));
    std::string line, headers;
    int code = 200;
    bool type = false;
    while (std::getline(in, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            throw 502;
        std::string name = line.substr(0, colon);
        for (size_t i = 0; i < name.size(); ++i)
            name[i] = std::tolower(static_cast<unsigned char>(name[i]));
        if (name == "status")
        {
            std::istringstream status(line.substr(colon + 1));
            if (!(status >> code) || code < 200 || code > 599)
                throw 502;
        }
        else if (name != "content-length" && name != "connection" && name != "transfer-encoding" &&
                 name != "server")
        {
            headers += line + "\r\n";
            if (name == "content-type")
                type = true;
            if (name == "location" && code == 200)
                code = 302;
        }
    }
    if (!type)
        headers += "Content-Type: text/html; charset=utf-8\r\n";
    return pack(code, output.substr(end + skip), headers);
}
