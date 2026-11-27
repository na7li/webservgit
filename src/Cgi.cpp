#include "../headers/Cgi.hpp"
#include "../headers/Response.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <vector>
#include <cctype>

// Killed CGI processes are reaped on later event-loop iterations, never waited
// for synchronously while other clients may need service.
static pid_t retired[1024] = {0};
void Cgi::reap()
{
    for (size_t i = 0; i < 1024; ++i)
        if (retired[i] > 0 && waitpid(retired[i], NULL, WNOHANG) != 0)
            retired[i] = 0;
}
static void terminateChild(pid_t pid)
{
    kill(pid, SIGKILL);
    if (waitpid(pid, NULL, WNOHANG) == 0)
    {
        for (size_t i = 0; i < 1024; ++i)
            if (!retired[i])
            {
                retired[i] = pid;
                break;
            }
    }
}
Cgi::Cgi() : _pid(-1), _started(0) {}
Cgi::~Cgi()
{
    if (_pid > 0)
    {
        terminateChild(_pid);
    }
    if (!_input.empty())
        std::remove(_input.c_str());
    if (!_output.empty())
        std::remove(_output.c_str());
}
static int temporary(std::string &path)
{
    static unsigned long serial = 0;
    for (unsigned i = 0; i < 1000; ++i)
    {
        std::ostringstream name;
        name << "/tmp/webserv-cgi-" << std::time(NULL) << "-" << ++serial;
        path = name.str();
        int fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
        {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
            {
                close(fd);
                std::remove(path.c_str());
                path.clear();
                throw 500;
            }
            return fd;
        }
    }
    path.clear();
    throw 500;
}
void Cgi::start(const HttpRequest &r, const ServerConfig &c, const std::string &script,
                const std::string &interpreter, const std::string &remote)
{
    int input = temporary(_input), output = -1;
    try
    {
        output = temporary(_output);
        size_t pos = 0;
        while (pos < r._body.size())
        {
            ssize_t n = write(input, r._body.data() + pos, r._body.size() - pos);
            if (n <= 0)
                throw 500;
            pos += n;
        }
        close(input);
        input = open(_input.c_str(), O_RDONLY);
        if (input < 0)
            throw 500;
        if (fcntl(input, F_SETFD, FD_CLOEXEC) < 0)
            throw 500;
        std::vector<std::string> env;
        std::ostringstream length, port;
        length << r._body.size();
        port << c._port;
        env.push_back("GATEWAY_INTERFACE=CGI/1.1");
        env.push_back("SERVER_SOFTWARE=webserv");
        env.push_back("SERVER_PROTOCOL=" + r._version);
        env.push_back("REQUEST_METHOD=" + r._method);
        env.push_back("QUERY_STRING=" + r._query);
        env.push_back("CONTENT_LENGTH=" + length.str());
        std::string filename = script.substr(script.rfind('/') + 1);
        size_t scriptEnd = r._path.find(filename);
        scriptEnd = scriptEnd == std::string::npos ? r._path.size() : scriptEnd + filename.size();
        env.push_back("SCRIPT_NAME=" + r._path.substr(0, scriptEnd));
        env.push_back("SCRIPT_FILENAME=" + script.substr(script.rfind('/') + 1));
        env.push_back("PATH_INFO=" + r._path.substr(scriptEnd));
        env.push_back("REQUEST_URI=" + r._path + (r._query.empty() ? "" : "?" + r._query));
        env.push_back("SERVER_PORT=" + port.str());
        env.push_back("SERVER_NAME=" +
                      (r._headers.count("host") ? r._headers.find("host")->second : c._host));
        env.push_back("REMOTE_ADDR=" + remote);
        env.push_back("PATH=/usr/bin:/bin");
        std::map<std::string, std::string>::const_iterator it;
        for (it = r._headers.begin(); it != r._headers.end(); ++it)
        {
            if (it->first == "content-length")
                continue;
            if (it->first == "content-type")
            {
                env.push_back("CONTENT_TYPE=" + it->second);
                continue;
            }
            std::string key = "HTTP_" + it->first;
            for (size_t i = 0; i < key.size(); ++i)
                key[i] = key[i] == '-' ? '_' : std::toupper(static_cast<unsigned char>(key[i]));
            env.push_back(key + "=" + it->second);
        }
        std::vector<char *> values;
        for (size_t i = 0; i < env.size(); ++i)
            values.push_back(const_cast<char *>(env[i].c_str()));
        values.push_back(NULL);
        size_t slash = script.rfind('/');
        std::string directory = script.substr(0, slash), name = script.substr(slash + 1);
        char *args[3] = {const_cast<char *>(interpreter.c_str()), const_cast<char *>(name.c_str()),
                         NULL};
        _started = std::time(NULL);
        _pid = fork();
        if (_pid < 0)
            throw 500;
        if (_pid == 0)
        {
            if (dup2(input, 0) < 0 || dup2(output, 1) < 0 || chdir(directory.c_str()) < 0)
                std::exit(126);
            close(input);
            close(output);
            execve(interpreter.c_str(), args, &values[0]);
            std::exit(127);
        }
        close(input);
        close(output);
    }
    catch (...)
    {
        if (input >= 0)
            close(input);
        if (output >= 0)
            close(output);
        throw;
    }
}
bool Cgi::finish(std::string &response)
{
    int status = 0;
    pid_t result = waitpid(_pid, &status, WNOHANG);
    struct stat st;
    if (result == 0)
    {
        if (std::time(NULL) - _started >= 5 ||
            (stat(_output.c_str(), &st) == 0 && st.st_size > 16 * 1024 * 1024))
        {
            terminateChild(_pid);
            _pid = -1;
            throw 504;
        }
        return false;
    }
    if (result < 0)
        throw 502;
    _pid = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw 502;
    if (stat(_output.c_str(), &st) != 0 || st.st_size > 16 * 1024 * 1024)
        throw 502;
    std::ifstream in(_output.c_str(), std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    response = Response::cgiResponse(out.str());
    return true;
}
