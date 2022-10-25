#pragma once
#include "winsock2.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include "json.hpp"
#include <string>
#include <map>
using std::cout;
using std::string;
using std::map;
using json = nlohmann::json;
using std::to_string;

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

string http_response(int status, const char * content, int size = -1) {
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
	result = string("HTTP/1.1 ") + to_string(status) + " " + name + "\n";
	result += "Content-Type: text/html; charset=UTF-8\n";
	result += "Content-Length: " + to_string(size == -1 ? strlen(content) : size) + "\n";
	result += "\n";
	result += content;
	return result;
}

int main() {
	// Init
	WebConfig config = config_load();


	WSADATA wsaData;
	/*
		select()机制中提供的fd_set的数据结构，实际上是long类型的数组，
		每一个数组元素都能与一打开的文件句柄（不管是socket句柄，还是其他文件或命名管道或设备句柄）建立联系，建立联系的工作由程序员完成.
		当调用select()时，由内核根据IO状态修改fd_set的内容，由此来通知执行了select()的进程哪个socket或文件句柄发生了可读或可写事件。
	*/
	fd_set rfds;				//用于检查socket是否有数据到来的的文件描述符，用于socket非阻塞模式下等待网络事件通知（有数据到来）
	fd_set wfds;				//用于检查socket是否可以发送的文件描述符，用于socket非阻塞模式下等待网络事件通知（可以发送数据）
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

	SOCKET sessionSocket = INVALID_SOCKET;

	int addrLen;

	//创建监听socket
	srvSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (srvSocket != INVALID_SOCKET)
		printf("Socket create Ok!\n");


	//设置服务器的端口和地址
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config.port);
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY); //主机上任意一块网卡的IP地址


	//binding
	int rtn = bind(srvSocket, (LPSOCKADDR)&addr, sizeof(addr));
	if (rtn != SOCKET_ERROR)
		printf("Socket bind Ok!\n");

	//监听
	rtn = listen(srvSocket, 5);
	if (rtn != SOCKET_ERROR)
		printf("Socket listen Ok!\n");

	clientAddr.sin_family = AF_INET;
	addrLen = sizeof(clientAddr);

	//设置接收缓冲区
	char recvBuf[4096];

	u_long blockMode = 1;//将srvSock设为非阻塞模式以监听客户连接请求

	//调用ioctlsocket，将srvSocket改为非阻塞模式，改成反复检查fd_set元素的状态，看每个元素对应的句柄是否可读或可写
	if ((rtn = ioctlsocket(srvSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) { //FIONBIO：允许或禁止套接口s的非阻塞模式。
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

		if (!first_connetion) {
			if (sessionSocket != INVALID_SOCKET) { //如果sessionSocket是有效的
				FD_SET(sessionSocket, &rfds);
				FD_SET(sessionSocket, &wfds);
			}

		}

		select(0, &rfds, &wfds, NULL, NULL);

		if (FD_ISSET(srvSocket, &rfds)) {

			sessionSocket = accept(srvSocket, (LPSOCKADDR)&clientAddr, &addrLen);
			if (sessionSocket != INVALID_SOCKET)
				printf("Socket listen one client request!\n");

			if ((rtn = ioctlsocket(sessionSocket, FIONBIO, &blockMode) == SOCKET_ERROR)) { //FIONBIO：允许或禁止套接口s的非阻塞模式。
				cout << "ioctlsocket() failed with error!\n";
				return 0;
			}
			cout << "ioctlsocket() for session socket ok!	Waiting for client connection and data\n";

			first_connetion = false;

		}

		if (FD_ISSET(sessionSocket, &rfds)) {
			//receiving data from client
			memset(recvBuf, '\0', 4096);
			rtn = recv(sessionSocket, recvBuf, 256, 0);
			if (rtn > 0) {
				printf("Received %d bytes from client: %s\n", rtn, recvBuf);
				is_responding = true;
			}
			else { // If client is leaving
				printf("Client leaving ...\n");
				closesocket(sessionSocket);
				sessionSocket = INVALID_SOCKET;
			}
		}

		if (is_responding && FD_ISSET(sessionSocket, &wfds)) {
			is_responding = false;

			string &content = responde_content;

			cout << "Responding:\n" << content << std::endl;

			int bytes = send(sessionSocket, content.c_str(), content.size(), 0);
			cout << "Sending " << to_string(bytes) << " bytes." << std::endl;
		}
	}

}