#include "main.h"

using std::string;
using std::cout;
using std::endl;

//多线程数据转发
/*
方法1. 建立多个转发线程，接收线程唯一. 每个接收线程独立对应一个数据接收缓冲区，并设立条件变量，当接收进程收到数据并存储缓冲区x中时唤醒线程x进行转发
当前数据接收缓冲区应进行合理选择，正在使用的缓冲区应设置标志位，对标志位进行读写时应加锁
最初可以初始化N个线程，当线程不够使用时再动态增加
 **方法1 用一个线程接收数据多个线程转发数据，一定程度上可以提高效率**
方法2. 多个线程，每个线程均可收发，由于多个绑定同一地址端口对的socket只有第一绑定的可以接收数据，因此应采用不同线程不同端口号的方式
	服务器提供多个接收端口号，其中一个用作注册端口号，即客户端连接服务器时访问的端口号，
	服务器收到注册信息后启动一个新的线程并创建新的socket向客户端发送注册成功信息，（此时系统给分配了一个新的端口号）
	客户端注册成功后添加进客户列表，方式多次注册导致启动多余线程。 
	之后该用户端用服务器分配的端口号进行数据请求
 */

bool Server::run_flag = true; 

Server::Server(int port):
	register_port_(port)
{

}

Server::~Server()
{

}

//初始化socket返回句柄
//ip为本地ip，端口默认为0，由系统自动分配 
int Server::initSocket(const int port, const std::string ip, int time_out)
{
	struct sockaddr_in local_addr;
	bzero(&local_addr,sizeof(local_addr));//init 0
	
	local_addr.sin_port = htons(port);
	local_addr.sin_family = AF_INET;
	int convert_ret = inet_pton(AF_INET, ip.c_str(), &local_addr.sin_addr);
	if(convert_ret !=1)
	{
		perror("convert socket ip failed, please check the format!");
		return -1;
	}

	int fd = socket(PF_INET,SOCK_DGRAM , 0);
	if(fd < 0)
	{
		perror("build socket error");
		return -1;
	}
	int udp_opt = 1;
	// 设置地址可复用 
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &udp_opt, sizeof(udp_opt));
	if(time_out)
	{
		struct timeval timeout;
	    timeout.tv_sec = time_out;//秒
	    //timeout.tv_usec = 0;//微秒
	    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	}
	
	int ret = bind(fd, (struct sockaddr*)&local_addr,sizeof(local_addr));
	if(ret < 0)
	{
		std::cout << "udp bind ip: "<< ip << "\t,port: "<< port << " failed!!" << std:: endl;
		return -1;
	}
	return fd;
} 

//打印socket地址以及端口号信息 
void showSocketMsg(const std::string& prefix, int fd)
{
	struct sockaddr_in serverAddr;
	socklen_t server_len; // = sizeof(sockaddr_in);
	//获取socket信息, ip,port.. 
	getsockname(fd,  (struct sockaddr *)&serverAddr, &server_len);
	char ip[16];
	inet_ntop(AF_INET,&serverAddr.sin_addr,ip,server_len);
	cout << prefix << "\t ip: " << ip << "\t port: " << serverAddr.sin_port << endl;
}

int Server::initSocketAutoAssignPort(uint16_t& port)
{
	struct sockaddr_in local_addr;
	bzero(&local_addr,sizeof(local_addr));//init 0
	local_addr.sin_family = AF_INET;
	int convert_ret = inet_pton(AF_INET, "0.0.0.0", &local_addr.sin_addr);
	
	for(port=50000;port<60000; ++port)
	{
		local_addr.sin_port = htons(port);
		int fd = socket(PF_INET,SOCK_DGRAM , 0);
		int ret = bind(fd, (struct sockaddr*)&local_addr,sizeof(local_addr));
		if(ret == 0)
		{
			cout << "AutoAssignPort: " << port << endl;
			return fd;
		}
	}
}

//接收客户端注册信息的线程 
void Server::receiveRegisterThread()
{
	int register_fd = initSocket(register_port_); //初始化注册socket 
	showSocketMsg("register sockect ",register_fd);
	
	const int BufLen =20;
	uint8_t *recvbuf = new uint8_t [BufLen];
	const transPack_t *pkg = (const transPack_t *)recvbuf;
	struct sockaddr_in client_addr;
	socklen_t clientLen = sizeof(client_addr);
	
	while(run_flag)
	{
		int len = recvfrom(register_fd, recvbuf, BufLen,0,(struct sockaddr*)&client_addr, &clientLen);
		if(len <=0 )
			continue;
		if(recvbuf[0] != 0x66 || recvbuf[1] != 0xcc)
			continue;
		if(pkg->type != RequestRegister)
			continue;
		
		// 收到客户端请求注册的信息 
		uint16_t clientId = pkg->senderId;
		auto it = clients_.find(clientId);
		if (it != clients_.end()) //查找到目标客户端 ,表明已经注册
			continue; 
		
		cout << "receiveRegisterMsg, client id:  " << pkg->senderId << "\t msg len:" << len << endl;
		
		clientInfo_t client;
		client.addr = client_addr;
		client.connect = false; //此时客户端与服务端还未建立真正的连接 
		clients_[clientId] = client; //新注册的客户端填入map 
		//为新注册的客户端新创建一个服务套接字
		uint16_t new_port;
		int server_fd = initSocketAutoAssignPort(new_port);
		
		//向客户端回应为其分配的新端口号信息 
		transPack_t pkg;
		int headerLen = sizeof(transPack_t); 
		pkg.length = 2;
		pkg.type = ResponseRegister;
		char *buf = new char[headerLen+pkg.length];
		memcpy(buf, &pkg, headerLen);

		buf[headerLen] = new_port%256;
		buf[headerLen+1] = new_port/256;
		sendto(register_fd, buf, headerLen+pkg.length, 0, 
				(struct sockaddr*)&client_addr, sizeof(sockaddr_in));
		
		//启动接收和转发线程 
		std::thread t(&Server::receiveAndTransThread,this,server_fd);
		t.detach();
	}
	
	delete [] recvbuf;
}

//接收客户端信息并进行转发的线程 
//客户端断开连接后关闭线程 
void Server::receiveAndTransThread(int server_fd)
{
	const int BufLen1 =2*sizeof(transPack_t);
	uint8_t *recvbuf = new uint8_t [BufLen1];
	const transPack_t *pkg = (const transPack_t *)recvbuf;
	struct sockaddr_in client_addr;
	socklen_t clientLen = sizeof(client_addr);
	uint16_t clientId;
	while(run_flag)
	{
		//cout << "new thread start to receive msgs..." << endl;
		int len = recvfrom(server_fd, recvbuf, BufLen1,0,(struct sockaddr*)&client_addr, &clientLen);
		cout << "new thread received msgs. length: " << len << endl;
		if(recvbuf[0] != 0x66 || recvbuf[1] != 0xcc || pkg->type != RequestRegister)
			continue;
		clientId = pkg->senderId;
		clients_[clientId].connect = true; //连接成功
		clients_[clientId].addr = client_addr; //写入客户端地址
		clients_[clientId].fd =  server_fd; //将与该用户建立连接的套接字保存 
		//发送注册成功
		transPack_t temp_pkg;
		temp_pkg.type = RegisterOK;
		//发送多次注册成功信号 
		for(int i=0; i<3; ++i)
		{
			cout << "clientId: "<< clientId << " register ok.  addr " << clients_[clientId].addr.sin_port << endl; 
			sendto(server_fd,(char*)&temp_pkg, sizeof(temp_pkg), 0, (struct sockaddr*)&client_addr, sizeof(sockaddr_in));
			usleep(50000);
		}
		break;
	}
	 
	delete [] recvbuf;
	const int BufLen2 = 100000;
	recvbuf = new uint8_t [BufLen2];
	const transPack_t *_pkg = (const transPack_t *)recvbuf;
	
	//配置为非阻塞，并设置超时时间 
	struct timeval timeout;
    timeout.tv_sec = 1;//秒
    //timeout.tv_usec = 0;//微秒
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	    
	while(run_flag && clients_[clientId].connect)
	{
		int len = recvfrom(server_fd, recvbuf, BufLen2,0,(struct sockaddr*)&client_addr, &clientLen);
		//cout << "received msgs len:" << len  << "  type: " << int(_pkg->type) << endl;
		if(len <= 0) continue;
		if(_pkg->head[0] != 0x66 || _pkg->head[1] != 0xcc)
			continue;
		if(_pkg->type == HeartBeat)
		{
			sendto(server_fd,recvbuf, len, 0, (struct sockaddr*)&client_addr, clientLen);
			clients_[clientId].lastHeartBeatTime = time(0); //记录客户心跳时间 
    
		}
		else if(_pkg->type == Video || _pkg->type == Audio)
			msgTransmit(recvbuf, len);
			
	}
	cout << "delete client : " << clientId;
	clients_.erase(clientId);
	delete [] recvbuf;
	close(server_fd);
}

void Server::run()
{
	std::thread t1 = std::thread(&Server::printThread,this,5);
	t1.detach();
	
	std::thread t2 = std::thread(&Server::heartBeatThread,this);
	t2.detach(); 
	
	//新建接收客户端注册信息的线程 
	std::thread t = std::thread(&Server::receiveRegisterThread,this);
	t.join();
}

void Server::msgTransmit(const uint8_t* buf, int len)
{
	uint16_t dstClientId = ((const transPack_t *)buf)->receiverId;

	auto it = clients_.find(dstClientId);
	if (it == clients_.end()) //未查找到目标客户端 
	{
		cout << "No client : " << dstClientId << endl;
		return;
	}
	int send_len = sendto(clients_[dstClientId].fd, buf, len, 0, (struct sockaddr*)&clients_[dstClientId].addr, sizeof(sockaddr_in));
	cout << "transmitting : " << send_len << " bytes to id: " << dstClientId << "\tport：" << clients_[dstClientId].addr.sin_port << endl;
}

//由于遇到过服务器自动断开的问题 
//定时向终端打印数据的线程，保持服务器一直处于唤醒状态
void Server::printThread(int interval)
{
	int i=0;
	while(run_flag)
	{
		sleep(interval);
		printf("this server has been running for %d seconds.\n",interval*(++i));
	}
}

void Server::heartBeatThread()
{
	int heartBeatInterval = 5; //心跳间隔5s 
	while(run_flag)
	{
		cout << "clients size: " << clients_.size() << "\t sending heart beat pkg...\n";
		for(auto client =clients_.begin(); client!= clients_.end(); ++client)
		{
			if(client->second.lastHeartBeatTime ==0)
			    continue;
			if(time(0) - client->second.lastHeartBeatTime >heartBeatInterval+1)
			{
				client->second.connect = false;
				cout << "client " << client->first  << "  disconnect." << endl;
			}
		}
		std::this_thread::sleep_for(std::chrono::seconds(heartBeatInterval)); 
	}

}

//系统中断信号捕获
void sigint_handler(int signal_num)
{
	//std::cout << "signal_num: " << signal_num << std::endl;
	Server::run_flag = false;
	usleep(100000); //预留时间清理线程 
	exit(0);
}

int main(int argc,char** argv)
{
	signal(SIGINT, sigint_handler);
	int port = 8617;
	if(argc > 1)
		port = atoi(argv[1]);
	Server server(port);
	
	server.run();
 
	return 0;
}

// scp * root@aliyun:/root/seu/wendao/remote_driverless_server
