#pragma once
#include "winsock2.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "json.hpp"
#include <string>
#include <map>
#include <filesystem>
#include <vector>
#include "termcolor.hpp"
using std::cout;
using std::vector;
using std::string;
using std::map;
using std::endl;
using json = nlohmann::json;
using std::to_string;
namespace fs = std::filesystem;

#pragma comment(lib,"ws2_32.lib")

namespace wc {
	struct WebConfig {
		int port;
		string rootPath;
	};
	void from_json(const json& j, WebConfig& w) {
		j.at("port").get_to(w.port);
		j.at("rootPath").get_to(w.rootPath);
	}
}
using namespace wc;

WebConfig config_load() {
	std::ifstream f("config.json");

	if (!f.good()) {
		cout << "Config loading error.";
		exit(0);
	}

	return json::parse(f).get<WebConfig>();
}
WebConfig wconfig;

map<string, string> fileToType;

void http_init() {
	fileToType[".jpg"] = "image/jpeg";
	fileToType[".png"] = "image/png";
	fileToType[".gif"] = "image/gif";
	fileToType[".css"] = "text/css";
	fileToType[".html"] = "text/html";
	fileToType[".js"] = "application/x-javascript";
}

string http_get_file_type(string file) {
	fs::path fp = file;
	string ext = fp.extension().string();
	cout << "EXTENSION: " << ext << std::endl;
	if (fileToType.count(ext) > 0)
		return fileToType[ext];
	else
		return "application/octet-stream";
}

string http_response(int status, const char * content, int size = -1, string type = "null") {
	string result;
	string name;
	switch (status) {
	case 200:
		name = "OK";
		break;
	case 404:
		name = "Not Found";
		break;
	case 403:
		name = "Forbidden";
		break;
	}
	size = size == -1 ? strlen(content) : size;
	result = string("HTTP/1.1 ") + to_string(status) + " " + name + "\n";
	if(type != "null")
		result += string("Content-Type: ") + type + "; charset=UTF-8\n";
	result += "Content-Length: " + to_string(size) + "\n";
	result += "\n";
	cout << std::endl;
	//cout << "Responde header:\n" << result << std::endl;
	result.append(content, size);
	return result;
}

struct HttpRequest {
	string type;
	string path;
	bool is_request = false;
};

HttpRequest http_request_parse(string content) {
	std::istringstream ss(content);
	string line;
	HttpRequest result;
	while (std::getline(ss, line)) {
		if (line.find("GET") != string::npos) {
			std::istringstream lss(line);
			lss >> result.type >> result.path;
			result.is_request = true;
		}
	}
	return result;
}

const int READ_BUFFER_SIZE = 25 * 1024 * 1024;
char rdBuf[READ_BUFFER_SIZE];
string http_responde_client(HttpRequest request) {
	if (request.type == "GET") {
		string path = request.path;
		if (path[path.size() - 1] == '/') {
			path += "index.html";
		}
		else if (!fs::path(path).has_extension()) {
			path += "/index.html";
		}
		
		path = wconfig.rootPath + path;
		cout << "Request file at path: " << path << std::endl;
		std::ifstream f(path, std::ios::binary);
		if (!f.good()) {
			cout << termcolor::red << "File not found. Redirecting to 404 page..." << termcolor::reset << std::endl;
			std::ifstream ff(wconfig.rootPath + "/404.html", std::ios::binary);
			if (ff.good()) {
				ff.read(rdBuf, READ_BUFFER_SIZE);
				ff.close();
				return http_response(404, rdBuf, ff.gcount(), ".html");
			}
			else
				return http_response(404, "");
		}
			
		f.read(rdBuf, READ_BUFFER_SIZE);
		f.close();
		return http_response(200, rdBuf, f.gcount(), http_get_file_type(path));
	}
}

struct Session {
	SOCKET socket;
	bool is_responding = false;
	string response = "";

	void responde_request(string res) {
		is_responding = true;
		response = res;
	}

	Session(SOCKET sock) {
		socket = sock;
	}

	bool operator == (const SOCKET& sock) {
		return socket == sock;
	}
};

int main() {
	// Init
	http_init();
	wconfig = config_load();


	WSADATA wsaData;
	fd_set rfds;
	fd_set wfds;
	bool first_connetion = true;

	int nRc = WSAStartup(0x0202, &wsaData);

	if (nRc) {
		printf("Winsock  startup failed with error!\n");
	}

	if (wsaData.wVersion != 0x0202) {
		printf("Winsock version is not correct!\n");
	}

	printf("Winsock  startup Ok!\n");


	SOCKET srvSocket;

	sockaddr_in addr, clientAddr;

	vector<Session> sessions;

	int addrLen;

	srvSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (srvSocket != INVALID_SOCKET)
		printf("Socket create Ok!\n");

	addr.sin_family = AF_INET;
	addr.sin_port = htons(wconfig.port);
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	int rtn = bind(srvSocket, (LPSOCKADDR)&addr, sizeof(addr));
	if (rtn != SOCKET_ERROR)
		printf("Socket bind Ok!\n");

	rtn = listen(srvSocket, 5);
	if (rtn != SOCKET_ERROR)
		printf("Socket listen Ok!\n");

	clientAddr.sin_family = AF_INET;
	addrLen = sizeof(clientAddr);

	char recvBuf[4096];

	u_long blockMode = 1;

	if ((rtn = ioctlsocket(srvSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) {
		cout << "ioctlsocket() failed with error!\n";
		return 0;
	}
	cout << "ioctlsocket() for server socket ok!	Waiting for client connection and data\n";

	// If server gonna respond client then set to true
	bool is_responding = false;
	string responde_content = "";

	while (true) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		FD_SET(srvSocket, &rfds);

		for(auto ses:sessions) {
			FD_SET(ses.socket, &rfds);
			FD_SET(ses.socket, &wfds);
		}

		select(0, &rfds, &wfds, NULL, NULL);

		if (FD_ISSET(srvSocket, &rfds)) {

			SOCKET sessionSocket = accept(srvSocket, (LPSOCKADDR)&clientAddr, &addrLen);
			if (sessionSocket != INVALID_SOCKET)
				printf("Socket listen one client request!\n");

			if ((rtn = ioctlsocket(sessionSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) {
				cout << "ioctlsocket() failed with error!\n";
				return 0;
			}
			cout << "ioctlsocket() for session socket ok!	Waiting for client connection and data\n";

			sessions.push_back(sessionSocket);

		}

		for (auto& ses:sessions) {
			auto& sessionSocket = ses.socket;
			if (FD_ISSET(sessionSocket, &rfds)) {
				//receiving data from client
				memset(recvBuf, '\0', 4096);
				rtn = recv(sessionSocket, recvBuf, 4096, 0);
				if (rtn > 0) {
					printf("Received %d bytes from client.\n", rtn);

					// Trying parse the data
					HttpRequest request = http_request_parse(recvBuf);
					if (!request.is_request) {
						//puts("Not a http request.");
						cout << termcolor::red << "Not a http request.\n" << termcolor::reset;
					}
					else {
						cout << "Getting a request.\nType: " << request.type << std::endl << "Path: " << request.path << std::endl;

						// Dealing with the request.

						ses.responde_request(http_responde_client(request));
					}
				}
				else { // If client is leaving
					cout << termcolor::green << "Client leaving." << termcolor::reset << std::endl;
					closesocket(sessionSocket);
					sessionSocket = INVALID_SOCKET;
				}
			}

			if (ses.is_responding && FD_ISSET(sessionSocket, &wfds)) {
				ses.is_responding = false;

				string& content = ses.response;

				//cout << "Responding:\n" << content << std::endl;

				int bytes = send(sessionSocket, content.c_str(), content.size(), 0);
				cout << "Sending " << to_string(bytes) << " bytes." << std::endl;
			}
		}
		
		sessions.erase(std::remove(sessions.begin(), sessions.end(), INVALID_SOCKET), sessions.end());
	}

}