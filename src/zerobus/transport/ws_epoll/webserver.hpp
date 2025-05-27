#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <format>

using zerobus::HeaderKey;

namespace fs = std::filesystem;

constexpr std::pair<std::string_view, std::string_view> content_types[] = {
    {".txt", "text/plain; charset=utf-8"},
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"}
};

inline std::string_view get_content_type(const fs::path& file_path) {
    auto ext = file_path.extension().string();
    for (const auto &[k,v]: content_types) {
        if (k == ext) return v;
    }
    return "application/octet-stream";
}

constexpr std::string_view response403 = "HTTP/1.1 403 Forbidden\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";
constexpr std::string_view response400 = "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Length: 0\r\n"
                      "Connection: close\r\n"
                      "\r\n";

constexpr std::string_view response404 = "HTTP/1.1 404 Not Found\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";
constexpr std::string_view response405 = "HTTP/1.1 405 Method Not Allowed\r\n"
                      "Content-Length: 0\r\n"
                      "Allow: GET\r\n"
                      "\r\n";
constexpr std::string_view response500 = "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n";


template<std::invocable<std::string_view> Fn>
inline bool handle_http_request(std::string_view http_header,
                        Fn&& callback,
                        const fs::path& root_dir) {
    static_assert(std::is_same_v<std::invoke_result_t<Fn, std::string_view>,bool>);
    size_t first_line_end = http_header.find("\r\n");
    if (first_line_end == std::string_view::npos) {
        callback(response400);
        return false;
    }

    std::string_view first_line = http_header.substr(0, first_line_end);
    size_t method_end = first_line.find(' ');
    if (method_end == std::string_view::npos) {
        callback(response400);
        return false;
    }

    HeaderKey method = first_line.substr(0, method_end);
    if (method != "GET") {
        callback(response405);
        return true;
    }

    size_t path_start = method_end + 1;
    size_t path_end = first_line.find(' ', path_start);
    if (path_end == std::string_view::npos) {
        callback(response400);
        return false;
    }

    std::string_view path = first_line.substr(path_start, path_end - path_start);

    std::string decoded_path;
    decoded_path.reserve(path.size());
    bool is_percent = false;
    std::string hex;
    for (char c : path) {
        if (c == '%' && !is_percent) {
            is_percent = true;
            hex.clear();
        } else if (is_percent) {
            hex += c;
            if (hex.size() == 2) {
                try {
                    decoded_path += static_cast<char>(std::stoi(hex, nullptr, 16));
                } catch (...) {
                    callback(response404);
                    return true;
                }
                is_percent = false;
            }
        } else {
            if (c == '/') {
                decoded_path += fs::path::preferred_separator;
            } else {
                decoded_path += c;
            }
        }
    }

    fs::path file_path = fs::canonical(root_dir / decoded_path.substr(1));
    if (!file_path.string().starts_with(root_dir.string())) {
        callback(response403);
        return true;
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        callback(response404);
        return true;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        callback(response500);
        return true;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string header = std::format("HTTP/1.1 200 OK\r\n"
                         "Content-Type: {}\r\n"
                         "Content-Length: {}\r\n"
                         "\r\n",
                         get_content_type(file_path),file_size);
    if (!callback(header)) return false;

    // Čtení a odesílání obsahu souboru po částech
    constexpr size_t buffer_size = 8192;
    std::string buffer(buffer_size, '\0');
    while (file) {
        file.read(buffer.data(), buffer_size);
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            if (!callback(std::string_view(buffer.data(), bytes_read))) return false;
        }
    }
    return true;
}
